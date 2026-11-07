#if defined(OPENKNX_HOMEKEY_ENABLED) && defined(ARDUINO_ARCH_ESP32)
#include "HomeKeyModule.h"
#include "HomeKeyProvisioning.h"
#include "HomeKeyOnline.h"
#include "HomeKeyWifi.h"
#include "AccessControl.h"
#include <algorithm>
#include <sstream>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <sodium.h>

namespace
{
    template<typename Bytes> std::string hex(const Bytes& bytes)
    {
        static const char digits[] = "0123456789abcdef";
        std::string result;
        for (auto byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
        return result;
    }
}

HomeKeyModule openknxHomeKey;

void HomeKeyModule::setup(bool configured)
{
#ifdef OPENKNX_HOMEKEY_WEB
    setupWeb(); // Keep diagnostics reachable even without a valid ETS application.
#endif
    if (configured)
        setup();
}

void HomeKeyModule::setup()
{
    openknxNfc.attach(this); // The NFC worker is owned by AccessControl, not HomeKey.
    if (sodium_init() < 0 || !store.begin())
    {
        logErrorP("Home Key storage/crypto unavailable; access disabled");
        fatal = true; // HomeKey fails closed; ordinary UID scanning remains available.
    }
    events = xQueueCreate(4, sizeof(Event));
    if (!events) { fatal = true; logErrorP("Home Key event queue unavailable"); return; }
    diagnostics = xQueueCreate(1, sizeof(NfcDiagnostic));
    if (!diagnostics) logErrorP("NFC diagnostic queue unavailable; diagnostics disabled");
    provisioning = std::make_unique<HomeKeyProvisioning>(*this);
#ifdef OPENKNX_HOMEKEY_WEB
    setupWeb();
#endif
    logInfoP("Home Key ready for ETS enable; no endpoints authorized automatically");
}

void HomeKeyModule::loop()
{
    if (!knx.configured()) { homeKeyConfigured = false; enabled = false; policyLoaded = false; webCodeAllowed = false; cancelCommissioning(); return; }
    homeKeyConfigured = ParamHK_Enabled;
    allowUid = !homeKeyConfigured || ParamHK_AllowUid;
    if (!events) { enabled = false; webCodeAllowed = false; cancelCommissioning(); updateStatus(); return; }
    if (!policyLoaded)
    {
        std::unique_lock<std::recursive_mutex> guard(store.mutex, std::try_to_lock);
        if (!guard.owns_lock()) { enabled = false; webCodeAllowed = false; return; }
        static_assert(HK_Authorization02Enabled - HK_Authorization01Enabled == HomeKey::EtsRowBytes, "ETS row layout changed");
        identification.cancel();
        etsAuthorization = ParamHK_EtsAuthorization;
        HomeKey::EtsTableDiagnostic tableDiagnostic;
        etsTableValid = !etsAuthorization || HomeKey::parseEtsRows(knx.paramData(HK_Authorization01Enabled),
            HomeKey::EtsRows * HomeKey::EtsRowBytes, etsMappings, &tableDiagnostic);
        if (!etsTableValid)
            logErrorP("ETS permission slot %u invalid: %s; value=%u (0x%04X); conflicting slot=%u; parameter offset=%u",
                unsigned(tableDiagnostic.slot), tableDiagnostic.reason,
                unsigned(tableDiagnostic.value), unsigned(tableDiagnostic.value),
                unsigned(tableDiagnostic.conflictingSlot),
                unsigned(HK_Authorization01Enabled + (tableDiagnostic.slot ? tableDiagnostic.slot - 1 : 0) * HomeKey::EtsRowBytes));
        ++store.generation; // Downloaded policy invalidates every pending result.
        policyLoaded = true;
        endpointDirty = true;
        reportedAuthorization = reportedCommissioning = reportedResult = -1;
        logInfoP("Authorization source: %s; table %s; %u active mapping(s)",
            etsAuthorization ? "ETS" : "USB", etsTableValid ? "valid" : "INVALID", unsigned(activeMappings().size()));
    }
    enabled = homeKeyConfigured && !fatal && store.healthy;
    pairingPermitted = ParamHK_PairingPermitted;
    if (enabled && !started)
    {
        started = true;
        if (xTaskCreate([](void* self)
            {
                auto& module = *static_cast<HomeKeyModule*>(self);
                module.provisioning->begin();
                for (;;) { module.provisioning->poll(); vTaskDelay(pdMS_TO_TICKS(10)); }
            }, "HomeKeyHap", OPENKNX_HOMEKEY_HAP_STACK_SIZE, this, 1, &hapHandle) != pdPASS)
        {
            fatal = true;
            enabled = false;
            logErrorP("Home Key task creation failed; reboot required");
        }
    }
    if (!enabled || !pairingPermitted)
        cancelCommissioning();
#ifdef OPENKNX_HOMEKEY_WEB
    loopWeb();
#endif
    // Clear expired deadlines permanently; a retained millis deadline could
    // become positive again after a later timer wrap.
    uint32_t deadline = pairingUntil.load();
    if (deadline && !HomeKey::windowOpen(millis(), deadline)) cancelCommissioning();
    updateStatus();
    NfcDiagnostic diagnostic;
    if (diagnostics && xQueueReceive(diagnostics, &diagnostic, 0) == pdTRUE)
    {
        const size_t count = std::min(diagnostic.responseLength, diagnostic.response.size());
        logInfoP("Home Key SELECT: exchange=%s, response length=%u, status=%04X, dump=%u bytes",
            diagnostic.exchanged ? "OK" : "failed", unsigned(diagnostic.responseLength), unsigned(diagnostic.status), unsigned(count));
        char dump[64 * 3 + 1]{};
        constexpr char digits[] = "0123456789ABCDEF";
        for (size_t index = 0; index < count; ++index)
        {
            dump[index * 3] = digits[diagnostic.response[index] >> 4];
            dump[index * 3 + 1] = digits[diagnostic.response[index] & 15];
            dump[index * 3 + 2] = ' ';
        }
        logInfoP("%s", dump);
        if (diagnostic.versionAttempted)
        {
            constexpr const char* stages[] = {"master SELECT", "hardware", "software", "completion"};
            for (size_t index = 0; index < diagnostic.versionDiagnostic.attempted; ++index)
            {
                const auto& stage = diagnostic.versionDiagnostic.stages[index];
                if (stage.lengthCorrection)
                    logInfoP("Card GetVersion: stage=%s, length correction=%04X, retry Le=%02X",
                        stages[index], unsigned(stage.lengthCorrection), unsigned(stage.lengthCorrection & 0xff));
                logInfoP("Card GetVersion: stage=%s, exchange=%s, response length=%u, status=%04X",
                    stages[index], stage.exchanged ? "OK" : "failed", unsigned(stage.responseLength), unsigned(stage.status));
            }
            if (diagnostic.versionValid)
            {
                const auto& version = diagnostic.version;
                logInfoP("Card GetVersion: %s; HW=%s SW=%s",
                    version[0] == 4 && version[1] == 1 && version[7] == 4 && version[8] == 1 ? "NXP DESFire-family" : "unidentified family",
                    hex(std::array<uint8_t,7>{version[0], version[1], version[2], version[3], version[4], version[5], version[6]}).c_str(),
                    hex(std::array<uint8_t,7>{version[7], version[8], version[9], version[10], version[11], version[12], version[13]}).c_str());
            }
            else logInfoP("Card GetVersion: unsupported, malformed or timed out; UID policy unchanged");
        }
    }
    Event event;
    // Bound work per iteration. Never write KNX objects from either worker task.
    for (unsigned i = 0; i < 4 && xQueueReceive(events, &event, 0) == pdTRUE; ++i)
    {
        if (!openknxNfc.active()) continue;
        std::unique_lock<std::recursive_mutex> guard(store.mutex, std::try_to_lock);
        if (!guard.owns_lock())
        {
            xQueueSend(events, &event, 0);
            break;
        }
        if(identification.active(millis()))continue;
        bool granted = false;
        const char* reason = event.reason;
        if (event.generation == store.generation)
        {
            if (enabled && store.healthy && event.kind == EventKind::Authenticated && store.contains(event.issuer, event.endpoint))
            {
                auto id = lookupAccessId(event.issuer, event.endpoint);
                granted = id != HomeKey::InvalidId && openknxAccessControl.processHomeKeyCredential(id);
                if (id == HomeKey::InvalidId)
                {
                    reason = !policyLoaded ? "authorization policy not loaded"
                        : etsAuthorization && !etsTableValid ? "ETS permission table invalid (duplicate ID/device or malformed row)"
                        : activeMappings().empty() ? "no active NFC-Tag-ID permissions; enable the imported device and download parameters"
                        : "scanned device has no active NFC-Tag-ID permission; compare this endpoint with the enabled ETS row";
                    logInfoP("Authenticated issuer=%s endpoint=%s; NFC-Tag-ID unmapped", hex(event.issuer).c_str(), hex(event.endpoint).c_str());
                }
                else
                {
                    reason = "access control blocked (lock, delay, or NFC-Tag-ID occupied)";
                    if (granted) logInfoP("Home Key credential accepted: NFC-Tag-ID=%u", id);
                }
            }
            else if (event.kind == EventKind::Authenticated)
                reason = !enabled ? "HomeKey disabled; no credential authorized"
                    : "authenticated endpoint missing from credential store";
        }
        else reason = !store.healthy ? "credential storage unavailable" : "credential/policy changed during scan; retry";
        if (!granted)
        {
            logInfoP("NFC access denied: %s", reason);
            openknxAccessControl.rejectExternalCredential();
        }
        else logInfoP("NFC access granted (%s)", event.kind == EventKind::Authenticated ? "Home Key" : "UID");
    }
}

void HomeKeyModule::enqueue(const Event& event)
{
    if (openknxNfc.active() && events)
        xQueueSend(events, &event, 0); // Overflow drops the event, never grants access.
}

bool HomeKeyModule::pairingAllowed() const
{
    return enabled && pairingPermitted && HomeKey::windowOpen(millis(), pairingUntil.load(), pairingEpoch.load(), commissioningEpoch.load());
}

bool HomeKeyModule::prepareNfc(std::array<uint8_t,18>& ecp, uint32_t& token)
{
    std::unique_lock<std::recursive_mutex> guard(store.mutex, std::try_to_lock);
    if (!guard.owns_lock()) return false;
    token = store.generation;
    ecp.fill(0);
    if (!enabled || !provisioningReady || !store.healthy || store.identity.group_identifier.size()!=8) return true;
    const uint8_t head[8]={0x6a,2,0xcb,2,6,2,0x11,0};
    std::copy(head,head+8,ecp.begin());
    std::copy(store.identity.group_identifier.begin(),store.identity.group_identifier.end(),ecp.begin()+8);
    uint16_t crc=0x6363;
    for (size_t i=0;i<16;++i) {
        uint8_t byte=ecp[i]^(crc&0xff);byte^=byte<<4;
        crc=(crc>>8)^(uint16_t(byte)<<8)^(uint16_t(byte)<<3)^(byte>>4);
    }
    ecp[16]=crc;ecp[17]=crc>>8;
    return true;
}

bool HomeKeyModule::authorizeNfcUid(const uint8_t* uid, uint8_t length, uint32_t token)
{
    if (!allowUid || !openknxNfc.active()) return false;
    std::unique_lock<std::recursive_mutex> guard(store.mutex,std::try_to_lock);
    if (!guard.owns_lock() || token!=store.generation) return false;
    uint16_t reserved[HomeKey::MaxMappings];size_t count=0;
    if(identification.active(millis()))return false;
    for (const auto& mapping:activeMappings())
        if(!store.isRevoked(mapping.issuer,mapping.endpoint))reserved[count++]=mapping.accessId;
    return openknxAccessControl.processExternalNfcUid(uid,length,reserved,count);
}

bool HomeKeyModule::nfcIdReserved(uint16_t id)
{
    std::unique_lock<std::recursive_mutex> guard(store.mutex,std::try_to_lock);
    if(!guard.owns_lock())return true;
    if(homeKeyConfigured && !policyLoaded)return true;
    if(!homeKeyConfigured)return false;
    for(const auto& item:activeMappings())
        if(item.accessId==id&&!store.isRevoked(item.issuer,item.endpoint))return true;
    return false;
}

void HomeKeyModule::processInputKo(GroupObject& ko)
{
    if (!knx.configured() || !ParamHK_Enabled) return;
    if (started && ko.asap() == HK_KoLockFeedback)
        provisioning->feedback(ko.value(DPT_Switch));
    else if (ParamACC_EnableRawData && ko.asap() == HK_KoCommissioningControl)
    {
        requestCommissioning(ko.value(DPT_Switch));
    }
    else if (ParamACC_EnableRawData && ko.asap() == HK_KoEndpointIndex)
    {
        endpointIndex = ko.value(DPT_Value_1_Ucount);
        endpointDirty = true;
    }
}

void HomeKeyModule::publishEndpoint()
{
    char high[15] = {}, low[15] = {}, endpoint[15] = {};
    static const char digits[] = "0123456789abcdef";
    auto encode = [&](const uint8_t* data, size_t size, char* out)
    {
        for (size_t i = 0; i < size; ++i) { out[2*i] = digits[data[i] >> 4]; out[2*i+1] = digits[data[i] & 15]; }
    };
    size_t index = 0;
    for (const auto& issuer : store.entries)
        for (const auto& item : issuer.endpoints)
        {
            if (index++ != endpointIndex || issuer.id.size() != 8 || item.id.size() != 6) continue;
            encode(issuer.id.data(), 4, high); encode(issuer.id.data() + 4, 4, low); encode(item.id.data(), 6, endpoint);
        }
    KoHK_IssuerHigh.value(high, DPT_String_ASCII);
    KoHK_IssuerLow.value(low, DPT_String_ASCII);
    KoHK_EndpointId.value(endpoint, DPT_String_ASCII);
    endpointDirty = false;
    inspectedGeneration = store.generation;
}

void HomeKeyModule::updateStatus()
{
    // Match ETS visibility and avoid sending diagnostics or endpoint identities
    // through previously associated objects when raw data is disabled.
    if (!ParamHK_Enabled || !ParamACC_EnableRawData)
    {
        reportedCommissioning = reportedResult = reportedAuthorization = -1;
        endpointDirty = true;
        return;
    }
    int open = pairingAllowed();
    int result = commissioningResult.load();
    if (open != reportedCommissioning) { KoHK_CommissioningOpen.value(bool(open), DPT_Switch); reportedCommissioning = open; }
    if (result != reportedResult) { KoHK_CommissioningResult.value(uint8_t(result), DPT_Value_1_Ucount); reportedResult = result; }
    if (millis() - statusAt < 1000 && !endpointDirty && reportedAuthorization >= 0) return;
    std::unique_lock<std::recursive_mutex> guard(store.mutex, std::try_to_lock);
    if (!guard.owns_lock()) return; // Never wait for an APDU or credential write on the KNX thread.
    statusAt = millis();
    uint8_t status = authorizationStatus();
    if (status != reportedAuthorization) { KoHK_AuthorizationStatus.value(uint8_t(status), DPT_Value_1_Ucount); reportedAuthorization = status; }
    if (endpointDirty || inspectedGeneration != store.generation) publishEndpoint();
}

uint8_t HomeKeyModule::authorizationStatus()
{
    uint8_t status = (!store.healthy || fatal) ? 6 : !enabled ? 0 : !etsAuthorization ? 1 : !etsTableValid ? 3 : 2;
    if (status == 2)
        for (const auto& mapping : etsMappings)
        {
            if (!openknxAccessControl.homeKeyAccessIdAvailable(mapping.accessId)) { status = 5; break; }
            if (!store.contains(mapping.issuer, mapping.endpoint)) status = 4;
        }
    return status;
}

uint8_t HomeKeyModule::requestCommissioning(bool open)
{
    if (!open) { cancelCommissioning(); commissioningResult = 2; return HomeKeyOnline::Ok; }
    if (!started || !enabled) { commissioningResult = 3; return HomeKeyOnline::Disabled; }
    if (!ParamHK_PairingPermitted) { commissioningResult = 3; return HomeKeyOnline::NotPermitted; }
    uint32_t deadline = pairingUntil.load();
    if (deadline && !HomeKey::windowOpen(millis(), deadline)) cancelCommissioning();
    commissioningResult = 7; // Pending: the worker publishes the terminal result.
    if (!provisioning->openPairingWindow()) { commissioningResult = 5; return HomeKeyOnline::QueueBusy; }
    return HomeKeyOnline::Ok;
}

bool HomeKeyModule::processFunctionProperty(uint8_t object, uint8_t property, uint8_t length, uint8_t* data, uint8_t* result, uint8_t& resultLength)
{
    if (object != HomeKeyOnline::Object || property != HomeKeyOnline::Property) return false;
    result[0] = HomeKeyOnline::Invalid; result[1] = HomeKeyOnline::Version; resultLength = 2;
    HomeKeyOnline::Request request;
    if (!HomeKeyOnline::decode(data, length, request)) return true;
    if (!knx.configured()) { result[0] = HomeKeyOnline::Unconfigured; return true; }
    if (request.command == 1 || request.command == 2)
    {
        result[0] = requestCommissioning(request.command == 1);
        return true;
    }
    std::unique_lock<std::recursive_mutex> guard(store.mutex, std::try_to_lock);
    if (!guard.owns_lock()) { result[0] = HomeKeyOnline::Busy; return true; }
    if (!store.healthy || fatal) { result[0] = HomeKeyOnline::Unavailable; return true; }
    if(request.command>=5) {
        if(request.command==5) {
            if(!enabled||!openknxNfc.active()){result[0]=HomeKeyOnline::Disabled;return true;}
            if(!ParamHK_PairingPermitted){result[0]=HomeKeyOnline::NotPermitted;return true;}
            uint32_t token=esp_random();if(!token)token=1;
            identification.start(token,millis());++store.generation;
            result[0]=HomeKeyOnline::Ok;HomeKeyOnline::put32(result+2,token);resultLength=6;return true;
        }
        if(request.command==6||request.command==7) {
            if(request.generation!=identification.session){result[0]=HomeKeyOnline::Stale;return true;}
            if(request.command==7){identification.cancel();++store.generation;result[0]=HomeKeyOnline::Ok;return true;}
            if(!ParamHK_PairingPermitted){identification.cancel();result[0]=HomeKeyOnline::NotPermitted;return true;}
            if(!enabled||!identification.active(millis())){identification.cancel();result[0]=HomeKeyOnline::Expired;return true;}
            result[0]=HomeKeyOnline::Ok;result[2]=identification.ready;result[3]=identification.revoked;
            HomeKeyOnline::put32(result+4,identification.session);
            std::copy(identification.issuer.begin(),identification.issuer.end(),result+8);
            std::copy(identification.endpoint.begin(),identification.endpoint.end(),result+16);resultLength=22;return true;
        }
        if(request.generation!=store.generation){result[0]=HomeKeyOnline::Stale;return true;}
        HomeKey::IssuerId iid{};HomeKey::EndpointId eid{};
        std::copy(data+5,data+13,iid.begin());std::copy(data+13,data+19,eid.begin());
        if(request.command==10){result[0]=HomeKeyOnline::Ok;result[2]=store.isRevoked(iid,eid);result[3]=store.contains(iid,eid);resultLength=4;return true;}
        if(request.command==9&&!ParamHK_PairingPermitted){result[0]=HomeKeyOnline::NotPermitted;return true;}
        if (request.command == 8 && !store.isRevoked(iid,eid) && store.revocations.size() >= HomeKeyCredentialStore::MaxRevocations) {
            result[0] = HomeKeyOnline::RevocationsFull; return true;
        }
        identification.cancel();cancelCommissioning();
        const bool saved=request.command==8 ? store.removeEndpoint(iid,eid) : store.restoreEndpoint(iid,eid);
        if(saved)etsMappings.erase(std::remove_if(etsMappings.begin(),etsMappings.end(),[&](const auto& m){return m.issuer==iid&&m.endpoint==eid;}),etsMappings.end());
        result[0]=saved ? HomeKeyOnline::Ok : HomeKeyOnline::Unavailable;endpointDirty=true;return true;
    }
    uint8_t count = 0;
    for (const auto& issuer : store.entries) count += issuer.endpoints.size();
    if (request.command == 0)
    {
        resultLength = HomeKeyOnline::status(result, enabled, pairingPermitted, pairingAllowed(), commissioningResult,
                                             authorizationStatus(), count, store.generation);
        return true;
    }
    if (request.generation != store.generation) { result[0] = HomeKeyOnline::Stale; return true; }
    uint8_t index = 0;
    for (const auto& issuer : store.entries)
        for (const auto& item : issuer.endpoints)
        {
            if (index++ != request.index) continue;
            if (issuer.id.size() != 8 || item.id.size() != 6) { result[0] = HomeKeyOnline::Unavailable; return true; }
            if (request.command == 4)
            {
                HomeKey::IssuerId issuerId{};
                HomeKey::EndpointId endpointId{};
                std::copy(issuer.id.begin(), issuer.id.end(), issuerId.begin());
                std::copy(item.id.begin(), item.id.end(), endpointId.begin());
                cancelCommissioning();
                result[0] = store.removeEndpoint(issuerId, endpointId) ? HomeKeyOnline::Ok : HomeKeyOnline::Unavailable;
                endpointDirty = true;
                return true;
            }
            resultLength = HomeKeyOnline::endpoint(result, request.index, count, store.generation, issuer.id.data(), item.id.data());
            return true;
        }
    result[0] = HomeKeyOnline::NotFound;
    return true;
}

bool HomeKeyModule::processFunctionPropertyState(uint8_t object, uint8_t property, uint8_t length, uint8_t* data, uint8_t* result, uint8_t& resultLength)
{
    if (object != HomeKeyOnline::Object || property != HomeKeyOnline::Property) return false;
    HomeKeyOnline::Request request;
    if (!HomeKeyOnline::decode(data, length, request) || (request.command != 0 && request.command != 3 && request.command != 6 && request.command != 10))
    {
        result[0] = HomeKeyOnline::Invalid; result[1] = HomeKeyOnline::Version; resultLength = 2;
        return true;
    }
    return processFunctionProperty(object, property, length, data, result, resultLength);
}

namespace
{
    template<size_t N> bool parseId(const std::string& value, std::array<uint8_t, N>& result)
    {
        if (value.size() != N * 2) return false;
        auto nibble = [](char ch) -> int { if (ch >= '0' && ch <= '9') return ch - '0'; if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10; if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10; return -1; };
        for (size_t i = 0; i < N; ++i)
        {
            int high = nibble(value[i * 2]), low = nibble(value[i * 2 + 1]);
            if (high < 0 || low < 0) return false;
            result[i] = (high << 4) | low;
        }
        return true;
    }

}

bool HomeKeyModule::processCommand(const std::string cmd, bool diagnoseKo)
{
    const auto wifi = HomeKeyWifi::command(cmd, diagnoseKo, started,
        [this](const std::string& ssid, const std::string& password)
        { return provisioning && provisioning->wifi(ssid, password); });
    switch (wifi)
    {
        case HomeKeyWifi::Result::LocalOnly: logInfoP("WiFi configuration requires local console"); return true;
        case HomeKeyWifi::Result::Invalid: logInfoP("wifi <SSID> <password> (SSID: 1-32 bytes, password: 0-64 bytes)"); return true;
        case HomeKeyWifi::Result::Connecting: logInfoP("WiFi credentials saved; connecting"); return true;
        case HomeKeyWifi::Result::Queued: logInfoP("WiFi configuration queued"); return true;
        case HomeKeyWifi::Result::Busy: logInfoP("WiFi configuration busy; retry"); return true;
        case HomeKeyWifi::Result::Failed: logInfoP("WiFi configuration could not be saved or started; retry"); return true;
        case HomeKeyWifi::Result::NotHandled: break;
    }
    if (cmd == "hk memory")
    {
        showMemory();
        return true;
    }
    if (cmd != "hk" && cmd.rfind("hk ", 0) != 0) return false;
    // Credential policy and network secrets can only be configured at the local console.
    if (diagnoseKo) { logInfoP("Home Key configuration requires local console"); return true; }
    std::istringstream stream(cmd);
    std::string prefix, operation, issuer, endpoint;
    stream >> prefix >> operation;
    if ((operation == "pair" || operation == "code") && started && enabled && pairingPermitted)
    {
        std::string code, previous, extra; stream >> code >> previous;
        if (HomeKey::validPairingCode(code) && !(stream >> extra) &&
            (previous.empty() || (previous.size() == 8 && previous.find_first_not_of("0123456789") == std::string::npos)))
        {
            bool queued = operation == "pair" ? provisioning->pairingCode(code, previous) : provisioning->configurePairingCode(code, previous);
            logInfoP("Commissioning command %s", queued ? "queued" : "busy; retry");
        }
        else logWarningP("hk code/pair <new8digits> [old8digits]; existing code required for replacement");
        volatile char* secret = code.data();
        for (size_t i = 0; i < code.size(); ++i) secret[i] = 0;
        secret = previous.data();
        for (size_t i = 0; i < previous.size(); ++i) secret[i] = 0;
    }
    else
    {
        std::unique_lock<std::recursive_mutex> guard(store.mutex, std::try_to_lock);
        if (!guard.owns_lock()) { logInfoP("Home Key busy; retry command"); return true; }
        if (operation == "list")
        {
            for (const auto& entry : store.entries)
                for (const auto& item : entry.endpoints)
                {
                    HomeKey::IssuerId iid{}; HomeKey::EndpointId eid{};
                    if (entry.id.size() != iid.size() || item.id.size() != eid.size()) continue;
                    std::copy(entry.id.begin(), entry.id.end(), iid.begin());
                    std::copy(item.id.begin(), item.id.end(), eid.begin());
                    logInfoP("Issuer=%s endpoint=%s accessId=%u", hex(iid).c_str(), hex(eid).c_str(), lookupAccessId(iid, eid));
                }
        }
        else if (operation == "map" || operation == "unmap")
        {
            if (etsAuthorization) { logInfoP("ETS authorization table active; edit permissions in ETS"); return true; }
            stream >> issuer >> endpoint;
            HomeKey::Mapping mapping;
            unsigned id;
            if (!parseId(issuer, mapping.issuer) || !parseId(endpoint, mapping.endpoint)) return true;
            if (operation == "map")
            {
                if (!(stream >> id) || id >= MAX_NFCS || !store.healthy || !store.contains(mapping.issuer, mapping.endpoint) ||
                    !openknxAccessControl.homeKeyAccessIdAvailable(id)) { logInfoP("Mapping rejected"); return true; }
                mapping.accessId = id;
                if (!HomeKey::bind(store.mappings, mapping)) { logInfoP("ID already reserved or mapping limit reached"); return true; }
            }
            else
                store.mappings.erase(std::remove_if(store.mappings.begin(), store.mappings.end(), [&](const auto& value)
                    { return value.issuer == mapping.issuer && value.endpoint == mapping.endpoint; }), store.mappings.end());
            store.save();
            logInfoP("Mapping update %s", store.healthy ? "saved" : "FAILED");
        }
        else if (operation == "reset") { store.clear(); cancelCommissioning(); }
        else logInfoP("hk wifi <SSID> <password> | hk code <new8digits> [old8digits] | hk pair <new8digits> [old8digits] | hk list | hk map <issuer16hex> <endpoint12hex> <id0..1499> | hk unmap <issuer> <endpoint> | hk reset");
    }
    return true;
}

void HomeKeyModule::showMemory()
{
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    logInfoP("Internal heap: free=%u largest=%u minimum=%u bytes",
        unsigned(heap_caps_get_free_size(caps)), unsigned(heap_caps_get_largest_free_block(caps)),
        unsigned(heap_caps_get_minimum_free_size(caps)));
    logInfoP("Task stack minimum free/size: NFC=%u/%u HAP=%u/%u bytes",
        unsigned(openknxNfc.stackHeadroom()),
        unsigned(OPENKNX_NFC_STACK_SIZE),
        hapHandle ? unsigned(uxTaskGetStackHighWaterMark(hapHandle)) : 0,
        unsigned(OPENKNX_HOMEKEY_HAP_STACK_SIZE));

}

void HomeKeyModule::showHelp()
{
    openknx.console.printHelpLine("hk memory", "Internal heap and Home Key task stack headroom in bytes");
    openknx.console.printHelpLine("wifi <SSID> <password>", "Save WiFi and connect; available before ETS setup (local console)");
    openknx.console.printHelpLine("hk", "Show Home Key command syntax (local console)");
    openknx.console.printHelpLine("hk wifi <SSID> <password>", "Save WiFi and connect; also works before ETS setup");
    openknx.console.printHelpLine("hk code <new8digits> [old8digits]", "Save setup code; replacement requires old code, also via web console");
    openknx.console.printHelpLine("hk pair <new8digits> [old8digits]", "Save code and open pairing for 5 minutes; replacement requires old code");
    openknx.console.printHelpLine("hk list", "List provisioned issuer/endpoint IDs and their access IDs");
    openknx.console.printHelpLine("hk map <issuer16hex> <endpoint12hex> <id0..1499>", "Assign a provisioned endpoint to an NFC action ID; ETS authorization must be off");
    openknx.console.printHelpLine("hk unmap <issuer16hex> <endpoint12hex>", "Remove a local endpoint mapping; ETS authorization must be off");
    openknx.console.printHelpLine("hk reset", "Clear Home Key credentials/local mappings and close pairing; keep WiFi and ETS policy");
}
#endif

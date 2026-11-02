#if defined(OPENKNX_HOMEKEY_ENABLED) && defined(ARDUINO_ARCH_ESP32)
#include "HomeKeyModule.h"
#include "HomeKeyProvisioning.h"
#include "HomeKeyOnline.h"
#include "HomeKeyWifi.h"
#include "Pn7160Reader.hpp"
#include "AccessControl.h"
#include <algorithm>
#include <sstream>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <sodium.h>

HomeKeyModule openknxHomeKey;

void HomeKeyModule::setup()
{
    if (sodium_init() < 0 || !store.begin())
    {
        logErrorP("Home Key storage/crypto unavailable; access disabled");
        return;
    }
    events = xQueueCreate(4, sizeof(Event));
    if (!events) { fatal = true; logErrorP("Home Key event queue unavailable"); return; }
    provisioning = std::make_unique<HomeKeyProvisioning>(*this);
#ifdef OPENKNX_HOMEKEY_WEB
    setupWeb();
#endif
    logInfoP("Home Key ready for ETS enable; no endpoints authorized automatically");
}

void HomeKeyModule::loop()
{
    if (!knx.configured()) { enabled = false; policyLoaded = false; webCodeAllowed = false; cancelCommissioning(); return; }
    if (!events) { enabled = false; webCodeAllowed = false; cancelCommissioning(); updateStatus(); return; }
    if (!policyLoaded)
    {
        std::unique_lock<std::recursive_mutex> guard(store.mutex, std::try_to_lock);
        if (!guard.owns_lock()) { enabled = false; webCodeAllowed = false; return; }
        static_assert(HK_Authorization02Enabled - HK_Authorization01Enabled == HomeKey::EtsRowBytes, "ETS row layout changed");
        etsAuthorization = ParamHK_EtsAuthorization;
        etsTableValid = !etsAuthorization || HomeKey::parseEtsRows(knx.paramData(HK_Authorization01Enabled), HomeKey::EtsRows * HomeKey::EtsRowBytes, etsMappings);
        ++store.generation; // Downloaded policy invalidates every pending result.
        policyLoaded = true;
        endpointDirty = true;
        reportedAuthorization = reportedCommissioning = reportedResult = -1;
    }
    enabled = ParamHK_Enabled && !fatal;
    allowUid = ParamHK_AllowUid;
    pairingPermitted = ParamHK_PairingPermitted;
    if (enabled && !started)
    {
        started = true;
        if (xTaskCreate([](void* self) { static_cast<HomeKeyModule*>(self)->readerTask(); }, "HomeKeyNfc", 12288, this, 1, &readerHandle) != pdPASS ||
            xTaskCreate([](void* self)
            {
                auto& module = *static_cast<HomeKeyModule*>(self);
                module.provisioning->begin();
                for (;;) { module.provisioning->poll(); vTaskDelay(pdMS_TO_TICKS(10)); }
            }, "HomeKeyHap", 12288, this, 1, &hapHandle) != pdPASS)
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
    Event event;
    // Bound work per iteration. Never write KNX objects from either worker task.
    for (unsigned i = 0; i < 4 && xQueueReceive(events, &event, 0) == pdTRUE; ++i)
    {
        if (!enabled) continue;
        std::unique_lock<std::recursive_mutex> guard(store.mutex, std::try_to_lock);
        if (!guard.owns_lock())
        {
            xQueueSend(events, &event, 0);
            break;
        }
        bool granted = false;
        if (store.healthy && event.generation == store.generation)
        {
            if (event.kind == EventKind::Authenticated && store.contains(event.issuer, event.endpoint))
            {
                auto id = lookupAccessId(event.issuer, event.endpoint);
                granted = id != HomeKey::InvalidId && openknxAccessControl.processHomeKeyCredential(id);
            }
            else if (event.kind == EventKind::Uid && allowUid)
            {
                uint16_t reserved[HomeKey::MaxMappings];
                size_t count = 0;
                for (const auto& mapping : activeMappings()) reserved[count++] = mapping.accessId;
                granted = openknxAccessControl.processExternalNfcUid(event.uid, event.uidLength, reserved, count);
            }
        }
        if (!granted) openknxAccessControl.rejectExternalCredential();
    }
}

void HomeKeyModule::enqueue(const Event& event)
{
    if (enabled && events)
        xQueueSend(events, &event, 0); // Overflow drops the event, never grants access.
}

bool HomeKeyModule::pairingAllowed() const
{
    return enabled && pairingPermitted && HomeKey::windowOpen(millis(), pairingUntil.load(), pairingEpoch.load(), commissioningEpoch.load());
}

void HomeKeyModule::readerTask()
{
#ifdef HOMEKEY_READER_I2C
    reader = std::make_unique<Pn7160Reader>(std::array<uint8_t, 4>{}, HOMEKEY_IRQ_PIN, HOMEKEY_VEN_PIN, ecp);
#else
    reader = std::make_unique<Pn7160Reader>(std::array<uint8_t, 4>{HOMEKEY_SPI_CS, HOMEKEY_SPI_SCLK, HOMEKEY_SPI_MISO, HOMEKEY_SPI_MOSI}, HOMEKEY_IRQ_PIN, HOMEKEY_VEN_PIN, ecp);
#endif
    bool connected = false;
    uint32_t appliedGeneration = UINT32_MAX;
    for (;;)
    {
        if (!enabled || !provisioningReady)
        {
            if (connected) reader->stop();
            connected = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        bool healthy;
        uint32_t generation;
        {
            std::lock_guard<std::recursive_mutex> guard(store.mutex);
            healthy = store.healthy;
            generation = store.generation;
            if (generation != appliedGeneration)
            {
                ecp.fill(0);
                if (store.identity.group_identifier.size() == 8)
                {
                    const uint8_t head[8] = {0x6a, 2, 0xcb, 2, 6, 2, 0x11, 0};
                    std::copy(head, head + 8, ecp.begin());
                    std::copy(store.identity.group_identifier.begin(), store.identity.group_identifier.end(), ecp.begin() + 8);
                    // CRC-A is owned with the ECP buffer; PN7161 sends the first 16 bytes.
                    uint16_t crc = 0x6363;
                    for (size_t i = 0; i < 16; ++i)
                    {
                        uint8_t byte = ecp[i] ^ (crc & 0xff);
                        byte ^= byte << 4;
                        crc = (crc >> 8) ^ (uint16_t(byte) << 8) ^ (uint16_t(byte) << 3) ^ (byte >> 4);
                    }
                    ecp[16] = crc; ecp[17] = crc >> 8;
                }
            }
        }
        if (!healthy) { if (connected) reader->stop(); connected = false; vTaskDelay(pdMS_TO_TICKS(500)); continue; }
        if (!connected)
        {
            connected = reader->init() && reader->beginDiscovery();
            if (!connected) { reader->stop(); vTaskDelay(pdMS_TO_TICKS(5000)); continue; }
            appliedGeneration = generation;
        }
        else if (generation != appliedGeneration)
        {
            if (!reader->updateECP()) { reader->stop(); connected = false; continue; }
            appliedGeneration = generation;
        }
        std::vector<uint8_t> uid;
        std::array<uint8_t, 2> atqa{};
        uint8_t sak = 0;
        if (reader->pollForTag(uid, atqa, sak, 250))
        {
            handleTag(uid, sak);
            while (enabled && reader->isTagStillPresent()) vTaskDelay(pdMS_TO_TICKS(100));
            reader->releaseTag();
        }
        if (!reader->healthCheck()) { reader->stop(); connected = false; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void HomeKeyModule::processInputKo(GroupObject& ko)
{
    if (!knx.configured()) return;
    if (started && ko.asap() == HK_KoLockFeedback)
        provisioning->feedback(ko.value(DPT_Switch));
    else if (ko.asap() == HK_KoCommissioningControl)
    {
        requestCommissioning(ko.value(DPT_Switch));
    }
    else if (ko.asap() == HK_KoEndpointIndex)
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
    if (!HomeKeyOnline::decode(data, length, request) || (request.command != 0 && request.command != 3))
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
    template<typename Bytes> std::string hex(const Bytes& bytes)
    {
        static const char digits[] = "0123456789abcdef";
        std::string result;
        for (auto byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
        return result;
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
    logInfoP("HomeKey stack minimum free: NFC=%u HAP=%u bytes (12288 each)",
        readerHandle ? unsigned(uxTaskGetStackHighWaterMark(readerHandle)) : 0,
        hapHandle ? unsigned(uxTaskGetStackHighWaterMark(hapHandle)) : 0);

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

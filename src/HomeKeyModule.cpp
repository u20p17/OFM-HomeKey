#if defined(OPENKNX_HOMEKEY_ENABLED) && defined(ARDUINO_ARCH_ESP32)
#include "HomeKeyModule.h"
#include "HomeKeyProvisioning.h"
#include "Pn7160Reader.hpp"
#include "AccessControl.h"
#include <algorithm>
#include <sstream>
#include <esp_timer.h>
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
    logInfoP("Home Key ready for ETS enable; no endpoints authorized automatically");
}

void HomeKeyModule::loop()
{
    if (!knx.configured()) { enabled = false; policyLoaded = false; cancelCommissioning(); return; }
    if (!events) { enabled = false; cancelCommissioning(); updateStatus(); return; }
    if (!policyLoaded)
    {
        std::unique_lock<std::recursive_mutex> guard(store.mutex, std::try_to_lock);
        if (!guard.owns_lock()) { enabled = false; return; }
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
    reader = std::make_unique<Pn7160Reader>(std::array<uint8_t, 4>{HOMEKEY_SPI_CS, HOMEKEY_SPI_SCLK, HOMEKEY_SPI_MISO, HOMEKEY_SPI_MOSI}, HOMEKEY_IRQ_PIN, HOMEKEY_VEN_PIN, ecp);
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
        if (!ko.value(DPT_Switch)) { cancelCommissioning(); commissioningResult = 2; }
        else if (!started || !enabled || !ParamHK_PairingPermitted) commissioningResult = 3;
        else
        {
            uint32_t deadline = pairingUntil.load();
            if (deadline && !HomeKey::windowOpen(millis(), deadline)) cancelCommissioning();
            if (!provisioning->openPairingWindow()) commissioningResult = 5;
        }
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
    int status = (!store.healthy || fatal) ? 6 : !enabled ? 0 : !etsAuthorization ? 1 : !etsTableValid ? 3 : 2;
    if (status == 2)
        for (const auto& mapping : etsMappings)
        {
            if (!openknxAccessControl.homeKeyAccessIdAvailable(mapping.accessId)) { status = 5; break; }
            if (!store.contains(mapping.issuer, mapping.endpoint)) status = 4;
        }
    if (status != reportedAuthorization) { KoHK_AuthorizationStatus.value(uint8_t(status), DPT_Value_1_Ucount); reportedAuthorization = status; }
    if (endpointDirty || inspectedGeneration != store.generation) publishEndpoint();
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
    if (cmd != "hk" && cmd.rfind("hk ", 0) != 0) return false;
    // Credential policy and network secrets can only be configured at the local console.
    if (diagnoseKo) { logInfoP("Home Key configuration requires local console"); return true; }
    std::istringstream stream(cmd);
    std::string prefix, operation, issuer, endpoint;
    stream >> prefix >> operation;
    if (operation == "wifi" && started)
    {
        std::string ssid, password;
        stream >> ssid;
        std::getline(stream >> std::ws, password);
        if (!ssid.empty() && ssid.size() <= 32 && password.size() <= 64)
            logInfoP("WiFi configuration %s", provisioning->wifi(ssid, password) ? "queued" : "busy; retry");
    }
    else if ((operation == "pair" || operation == "code") && started && enabled && pairingPermitted)
    {
        std::string code; stream >> code;
        if (HomeKey::validPairingCode(code))
        {
            bool queued = operation == "pair" ? provisioning->pairingCode(code) : provisioning->configurePairingCode(code);
            logInfoP("Commissioning command %s", queued ? "queued" : "busy; retry");
        }
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
        else logInfoP("hk wifi <SSID> <password> | hk code <8digits> | hk pair <8digits> | hk list | hk map <issuer16hex> <endpoint12hex> <id0..1499> | hk unmap <issuer> <endpoint> | hk reset");
    }
    return true;
}
#endif

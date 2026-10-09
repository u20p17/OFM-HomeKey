#if defined(OPENKNX_HOMEKEY_ENABLED) && defined(ARDUINO_ARCH_ESP32)
// NFC service and controller lifecycle adapted from rednblkx/HomeKey-ESP32 (MIT).
#include "HomeKeyProvisioning.h"
#include "HomeKeyModule.h"
#include <sodium.h>
#ifdef STRINGIFY
#undef STRINGIFY
#endif
#include "HomeSpan.h"
#include "HAP.h"
#include "HK_HomeKit.h"
#include <algorithm>
#include <atomic>
#include <mbedtls/sha256.h>
#include <esp_mac.h>
#include <esp_random.h>

CUSTOM_CHAR(ConfigurationState, 263, PR+EV, UINT16, 0, 0, 1, true)
CUSTOM_CHAR(HardwareFinish, 26C, PR, TLV_ENC, NULL_TLV, NULL_TLV, NULL_TLV, true)
CUSTOM_CHAR(NFCAccessControlPoint, 264, PR+PW+WR, TLV_ENC, NULL_TLV, NULL_TLV, NULL_TLV, true)
CUSTOM_CHAR(NFCAccessSupportedConfiguration, 265, PR, TLV_ENC, NULL_TLV, NULL_TLV, NULL_TLV, true)
CUSTOM_CHAR(LockControlPoint, 19, PW, TLV_ENC, NULL_TLV, NULL_TLV, NULL_TLV, true)

namespace
{
    void randomUnavailableCode(char (&code)[9])
    {
        do { snprintf(code, sizeof(code), "%08lu", static_cast<unsigned long>(10000000 + esp_random() % 89999999)); }
        while (!HomeKey::validPairingCode(code));
    }
    HomeKeyProvisioning* active = nullptr;
    SpanCharacteristic* lockState = nullptr;
    SpanCharacteristic* lockTarget = nullptr;
    std::atomic<bool> ready{false};
    struct AccessoryInfo : Service::AccessoryInformation
    {
        AccessoryInfo() { opt.push_back(&_CUSTOM_HardwareFinish); }
    };
    struct LockService : Service::LockMechanism
    {
        LockService()
        {
            lockState = new Characteristic::LockCurrentState(3); // Unknown until actual KNX feedback.
            lockTarget = new Characteristic::LockTargetState(1);
        }
        boolean update() override { return false; } // No implicit Home app unlock authority.
    };
    struct ManagementService : SpanService
    {
        ManagementService() : SpanService("44", "LockManagement", true)
        {
            req.push_back(&_CUSTOM_LockControlPoint); req.push_back(&hapChars.Version);
            new Characteristic::LockControlPoint(); new Characteristic::Version();
        }
    };
    struct NfcService : SpanService
    {
        HomeKeyModule& module;
        SpanCharacteristic* control;
        SpanCharacteristic* state;
        NfcService(HomeKeyModule& owner) : SpanService("266", "NFCAccess", true), module(owner)
        {
            std::lock_guard<std::recursive_mutex> guard(owner.store.mutex);
            req.push_back(&_CUSTOM_ConfigurationState);
            req.push_back(&_CUSTOM_NFCAccessControlPoint);
            req.push_back(&_CUSTOM_NFCAccessSupportedConfiguration);
            state = new Characteristic::ConfigurationState(owner.store.identity.private_key.size() == 32 ? 1 : 0);
            TLV8 config(nullptr, 0); config.add(1, 0x10); config.add(2, 0x10);
            new Characteristic::NFCAccessSupportedConfiguration(config);
            control = new Characteristic::NFCAccessControlPoint();
        }
        boolean update() override
        {
            if (!control->updated()) return true;
            TLV8 input(nullptr, 0); control->getNewTLV(input);
            if (!input.pack_size() || input.pack_size() > 4096) return false;
            auto operation = input.find(1);
            if (operation == input.end() || input.len(operation) != 1) return false;
            uint8_t value = (*operation)[0];
            // Read and revocation remain available after commissioning closes.
            if (value != 1 && value != 3 && !(value == 2 && module.pairingAllowed())) return false;
            std::vector<uint8_t> data(input.pack_size()); input.pack(data.data());
            std::lock_guard<std::recursive_mutex> guard(module.store.mutex);
            if (!module.store.healthy || (value == 2 && !module.pairingAllowed())) return false;
            HK_HomeKit context(module.store, [this]() { module.store.eraseReaderKey(); }, data);
            auto response = context.processResult();
            module.store.save();
            if (response.empty() || !module.store.healthy) return false;
            TLV8 output(nullptr, 0); output.unpack(response.data(), response.size());
            control->setTLV(output, false);
            state->setVal(module.store.identity.private_key.size() == 32 ? 1 : 0);
            return true;
        }
    };
}

void HomeKeyProvisioning::begin()
{
    std::unique_lock<std::shared_mutex> guard(homeSpan.getMutex());
    active = this;
    commands = xQueueCreate(4, sizeof(Command));
    homeSpan.setSerialInputDisable(true); // OpenKNX owns the console.
    homeSpan.setLogLevel(-1);
    char unavailableCode[9];
    randomUnavailableCode(unavailableCode);
    homeSpan.setPairingCode(unavailableCode);
    homeSpan.begin(Category::Locks, "OpenKNX Home Key", "OpenKNX-HK-", "OpenKNX");
    new SpanAccessory();
    new AccessoryInfo();
    new Characteristic::Identify(); new Characteristic::Manufacturer("OpenKNX");
    new Characteristic::Model("XIAO ESP32-S3 PN7161"); new Characteristic::Name("OpenKNX Home Key");
    new Characteristic::FirmwareRevision("0.1.0");
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char serial[13]; snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    new Characteristic::SerialNumber(serial);
    uint8_t finishBytes[] = {1, 4, 0xce, 0xd5, 0xda, 0};
    TLV8 finish(nullptr, 0); finish.unpack(finishBytes, sizeof(finishBytes)); new Characteristic::HardwareFinish(finish);
    new Service::HAPProtocolInformation(); new Characteristic::Version();
    new ManagementService(); new LockService(); new NfcService(module);
    homeSpan.setControllerCallback([]() { if (active) active->synchronizeControllers(); });
    guard.unlock();
    homeSpan.poll(); // HAP loads its persisted controllers on the first poll.
    guard.lock();
    synchronizeControllers();
    module.provisioningReady = true;
    ready = true;
}
void HomeKeyProvisioning::poll()
{
    Command command;
    while (commands && xQueueReceive(commands, &command, 0) == pdTRUE)
    {
        if (command.pair)
        {
            if (module.pairingAllowed()) homeSpan.setPairingCode(command.code, false);
        }
        else homeSpan.setWifiCredentials(command.ssid, command.password);
        volatile char* secret = command.password;
        for (size_t i = 0; i < sizeof(command.password); ++i) secret[i] = 0;
    }
    int feedback = pendingFeedback.exchange(-1);
    if (feedback >= 0)
    {
        lockState->setVal(feedback ? 1 : 0); lockTarget->setVal(feedback ? 1 : 0);
    }
    homeSpan.poll();
    bool open = module.pairingAllowed();
    if (commissioningWasOpen && !open)
    {
        std::unique_lock<std::shared_mutex> guard(homeSpan.getMutex());
        char code[9];
        randomUnavailableCode(code);
        homeSpan.setPairingCode(code, false);
    }
    commissioningWasOpen = open;
}
bool HomeKeyProvisioning::wifi(const std::string& ssid, const std::string& password)
{
    if (!ready || !commands) return false;
    Command command;
    snprintf(command.ssid, sizeof(command.ssid), "%s", ssid.c_str());
    snprintf(command.password, sizeof(command.password), "%s", password.c_str());
    bool accepted = xQueueSend(commands, &command, 0) == pdTRUE;
    volatile char* secret = command.password;
    for (size_t i = 0; i < sizeof(command.password); ++i) secret[i] = 0;
    return accepted;
}
bool HomeKeyProvisioning::pairingCode(const std::string& code)
{
    if (!ready || !commands) return false;
    Command command; command.pair = true;
    snprintf(command.code, sizeof(command.code), "%s", code.c_str());
    return xQueueSend(commands, &command, 0) == pdTRUE;
}
void HomeKeyProvisioning::feedback(bool locked)
{
    pendingFeedback = locked ? 1 : 0;
}
void HomeKeyProvisioning::synchronizeControllers()
{
    std::lock_guard<std::recursive_mutex> guard(module.store.mutex);
    if (!module.store.healthy) return;
    if (HAPClient::nAdminControllers() == 0) { module.store.clear(); return; }
    std::vector<std::vector<uint8_t>> retained;
    for (auto it = homeSpan.controllerListBegin(); it != homeSpan.controllerListEnd(); ++it)
    {
        std::vector<uint8_t> hashInput = {'k','e','y','-','i','d','e','n','t','i','f','i','e','r'};
        hashInput.insert(hashInput.end(), it->getLTPK(), it->getLTPK() + 32);
        uint8_t hash[32];
        if (mbedtls_sha256(hashInput.data(), hashInput.size(), hash, 0) != 0) { module.store.healthy = false; return; }
        std::vector<uint8_t> id(hash, hash + 8); retained.push_back(id);
        auto existing = std::find_if(module.store.entries.begin(), module.store.entries.end(), [&](const auto& issuer) { return issuer.id == id; });
        if (existing == module.store.entries.end())
        {
            ddk::Issuer issuer; issuer.id = id; issuer.public_key.assign(it->getLTPK(), it->getLTPK() + 32);
            module.store.entries.push_back(std::move(issuer));
        }
    }
    module.store.entries.erase(std::remove_if(module.store.entries.begin(), module.store.entries.end(), [&](const auto& issuer)
        { return std::find(retained.begin(), retained.end(), issuer.id) == retained.end(); }), module.store.entries.end());
    module.store.save();
}
#endif

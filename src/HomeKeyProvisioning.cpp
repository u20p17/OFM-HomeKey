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
    if (nvs_open_from_partition("homekey", "commissioning", NVS_READWRITE, &codeHandle) == ESP_OK)
    {
        size_t size = sizeof(configuredCode);
        if (nvs_get_blob(codeHandle, "code", configuredCode, &size) != ESP_OK || size != sizeof(configuredCode) ||
            configuredCode[8] != 0 || !HomeKey::validPairingCode(std::string(configuredCode, 8)))
            std::fill(std::begin(configuredCode), std::end(configuredCode), 0);
    }
    module.commissioningResult = HomeKey::validPairingCode(configuredCode) ? 0 : 4;
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
    new Characteristic::FirmwareRevision("0.2.0");
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
            if (command.epoch == module.commissioningEpoch.load())
            {
                if (!module.enabled || !module.pairingPermitted) module.commissioningResult = 3;
                else
                {
                    bool persisted = true;
                    if (command.saveCode)
                    {
                        persisted = codeHandle && HomeKey::validPairingCode(command.code) &&
                            nvs_set_blob(codeHandle, "code", command.code, sizeof(command.code)) == ESP_OK && nvs_commit(codeHandle) == ESP_OK;
                        if (persisted) std::copy(std::begin(command.code), std::end(command.code), configuredCode);
                        else module.commissioningResult = 6;
                    }
                    if (persisted && command.open)
                    {
                        if (!HomeKey::validPairingCode(configuredCode)) module.commissioningResult = 4;
                        else
                        {
                            homeSpan.setPairingCode(configuredCode, false); // SRP stays on the HAP worker.
                            if (command.epoch == module.commissioningEpoch.load() && module.enabled && module.pairingPermitted)
                            {
                                uint32_t deadline = millis() + 300000;
                                module.pairingEpoch = command.epoch;
                                module.pairingUntil = deadline ? deadline : 1;
                                module.commissioningResult = 1;
                                commissioningWasOpen = true;
                            }
                            else
                            {
                                char code[9]; randomUnavailableCode(code); homeSpan.setPairingCode(code, false);
                            }
                        }
                    }
                    else if (persisted) module.commissioningResult = 0;
                }
            }
        }
        else homeSpan.setWifiCredentials(command.ssid, command.password);
        volatile char* secret = command.password;
        for (size_t i = 0; i < sizeof(command.password); ++i) secret[i] = 0;
        secret = command.code;
        for (size_t i = 0; i < sizeof(command.code); ++i) secret[i] = 0;
    }

    int feedback = pendingFeedback.exchange(-1);
    if (feedback >= 0)
    {
        lockState->setVal(feedback ? 1 : 0); lockTarget->setVal(feedback ? 1 : 0);
    }
    bool open = module.pairingAllowed();
    if (commissioningWasOpen && !open)
    {
        std::unique_lock<std::shared_mutex> guard(homeSpan.getMutex());
        char code[9]; randomUnavailableCode(code); homeSpan.setPairingCode(code, false);
        if (module.commissioningResult == 1) module.commissioningResult = 2;
    }
    commissioningWasOpen = open;
    homeSpan.poll();
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
bool HomeKeyProvisioning::queueCode(const std::string& code, bool open)
{
    if (!ready || !commands || !HomeKey::validPairingCode(code)) return false;
    module.cancelCommissioning();
    Command command;
    command.pair = true; command.saveCode = true; command.open = open;
    command.epoch = module.commissioningEpoch.load();
    snprintf(command.code, sizeof(command.code), "%s", code.c_str());
    bool accepted = xQueueSend(commands, &command, 0) == pdTRUE;
    volatile char* secret = command.code;
    for (size_t i = 0; i < sizeof(command.code); ++i) secret[i] = 0;
    return accepted;
}
bool HomeKeyProvisioning::pairingCode(const std::string& code)
{
    return queueCode(code, true);
}
bool HomeKeyProvisioning::configurePairingCode(const std::string& code)
{
    return queueCode(code, false);
}
bool HomeKeyProvisioning::openPairingWindow()
{
    if (!ready || !commands) return false;
    Command command; command.pair = true; command.open = true;
    command.epoch = module.commissioningEpoch.load();
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

#pragma once
#if defined(OPENKNX_HOMEKEY_ENABLED) && defined(ARDUINO_ARCH_ESP32)
#include "OpenKNX.h"
#include "CredentialStore.h"
#include "NfcReader.hpp"
#include <atomic>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

class HomeKeyProvisioning;
class HomeKeyModule : public OpenKNX::Module
{
  public:
    const std::string name() override { return "HomeKey"; }
    const std::string version() override { return MODULE_HomeKey_Version; }
    void setup() override;
    void loop() override;
    void processInputKo(GroupObject&) override;
    void processBeforeTablesUnload() override { enabled = false; }
    void processBeforeRestart() override { enabled = false; }
    bool processCommand(const std::string cmd, bool diagnoseKo) override;
    bool pairingAllowed() const;
    HomeKeyCredentialStore store;
  private:
    friend class HomeKeyProvisioning;
    enum class EventKind : uint8_t { Denied, Uid, Authenticated };
    struct Event
    {
        EventKind kind = EventKind::Denied;
        HomeKey::IssuerId issuer{};
        HomeKey::EndpointId endpoint{};
        uint32_t generation = 0;
        uint8_t uid[10] = {};
        uint8_t uidLength = 0;
    };
    void readerTask();
    void handleTag(const std::vector<uint8_t>& uid, uint8_t sak);
    void authenticate(const std::vector<uint8_t>& selectResponse);
    void enqueue(const Event&);
    std::array<uint8_t, 18> ecp{};
    std::unique_ptr<INfcReader> reader;
    std::unique_ptr<HomeKeyProvisioning> provisioning;
    QueueHandle_t events = nullptr;
    std::atomic<bool> enabled{false};
    std::atomic<bool> allowUid{false};
    std::atomic<bool> pairingPermitted{false};
    std::atomic<uint32_t> pairingUntil{0};
    bool started = false;
    bool fatal = false;
    std::atomic<bool> provisioningReady{false};
    TaskHandle_t readerHandle = nullptr;
    TaskHandle_t hapHandle = nullptr;
};
extern HomeKeyModule openknxHomeKey;
#endif

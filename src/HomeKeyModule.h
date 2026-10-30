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
    void processBeforeTablesUnload() override { enabled = false; policyLoaded = false; cancelCommissioning(); }
    void processBeforeRestart() override { enabled = false; cancelCommissioning(); }
    bool processCommand(const std::string cmd, bool diagnoseKo) override;
    void showHelp() override;
    bool pairingAllowed() const;
    bool processFunctionProperty(uint8_t object, uint8_t property, uint8_t length, uint8_t* data, uint8_t* result, uint8_t& resultLength) override;
    bool processFunctionPropertyState(uint8_t object, uint8_t property, uint8_t length, uint8_t* data, uint8_t* result, uint8_t& resultLength) override;
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
    void cancelCommissioning() { ++commissioningEpoch; pairingUntil = 0; }
    const std::vector<HomeKey::Mapping>& activeMappings() const { return etsAuthorization ? etsMappings : store.mappings; }
    uint16_t lookupAccessId(const HomeKey::IssuerId& issuer, const HomeKey::EndpointId& endpoint) const
    {
        return policyLoaded ? HomeKey::authorizedId(etsAuthorization, etsTableValid, store.mappings, etsMappings, issuer, endpoint) : HomeKey::InvalidId;
    }
    void updateStatus();
    uint8_t authorizationStatus();
    uint8_t requestCommissioning(bool open);
    void publishEndpoint();
    std::atomic<bool> policyLoaded{false};
    bool etsAuthorization = false;
    bool etsTableValid = false;
    std::vector<HomeKey::Mapping> etsMappings;
    uint32_t statusAt = 0;
    uint32_t inspectedGeneration = UINT32_MAX;
    uint8_t endpointIndex = 0;
    bool endpointDirty = true;
    int reportedAuthorization = -1;
    int reportedCommissioning = -1;
    int reportedResult = -1;
    std::atomic<uint32_t> commissioningEpoch{0};
    std::atomic<uint32_t> pairingEpoch{0};
    std::atomic<uint8_t> commissioningResult{0};
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

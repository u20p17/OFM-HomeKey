#pragma once
#if defined(OPENKNX_HOMEKEY_ENABLED) && defined(ARDUINO_ARCH_ESP32)
#include "OpenKNX.h"
#include "CredentialStore.h"
#include "HomeKeyPolicy.h"
#include "HomeKeyIdentification.h"
#include "NfcReaderService.h"
#include <atomic>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#ifndef OPENKNX_HOMEKEY_HAP_STACK_SIZE
#define OPENKNX_HOMEKEY_HAP_STACK_SIZE 12288
#endif

class HomeKeyProvisioning;
class HomeKeyModule : public OpenKNX::Module, public NfcReaderExtension
{
  public:
    const std::string name() override { return "HomeKey"; }
    const std::string version() override { return MODULE_HomeKey_Version; }
    void setup(bool configured) override;
    void setup() override;
    void loop() override;
    void processInputKo(GroupObject&) override;
    void processBeforeTablesUnload() override { homeKeyConfigured = false; enabled = false; policyLoaded = false; webCodeAllowed = false; cancelCommissioning(); }
    void processBeforeRestart() override { homeKeyConfigured = false; enabled = false; webCodeAllowed = false; cancelCommissioning(); }
    bool processCommand(const std::string cmd, bool diagnoseKo) override;
    void showHelp() override;
    bool pairingAllowed() const;
    bool processFunctionProperty(uint8_t object, uint8_t property, uint8_t length, uint8_t* data, uint8_t* result, uint8_t& resultLength) override;
    bool processFunctionPropertyState(uint8_t object, uint8_t property, uint8_t length, uint8_t* data, uint8_t* result, uint8_t& resultLength) override;
    HomeKeyCredentialStore store;
    bool nfcReady() const override { return policyLoaded && (!enabled || provisioningReady); }
    bool prepareNfc(std::array<uint8_t,18>&, uint32_t& token) override;
    bool processNfcTag(INfcReader&, const std::vector<uint8_t>& uid, uint8_t sak) override;
    bool nfcIdReserved(uint16_t id) override;
    bool authorizeNfcUid(const uint8_t*, uint8_t length, uint32_t token) override;
  private:
    friend class HomeKeyProvisioning;
    enum class WebCodeState : uint8_t { Idle, Pending, Saved, Rejected, Failed, WrongCode, RateLimited };
    std::atomic<WebCodeState> webCodeState{WebCodeState::Idle};
    std::atomic<bool> webCodeAllowed{false};
    std::atomic<bool> codeConfigured{false};
    std::array<char, 5> qrSetupId{};
    std::atomic<uint32_t> codeBlockedUntil{0};
#ifdef OPENKNX_HOMEKEY_WEB
    bool webInitialized = false;
    void setupWeb();
    void loopWeb();
    struct WebCodeCommand { char code[9] = {}; char previous[9] = {}; uint32_t epoch = 0; };
    QueueHandle_t webCommands = nullptr;
    std::string webToken;
#endif
    enum class EventKind : uint8_t { Denied, Authenticated };
    struct Event
    {
        EventKind kind = EventKind::Denied;
        const char* reason = "credential not accepted";
        HomeKey::IssuerId issuer{};
        HomeKey::EndpointId endpoint{};
        uint32_t generation = 0;
    };
    HomeKeyIdentification identification;
    void showMemory();
    void authenticate(INfcReader&, const std::vector<uint8_t>& selectResponse);
    void enqueue(const Event&);
    struct NfcDiagnostic
    {
        std::array<uint8_t, 64> response{};
        std::array<uint8_t, 14> version{};
        HomeKey::CardVersionDiagnostic versionDiagnostic{};
        size_t responseLength = 0;
        uint16_t status = 0;
        bool exchanged = false;
        bool versionAttempted = false;
        bool versionValid = false;
    };
    QueueHandle_t diagnostics = nullptr;
    void cancelCommissioning() { ++commissioningEpoch; pairingUntil = 0; }
    const std::vector<HomeKey::Mapping>& activeMappings() const { return etsAuthorization ? etsMappings : store.mappings; }
    uint16_t lookupAccessId(const HomeKey::IssuerId& issuer, const HomeKey::EndpointId& endpoint) const
    {
        if(store.isRevoked(issuer,endpoint))return HomeKey::InvalidId;
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
    std::unique_ptr<HomeKeyProvisioning> provisioning;
    QueueHandle_t events = nullptr;
    // ETS-requested mode, separate from runtime health: failures must not turn
    // HomeKey-only configuration into ordinary UID authorization.
    std::atomic<bool> homeKeyConfigured{false};
    std::atomic<bool> enabled{false};
    std::atomic<bool> allowUid{false};
    std::atomic<bool> pairingPermitted{false};
    std::atomic<uint32_t> pairingUntil{0};
    bool started = false;
    bool fatal = false;
    std::atomic<bool> provisioningReady{false};
    TaskHandle_t hapHandle = nullptr;
};
extern HomeKeyModule openknxHomeKey;
#endif

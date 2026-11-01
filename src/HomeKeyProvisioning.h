#pragma once
#if defined(OPENKNX_HOMEKEY_ENABLED) && defined(ARDUINO_ARCH_ESP32)
#include <string>
#include <atomic>
#include "HomeKeyCodeRequest.h"
#include <nvs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
class HomeKeyModule;
class HomeKeyProvisioning
{
  public:
    explicit HomeKeyProvisioning(HomeKeyModule& owner) : module(owner) {}
    void begin();
    void poll();
    bool wifi(const std::string& ssid, const std::string& password);
    bool pairingCode(const std::string& code, const std::string& previous = {});
    bool configurePairingCode(const std::string& code, const std::string& previous = {}, bool webRequest = false);
    bool openPairingWindow();
    void feedback(bool locked);
    void synchronizeControllers();
  private:
    HomeKeyModule& module;
    bool commissioningWasOpen = false;
    nvs_handle_t codeHandle = 0;
    char configuredCode[9] = {};
    bool codeStorageHealthy = false;
    struct Command;
    bool queueCode(const std::string& code, const std::string& previous, bool open, bool webRequest);
    bool authorizeCodeChange(const Command& command);
    bool saveAttemptCount();
    HomeKey::CodeAttemptLimit attempts;
    std::atomic<int> pendingFeedback{-1};
    struct Command
    {
        bool pair = false;
        bool saveCode = false;
        bool open = false;
        bool webRequest = false;
        uint32_t epoch = 0;
        char ssid[33] = {};
        char password[65] = {};
        char code[9] = {};
        char previous[9] = {};
    };
    QueueHandle_t commands = nullptr;
};
#endif

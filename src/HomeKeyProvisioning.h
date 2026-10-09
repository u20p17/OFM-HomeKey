#pragma once
#if defined(OPENKNX_HOMEKEY_ENABLED) && defined(ARDUINO_ARCH_ESP32)
#include <string>
#include <atomic>
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
    bool pairingCode(const std::string& code);
    void feedback(bool locked);
    void synchronizeControllers();
  private:
    HomeKeyModule& module;
    bool commissioningWasOpen = false;
    std::atomic<int> pendingFeedback{-1};
    struct Command
    {
        bool pair = false;
        char ssid[33] = {};
        char password[65] = {};
        char code[9] = {};
    };
    QueueHandle_t commands = nullptr;
};
#endif

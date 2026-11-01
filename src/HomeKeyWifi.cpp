#if defined(OPENKNX_HOMEKEY_ENABLED) && defined(ARDUINO_ARCH_ESP32)
#include "HomeKeyWifi.h"
#include <WiFi.h>
#include <esp_wifi.h>
#ifdef STRINGIFY
#undef STRINGIFY
#endif
#include <HomeSpan.h>
#include <sstream>
#ifdef OPENKNX_HOMEKEY_WEB
#include <nvs.h>
#endif

namespace HomeKeyWifi
{
    namespace
    {
        bool valid(const std::string& ssid, const std::string& password)
        {
            return !ssid.empty() && ssid.size() <= 32 && password.size() <= 64 &&
                   ssid.find('\0') == std::string::npos && password.find('\0') == std::string::npos;
        }
    }

    bool configure(const std::string& ssid, const std::string& password)
    {
        if (!valid(ssid, password)) return false;
        WiFi.persistent(true);
        if (!WiFi.mode(WIFI_STA)) return false;
        // HomeSpan may already have initialized the driver with RAM storage.
        // persistent(true) alone only affects the next driver initialization.
        if (esp_wifi_set_storage(WIFI_STORAGE_FLASH) != ESP_OK) return false;
        // Save both HomeSpan's network and Arduino's station configuration:
        // KNX IP must be able to reconnect before ETS enables Home Key.
        homeSpan.setWifiCredentials(ssid.c_str(), password.c_str());
#ifdef OPENKNX_HOMEKEY_WEB
        // OFM-Network reloads this namespace on startup, before Home Key starts.
        nvs_handle_t network;
        if (nvs_open("WIFI", NVS_READWRITE, &network) != ESP_OK) return false;
        const bool saved = nvs_set_str(network, "SSID", ssid.c_str()) == ESP_OK &&
                           nvs_set_str(network, "PSK", password.c_str()) == ESP_OK &&
                           nvs_commit(network) == ESP_OK;
        nvs_close(network);
        if (!saved) return false;
#endif
        WiFi.setAutoReconnect(true);
        // Only request connection here; do not wait for association or DHCP.
        // No HomeKit pairing or credential authorization is enabled here.
        return WiFi.begin(ssid.c_str(), password.c_str()) != WL_CONNECT_FAILED;
    }

    Result command(const std::string& text, bool remote, bool workerStarted, const Queue& queue)
    {
        std::istringstream stream(text);
        std::string prefix, operation, ssid, password;
        stream >> prefix;
        if (prefix == "hk") { stream >> operation; if (operation != "wifi") return Result::NotHandled; }
        else if (prefix != "wifi") return Result::NotHandled;
        if (remote) return Result::LocalOnly;
        stream >> ssid;
        std::getline(stream >> std::ws, password);
        if (!valid(ssid, password)) return Result::Invalid;
        if (workerStarted) return queue(ssid, password) ? Result::Queued : Result::Busy;
        // The local console runs on the OpenKNX thread. Until the HAP worker
        // starts, it is the only caller of HomeSpan's Wi-Fi setter.
        return configure(ssid, password) ? Result::Connecting : Result::Failed;
    }
}
#endif

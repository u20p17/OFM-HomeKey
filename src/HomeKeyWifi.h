#pragma once
#include <functional>
#include <string>

namespace HomeKeyWifi
{
    enum class Result { NotHandled, LocalOnly, Invalid, Connecting, Queued, Busy, Failed };
    using Queue = std::function<bool(const std::string&, const std::string&)>;
    bool configure(const std::string& ssid, const std::string& password);
    Result command(const std::string& text, bool remote, bool workerStarted, const Queue& queue);
}

#pragma once
#include "HomeKeyPolicy.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace HomeKey
{
    struct CodeRequest
    {
        char token[33] = {};
        char previous[9] = {};
        char code[9] = {};
    };

    inline bool parseCodeRequest(const uint8_t* body, size_t size, CodeRequest& request)
    {
        request = {};
        if (!body || (size != 42 && size != 50) || body[32] != '\n' || body[size - 9] != '\n')
            return false;
        for (size_t i = 0; i < 32; ++i)
            if (!((body[i] >= '0' && body[i] <= '9') || (body[i] >= 'a' && body[i] <= 'f')))
                return false;
        if (size == 50)
            for (size_t i = 33; i < 41; ++i)
                if (body[i] < '0' || body[i] > '9') return false;
        if (!validPairingCode(std::string_view(reinterpret_cast<const char*>(body + size - 8), 8)))
            return false;
        std::memcpy(request.token, body, 32);
        if (size == 50) std::memcpy(request.previous, body + 33, 8);
        std::memcpy(request.code, body + size - 8, 8);
        return true;
    }

    inline bool matchesPreviousCode(const char (&stored)[9], const char (&previous)[9])
    {
        // Always compare the complete padded value, including for initial setup.
        volatile uint8_t difference = 0;
        for (size_t i = 0; i < 9; ++i)
            difference = difference | static_cast<uint8_t>(stored[i] ^ previous[i]);
        return difference == 0;
    }

    struct CodeAttemptLimit
    {
        static constexpr uint8_t MaxFailures = 5;
        static constexpr uint32_t CooldownMs = 300000;
        uint8_t failures = 0;
        uint32_t blockedUntil = 0;

        uint32_t remaining(uint32_t now) const
        {
            return windowOpen(now, blockedUntil) ? blockedUntil - now : 0;
        }
        void restore(uint8_t count, uint32_t now)
        {
            failures = std::min(count, MaxFailures);
            blockedUntil = failures == MaxFailures ? now + CooldownMs : 0;
            if (failures == MaxFailures && !blockedUntil) blockedUntil = 1;
        }
        void failed(uint32_t now) { restore(failures + 1, now); }
        void clear() { failures = 0; blockedUntil = 0; }
    };
}

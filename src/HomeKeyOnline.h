#pragma once
#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace HomeKeyOnline
{
    constexpr uint8_t Object = 165, Property = 3, Version = 1;
    enum Result : uint8_t { Ok, Busy, Invalid, Unconfigured, Disabled, NotPermitted, QueueBusy, Unavailable, Stale, NotFound, ProgrammingRequired };
    struct Request { uint8_t command = 0, index = 0; uint32_t generation = 0; };
    inline uint32_t get32(const uint8_t* bytes)
    {
        return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) | bytes[3];
    }
    inline void put32(uint8_t* bytes, uint32_t value)
    {
        bytes[0] = value >> 24; bytes[1] = value >> 16; bytes[2] = value >> 8; bytes[3] = value;
    }
    inline bool decode(const uint8_t* data, size_t length, Request& request)
    {
        if (!data || !length || data[0] > 4) return false;
        if (data[0] == 3 || data[0] == 4)
        {
            if (length != 6 || data[1] >= 64) return false;
            request.index = data[1]; request.generation = get32(data + 2);
        }
        else if (length != 1) return false;
        request.command = data[0];
        return true;
    }
    inline uint8_t status(uint8_t* out, bool enabled, bool permitted, bool open, uint8_t result, uint8_t authorization, uint8_t count, uint32_t generation)
    {
        out[0] = Ok; out[1] = Version; out[2] = enabled; out[3] = permitted; out[4] = open;
        out[5] = result; out[6] = authorization; out[7] = count; put32(out + 8, generation);
        return 12;
    }
    inline uint8_t endpoint(uint8_t* out, uint8_t index, uint8_t count, uint32_t generation, const uint8_t* issuer, const uint8_t* id)
    {
        out[0] = Ok; out[1] = Version; out[2] = index; out[3] = count; put32(out + 4, generation);
        std::copy(issuer, issuer + 8, out + 8); std::copy(id, id + 6, out + 16);
        return 22;
    }
}

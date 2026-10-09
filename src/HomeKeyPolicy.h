#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <string>

namespace HomeKey
{
    using IssuerId = std::array<uint8_t, 8>;
    using EndpointId = std::array<uint8_t, 6>;
    struct Mapping
    {
        IssuerId issuer{};
        EndpointId endpoint{};
        uint16_t accessId = 0xffff;
    };
    constexpr size_t MaxMappings = 64;
    constexpr uint16_t InvalidId = 0xffff;

    inline bool validPairingCode(const std::string& code)
    {
        return code.size() == 8 && code.find_first_not_of("0123456789") == std::string::npos &&
               code.find_first_not_of(code[0]) != std::string::npos && code != "12345678" && code != "87654321";
    }

    // A failed or malformed SELECT never becomes a conventional UID grant.
    enum class Selection { HomeKey, OrdinaryTag, Denied };
    inline Selection classify(bool isoDep, bool exchanged, const std::vector<uint8_t>& response)
    {
        if (!isoDep)
            return Selection::OrdinaryTag;
        if (!exchanged || response.size() < 2)
            return Selection::Denied;
        uint16_t sw = (response[response.size() - 2] << 8) | response.back();
        if (sw == 0x9000)
            return Selection::HomeKey;
        return sw == 0x6a82 ? Selection::OrdinaryTag : Selection::Denied;
    }
    inline uint16_t lookup(const std::vector<Mapping>& mappings, const IssuerId& issuer, const EndpointId& endpoint)
    {
        for (const auto& entry : mappings)
            if (entry.issuer == issuer && entry.endpoint == endpoint && entry.accessId < 1500)
                return entry.accessId;
        return InvalidId;
    }
    inline bool bind(std::vector<Mapping>& mappings, const Mapping& entry)
    {
        if (entry.accessId >= 1500)
            return false;
        for (const auto& existing : mappings)
            if (existing.accessId == entry.accessId && (existing.issuer != entry.issuer || existing.endpoint != entry.endpoint))
                return false;
        for (auto& existing : mappings)
            if (existing.issuer == entry.issuer && existing.endpoint == entry.endpoint)
            {
                existing = entry;
                return true;
            }
        if (mappings.size() >= MaxMappings)
            return false;
        mappings.push_back(entry);
        return true;
    }
}

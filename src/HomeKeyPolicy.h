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

    constexpr size_t EtsRows = 16;
    constexpr size_t EtsRowBytes = 33;
    inline bool windowOpen(uint32_t now, uint32_t deadline)
    {
        return deadline != 0 && static_cast<int32_t>(deadline - now) > 0;
    }
    inline bool windowOpen(uint32_t now, uint32_t deadline, uint32_t requestEpoch, uint32_t currentEpoch)
    {
        return requestEpoch == currentEpoch && windowOpen(now, deadline);
    }
    template<size_t N> bool parseHex(const uint8_t* text, std::array<uint8_t, N>& bytes)
    {
        auto nibble = [](uint8_t ch) -> int
        {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i < N; ++i)
        {
            int high = nibble(text[i * 2]), low = nibble(text[i * 2 + 1]);
            if (high < 0 || low < 0) return false;
            bytes[i] = (high << 4) | low;
        }
        return true;
    }
    // Exact ETS layout: enabled bit 7, 16 issuer hex bytes + NUL, 12 endpoint hex
    // bytes + NUL, then a big-endian logical ID. Reject the entire table on ambiguity.
    inline bool parseEtsRows(const uint8_t* data, size_t size, std::vector<Mapping>& result)
    {
        result.clear();
        if (!data || size != EtsRows * EtsRowBytes) return false;
        std::vector<Mapping> parsed;
        parsed.reserve(EtsRows);
        for (size_t i = 0; i < EtsRows; ++i)
        {
            const uint8_t* row = data + i * EtsRowBytes;
            if (!(row[0] & 0x80)) continue;
            Mapping entry;
            entry.accessId = (uint16_t(row[31]) << 8) | row[32];
            if (entry.accessId >= 1500 || row[17] != 0 || row[30] != 0 || !parseHex(row + 1, entry.issuer) || !parseHex(row + 18, entry.endpoint)) return false;
            for (const auto& existing : parsed)
                if (existing.accessId == entry.accessId || (existing.issuer == entry.issuer && existing.endpoint == entry.endpoint)) return false;
            parsed.push_back(entry);
        }
        result.swap(parsed);
        return true;
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
    inline uint16_t authorizedId(bool etsMode, bool tableValid, const std::vector<Mapping>& local, const std::vector<Mapping>& table, const IssuerId& issuer, const EndpointId& endpoint)
    {
        if (etsMode && !tableValid) return InvalidId;
        return lookup(etsMode ? table : local, issuer, endpoint);
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

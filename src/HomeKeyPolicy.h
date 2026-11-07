#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>

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

    inline bool validPairingCode(std::string_view code)
    {
        return code.size() == 8 && code.find_first_not_of("0123456789") == std::string::npos &&
               code.find_first_not_of(code[0]) != std::string::npos && code != "12345678" && code != "87654321";
    }

    inline std::array<char, 5> pairingSetupId(const uint8_t (&mac)[6])
    {
        constexpr char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        uint32_t value = 0;
        for (auto byte : mac) value = (value * 256 + byte) % 1679616; // 36^4
        std::array<char, 5> id{};
        for (int i = 3; i >= 0; --i)
        {
            id[i] = digits[value % 36];
            value /= 36;
        }
        return id;
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
    struct EtsTableDiagnostic
    {
        const char* reason = nullptr;
        uint8_t slot = 0; // 1-based; zero means the entire input is unavailable.
        uint8_t conflictingSlot = 0;
        uint16_t value = 0;
    };
    // Exact ETS layout: enabled bit 7, 16 issuer hex bytes + NUL, 12 endpoint hex
    // bytes + NUL, then a big-endian logical ID. Reject the entire table on ambiguity.
    inline bool parseEtsRows(const uint8_t* data, size_t size, std::vector<Mapping>& result,
                             EtsTableDiagnostic* diagnostic = nullptr)
    {
        result.clear();
        if (diagnostic) *diagnostic = {};
        auto fail = [&](const char* reason, uint8_t slot, uint16_t value = 0, uint8_t conflict = 0)
        {
            if (diagnostic) *diagnostic = {reason, slot, conflict, value};
            return false;
        };
        if (!data || size != EtsRows * EtsRowBytes)
            return fail("permission data missing or wrong size", 0);
        std::vector<Mapping> parsed;
        parsed.reserve(EtsRows);
        std::array<uint8_t, EtsRows> sourceSlots{};
        for (size_t i = 0; i < EtsRows; ++i)
        {
            const uint8_t* row = data + i * EtsRowBytes;
            if (!(row[0] & 0x80)) continue;
            const uint8_t slot = i + 1;
            Mapping entry;
            entry.accessId = (uint16_t(row[31]) << 8) | row[32];
            if (entry.accessId >= 1500) return fail("NFC-Tag-ID outside 0..1499", slot, entry.accessId);
            if (row[17] != 0) return fail("Issuer-ID trailing byte must be 0x00", slot, row[17]);
            if (row[30] != 0) return fail("Endpoint-ID trailing byte must be 0x00", slot, row[30]);
            if (!parseHex(row + 1, entry.issuer)) return fail("Issuer-ID must contain 16 hex characters", slot);
            if (!parseHex(row + 18, entry.endpoint)) return fail("Endpoint-ID must contain 12 hex characters", slot);
            for (size_t previous = 0; previous < parsed.size(); ++previous)
            {
                const auto& existing = parsed[previous];
                if (existing.accessId == entry.accessId)
                    return fail("duplicate NFC-Tag-ID", slot, entry.accessId, sourceSlots[previous]);
                if (existing.issuer == entry.issuer && existing.endpoint == entry.endpoint)
                    return fail("duplicate Issuer-ID + Endpoint-ID", slot, entry.accessId, sourceSlots[previous]);
            }
            sourceSlots[parsed.size()] = slot;
            parsed.push_back(entry);
        }
        result.swap(parsed);
        return true;
    }

    // A failed or malformed SELECT never becomes a conventional UID grant.
    enum class Selection { HomeKey, OrdinaryTag, Denied };
    struct CardVersionDiagnostic
    {
        struct Stage
        {
            size_t responseLength = 0;
            uint16_t status = 0;
            bool exchanged = false;
            uint16_t lengthCorrection = 0;
        };
        std::array<Stage, 4> stages{};
        size_t attempted = 0;
    };
    template<typename Exchange> bool readCardVersion(Exchange exchange, std::array<uint8_t, 14>& version,
                                                    CardVersionDiagnostic* diagnostic = nullptr)
    {
        version.fill(0);
        if (diagnostic) *diagnostic = {};
        std::vector<uint8_t> response;
        std::array<uint8_t, 14> parsed{};
        for (size_t stage = 0; stage < 4; ++stage)
        {
            response.clear();
            auto command = stage == 0
                ? std::vector<uint8_t>{0x00, 0xa4, 0x04, 0x00, 7, 0xd2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x00}
                : std::vector<uint8_t>{0x90, uint8_t(stage == 1 ? 0x60 : 0xaf), 0, 0, 0};
            bool exchanged = exchange(command, response);
            uint16_t status = response.size() >= 2
                ? (uint16_t(response[response.size() - 2]) << 8) | response.back() : 0;
            uint16_t lengthCorrection = 0;
            if (stage == 0 && exchanged && response.size() == 2 && (status & 0xff00) == 0x6c00)
            {
                lengthCorrection = status;
                command.push_back(uint8_t(status));
                response.clear();
                exchanged = exchange(command, response);
                status = response.size() >= 2
                    ? (uint16_t(response[response.size() - 2]) << 8) | response.back() : 0;
            }
            if (diagnostic)
            {
                diagnostic->attempted = stage + 1;
                diagnostic->stages[stage] = {response.size(), status, exchanged, lengthCorrection};
            }
            const size_t expectedLength = stage == 0 ? 2 : stage == 3 ? 16 : 9;
            const uint16_t expectedStatus = stage == 0 ? 0x9000 : stage == 3 ? 0x9100 : 0x91af;
            const bool validLength = stage == 0 ? response.size() >= expectedLength : response.size() == expectedLength;
            if (!exchanged || !validLength || status != expectedStatus)
            {
                response.clear();
                return false;
            }
            if (stage == 1 || stage == 2)
                for (size_t index = 0; index < 7; ++index)
                    parsed[(stage - 1) * 7 + index] = response[index];
            response.clear();
        }
        version = parsed;
        return true;
    }
    inline Selection classify(bool isoDep, bool exchanged, const std::vector<uint8_t>& response)
    {
        if (!isoDep)
            return Selection::OrdinaryTag;
        if (!exchanged || response.size() < 2)
            return Selection::Denied;
        uint16_t sw = (response[response.size() - 2] << 8) | response.back();
        if (sw == 0x9000)
        {
            if (response.size() == 38 && response[0] == 0x6f && response[1] == 0x22 &&
                response[2] == 0x85 && response[3] == 0x20)
                return Selection::OrdinaryTag;
            return Selection::HomeKey;
        }
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

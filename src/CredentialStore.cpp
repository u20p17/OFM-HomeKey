#if defined(OPENKNX_HOMEKEY_ENABLED) && defined(ARDUINO_ARCH_ESP32)
#include "CredentialStore.h"
#include <algorithm>
#include <nvs_flash.h>
#include <esp_log.h>

namespace
{
    void wipe(std::vector<uint8_t>& bytes)
    {
        volatile uint8_t* data = bytes.data();
        for (size_t i = 0; i < bytes.size(); ++i) data[i] = 0;
    }
    constexpr size_t MaxBlob = 32768;
    constexpr size_t MaxIssuers = 16;
    constexpr size_t MaxEndpoints = 64;
    struct Codec
    {
        std::vector<uint8_t> data;
        size_t position = 0;
        bool valid = true;
        ~Codec() { wipe(data); }
        void put(uint32_t value, unsigned bytes)
        {
            for (unsigned i = 0; i < bytes; ++i)
                data.push_back(value >> (i * 8));
        }
        uint32_t get(unsigned bytes)
        {
            if (position + bytes > data.size()) { valid = false; return 0; }
            uint32_t value = 0;
            for (unsigned i = 0; i < bytes; ++i)
                value |= uint32_t(data[position++]) << (i * 8);
            return value;
        }
        void putBytes(const std::vector<uint8_t>& bytes)
        {
            if (bytes.size() > 65) { valid = false; return; }
            put(bytes.size(), 1);
            data.insert(data.end(), bytes.begin(), bytes.end());
        }
        std::vector<uint8_t> getBytes(size_t maximum)
        {
            size_t size = get(1);
            if (!valid || size > maximum || position + size > data.size()) { valid = false; return {}; }
            std::vector<uint8_t> bytes(data.begin() + position, data.begin() + position + size);
            position += size;
            return bytes;
        }
    };
}

bool HomeKeyCredentialStore::begin()
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    // Never erase an unreadable partition automatically: provisioning needs explicit recovery.
    if (nvs_flash_init_partition("homekey") != ESP_OK ||
        nvs_open_from_partition("homekey", "ofm-homekey", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    size_t size = 0;
    auto result = nvs_get_blob(handle, "state", nullptr, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) { healthy = true; return true; }
    if (result != ESP_OK || size < 4 || size > MaxBlob)
        return false;
    Codec c;
    c.data.resize(size);
    if (nvs_get_blob(handle, "state", c.data.data(), &size) != ESP_OK || c.get(4) != 0x31534b48)
        return false;
    identity.private_key = c.getBytes(32);
    identity.public_key = c.getBytes(65);
    identity.public_key_x = c.getBytes(32);
    identity.group_identifier = c.getBytes(8);
    identity.sub_identifier = c.getBytes(8);
    auto count = c.get(1);
    if (count > MaxIssuers) return false;
    size_t total = 0;
    for (size_t i = 0; i < count && c.valid; ++i)
    {
        ddk::Issuer issuer;
        issuer.id = c.getBytes(8);
        issuer.public_key = c.getBytes(32);
        issuer.public_key_x = c.getBytes(32);
        auto endpointCount = c.get(1);
        total += endpointCount;
        if (total > MaxEndpoints || issuer.id.size() != 8 || issuer.public_key.size() != 32) return false;
        for (size_t j = 0; j < endpointCount && c.valid; ++j)
        {
            ddk::Endpoint endpoint;
            endpoint.id = c.getBytes(6);
            endpoint.public_key = c.getBytes(65);
            endpoint.public_key_x = c.getBytes(32);
            endpoint.persistent_key = c.getBytes(32);
            endpoint.used_at = c.get(4);
            endpoint.counter = c.get(1);
            endpoint.key_type = static_cast<ddk::KeyType>(c.get(1));
            if (endpoint.id.size() != 6 || endpoint.public_key.size() != 65 || endpoint.public_key_x.size() != 32 ||
                (!endpoint.persistent_key.empty() && endpoint.persistent_key.size() != 32) || endpoint.key_type != ddk::KeyType::Secp256r1)
                return false;
            issuer.endpoints.push_back(std::move(endpoint));
        }
        entries.push_back(std::move(issuer));
    }
    count = c.get(1);
    if (count > HomeKey::MaxMappings) return false;
    for (size_t i = 0; i < count && c.valid; ++i)
    {
        HomeKey::Mapping mapping;
        for (auto& byte : mapping.issuer) byte = c.get(1);
        for (auto& byte : mapping.endpoint) byte = c.get(1);
        mapping.accessId = c.get(2);
        if (!contains(mapping.issuer, mapping.endpoint) || !HomeKey::bind(mappings, mapping)) return false;
    }
    bool empty = identity.private_key.empty() && identity.public_key.empty() && identity.public_key_x.empty() && identity.group_identifier.empty() && identity.sub_identifier.empty();
    bool complete = identity.private_key.size() == 32 && identity.public_key.size() == 65 && identity.public_key_x.size() == 32 &&
                    identity.group_identifier.size() == 8 && identity.sub_identifier.size() == 8;
    healthy = c.valid && c.position == c.data.size() && (empty || complete);
    return healthy;
}

void HomeKeyCredentialStore::save()
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    ++generation; // Invalidates queued events even if persistence subsequently fails.
    if (!healthy) return;
    Codec c;
    c.put(0x31534b48, 4);
    for (const auto* bytes : {&identity.private_key, &identity.public_key, &identity.public_key_x, &identity.group_identifier, &identity.sub_identifier})
        c.putBytes(*bytes);
    if (entries.size() > MaxIssuers || mappings.size() > HomeKey::MaxMappings) { healthy = false; return; }
    c.put(entries.size(), 1);
    size_t total = 0;
    for (const auto& issuer : entries)
    {
        total += issuer.endpoints.size();
        if (total > MaxEndpoints) { healthy = false; return; }
        c.putBytes(issuer.id); c.putBytes(issuer.public_key); c.putBytes(issuer.public_key_x);
        c.put(issuer.endpoints.size(), 1);
        for (const auto& endpoint : issuer.endpoints)
        {
            c.putBytes(endpoint.id); c.putBytes(endpoint.public_key); c.putBytes(endpoint.public_key_x); c.putBytes(endpoint.persistent_key);
            c.put(endpoint.used_at, 4); c.put(endpoint.counter, 1); c.put(static_cast<uint8_t>(endpoint.key_type), 1);
        }
    }
    // Revoked endpoints can never leave a live mapping behind.
    mappings.erase(std::remove_if(mappings.begin(), mappings.end(), [this](const auto& mapping)
        { return !contains(mapping.issuer, mapping.endpoint); }), mappings.end());
    c.put(mappings.size(), 1);
    for (const auto& mapping : mappings)
    {
        for (auto byte : mapping.issuer) c.put(byte, 1);
        for (auto byte : mapping.endpoint) c.put(byte, 1);
        c.put(mapping.accessId, 2);
    }
    healthy = c.valid && c.data.size() <= MaxBlob && nvs_set_blob(handle, "state", c.data.data(), c.data.size()) == ESP_OK && nvs_commit(handle) == ESP_OK;
    if (!healthy) ESP_LOGE("HomeKey", "Credential persistence failed; authentication disabled");
}

bool HomeKeyCredentialStore::contains(const HomeKey::IssuerId& issuerId, const HomeKey::EndpointId& endpointId) const
{
    for (const auto& issuer : entries)
        if (issuer.id.size() == issuerId.size() && std::equal(issuer.id.begin(), issuer.id.end(), issuerId.begin()))
            for (const auto& endpoint : issuer.endpoints)
                if (endpoint.id.size() == endpointId.size() && std::equal(endpoint.id.begin(), endpoint.id.end(), endpointId.begin()))
                    return true;
    return false;
}
void HomeKeyCredentialStore::provision_identity(const ddk::ReaderIdentity& value)
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if (identity.private_key != value.private_key || identity.group_identifier != value.group_identifier || identity.sub_identifier != value.sub_identifier)
    {
        for (auto& issuer : entries)
        {
            for (auto& endpoint : issuer.endpoints) wipe(endpoint.persistent_key);
            issuer.endpoints.clear();
        }
        mappings.clear();
    }
    wipe(identity.private_key);
    identity = value;
}

bool HomeKeyCredentialStore::removeEndpoint(const HomeKey::IssuerId& issuerId, const HomeKey::EndpointId& endpointId)
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if (!healthy || !contains(issuerId, endpointId)) return false;
    for (auto& issuer : entries)
    {
        if (issuer.id.size() != issuerId.size() || !std::equal(issuer.id.begin(), issuer.id.end(), issuerId.begin())) continue;
        issuer.endpoints.erase(std::remove_if(issuer.endpoints.begin(), issuer.endpoints.end(), [&](const auto& endpoint)
        {
            return endpoint.id.size() == endpointId.size() && std::equal(endpoint.id.begin(), endpoint.id.end(), endpointId.begin());
        }), issuer.endpoints.end());
    }
    save(); // Prunes local mappings and invalidates every queued authorization event.
    return healthy;
}

bool HomeKeyCredentialStore::eraseReaderKey()
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    wipe(identity.private_key);
    for (auto& issuer : entries)
        for (auto& endpoint : issuer.endpoints) wipe(endpoint.persistent_key);
    identity = {};
    for (auto& issuer : entries) issuer.endpoints.clear();
    mappings.clear();
    save();
    return healthy;
}
bool HomeKeyCredentialStore::clear()
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    ++generation;
    wipe(identity.private_key);
    for (auto& issuer : entries)
        for (auto& endpoint : issuer.endpoints) wipe(endpoint.persistent_key);
    identity = {}; entries.clear(); mappings.clear();
    healthy = handle && nvs_erase_all(handle) == ESP_OK && nvs_commit(handle) == ESP_OK;
    if (healthy) save();
    return healthy;
}
#endif

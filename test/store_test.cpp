#include "CredentialStore.h"
#include <cassert>
#include <cstring>
#include <iostream>

static std::vector<uint8_t> flash, staged;
static bool commitFails = false;
int nvs_flash_init_partition(const char* name) { assert(!strcmp(name, "homekey")); return ESP_OK; }
int nvs_open_from_partition(const char* part, const char*, int, nvs_handle_t* handle) { assert(!strcmp(part, "homekey")); *handle = 1; return ESP_OK; }
int nvs_get_blob(nvs_handle_t, const char*, void* out, size_t* size)
{
    if (flash.empty()) return ESP_ERR_NVS_NOT_FOUND;
    if (out) { assert(*size >= flash.size()); memcpy(out, flash.data(), flash.size()); }
    *size = flash.size(); return ESP_OK;
}
int nvs_set_blob(nvs_handle_t, const char*, const void* value, size_t size)
{
    staged.assign(static_cast<const uint8_t*>(value), static_cast<const uint8_t*>(value) + size); return ESP_OK;
}
int nvs_commit(nvs_handle_t) { if (commitFails) return ESP_FAIL; flash = staged; return ESP_OK; }
int nvs_erase_all(nvs_handle_t) { staged.clear(); return ESP_OK; }

int main()
{
    using namespace HomeKey;
    assert(validPairingCode("24681357"));
    assert(!validPairingCode("12345678") && !validPairingCode("87654321"));
    assert(!validPairingCode("00000000") && !validPairingCode("77777777"));
    assert(!validPairingCode("123") && !validPairingCode("2468x357"));
    // Selection alone is never treated as authentication. Ambiguous failures do not downgrade.
    assert(classify(false, false, {}) == Selection::OrdinaryTag);
    assert(classify(true, true, {0x6a, 0x82}) == Selection::OrdinaryTag);
    assert(classify(true, true, {0x90, 0}) == Selection::HomeKey);
    assert(classify(true, false, {0x90, 0}) == Selection::Denied);
    assert(classify(true, true, {0x69, 0x82}) == Selection::Denied);
    assert(classify(true, true, {0x90}) == Selection::Denied);
    assert(classify(true, true, {}) == Selection::Denied);

    HomeKeyCredentialStore store;
    assert(store.begin() && store.healthy);
    store.identity.private_key.assign(32, 1);
    store.identity.public_key.assign(65, 2);
    store.identity.public_key_x.assign(32, 3);
    store.identity.group_identifier.assign(8, 4);
    store.identity.sub_identifier.assign(8, 5);
    ddk::Issuer issuer;
    issuer.id.assign(8, 6); issuer.public_key.assign(32, 7);
    ddk::Endpoint endpoint;
    endpoint.id.assign(6, 8); endpoint.public_key.assign(65, 9);
    endpoint.public_key_x.assign(32, 10); endpoint.persistent_key.assign(32, 11);
    endpoint.used_at = 123456; endpoint.counter = 2;
    issuer.endpoints.push_back(endpoint); store.entries.push_back(issuer);
    Mapping mapping; mapping.issuer.fill(6); mapping.endpoint.fill(8); mapping.accessId = 1499;
    assert(store.contains(mapping.issuer, mapping.endpoint));
    assert(lookup(store.mappings, mapping.issuer, mapping.endpoint) == InvalidId); // Provisioning grants nothing.
    assert(HomeKey::bind(store.mappings, mapping));
    Mapping collision = mapping; collision.endpoint[0] = 12;
    assert(!HomeKey::bind(store.mappings, collision));
    collision.accessId = 1500; assert(!HomeKey::bind(store.mappings, collision));
    store.save(); assert(store.healthy);
    auto eventGeneration = store.generation;
    HomeKeyCredentialStore reboot;
    assert(reboot.begin());
    assert(reboot.identity.private_key == store.identity.private_key);
    assert(reboot.entries.front().endpoints.front().persistent_key == endpoint.persistent_key);
    assert(reboot.entries.front().endpoints.front().used_at == endpoint.used_at);
    assert(lookup(reboot.mappings, mapping.issuer, mapping.endpoint) == 1499);
    EndpointId unknown = mapping.endpoint; unknown[0] = 77;
    assert(lookup(reboot.mappings, mapping.issuer, unknown) == InvalidId);

    // Identity replacement invalidates every endpoint and its authorization mapping.
    auto unchanged = reboot.identity;
    reboot.provision_identity(unchanged);
    assert(reboot.contains(mapping.issuer, mapping.endpoint));
    auto replaced = unchanged; replaced.private_key[0] ^= 1;
    auto replacementGeneration = reboot.generation;
    reboot.provision_identity(replaced); reboot.save();
    assert(reboot.healthy && reboot.generation != replacementGeneration);
    assert(reboot.entries.front().endpoints.empty() && reboot.mappings.empty());
    assert(reboot.entries.front().public_key == issuer.public_key); // HAP issuer remains.
    HomeKeyCredentialStore replacedBoot; assert(replacedBoot.begin());
    assert(!replacedBoot.contains(mapping.issuer, mapping.endpoint));

    // Removing an endpoint atomically removes its mapping and invalidates pending events.
    store.entries.front().endpoints.clear(); store.save();
    assert(store.healthy && store.generation != eventGeneration && store.mappings.empty());
    HomeKeyCredentialStore revoked; assert(revoked.begin());
    assert(!revoked.contains(mapping.issuer, mapping.endpoint) && revoked.mappings.empty());

    // Storage corruption must not cause silent reset or grant from partially decoded records.
    auto goodFlash = flash;
    flash.pop_back(); HomeKeyCredentialStore truncated; assert(!truncated.begin() && !truncated.healthy);
    flash = goodFlash; flash.push_back(0); HomeKeyCredentialStore trailing; assert(!trailing.begin());
    flash = goodFlash; flash[0] ^= 1; HomeKeyCredentialStore badSchema; assert(!badSchema.begin());
    flash = goodFlash;
    commitFails = true; store.save(); assert(!store.healthy);
    commitFails = false; assert(store.clear() && store.healthy);
    HomeKeyCredentialStore reset; assert(reset.begin() && reset.entries.empty() && reset.mappings.empty() && reset.identity.private_key.empty());
    std::cout << "Home Key selection, mapping, persistence, revocation and failure-closed tests passed\n";
}

#include "CredentialStore.h"
#include "HomeKeyIdentification.h"
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
    // Commissioning closes at the deadline, stays closed after long uptime,
    // survives millis wrap, and cannot be reopened by a canceled worker result.
    assert(windowOpen(1000, 301000));
    assert(!windowOpen(301000, 301000) && !windowOpen(301001, 301000));
    assert(!windowOpen(0x90000000, 0));
    assert(windowOpen(0xfffffff0, 100) && !windowOpen(101, 100));
    assert(windowOpen(1000, 301000, 7, 7));
    assert(!windowOpen(1000, 301000, 7, 8));

    std::array<uint8_t, EtsRows * EtsRowBytes> rows{};
    auto setRow = [&](size_t index, const char* iid, const char* eid, uint16_t id)
    {
        uint8_t* row = rows.data() + index * EtsRowBytes;
        row[0] = 0x80; memcpy(row + 1, iid, 16); memcpy(row + 18, eid, 12);
        row[31] = id >> 8; row[32] = id;
    };
    std::vector<Mapping> table;
    assert(parseEtsRows(rows.data(), rows.size(), table) && table.empty());
    setRow(0, "001122334455AABB", "012345abcdef", 1499);
    assert(parseEtsRows(rows.data(), rows.size(), table) && table.size() == 1 && table[0].accessId == 1499);
    auto etsEntry = table.front();
    std::vector<Mapping> local{etsEntry}; local[0].accessId = 42;
    assert(authorizedId(false, false, local, table, etsEntry.issuer, etsEntry.endpoint) == 42);
    assert(authorizedId(true, true, local, table, etsEntry.issuer, etsEntry.endpoint) == 1499);
    assert(authorizedId(true, false, local, table, etsEntry.issuer, etsEntry.endpoint) == InvalidId);
    auto goodRows = rows;
    rows[31] = rows[32] = 0;
    assert(parseEtsRows(rows.data(), rows.size(), table) && table[0].accessId == 0); // Slot zero is valid, independent of action routing.
    rows = goodRows;
    setRow(1, "001122334455AABB", "012345abcdef", 1); // Same endpoint, different action.
    assert(!parseEtsRows(rows.data(), rows.size(), table) && table.empty());
    rows = goodRows; setRow(1, "001122334455AABB", "012345abcdee", 1499); // Same action, different endpoint.
    assert(!parseEtsRows(rows.data(), rows.size(), table) && table.empty());
    rows = goodRows; rows[17] = 'f'; assert(!parseEtsRows(rows.data(), rows.size(), table)); // Overlong issuer.
    rows = goodRows; rows[30] = 'f'; assert(!parseEtsRows(rows.data(), rows.size(), table)); // Overlong endpoint.
    rows = goodRows; rows[1] = 'z'; assert(!parseEtsRows(rows.data(), rows.size(), table));
    rows = goodRows; rows[18] = 0; assert(!parseEtsRows(rows.data(), rows.size(), table));
    rows = goodRows; rows[31] = 1500 >> 8; rows[32] = 1500 & 255; assert(!parseEtsRows(rows.data(), rows.size(), table));
    rows[0] = 0; assert(parseEtsRows(rows.data(), rows.size(), table) && table.empty()); // Disabled garbage grants nothing.
    assert(authorizedId(true, true, local, table, etsEntry.issuer, etsEntry.endpoint) == InvalidId); // Removed ETS row cannot use a local mapping.
    assert(!parseEtsRows(nullptr, rows.size(), table) && !parseEtsRows(rows.data(), rows.size()-1, table));
    rows.fill(0);
    for (size_t i = 0; i < EtsRows; ++i)
    {
        setRow(i, "001122334455aabb", "012345abcde0", i);
        const char digits[] = "0123456789abcdef";
        rows[i * EtsRowBytes + 29] = digits[i];
    }
    assert(parseEtsRows(rows.data(), rows.size(), table) && table.size() == EtsRows);

    // The single enabled slot shown in the ETS screenshot is a valid mapping.
    rows.fill(0);
    setRow(0, "a8587854c92ce68c", "f3d38447f0c2", 0);
    EtsTableDiagnostic diagnostic;
    assert(parseEtsRows(rows.data(), rows.size(), table, &diagnostic));
    assert(table.size() == 1 && table[0].accessId == 0 && diagnostic.reason == nullptr);
    assert(lookup(table, IssuerId{0xa8,0x58,0x78,0x54,0xc9,0x2c,0xe6,0x8c},
        EndpointId{0xf3,0xd3,0x84,0x47,0xf0,0xc2}) == 0);
    auto screenshotRows = rows;
    rows[17] = 0x20;
    assert(!parseEtsRows(rows.data(), rows.size(), table, &diagnostic));
    assert(table.empty() && diagnostic.slot == 1 && diagnostic.value == 0x20);
    assert(!strcmp(diagnostic.reason, "Issuer-ID trailing byte must be 0x00"));
    rows = screenshotRows; rows[30] = 0xff;
    assert(!parseEtsRows(rows.data(), rows.size(), table, &diagnostic));
    assert(diagnostic.slot == 1 && diagnostic.value == 0xff);
    assert(!strcmp(diagnostic.reason, "Endpoint-ID trailing byte must be 0x00"));
    rows = screenshotRows; setRow(5, "001122334455aabb", "012345abcdef", 0);
    assert(!parseEtsRows(rows.data(), rows.size(), table, &diagnostic));
    assert(diagnostic.slot == 6 && diagnostic.conflictingSlot == 1);
    assert(!strcmp(diagnostic.reason, "duplicate NFC-Tag-ID"));
    rows = screenshotRows; setRow(10, "a8587854c92ce68c", "f3d38447f0c2", 1);
    assert(!parseEtsRows(rows.data(), rows.size(), table, &diagnostic));
    assert(diagnostic.slot == 11 && diagnostic.conflictingSlot == 1);
    assert(!strcmp(diagnostic.reason, "duplicate Issuer-ID + Endpoint-ID"));
    assert(parseEtsRows(screenshotRows.data(), screenshotRows.size(), table, &diagnostic));
    assert(diagnostic.reason == nullptr && diagnostic.slot == 0);

    // Selection alone is never treated as authentication. Ambiguous failures do not downgrade.
    assert(classify(false, false, {}) == Selection::OrdinaryTag);
    assert(classify(true, true, {0x6a, 0x82}) == Selection::OrdinaryTag);
    assert(classify(true, true, {0x90, 0}) == Selection::HomeKey);
    assert(classify(true, false, {0x90, 0}) == Selection::Denied);
    assert(classify(true, true, {0x69, 0x82}) == Selection::Denied);
    assert(classify(true, true, {0x90}) == Selection::Denied);
    assert(classify(true, true, {}) == Selection::Denied);

    std::vector<uint8_t> companyFci(38, 0x5c);
    companyFci[0] = 0x6f; companyFci[1] = 0x22;
    companyFci[2] = 0x85; companyFci[3] = 0x20;
    companyFci[36] = 0x90; companyFci[37] = 0;
    assert(classify(true, true, companyFci) == Selection::OrdinaryTag);
    assert(classify(true, false, companyFci) == Selection::Denied);
    companyFci[37] = 0x01;
    assert(classify(true, true, companyFci) == Selection::Denied);
    companyFci[37] = 0;
    companyFci[1] = 0x21;
    assert(classify(true, true, companyFci) == Selection::HomeKey);
    companyFci[1] = 0x22; companyFci[3] = 0x1f;
    assert(classify(true, true, companyFci) == Selection::HomeKey);
    companyFci[3] = 0x20; companyFci.erase(companyFci.begin() + 4);
    assert(classify(true, true, companyFci) == Selection::HomeKey);
    assert(classify(true, true, {0x5c, 2, 2, 0, 0x90, 0}) == Selection::HomeKey);

    std::array<uint8_t, 14> cardVersion{};
    unsigned versionCalls = 0;
    CardVersionDiagnostic versionDiagnostic;
    auto versionReply = [](unsigned stage) {
        if (stage == 0) return std::vector<uint8_t>{0x90, 0};
        if (stage == 3) {
            std::vector<uint8_t> reply(16, 0xaa);
            reply[14] = 0x91; reply[15] = 0;
            return reply;
        }
        return std::vector<uint8_t>{4, 1, 1, 0x33, 0, 0x18, 5, 0x91, 0xaf};
    };
    assert(readCardVersion([&](const std::vector<uint8_t>& command, std::vector<uint8_t>& reply) {
        if (versionCalls == 0)
            assert(command == (std::vector<uint8_t>{0x00, 0xa4, 0x04, 0x00, 7, 0xd2, 0x76, 0, 0, 0x85, 1, 0}));
        else
            assert(command == (std::vector<uint8_t>{0x90, uint8_t(versionCalls == 1 ? 0x60 : 0xaf), 0, 0, 0}));
        reply = versionReply(versionCalls++);
        return true;
    }, cardVersion, &versionDiagnostic));
    assert(versionCalls == 4 && cardVersion[0] == 4 && cardVersion[10] == 0x33);
    assert(versionDiagnostic.attempted == 4 && versionDiagnostic.stages[3].responseLength == 16);
    assert(versionDiagnostic.stages[3].status == 0x9100 && versionDiagnostic.stages[3].exchanged);
    versionCalls = 0;
    assert(readCardVersion([&](const std::vector<uint8_t>&, std::vector<uint8_t>& reply) {
        reply = versionReply(versionCalls);
        if (versionCalls++ == 0) reply.insert(reply.begin(), {0x6f, 0});
        return true;
    }, cardVersion, &versionDiagnostic));
    assert(versionCalls == 4 && versionDiagnostic.stages[0].responseLength == 4);
    for (uint8_t correctedLength : {uint8_t(0), uint8_t(0x10)})
    {
        versionCalls = 0;
        assert(readCardVersion([&](const std::vector<uint8_t>& command, std::vector<uint8_t>& reply) {
            const unsigned call = versionCalls++;
            if (call == 0) reply = {0x6c, correctedLength};
            else {
                if (call == 1)
                    assert(command == (std::vector<uint8_t>{0, 0xa4, 4, 0, 7, 0xd2, 0x76, 0, 0, 0x85, 1, 0, correctedLength}));
                else
                    assert(command == (std::vector<uint8_t>{0x90, uint8_t(call == 2 ? 0x60 : 0xaf), 0, 0, 0}));
                reply = versionReply(call - 1);
            }
            return true;
        }, cardVersion, &versionDiagnostic));
        assert(versionCalls == 5 && versionDiagnostic.attempted == 4);
        assert(versionDiagnostic.stages[0].lengthCorrection == (0x6c00 | correctedLength));
        assert(versionDiagnostic.stages[0].status == 0x9000);
    }
    for (unsigned failure = 0; failure < 3; ++failure)
    {
        versionCalls = 0;
        assert(!readCardVersion([&](const std::vector<uint8_t>&, std::vector<uint8_t>& reply) {
            const unsigned call = versionCalls++;
            reply = call == 0 || failure == 1 ? std::vector<uint8_t>{0x6c, 0} : std::vector<uint8_t>{0x90};
            return !(call == 1 && failure == 0);
        }, cardVersion, &versionDiagnostic));
        assert(versionCalls == 2 && versionDiagnostic.attempted == 1);
        assert(versionDiagnostic.stages[0].lengthCorrection == 0x6c00);
        assert(versionDiagnostic.stages[0].exchanged == (failure != 0));
        assert(cardVersion == (std::array<uint8_t, 14>{}));
    }
    for (unsigned failureFrame = 0; failureFrame < 4; ++failureFrame)
    {
        for (unsigned failure = 0; failure < 6; ++failure)
        {
            if (failureFrame == 0 && failure == 5) continue;
            versionCalls = 0;
            assert(!readCardVersion([&](const std::vector<uint8_t>&, std::vector<uint8_t>& reply) {
                const bool fail = versionCalls == failureFrame;
                reply = versionReply(versionCalls++);
                if (fail && failure == 1) reply.pop_back();
                if (fail && failure == 2) reply[reply.size() - 2] ^= 1;
                if (fail && failure == 3) reply.back() ^= 1;
                if (fail && failure == 4) reply.clear();
                if (fail && failure == 5) reply.insert(reply.begin(), 0);
                return !(fail && failure == 0);
            }, cardVersion, &versionDiagnostic));
            assert(versionCalls == failureFrame + 1);
            assert(versionDiagnostic.attempted == versionCalls);
            assert(versionDiagnostic.stages[failureFrame].exchanged == (failure != 0));
            for (unsigned stage = failureFrame + 1; stage < 4; ++stage)
                assert(!versionDiagnostic.stages[stage].exchanged && versionDiagnostic.stages[stage].responseLength == 0);
            assert(cardVersion == (std::array<uint8_t, 14>{}));
        }
    }

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

    // Selected deletion preserves the reader identity, issuer and other devices.
    HomeKeyCredentialStore deletion;
    assert(deletion.begin());
    auto second = endpoint; second.id[0] = 42;
    deletion.entries.front().endpoints.push_back(second);
    auto secondMapping = mapping; secondMapping.endpoint[0] = 42; secondMapping.accessId = 1498;
    assert(HomeKey::bind(deletion.mappings, secondMapping));
    deletion.save();
    auto deletionGeneration = deletion.generation;
    assert(deletion.removeEndpoint(mapping.issuer, mapping.endpoint));
    assert(deletion.generation != deletionGeneration);
    assert(!deletion.contains(mapping.issuer, mapping.endpoint));
    assert(deletion.contains(secondMapping.issuer, secondMapping.endpoint));
    assert(deletion.identity.private_key == store.identity.private_key);
    assert(deletion.entries.front().public_key == issuer.public_key);
    assert(lookup(deletion.mappings, mapping.issuer, mapping.endpoint) == InvalidId);
    assert(lookup(deletion.mappings, secondMapping.issuer, secondMapping.endpoint) == 1498);
    HomeKeyCredentialStore deletedBoot; assert(deletedBoot.begin());
    assert(!deletedBoot.contains(mapping.issuer, mapping.endpoint));
    assert(deletedBoot.contains(secondMapping.issuer, secondMapping.endpoint));
    deletionGeneration = deletion.generation;
    assert(deletion.removeEndpoint(mapping.issuer, mapping.endpoint));
    assert(deletion.generation != deletionGeneration); // Repeated deletion is a persistent revocation.
    assert(deletion.isRevoked(mapping.issuer, mapping.endpoint));
    {
        HomeKeyCredentialStore::Session session(deletion);
        deletion.entries.front().endpoints.push_back(endpoint); // HAP/NFC may reprovision a deleted endpoint.
        auto* referenced = &deletion.entries.front().endpoints.back();
        assert(!deletion.contains(mapping.issuer, mapping.endpoint));
        deletion.save();
        assert(referenced == &deletion.entries.front().endpoints.back() && referenced->id == endpoint.id);
        HomeKeyCredentialStore concurrentBoot; assert(concurrentBoot.begin());
        assert(!concurrentBoot.contains(mapping.issuer, mapping.endpoint));
        assert(concurrentBoot.entries.front().endpoints.size() == 1); // Revoked key was never persisted.
    }
    assert(deletion.entries.front().endpoints.size() == 1);
    HomeKeyCredentialStore revokedBoot; assert(revokedBoot.begin());
    assert(revokedBoot.isRevoked(mapping.issuer, mapping.endpoint));
    assert(revokedBoot.clear());
    HomeKeyCredentialStore clearedBoot; assert(clearedBoot.begin());
    assert(clearedBoot.isRevoked(mapping.issuer, mapping.endpoint));
    assert(clearedBoot.restoreEndpoint(mapping.issuer, mapping.endpoint));
    HomeKeyCredentialStore restoredBoot; assert(restoredBoot.begin());
    assert(!restoredBoot.isRevoked(mapping.issuer, mapping.endpoint));
    deletion.save(); // Restore the remaining device before testing a failed commit.

    commitFails = true;
    assert(!deletion.removeEndpoint(secondMapping.issuer, secondMapping.endpoint));
    assert(!deletion.healthy && deletion.generation != deletionGeneration);
    commitFails = false;

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
    // Old schema migrates losslessly on save; new revocations are bounded and never evicted.
    auto migrated = flash; assert(migrated.size() > 6);
    migrated[3] = 0x31; migrated.resize(migrated.size()-2); flash = migrated;
    HomeKeyCredentialStore oldSchema; assert(oldSchema.begin()); oldSchema.save();
    assert(flash[3] == 0x32);
    HomeKeyCredentialStore bounded; assert(bounded.begin());
    for (unsigned i=0; i<HomeKeyCredentialStore::MaxRevocations; ++i) {
        EndpointId id{}; id[0]=i;
        assert(bounded.removeEndpoint(mapping.issuer,id));
    }
    EndpointId overflow{}; overflow[0]=200;
    assert(!bounded.removeEndpoint(mapping.issuer,overflow) && bounded.healthy);
    HomeKeyCredentialStore fullBoot; assert(fullBoot.begin());
    assert(fullBoot.revocations.size()==HomeKeyCredentialStore::MaxRevocations);
    assert(fullBoot.isRevoked(mapping.issuer,EndpointId{}));
    assert(fullBoot.removeEndpoint(mapping.issuer,EndpointId{})); // Idempotent at capacity.

    HomeKeyIdentification identify;
    assert(!identify.active(0)); identify.start(7,0xfffffff0);
    assert(identify.active(0));
    assert(!identify.capture(6,0,mapping.issuer,mapping.endpoint,false));
    assert(identify.capture(7,0,mapping.issuer,mapping.endpoint,true));
    assert(identify.ready && identify.revoked && identify.active(0));
    assert(!identify.capture(7,1,mapping.issuer,unknown,false));
    assert(!identify.active(uint32_t(0xfffffff0+60000)));
    identify.start(8,100); assert(!identify.ready);
    identify.cancel(); assert(!identify.active(101));
    assert(!identify.capture(8,101,mapping.issuer,mapping.endpoint,false));

    std::cout << "Home Key ETS policy, commissioning, selection, mapping, persistence and revocation tests passed\n";
}

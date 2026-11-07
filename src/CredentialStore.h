#pragma once
#include "HomeKeyPolicy.h"
#include "ddk/store/CredentialStore.h"
#include "ddk/store/ReaderIdentity.h"
#include "ddk/store/Issuer.h"
#include <mutex>
#include <nvs.h>

class HomeKeyCredentialStore : public ddk::CredentialStore
{
  public:
    bool begin();
    const ddk::ReaderIdentity& reader_identity() const override { return identity; }
    void provision_identity(const ddk::ReaderIdentity& value) override;
    ddk::span<ddk::Issuer> issuers() override { return {entries.data(), entries.size()}; }
    void save() override;
    bool eraseReaderKey();
    bool removeEndpoint(const HomeKey::IssuerId&, const HomeKey::EndpointId&);
    bool clear();
    bool isRevoked(const HomeKey::IssuerId&, const HomeKey::EndpointId&) const;
    bool restoreEndpoint(const HomeKey::IssuerId&, const HomeKey::EndpointId&);
    void pruneRevoked();
    // DDK can save while a profile still holds Endpoint pointers. Persist only
    // non-revoked keys, but defer vector erasure until those references expire.
    class Session {
      public:
        explicit Session(HomeKeyCredentialStore& value) : store(value) { store.deferRevokedPruning = true; }
        ~Session() { store.deferRevokedPruning = false; store.pruneRevoked(); }
        Session(const Session&) = delete;
      private:
        HomeKeyCredentialStore& store;
    };
    static constexpr size_t MaxRevocations = 128;
    struct Revocation { HomeKey::IssuerId issuer; HomeKey::EndpointId endpoint; };
    std::vector<Revocation> revocations;
    bool contains(const HomeKey::IssuerId&, const HomeKey::EndpointId&) const;
    std::recursive_mutex mutex;
    ddk::ReaderIdentity identity;
    std::vector<ddk::Issuer> entries;
    std::vector<HomeKey::Mapping> mappings;
    uint32_t generation = 0;
    bool healthy = false;
  private:
    bool deferRevokedPruning = false;
    nvs_handle_t handle = 0;
};

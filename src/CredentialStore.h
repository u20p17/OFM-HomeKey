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
    bool clear();
    bool contains(const HomeKey::IssuerId&, const HomeKey::EndpointId&) const;
    std::recursive_mutex mutex;
    ddk::ReaderIdentity identity;
    std::vector<ddk::Issuer> entries;
    std::vector<HomeKey::Mapping> mappings;
    uint32_t generation = 0;
    bool healthy = false;
  private:
    nvs_handle_t handle = 0;
};

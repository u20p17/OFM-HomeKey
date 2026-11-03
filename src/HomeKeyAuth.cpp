#if defined(OPENKNX_HOMEKEY_ENABLED) && defined(ARDUINO_ARCH_ESP32)
#include "HomeKeyModule.h"
#include "ddk/homekey/ProfileFactory.h"
#include "ddk/transport/NfcChannel.h"
#include <algorithm>

void HomeKeyModule::handleTag(const std::vector<uint8_t>& uid, uint8_t sak)
{
    std::vector<uint8_t> response;
    bool isoDep = sak & 0x20;
    bool exchanged = false;
    if (isoDep)
        exchanged = reader->exchangeApdu({0x00, 0xa4, 0x04, 0x00, 7, 0xa0, 0, 0, 8, 0x58, 1, 1, 0}, response, 1000);
    auto selection = HomeKey::classify(isoDep, exchanged, response);
    if (selection == HomeKey::Selection::HomeKey)
    {
        authenticate(response);
        return; // Never fall back to a UID after a Home Key authentication attempt.
    }
    Event event;
    event.reason = !isoDep ? "tag is not ISO-DEP; UID not accepted"
        : !exchanged ? "Home Key SELECT exchange failed"
        : response.size() < 2 ? "Home Key SELECT response too short"
        : selection == HomeKey::Selection::OrdinaryTag ? "Home Key applet not found; UID not accepted"
        : "Home Key SELECT rejected";
    {
        std::lock_guard<std::recursive_mutex> guard(store.mutex);
        event.generation = store.generation;
    }
    if (selection == HomeKey::Selection::OrdinaryTag && allowUid && (uid.size() == 4 || uid.size() == 7 || uid.size() == 10))
    {
        event.kind = EventKind::Uid;
        event.uidLength = uid.size();
        std::copy(uid.begin(), uid.end(), event.uid);
    }
    enqueue(event);
}

void HomeKeyModule::authenticate(const std::vector<uint8_t>& response)
{
    std::lock_guard<std::recursive_mutex> guard(store.mutex);
    Event event;
    if (!store.healthy || store.identity.private_key.size() != 32 || !enabled)
    {
        event.reason = !store.healthy ? "Home Key credential storage unhealthy"
            : store.identity.private_key.empty() ? "Home Key reader private key missing"
            : store.identity.private_key.size() != 32 ? "Home Key reader private key has invalid length"
            : "Home Key module disabled during authentication";
        event.generation = store.generation; enqueue(event); return;
    }
    bool exchangeFailed = false;
    auto channel = std::make_shared<ddk::NfcChannel>([this, &exchangeFailed](std::vector<uint8_t>& send, std::vector<uint8_t>& receive)
        {
            const bool exchanged = enabled && send.size() <= 255 && reader->exchangeApdu(send, receive, 1000);
            if (!exchanged) exchangeFailed = true;
            return exchanged;
        });
    ddk::Session session(channel, store);
    auto profile = ddk::homekey::make_profile();
    event.reason = "Home Key SELECT response validation failed";
    if (profile->validate_select(session, response) == ddk::FailureReason::None)
    {
        auto state = ddk::FlowState::Selected;
        for (unsigned steps = 0; steps < 12 && enabled && state != ddk::FlowState::Done && state != ddk::FlowState::Failed; ++steps)
            state = profile->step(session, state);
        if (state == ddk::FlowState::Done)
        {
            event.reason = "Home Key authentication outcome invalid";
            auto outcome = profile->finalize(session);
            if (outcome && outcome.issuer && outcome.endpoint && outcome.issuer->id.size() == event.issuer.size() && outcome.endpoint->id.size() == event.endpoint.size())
            {
                std::copy(outcome.issuer->id.begin(), outcome.issuer->id.end(), event.issuer.begin());
                std::copy(outcome.endpoint->id.begin(), outcome.endpoint->id.end(), event.endpoint.begin());
                store.save(); // Persist updated endpoint keys before any authorization event.
                if (!store.healthy)
                    event.reason = "authenticated credential could not be saved";
                else if (!store.contains(event.issuer, event.endpoint))
                    event.reason = "authenticated endpoint missing from credential store";
                else if (lookupAccessId(event.issuer, event.endpoint) == HomeKey::InvalidId)
                    event.reason = "authenticated endpoint has no action-ID mapping";
                else
                    event.kind = EventKind::Authenticated;
            }
        }
        else
            event.reason = exchangeFailed ? "Home Key APDU exchange failed"
                : state == ddk::FlowState::Failed ? "Home Key authentication failed"
                : "Home Key authentication interrupted or step limit reached";
        profile->control_flow(session, event.kind == EventKind::Authenticated ? 1 : 0, 0);
    }
    event.generation = store.generation;
    enqueue(event);
}
#endif

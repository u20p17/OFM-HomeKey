#pragma once
#include "HomeKeyPolicy.h"

// Accessed under the credential-store mutex. A result remains private to the
// token returned by start; ready sessions still suppress access until canceled.
class HomeKeyIdentification {
public:
    void start(uint32_t token,uint32_t now) { session=token;started=now;waiting=true;ready=false;revoked=false;issuer.fill(0);endpoint.fill(0); }
    bool active(uint32_t now) const { return waiting && uint32_t(now-started)<60000; }
    void cancel() { waiting=ready=false; }
    bool capture(uint32_t token,uint32_t now,const HomeKey::IssuerId& iid,const HomeKey::EndpointId& eid,bool blocked) {
        if(!active(now)||token!=session||ready)return false;
        issuer=iid;endpoint=eid;revoked=blocked;ready=true;return true;
    }
    uint32_t session=0;
    bool ready=false,revoked=false;
    HomeKey::IssuerId issuer{};
    HomeKey::EndpointId endpoint{};
private:
    uint32_t started=0;
    bool waiting=false;
};

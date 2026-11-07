#include "HomeKeyOnline.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main() {
    using namespace HomeKeyOnline;
    Request r;
    assert(!decode(nullptr, 0, r));
    uint8_t invalid[] = {4, 0};
    assert(!decode(invalid, 1, r));
    for (uint8_t command = 0; command < 3; ++command) {
        assert(decode(&command, 1, r) && r.command == command);
        assert(!decode(&command, 2, r));
    }
    uint8_t request[] = {3, 63, 0x81, 0x23, 0x45, 0x67};
    assert(decode(request, 6, r) && r.index == 63 && r.generation == 0x81234567);
    assert(!decode(request, 5, r));
    request[1] = 64; assert(!decode(request, 6, r));
    request[0] = 4; request[1] = 63;
    assert(decode(request, 6, r) && r.command == 4 && r.index == 63 && r.generation == 0x81234567);
    assert(!decode(request, 5, r));
    request[1] = 64; assert(!decode(request, 6, r));
    request[0] = 5; request[1] = 0; assert(!decode(request, 6, r));
    uint8_t extended[19] = {};
    for (uint8_t command=5;command<=10;++command) {
        extended[0]=command;
        const size_t length=command==5?1:command<=7?5:19;
        assert(decode(extended,length,r) && r.command==command);
        assert(!decode(extended,length-1,r)); assert(!decode(extended,length+1,r));
    }
    extended[0]=11;assert(!decode(extended,1,r));
    uint8_t out[24]; std::memset(out, 0xff, sizeof(out));
    assert(status(out, true, false, true, 7, 2, 64, 0x81234567) == 12);
    assert(out[0] == Ok && out[1] == Version && out[2] == 1 && out[3] == 0 && out[4] == 1);
    assert(out[5] == 7 && out[6] == 2 && out[7] == 64 && get32(out + 8) == 0x81234567 && out[12] == 0xff);
    uint8_t issuer[] = {0, 1, 2, 3, 4, 5, 6, 7}, id[] = {0, 8, 9, 10, 11, 12};
    assert(endpoint(out, 63, 64, 0x81234567, issuer, id) == 22);
    assert(out[2] == 63 && out[3] == 64 && get32(out + 4) == 0x81234567);
    assert(!std::memcmp(out + 8, issuer, 8) && !std::memcmp(out + 16, id, 6) && out[22] == 0xff);
    std::cout << "Home Key online protocol tests passed\n";
}

# OFM-HomeKey 0.1.0

Local ESP32 prototype for OAM-AccessControl, on `v1dev_HomeKey`.
No remote repository or publication is created. The application records this
repository's exact commit in `dependencies.txt` and links its local checkout.

## Runtime and authorization

HomeSpan exposes a lock accessory and NFC Access provisioning service. A
separate worker owns the PN7161 SPI/NCI reader. DigitalDoorKey runs its Home Key
profile through explicit `FlowState::Done`, validates the authentication outcome,
and persists updated endpoint keys before queuing an authorization result.
Only the OpenKNX thread invokes the private OFM-AccessControl friend adapter.

Pairing alone grants no access. A complete 8-byte issuer ID and 6-byte endpoint
ID must be explicitly mapped to logical NFC ID 0..1499. That slot must have an
empty conventional UID record. Mapped IDs are reserved against UID enrollment
and lookup. Existing NFCACT actions and KNX status/synchronization handle the
mapped ID. A failed Home Key authentication never falls back to its UID.
Conventional tags can be enabled separately in ETS. Home app remote unlock
requests are rejected; lock state comes from the actual KNX feedback object.

Events use a bounded queue. Credential mutations invalidate queued events by
generation; dispatch rechecks storage health, current issuer/endpoint presence,
and the mapping. The OpenKNX thread uses nonblocking storage locks. Crypto,
APDU waits, Wi-Fi, SRP and HomeKit polling stay on worker tasks. ECP storage is
an owned 18-byte member, retained for the reader lifetime.

## Storage and lifecycle

Reader identity, endpoint keys/counters and mappings use one atomic, bounded
NVS blob (schema `HKS1`) in partition `homekey`, namespace `ofm-homekey`.
Limits: 16 issuers, 64 endpoints total, 64 mappings, 32 KB encoded blob.
No private credential is placed in ETS, KNX objects or public logs. HAP pairing
and Wi-Fi use HomeSpan's namespaces in the default NVS partition.

Persistence failure disables authentication. Corrupt records are not silently
erased. Endpoint removal prunes its mappings; removing an issuer makes all its
endpoints inaccessible. Removing the last HAP administrator clears Home Key
state. Replacing the reader key/group/sub-identifier clears endpoint keys and
mappings while retaining paired issuer public keys. `hk reset` explicitly clears
Home Key data and closes commissioning; it does not erase HAP pairing or UID
records. Firmware updates retain the NVS partitions. ETS disables access while
tables are unloaded and reloads policy afterward, retaining credentials/mappings.
Reboot retains mappings but closes commissioning and rotates the setup code.

This development layout does not enable NVS/flash encryption or secure boot.
Production storage protection, pairing-window race hardening against an already
started HAP Pair-Setup, OTA/reset policy validation, watchdog/RF testing and
on-device iPhone/Watch provisioning are remaining work. Native tests cover the
store and selection/mapping policy; they do not simulate an Apple credential.

## Commissioning

Use OAM's `debug_ESP32S3_AppleHomeKey` target and its separate application/header.
Enable Home Key and local NFC commissioning in ETS, then use the local USB
console (KNX diagnostic commands cannot configure this module):

```
hk wifi <SSID-without-spaces> <password>
hk pair <8-digit-HomeKit-code>
hk list
hk map <issuer16hex> <endpoint12hex> <id0..1499>
hk unmap <issuer16hex> <endpoint12hex>
hk reset
```

Choose a HomeKit code that is neither repeated digits nor 12345678/87654321.
`hk pair` opens a five-minute provisioning window. Add the accessory in Apple
Home using that code. Existing paired controllers may read or remove NFC
credentials after the window closes; writes require the local window.
After provisioning, list endpoints and map each authorized device to an empty
logical ID configured in the existing NFC action table. Until mapped, taps are
denied. Map updates and revocations invalidate pending results immediately.
Hardware commissioning is a validation plan, not a claim that an iPhone/Watch
has already been provisioned on this board.

Lock feedback is KO 11, 1=physically locked; its initial HomeKit state is Unknown.
The PN7161 prototype pins are CS=GPIO1, SCLK=7, MISO=8, MOSI=9, IRQ=3, VEN=4.
The reference adapter includes its PMU configuration; confirm supply, module
variant, RF tuning and carrier wiring before flashing. This SPI profile cannot
be combined with the fingerprint/relay peripheral profile using those pins.

## Dependencies and provenance

DigitalDoorKey and rednblkx/pn7160 are separately committed local wrappers.
HomeSpan is pinned to 2.1.5, SHA
`45208b3ae9c2fb61d4862ea7aed34c75234506fe`, compatible with Arduino 3.2.1.
The selected toolchain is PioArduino 54.03.21-2, ESP-IDF 5.4, GCC 14.2, C++23.

`NfcReader.hpp`, `Pn7160Reader.hpp` and `Pn7160Reader.cpp` were extracted from
rednblkx/HomeKey-ESP32 at `f167cba1950aa9e37afb9cb06163a78299599a67`.
NFC services and issuer lifecycle follow its HKServices/HomeKitLock design.
The included MIT license preserves the reference author's attribution.

## Native tests

From the OAM application root:

```sh
c++ -std=c++17 -DOPENKNX_HOMEKEY_ENABLED -DARDUINO_ARCH_ESP32 \
  -Ilib/OFM-HomeKey/test/stubs -Ilib/OFM-HomeKey/src -Ilib/DigitalDoorKey/include \
  lib/OFM-HomeKey/test/store_test.cpp lib/OFM-HomeKey/src/CredentialStore.cpp \
  -o /tmp/oam-homekey-store-test
/tmp/oam-homekey-store-test
```

These exercise selection downgrade denial, setup-code validation, explicit
mappings and collisions, reboot persistence, endpoint/identity revocation,
stale-event generation, corrupt/truncated storage, failed NVS commits and reset.

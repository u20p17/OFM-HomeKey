# OFM-HomeKey 0.4.0

Local ESP32 prototype for OAM-AccessControl, on `v1dev_HomeKey`.
No remote repository or publication is created. The application records this
repository's exact commit in `dependencies.txt` and links its local checkout.

## Runtime and authorization

HomeSpan exposes a lock accessory and NFC Access provisioning service. A
separate worker owns the PN7161 I2C/NCI reader. DigitalDoorKey runs its Home Key
profile through explicit `FlowState::Done`, validates the authentication outcome,
and persists updated endpoint keys before queuing an authorization result.
Only the OpenKNX thread invokes the private OFM-AccessControl friend adapter.

Pairing alone grants no access. A complete 8-byte issuer ID and 6-byte endpoint
ID must be explicitly mapped to logical NFC ID 0..1499. That slot must have an
empty conventional UID record. Permissions can come from the ETS table or
local USB mappings, selected explicitly in ETS; local mode is the default.
When ETS mode is selected, USB map/unmap commands are rejected and saved local
mappings cannot grant access. Mapped IDs are reserved against UID enrollment
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

Reader identity, endpoint keys/counters and local USB mappings use one atomic, bounded
NVS blob (schema `HKS1`) in partition `homekey`, namespace `ofm-homekey`.
Limits: 16 issuers, 64 endpoints total, 64 mappings, 32 KB encoded blob.
The module never writes private credentials to ETS, KNX objects or its logs.
The standard Network web console nevertheless echoes entered commands to all
connected consoles; do not use it for password/setup-code commands. HAP pairing
and Wi-Fi use HomeSpan's namespaces in the default NVS partition. The locally
configured HomeKit setup code is stored privately in the `commissioning`
namespace of `homekey`; it is never placed in an ETS parameter or telegram.
ETS mappings live in the KNX parameter table, not in the credential blob.

Persistence failure disables authentication. Corrupt records are not silently
erased. Endpoint removal prunes its mappings; removing an issuer makes all its
endpoints inaccessible. Removing the last HAP administrator clears Home Key
state. Replacing the reader key/group/sub-identifier clears endpoint keys and
mappings while retaining paired issuer public keys. `hk reset` explicitly clears
Home Key data and closes commissioning; it does not erase HAP pairing or UID
records or the privately configured commissioning code. Firmware updates retain the NVS partitions. ETS disables access while
tables are unloaded and reloads policy afterward, retaining credentials/mappings.
Reboot retains configuration but closes commissioning and replaces the active
HomeSpan setup code with an undisclosed random code. ETS can reopen the window
using the privately stored local code. Expired deadlines are cleared, and a
request epoch prevents a late worker from reopening a canceled window.

This development layout does not enable NVS/flash encryption or secure boot.
Production storage protection, pairing-window race hardening against an already
started HAP Pair-Setup, OTA/reset policy validation, watchdog/RF testing and
on-device iPhone/Watch provisioning are remaining work. Native tests cover the
store and selection/mapping policy; they do not simulate an Apple credential.

## Commissioning from ETS

Use OAM's `debug_ESP32S3_AppleHomeKey` or REG1 Home Key target and the matching
development application A6/01 version 11.1, generated from
`src/AccessControl.xml` with `include/knxprod.h`. Home Key is now part of OAM's
original development application. Existing A6/02 Home Key installations and
older A6/01 downloads need a firmware rebuild and matching ETS download.
In ETS the Apple Home Key page is inside Zutrittskontrolle, with parameter
context help and an Apple icon. The OFM-Network web UI has a separate Home Key
page using the standard OpenKNX layout. Setup-code writes require ETS Home Key
and commissioning permission plus programming mode; the saved code is never
returned to the Home Key page. Once initialized, replacement requires the existing
setup code, verified on the HAP worker. Five wrong attempts impose a five-minute
cooldown shared by the form and console; the counter persists across reboot.
An exhausted cooldown restarts on reboot. Invalid code storage or failure to
persist the counter disables code changes rather than allowing initialization.
HTTP must only be used on a trusted network.
Enable Home Key and `NFC-Inbetriebnahme über USB/KNX erlauben` in ETS.
Set network credentials and a private setup code once at the local USB console:

```
hk wifi <SSID-without-spaces> <password>
hk code <8-digit-HomeKit-code>
```

Choose a code that is neither repeated digits nor 12345678/87654321. Wait until
the HAP worker is ready; retry if a command reports busy. The code is persisted
on that worker. A successful `hk code` does not open commissioning.
For replacement use `hk code <new8digits> <old8digits>` at USB, or the Home Key
form. `hk pair <new8digits> <old8digits>` requires the old code too, even if the
new code is unchanged. For reopening a window without changing the code, use ETS.
Do not enter these commands into the standard web console: it broadcasts input.
The setup code is not a web administrator password. Web console
authentication and a configurable first-use administrator password are deferred;
do not expose these development tools to untrusted clients.

The OAM web interface includes overview, console and Home Key, but no web group
monitor or file manager/upload. During an open pairing window, enter the current
setup code under **In Apple Home hinzufügen** to generate a scannable HomeKit
`X-HM://` QR locally in the browser. The local qrcode-generator 2.0.4 asset is
MIT-licensed (see `web/LICENSE-qrcode.txt`); no online QR service is used. HomeSpan
and the webpage use the same stable four-character MAC-derived setup ID and lock
category with IP pairing. The saved code is never returned; entered codes are
not verified by this form, and wrong codes cannot pair. The QR itself contains
the entered code, so keep it private. It clears on hide/page exit and when status
polling detects a closed window, pending code changes or connection loss.
Scan with the iPhone camera or Apple Home's accessory scanner on the same network.
Pairing does not configure ETS authorization or grant access. The short setup ID
is a discovery aid, not a secret or an authentication credential.

Use the ETS parameter-page buttons `Pairing öffnen (5 Minuten)`, `Pairing
schließen` and `Status aktualisieren`. They connect to the device through the
ETS KNX interface using its individual address; no group addresses or Wi-Fi
transport are required. The open button waits for the worker's actual result;
a rejected/canceled request is reported and an accepted request is closed on
failure. The permit and stored private setup code remain mandatory.

After provisioning, select `Berechtigungen aus ETS-Tabelle` = Yes and press
`Provisionierte Geräte-IDs importieren`. The script reads a consistent snapshot
of public issuer/endpoint IDs into completely empty rows of the 16-row table.
It preserves populated/partial rows and existing rights, deduplicates full IDs,
and aborts before writing if there is insufficient space or the snapshot changes
repeatedly. Imported rows stay disabled with action ID 0. Select an unused NFC
action ID, configure its NFCACT action, enable the row and download to authorize
it. Importing IDs alone grants no access. No private keys/setup codes are read.
The online status field records the last query and is refreshed on demand.


#### Link a Home Key permission to a KNX action

`NFC-Tag-ID` (formerly `Logische NFC-Aktions-ID`) is the NFC credential/tag slot (0..1499), not the
number of an action channel. For example, assign the phone slot **100** in its
Home Key permission row. In **Zuweisung von NFC-Tags zu Aktionen**, add
**Tag 100 → Aktion 1**. Configure **Aktion 1** with the required switching
behavior and link its switching communication object to the KNX actuator's
group address. Download the changed parameters. Authentication then follows
phone → slot 100 → NFC assignment → action 1 → KNX group address.

Conventional UID tags use the same slot namespace. Keep their occupied slots
separate from active Home Key permission IDs; an occupied UID slot blocks the
Home Key permission. Different slots may select the same action, for example
**Tag 5 → Aktion 1** alongside **Tag 100 → Aktion 1**. Enable
`Konventionelle UID-Tags zulassen` when using ordinary tags with the external
reader.

The NFC table's **Info holen** button reads the conventional UID tag stored in
the selected slot. For active Home Key rows it instead shows the description
from the current ETS project, checks the reader's provisioned endpoint list and
reports duplicate Home Key IDs or an occupied conventional UID slot. The field
shows up to 80 characters; the progress dialog carries the full text. Project
names and assignments are not proof that parameters have been downloaded, and
local USB mappings are not listed. The button does not scan, enroll, authorize
or trigger an action. Read failures replace stale information and disconnect.

ETS validates unique IDs and identities across active Home Key permissions and
names the conflicting rows before download. The firmware independently rejects
the entire table on duplicates. Repeating a tag ID in the NFC action table is
allowed: it intentionally selects multiple action channels.

The ETS Group Monitor remains an alternative. Assign addresses to these objects:

| KO | Function | DPT |
| --- | --- | --- |
| 11 | Physical lock feedback, 1=locked | 1.001 |
| 12 | Commissioning command: 1=open for five minutes, 0=close | 1.001 |
| 13 | Actual commissioning window open | 1.001 |
| 14 | Authorization table status | 5.010 |
| 60 | Select provisioned endpoint index, 0..63 | 5.010 |
| 61 | Issuer ID, first 8 hex characters | 16.000 |
| 62 | Issuer ID, last 8 hex characters | 16.000 |
| 63 | Endpoint ID, 12 hex characters | 16.000 |
| 64 | Commissioning command result | 5.010 |

Write 1 to KO 12, wait for KO 13=1, then add the accessory in Apple Home using
the locally configured code. An ETS request cannot open commissioning while the
ETS permit is off, before startup completes, or without a valid private code.
Read/removal operations remain available to paired controllers after the window
closes; NFC credential writes require the open window. HomeKit pairing alone
continues to grant no door access.

For ETS authorization, set `Berechtigungen aus ETS-Tabelle` to Yes. After a
phone/watch is provisioned, write index 0 to KO 60 (then 1, 2, etc.). Join the
strings from KOs 61 and 62 to get its full 16-character issuer ID; KO 63 supplies
the 12-character endpoint ID. Out-of-range indexes return blank IDs. Indexes can
change when credentials are removed; use the full IDs as authorization keys.
The objects contain public identifiers only, never private keys or setup codes.

Enable one of the 16 ETS authorization rows, enter those IDs and an empty
logical NFC action ID 0..1499, configure its existing NFCACT action and download
the parameters. Add a separate row for each authorized endpoint. Disable/remove
a row and download to revoke its authorization. Download closes commissioning
and invalidates all queued results before the new table is used.

Malformed hex, overlong values, duplicate endpoint rows, duplicate action IDs
or IDs outside 0..1499 invalidate the entire ETS table: no Home Key can use a
local mapping as fallback. A well-formed row for an endpoint not yet provisioned
remains pending and cannot grant until that identity exists and authenticates.
A UID-occupied/enrolling ID is blocked; mapped IDs are reserved against ordinary
UID enrollment/lookup. Other valid, provisioned rows remain usable when one row
is pending or blocked. ETS rows remain policy declarations across credential
reset/reprovisioning; remove the row to revoke the identity's policy permanently.

Authorization status (KO 14): 0=disabled, 1=local USB policy active,
2=ETS table accepted/all endpoints available, 3=invalid ETS table,
4=one or more endpoints not provisioned, 5=UID slot collision/enrollment,
6=storage/crypto/runtime unavailable. An empty valid ETS table reports 2 and
authorizes nobody. Status 4/5 reports an affected row; runtime checks still apply
to each endpoint and action ID.

Commissioning result (KO 64): 0=ready/code saved, 1=window opened,
2=closed/expired, 3=not permitted/not started, 4=no private setup code,
5=worker not ready/queue busy, 6=private code persistence failed. KO 13 is the
current open state; KO 64 reports the last processed command/result. Repeated
open requests start a fresh five-minute window after the worker applies the
code. Closing cancels queued requests as well as the current window.

## Local USB alternatives

Wi-Fi setup is available from boot, independently of ETS configuration, Home
Key enablement and reader/storage initialization:

```
wifi <SSID> <password>
hk wifi <SSID> <password>
```

Both commands save HomeSpan and ESP32 station credentials and request a
connection without waiting for association/DHCP. `wifi <SSID>` selects an open
network. Once the HAP worker starts, changes are queued to that worker.
KNX diagnostic commands cannot configure Wi-Fi. Network setup does not open a
pairing window, enable Home Key or authorize endpoints. Console help lists the
command. With KNX IP, saved station credentials reconnect before ETS setup.

From the OAM application root, verify bootstrap, queue ownership, persistence
selection, malformed input and remote rejection with:

```sh
c++ -std=c++17 -DOPENKNX_HOMEKEY_ENABLED -DARDUINO_ARCH_ESP32 \
  -Itest/homekey_wifi/stubs -Ilib/OFM-HomeKey/src \
  test/homekey_wifi/command_test.cpp lib/OFM-HomeKey/src/HomeKeyWifi.cpp \
  -o /tmp/oam-homekey-wifi-test
/tmp/oam-homekey-wifi-test
```

```
hk pair <new8digits> [old8digits]
hk list
hk map <issuer16hex> <endpoint12hex> <id0..1499>
hk unmap <issuer16hex> <endpoint12hex>
hk reset
```

`hk pair` saves the code and opens a window; replacement requires the old code.
Map/unmap works only when ETS
authorization is set to No; that mode retains up to 64 persistent local mappings.
KNX diagnostic console commands cannot configure Wi-Fi, setup codes or local
mappings. The standard web console is not a KNX diagnostic console and can run
these commands; its unrestricted administration is a development limitation.
`hk reset` clears credentials/local mappings and closes commissioning,
retaining HAP/Wi-Fi, the private setup code and the ETS policy declarations.
Hardware commissioning remains a validation plan: no phone/watch has been
provisioned on this board during these software checks.

Lock feedback is KO 11, 1=physically locked; its initial HomeKit state is Unknown.
The XIAO Apple Home Key TP and IP targets use `HOMEKEY_READER_I2C`:
SDA=GPIO5 (D4), SCL=GPIO6 (D5), IRQ=GPIO3 (D2), VEN=GPIO4 (D3),
DWL=GPIO2 (D1), address 0x28 at 400 kHz. VDD and VANT both connect to
3.3 V, with common ground. Both default to `HOMEKEY_PN7161_VANT_MV=3300` and hold
DWL low from setup. This wiring cannot be combined with the optional
fingerprint/relay peripheral profile. Earlier XIAO SPI builds are incompatible
with this I2C module and must be replaced.

REG1 v00.11 uses `HOMEKEY_READER_I2C`: SDA=21, SCL=22, IRQ=14, VEN=13,
DWL=19, address 0x28 at 400 kHz. Its Home Key TP, IP and TPIP environments
exclude the legacy PN7160 owner and share the same NCI/ECP/authentication code.
The I2C transport reads NCI headers and payloads separately, rejects incomplete
transfers, bounds IRQ waits and retries a NACKed NCI frame up to three total
attempts with 5 ms between attempts. Per [UM11495 §4.2.3](https://www.nxp.com/docs/en/user-manual/UM11495.pdf),
any NACK requires resending the whole frame. On IDF 5.4+ writes use a
synchronous ESP-IDF device handle attached to the existing Wire bus. The IDF
bus semaphore serializes writes with Wire reads; ACK checking stays enabled.
This avoids Arduino's `i2cWrite()` reporting every recoverable standby NACK as
an error before the transport can retry. Recovered NACKs are debug messages;
an exhausted retry budget or another transfer failure is still an error.
IDF 5.4/5.5 uses `ESP_ERR_INVALID_STATE` for a NACK, while IDF 6 uses
`ESP_ERR_INVALID_RESPONSE`. Timeouts and other errors are returned without
replay; an uninitialized bus is rejected before writing. The ten-second reader
health check can wake a sleeping PN7161 and trigger this normal flow control.
The device handle is released after the NCI worker stops; the shared Wire bus
stays alive. Older IDF versions retain the locked Arduino HAL path.
`HOMEKEY_PN7161_VANT_MV` selects the PMU/TXLDO preset in each environment's
build configuration: REG1 and XIAO default to `3300` (VDD=VANT=3.3 V).
An explicit `5000` override requires VDD=3.3 V and VANT=5 V wiring.
Only these two values are supported. The complete
preset is applied on every reader initialization, following the
[module vendor](https://www.elechouse.com/docs/pn7161/) configuration.
The flag configures the reader; it does not change the physical supply voltage.
Power down before changing VANT wiring or presets, then cold-start with matching
firmware. Never apply 5 V to VDD or ESP32-facing signals. Neither board has been
validated with a phone/watch by these software checks.

HomeKit's model can be set with `HOMEKEY_DEVICE_MODEL`. Build-time
`OPENKNX_WIFI_SSID`/`OPENKNX_WIFI_PASSWORD` credentials are also supplied to
HomeSpan; otherwise an already connected KNX Wi-Fi network is reused or
HomeSpan loads its own persisted credentials. The local `hk wifi` command
remains available.

## Dependencies and provenance

`hk memory` reports free, largest-block and minimum-free internal heap and the
Home Key tasks' minimum remaining stack space. The stack values are in bytes,
reported alongside the configured size; zero minimum-free means that task has
not started or has exhausted its stack margin. `OPENKNX_HOMEKEY_NFC_STACK_SIZE`
and `OPENKNX_HOMEKEY_HAP_STACK_SIZE` default to 12288 bytes. REG1 sets each to
8192 bytes, recovering 8192 bytes of heap with both tasks running. The NFC task
starts only after HAP storage/crypto initialization, reducing startup allocation
overlap. Run `hk memory` after boot, web access, HomeKit pairing and a Home Key
authentication to check margins. Idle headroom alone does not cover all crypto
paths; the REG1 sizes retain about 5 KiB beyond the observed startup usage.

The Home Key page follows OAM-NeoPixel Studio's compact tabs, green actions,
rounded controls and bordered cards. `Setup-Code`, `Apple Home` and `Status`
separate code configuration, local QR generation and live commissioning status.
Arrow keys plus Home/End navigate the tabs. The selected tab is stored in the
URL fragment (`#setup-code`, `#apple-home`, `#status`) and restored after reload;
no setup code is stored in the URL or browser storage. Leaving the Apple Home
tab clears the QR code and its input. Status polls that find commissioning
blocked, pairing closed or the device offline hide the QR code but retain the
input in the open page. An amber message names the exact ETS permission or
pairing action required; the QR button becomes available once those conditions
are satisfied. The responsive layout stacks cards on small screens.
Disabled setup-code actions name the missing ETS permission or programming-mode
requirement instead of showing only a generic warning. No commissioning or
authorization gates are changed.

The ESP32 Home Key web page uses the network module's `HtmlWriter`, which
assembles its body in checked 512-byte chunks. It keeps the existing form token,
code-change checks and browser-generated QR behavior while avoiding a complete
HTML string and a second response-body copy on the ESP32 heap. Low-memory
responses are HTTP 503; retry after other browser connections close. Firmware
builds need the matching OFM-Network implementation of `HtmlWriter`.

DigitalDoorKey and rednblkx/pn7160 are separately committed local wrappers.
OAM pins HomeSpan 2.1.8, SHA
`107ffc07f4455754ea89068d8cf2e992de3583e6`, with Arduino 3.3.8.
The selected HomeKey toolchain is PioArduino 55.03.38-1, ESP-IDF 5.5.4, C++23.

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

These exercise the actual ETS row decoder and authority selection, all 16 rows,
malformed/overlong IDs and duplicates, no local fallback in ETS mode, empty-table
revocation, setup-code validation, expiry/millis wrap and canceled request epochs,
selection downgrade denial, mappings/collisions, reboot persistence,
endpoint/identity revocation, stale-event generation, corrupt/truncated storage,
failed NVS commits and reset. HomeSpan/NVS commissioning commands and real KNX
telegram timing still require device testing.

## ETS device names and removal

Home Key 0.4 / development application 11.3 adds a 32-character `Bezeichnung`
field to every ETS permission row. Names are descriptive metadata; they never
authorize access. Import preserves existing descriptions and treats a named
incomplete row as occupied. Description storage follows the original 16 × 33
byte authorization rows, whose ID/action layout remains unchanged.

Each row has `Gerät löschen (Prog-Modus)`. Turn on programming mode and click
that row's button to revoke its endpoint persistently in the reader, close any
open commissioning window and invalidate queued authorization events. Reader
identity, issuer/controller pairing and other endpoints remain intact. Local
mappings for the endpoint are pruned. Matching ETS rows and their descriptions
are cleared after the reader confirms deletion; save the ETS project and
redownload the parameters. An already absent endpoint can be cleared from ETS
without deleting another device. Busy/stale lists, missing programming mode and
persistence failures preserve the ETS rows. A failed ETS parameter write after
reader revocation restores the row values and reports that cleanup must be
retried; it does not restore the reader credential.

Use the regenerated application/header together and regenerate the signed
knxprod with OpenKNXproducer on the ETS machine. This changes the development
application version to 11.3 and requires a new ETS application download. The
reader's NVS credential schema is unchanged, so this update does not itself
reset provisioned endpoints.

## ETS online protocol and script tests

The module exposes function property object 165/property 3 through the unchanged
OGM-Common wrapper (object 158/property 3), including response fragmentation for
small APDUs. Requests: `0` status, `1` open, `2` close,
`3,index,generationBE32` for an endpoint, or `4,index,generationBE32` to delete
that endpoint while programming mode is active. Deletion replies use the two-byte
result/version envelope; result 10 means programming mode is required. Replies start with result/version (protocol version 1). Status
is 12 bytes: result, version, enabled, permitted, open, commissioning result,
authorization status, endpoint count, generation BE32. Endpoint is 22 bytes:
result, version, index, count, generation BE32, issuer ID (8), endpoint ID (6).
Busy/stale snapshots retry; commissioning result 7 means the request is pending.
Read-only function-property state requests cannot open/close commissioning or delete endpoints.

From OAM-AccessControl, run:

```sh
c++ -std=c++17 -Ilib/OFM-HomeKey/src lib/OFM-HomeKey/test/online_test.cpp -o /tmp/oam-homekey-online-test
/tmp/oam-homekey-online-test
/System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc lib/OFM-HomeKey/test/ets_script_test.js
```

JavaScript tests mock the ETS device/online APIs, cover preservation, cancellation,
capacity, rollback and changing snapshots, and exercise the real Common wrapper
at 15-byte APDU size. ETS on Windows and physical KNX/Apple provisioning still
require hardware validation.

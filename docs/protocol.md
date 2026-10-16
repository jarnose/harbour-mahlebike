# The Mahle bike protocol, as this app uses it

Sources, in order of weight:

1. The Bluetooth code of the official **My SmartBike 3.0.5** Android app (`com.mahle.sbs`, XAPK from
   APKPure), decompiled to smali-like text with androguard. The package `com.mahle.protocol.*` holds
   the commands and the frame parsers per protocol version, `com.mahle.refactored.gatt` the GATT
   description, `com.mahle.refactored.ble` the connection handling.
2. The Home Assistant integration
   [ha-mysmartbike_ble](https://github.com/ReneNulschDE/ha-mysmartbike_ble) (MIT), whose parser tests
   carry real frames from an Orbea Vibe, a Schindelhauer Arthur IX and an X20 (HUS) bike. Every vector in
   `tests/test_mahle.cpp` is one of those.

**Nothing here has been run against a bike by this project.** Where a sentence below says *verified*,
it means "the same in the SDK and in the integration", not "seen on a bike here".

## GATT

| | UUID | Properties | Use |
|---|---|---|---|
| Service | `0000FFE0-…` | | The bike's service. The official scan filter looks for it. |
| `FFD1` | | read, notify | The frames. |
| `FFE1` | | read, notify | "EBM Button Read": the handlebar remote. The app subscribes and logs it; **not decoded**. |
| `FFE2` | | write | Commands. The official app writes with `WRITE_TYPE_NO_RESPONSE`, in fragments of at most 20 bytes, waiting for each write to complete. Every command here is shorter than that. |

Other services in the APK that this app does not touch: `FE59` (Nordic DFU, firmware update) and `180D`
(heart rate, for the "auto assist" feature).

The official app's scan filter (`FactoryBleFilter`) accepts a device when it advertises `FFE0` **or** its
MAC address begins with `00:1E:C0` or `D8:80:39`. Advertised names are `iWoc…` (X25, X35+) and `HUS…` (X20).

### Bonding

Normal monitoring does not need it: the integration connects and subscribes without pairing. The official
app bonds only inside `ConnectSecureAndDiagnosisUseCase`, the dealer's diagnosis / coding flow
(`$D$Z#@` starts a secure session, the bike answers with a challenge `Z…@`, the answer goes back as `Z0…@`).
That flow is **not implemented and will not be**: it exists to write coding into the drive system.
The app has a "Pair" item in the bike list for a bike that refuses notifications on an unencrypted link.

## Framing

`$` type `$` subtype `#` payload `#@`, ASCII for the header, binary for a broadcast payload. A broadcast has
an upper-case `Z` subtype and a lower-case type (`$b$Z#`, `$m$Z#`, `$j$Z#`); a reply echoes its command with a
lower-case type (`$s$V#` answers `$S$V#@`). Big-endian throughout. A notification is one frame; the one
frame longer than 20 bytes, the VIN reply (24), arrives in two notifications on a link that has not
negotiated a bigger payload, which the parser joins. The older spelling `R0<VIN>@` is also accepted.

## What we send

| Bytes | Meaning | Source |
|---|---|---|
| `$S$V#@` | VIN request | `VinNumberRequestCmd` |
| `$S$P#@` | protocol version request | `ProtocolRequestCmd` |
| `$M$A#<level>#@` | set the assist level | `AssistLevelRelativeCmd`, see below |
| `$M$R#@` | reset trip A | `ResetTripCmdV100/V102` |
| `$S$T#HH:mm#@` | set the bike's clock | `TimeCmd` |

On connect the app sends the first two, 250 ms apart, once more after four seconds if either is unanswered.
The integration additionally writes `$D$I#@` when it disconnects. That is `DiagnosisInitRequestCmd`, which
starts a diagnosis session; this app never sends it. (A bike that has nobody connected switches itself off
after about five minutes; the integration's author attributes that to the write, the idle timer is the more
likely cause. Unverified either way.)

### The assist level command is the least certain thing here

The official app's `+` and `−` buttons call `changeLevel(current ± 1)`, which reaches
`AssistLevelRelativeCmd(level)` and writes `$M$A#<level>#@` with no arithmetic in between. So the number on
the wire is the **target level**, in spite of the class name. The bike answers `$m$A#<min><max><current>#@`
(10 bytes, three single-character digits) or `$m$A#OK#@`.

Because the same command is used to ask for the range, and level 0 is a real level, this app has no
"read the range" command: asking with `0` could switch the assistance off. It only sends `current ± 1`,
inside the range the bike last reported (0 to 5 until it has), and after sending checks that the motor
frame shows the new level within 3.5 s; if not, the page says "the bike did not confirm". Controls are
**off by default** for this reason.

## What the bike sends

The protocol version from `$S$P#@` (`EBM`, `100`, `102`, `200`, `300`; some firmwares answer `1.02`) selects the
layout of the 20-byte frames; **length does not identify a layout**. 300 is byte-for-byte 102. The
short frames (17 to 19 bytes) are the older ebikemotion layouts (X25, X35+), `iWoc` bikes.

Battery `$b$Z#`:

| Offset | EBM (≥ 17 B) | 100 / 102 / 300 (20 B) | 200 (20 B) |
|---|---|---|---|
| 5–6 | voltage / 10 | voltage / 100 | same |
| 7 | SOC | SOC, whole byte | bit 7 charging, bits 0–6 SOC |
| 8 | temperature, signed | same | same |
| 9–10 | current / 10, **unsigned** | current / 10, signed | same |
| 11–12 | nominal capacity, Wh / 10 | same | same |
| 13–14 | remaining, Wh / 10 | same | same |
| 15 | | MOSFET temperature, signed | same |
| 15–16 (EBM) / 16–17 | pack × 10000 + cycles | same | same; pack 0 is the DC/DC current, not a pack |

Motor `$m$Z#`: 5 assist level, 6 temperature (`0xD8` = −40 = no reading yet), 9–10 speed / 10 km/h in every
layout. 7–8 is the motor *current* in EBM and 100 (/ 10 A) but the motor *power* in 102 and later (/ 100 W); the
rider's power is 12–13 / 10 W from 102 on, and a signed torque percentage in 100. The wheel-speed byte (11) is
valid whenever the max-torque field (16–17) is not 0, from 102 on.

EBM `$j$Z#`: odometer and range are 32-bit / 10000 km in the short frame and 24-bit / 10 and 16-bit / 10 km
in the 20-byte frames, where consecutive frames alternate between the **lifetime** values and **trip A**
(slot byte: 2 = trip). The light flag, an error byte, two accelerometer axes follow; 200 packs an M-Platform
device id with the error id, and the app shows `device base + id` (drive unit 0, head unit 100, internal
battery 200, external battery 300, charger 400, radar 500), so `204` is fault 4 on the internal battery. What
a number means is explained only by the official app's server; the APK carries no text for them.

Frames recognised and deliberately not decoded: `$b$P` / `$b$R` (power source, 102), `$f$C` / `$f$D` (Trio
wireless remote pairing, 200+), `$m$G` (traction control / auto hold, 200), `$m$M` / `$M$M` (motor maps),
`$s$L` (blinking lights), `$d$…` (diagnosis), `$i$C` (calibration).

## Not in the bike at all

Locking and unlocking the bike's assistance goes through Mahle's cloud (a personal key tied to an account):
the app texts say "you need an active connection to the eBike and to the internet". This app has no network
permission and does not do it.

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

**Tried on one bike so far: an `iWoc8910` (2026-10-16).** Connection, the data frames, the VIN, the assist level
command and the motor maps (read, write, read back) work. Its frames are the short ebikemotion ones (battery 19 bytes,
motor 18, ebm 17), it never answers `$S$P#@`, and its VIN comes in the `R0<VIN>@` spelling. Where a sentence below says *verified*, it means "the same in the SDK and in the
integration", unless it says it was seen on the bike.

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

### Reading the components (the "eBike information" screen)

Battery, motor, trip and fault data arrive by themselves in the broadcast frames. Serial numbers, firmware
versions, coding and status per component have to be asked for with a diagnosis session
(`DiagnosisInitRequestCmd`, `DiagnosisReadUnitRequestCmd`, `DiagnosisEndRequestCmd`):

| Bytes | Meaning |
|---|---|
| `$D$I#@` | start; answer `$d$I#OK#@` (or `ER`) |
| `$D$R#<c>#@` | read; `c` is `0` all, `1` joystick, `2` display, `3` tracker, `4` controller, `5` battery, `6` motor, `7` range extender, `8` radar, `D` internal HMI |
| `$d$R#<c><t>#<data>#@` | one answer per component and data type; `t` is 1 serial, 2 firmware, 3 coding, 4 status, 5 VIN. The official app shows a serial as upper-case hex of the bytes. A refusal is `$d$R#<c><t>ER#@`. |
| `$D$E#@` | end; answer `$d$E#OK#@` |

The official app waits 6.5 s for "all", 5 s for one component, and does this inside its secure-and-diagnosis
use case, which bonds first. Model names ("Battery model", "Motor model") are not in these frames; they come
from Mahle's cloud. **Unverified:** whether an unbonded link is answered; the layout above is from the SDK's
parsers (the answer is `String(bytes, 5, 1)` device, `String(bytes, 6, 1)` type, data from byte 8 to the
terminator). The app's "Bike information" page only reads: it checks the bike is standing, starts the session,
asks for `0`, waits until the answers stop (2 s quiet, 12 s at most), and always ends the session, also after
an error.

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
**The iWoc8910 answered the VIN request but never the protocol request.** The frames then decode by length
(and the `HIJ` marker for the 20-byte trip frames), which is what the integration does when `$S$P#@` times out.
The integration additionally writes `$D$I#@` when it disconnects. That is `DiagnosisInitRequestCmd`, which
starts a diagnosis session; this app never sends it. (A bike that has nobody connected switches itself off
after about five minutes; the integration's author attributes that to the write, the idle timer is the more
likely cause. Unverified either way.)

### The assist level command

**Seen on the bike:** `$M$A#<level>#@` sets the target level (2, 3, 2, 1, 0, 1, 2, 1 in turn, each answered
`$m$A#OK#@`), and the bike also sends `$m$A#031#@` (min 0, max 3, current) by itself about every 20 s, so the
range is known without asking. That bike has levels 0 to 3 and answers `$m$A#ER#@` to a level above that (the range
frame can be 20 s away after a connect, so the app learns the limit from the refusal too). The rest of this section is the reasoning before
that test.

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

## Motor maps (Eco / Dynamic / Sport)

The official app's "motor maps" screen sets how much of its power the motor gives per assist level. The
mode names (Eco, Urban, Sport, Custom, Factory) are **presets that the app fetches from Mahle's server**: the
APK holds no numbers for them. The values this app offers (Eco 30/42/65, Dynamic 40/70/90, Sport 60/80/100 for
levels 1/2/3) are the ones read off the Android app by hand.

On an X35-type bike there are three maps, numbered like the levels, each a single percentage:

| | Bytes |
|---|---|
| Read | `$M$M#R<n>` + 10 zeros + `00` + `#@`, 20 bytes. The official app only ever sends `n = 0`; the bike then sends every map. |
| Write | `$M$M#W<n>` + 10 zeros + `<percent>` + `#@`, 20 bytes (`EngineMapConstWriteCmdEbm`) |
| Reply to a read | `$m$M#` + `R`/`W` + `<n>` + curve points at 7..16 + `<percent>` at 17 + `#@`. A (speed, power) pair counts as a curve point only when its power byte is below 255, so a percentage map has none. |
| Reply to a write | `$m$M#OK#@` or `$m$M#ER#@` (9 bytes) |

The official app writes the maps one at a time, waits 5 s for the answer and tries three times. It replaces
the percentages 35 and 64 by 34 and 65, because they are the characters `#` and `@`. Its own bike-lock feature
writes all three maps (0 % when locked, 100 % when not), which is what confirms "map number = assist level".
Other layouts exist in the SDK for bikes with curve maps (`EngineMapSpeedWriteCmdEbm`) and for 100 / 102 / 200
(`EngineMapWriteCmdV…`, five parameters: peak power, responsiveness, acceleration, overrun and de-rating for the
M40 mid drive); **this app writes none of them**.

How the app uses it: it reads first, and refuses to write unless it received exactly three maps, numbered one
after another, all of the percentage kind, and the bike is standing still. Before writing it stores the
values it had (`previous`, and `original` the first time ever), writes level by level waiting for each OK, reads
everything back and compares (a 35 is expected to read back as 34). **Seen on the iWoc8910:** a read of map 0 gets three frames, `$m$M#W<n>` + 255 fill + percent (the reply always
carries `W`, also to a read), followed by two stray `$m$M#OK#@`; a write gets its `OK` in about 100 ms, and one
in three of the first tries lost its answer, which the retry covers. Eco, Dynamic and Sport were written and read
back correctly. Asking for maps 1, 2, 3 one by one got no answer in the one try, which happened as the link dropped.

## Not in the bike at all

Locking and unlocking the bike's assistance goes through Mahle's cloud (a personal key tied to an account):
the app texts say "you need an active connection to the eBike and to the internet". This app has no network
permission and does not do it.

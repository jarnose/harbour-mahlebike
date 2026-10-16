# Mahle Bike (harbour-mahlebike)

A Sailfish OS app for e-bikes with a **Mahle drive system** (ebikemotion X20, X25, X35+; bikes whose
Bluetooth name starts with `iWoc` or `HUS`, sold as Orbea, Scott, Bianchi, Pinarello, Schindelhauer and
others, and used with the My SmartBike app). It talks to the bike over Bluetooth LE and shows, live:

* speed, assist level, battery charge (%, Wh, voltage, current, temperature, cycles, a second battery),
* motor power and your own power (on bikes that report it), motor current and torque (on the older ones),
* trip, odometer, range, the light, and the fault code with the device it belongs to,

and records **rides**: speed, power, battery and assist level once a second, with the phone's GPS track if
you want one. A ride gets distance, time, averages, climb, battery used and Wh per km, charts, a sketch of
the route, and exports as **GPX** (the bike's readings as extensions) or CSV.

Native C++/QML (Silica). No network permission, no account. Unofficial: not made, endorsed or supported by
Mahle or any bike brand.

## Status: written overnight, never run against a bike

The protocol comes from [ha-mysmartbike_ble](https://github.com/ReneNulschDE/ha-mysmartbike_ble) and from the
decompiled Bluetooth code of the official app (3.0.5). The decoders are tested against the real captures in
that project's tests; the BlueZ link, the recorder and the UI have been built and rendered offscreen but
not used on a bike. [`docs/protocol.md`](docs/protocol.md) says what is verified and what is not. The first
thing to try on a bike, in this order:

1. Choose the bike (Pulley menu > the title). It should show speed, battery and trip within seconds.
2. If the numbers are wrong, **Settings > Diagnostics > Record every frame to a file**, ride for a minute and
   send `frames.log` (it has your bike's serial number in it). Layouts differ by protocol version, which is
   shown in Settings.
3. Only then turn on **Bike controls** (assist level, trip reset, clock). The assist command is the least
   certain thing in the protocol; the app checks that the bike obeyed and says so when it did not.

## Things to know

* The bike talks to **one phone at a time**: close My SmartBike first.
* A bike with nobody connected switches itself off after about five minutes.
* Nothing needs pairing for the numbers to flow. A bike that refuses notifications on an unencrypted
  link can be paired from the bike list (long-press).
* Automatic recording starts at 3 km/h and ends after five minutes standing still. A ride shorter than ten
  seconds is discarded. A ride with no data from the bike for ten minutes ends.
* The GPS is used only while a ride is recording, and only if "Record the route with GPS" is on.

## Build

```
~/SailfishOS/bin/sfdk -c target=SailfishOS-5.1.0.11-aarch64 build
```

or open the folder in Qt Creator with the Sailfish SDK. Tests and checks: `tests/run.sh`, `tools/ci.sh`,
`tools/qmlcheck/render.py` (see [`tests/README.md`](tests/README.md)).

Permissions (Sailjail): `Bluetooth`, `Location` (the ride's track), `Documents` (exports to
`~/Documents/MahleBike`).

## Licence

MIT. The frame layouts follow ha-mysmartbike_ble (MIT, Rene), see `LICENSE`.

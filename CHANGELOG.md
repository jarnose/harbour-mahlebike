# Changelog

## 0.1.0

First version. Written from the protocol of the Home Assistant integration ha-mysmartbike_ble and
the Bluetooth code of the official My SmartBike app 3.0.5. **Not yet tried on a real bike.**

- Connects to a Mahle bike (iWoc / HUS, protocols EBM, 100, 102, 200 and 300) over BlueZ and shows
  speed, assist level, battery (charge, Wh, voltage, current, temperatures, cycles, a second
  battery), motor and rider power, trip, odometer, range, light and fault codes.
- Reconnects by itself when the bike is switched on or the link drops.
- Records rides (1 Hz) with an optional GPS track; per-ride statistics, charts and a route sketch;
  export as GPX (with the bike's readings as extensions) or CSV.
- Optional bike controls, off by default: assist level up/down, trip reset, set the bike's clock.
- A demo mode with a pretend bike, to try the app and record a ride without a bike.
- Metric or imperial units, Finnish translation, a log with the raw frames for bug reports.

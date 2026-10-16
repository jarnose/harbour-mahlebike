# Changelog

## 1.0.1

- Bike information: the component VINs (`R<component><VIN>@`) are recognised instead of being logged as unknown
  frames. Confirmed on the iWoc8910 that the diagnosis session works on an unpaired link.

## 1.0.0

First public release. Everything in 0.1.0 to 0.3.0, tried on one bike (an iWoc8910, ebikemotion X35 type):
connection and live data, assist level changes, motor maps (read, write, read back), rides. The bike information
page (diagnosis session) and the GPS track / GPX export of a ride are written from the official app's code but not
yet confirmed on a bike; other protocol generations (100, 102, 200, 300) are decoded from the integration's
captures only. Reports with a frame capture (Settings > Diagnostics) are welcome.

## 0.3.0

- "Bike information" page: the bike's own broadcast (VIN, capacity, charge cycles, odometer, assist levels)
  and, read with the official app's diagnosis session, the serial numbers and firmware versions of its
  components. Read-only; the last result is kept per bike.

## 0.2.1

- A refused assist level (the bike has levels 0 to 3) now says so and stops the + button there, instead of
  waiting for the bike's range frame.
- Settings shows the protocol as "EBM" for a bike that never answers the protocol request.

## 0.2.0

- Motor maps (Eco / Dynamic / Sport): reads the bike's maps, writes a preset or your own percentages for
  assist levels 1 to 3, keeps what the bike had (the previous values and the very first seen) and offers to
  restore them, and reads the maps back to check. X35-type percentage maps only; a bike with curve-type maps
  is read but never written. Presets are the official app's: Eco 30/42/65, Dynamic 40/70/90, Sport 60/80/100.
  Needs "Allow the app to control the bike"; refuses to write while the bike is moving.

## 0.1.0

First version. Written from the protocol of the Home Assistant integration ha-mysmartbike_ble and
the Bluetooth code of the official My SmartBike app 3.0.5. Tried on an iWoc8910: connection, data and
assist level changes work.

- Connects to a Mahle bike (iWoc / HUS, protocols EBM, 100, 102, 200 and 300) over BlueZ and shows
  speed, assist level, battery (charge, Wh, voltage, current, temperatures, cycles, a second
  battery), motor and rider power, trip, odometer, range, light and fault codes.
- Reconnects by itself when the bike is switched on or the link drops.
- Records rides (1 Hz) with an optional GPS track; per-ride statistics, charts and a route sketch;
  export as GPX (with the bike's readings as extensions) or CSV.
- Optional bike controls, off by default: assist level up/down, trip reset, set the bike's clock.
- A demo mode with a pretend bike, to try the app and record a ride without a bike.
- Metric or imperial units, Finnish translation, a log with the raw frames for bug reports.

# Tests

`src/protocol/` and `src/ride/` are Qt-free on purpose, so they compile and run with plain `g++` on a
development machine. A protocol decoder is exactly the kind of code where "it compiles" proves nothing, and
the bike is a slow place to find out.

```
tests/run.sh
```

builds and runs each suite twice, in strict `c++17` and in `gnu++17` (the mode the Sailfish build uses, where
`unix` is a macro).

| suite | what it checks |
|---|---|
| `mahle` | protocol versions, command bytes, every frame layout (EBM, 100, 102, 200, 300) against real captures from the Home Assistant integration's tests, the second battery, the lifetime / trip alternation, a VIN split over two notifications, noise |
| `ride` | distance integration, gaps, assist time, energy, power, ascent (with GPS noise), haversine, GPX and CSV output, ISO time |

## What is not tested here

Everything that talks to BlueZ, SQLite, the GPS or the screen. `tools/qmlcheck/render.py` loads every QML page
offscreen against stand-ins for Silica and a mock `AppController` and fails on any QML warning; it needs
`PySide6-Essentials` in a venv (see its header) and is not part of CI. `tools/ci.sh` runs what CI runs.

#!/usr/bin/env python3
"""Renders every page offscreen against stand-ins for Sailfish.Silica.

The phone shows a QML mistake (a misspelt property, a missing import, a binding that throws) as a
blank or half-drawn page and a line in a log nobody reads. The C++ build cannot see it, and
`lupdate` and tools/check_qml.py only read the text. This loads each page for real, with a mock
AppController, and fails on any QML warning. It also writes a PNG per page, which is how layout
gets looked at without a phone: the stand-ins are crude, so judge structure, not pixels.

    python3 -m venv /tmp/qv && /tmp/qv/bin/pip install PySide6-Essentials
    /tmp/qv/bin/python tools/qmlcheck/render.py [outdir]
"""
import math
import os
import sys
import time

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtCore import (QAbstractListModel, QModelIndex, QObject, QTimer, QUrl, Property, Qt, Signal, Slot,
                            qInstallMessageHandler)
from PySide6.QtGui import QColor, QGuiApplication, QImage, QPainter, QFont
from PySide6.QtQuick import QQuickImageProvider, QQuickView

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/mahlebike-qml"
os.makedirs(OUT, exist_ok=True)

messages = []


def handler(mode, ctx, msg):
    messages.append(msg)
    print("QML:", msg)


qInstallMessageHandler(handler)


class ThemeIcons(QQuickImageProvider):
    def __init__(self):
        super().__init__(QQuickImageProvider.Image)

    def requestImage(self, ident, size, requested):
        img = QImage(32, 32, QImage.Format_ARGB32)
        img.fill(QColor(0, 0, 0, 0))
        return img


class ListModel(QAbstractListModel):
    def __init__(self, names, rows):
        super().__init__()
        self._names = [n.encode() for n in names]
        self._rows = rows

    def rowCount(self, parent=QModelIndex()):
        return len(self._rows)

    def data(self, index, role):
        if not index.isValid():
            return None
        key = self._names[role - Qt.UserRole - 1].decode()
        return self._rows[index.row()].get(key)

    def roleNames(self):
        return {Qt.UserRole + 1 + i: n for i, n in enumerate(self._names)}


BIKE_ROLES = ["name", "address", "rssi", "paired", "connected", "selected"]
BIKES = [
    dict(name="iWoc 7A21", address="00:1E:C0:12:34:56", rssi=-58, paired=False, connected=False, selected=True),
    dict(name="HUS N46T", address="D8:80:39:AA:BB:CC", rssi=-81, paired=True, connected=False, selected=False),
]

RIDE_ROLES = ["rideId", "startMs", "name", "distanceKm", "durationS", "avgKmh", "energyWh", "hasEnergy", "hasTrack",
              "section", "recording"]
RIDES = [
    dict(rideId=3, startMs=1760600000000, name="Commute", distanceKm=12.4, durationS=2100, avgKmh=21.3,
         energyWh=38.0, hasEnergy=True, hasTrack=True, section="2025-10", recording=False),
    dict(rideId=2, startMs=1760500000000, name="", distanceKm=48.9, durationS=7800, avgKmh=22.5, energyWh=190.0,
         hasEnergy=True, hasTrack=False, section="2025-10", recording=False),
    dict(rideId=1, startMs=1759500000000, name="Sunday loop", distanceKm=63.0, durationS=10800, avgKmh=21.0,
         energyWh=0.0, hasEnergy=False, hasTrack=True, section="2025-10", recording=False),
]

LIVE = dict(speedKmh=23.4, assist=2, assistLow=0, assistHigh=5, soc=57, voltage=37.8, remainingWh=200.3,
            nominalWh=352.8, batteryTemp=22, mosTemp=24, cycles=5, charging=False, batteryCurrent=4.2,
            motorTemp=31, motorPowerW=180.0, riderPowerW=96.0, wheelRpm=180, odometerKm=1234.5, rangeKm=61.0,
            tripKm=14.2, tripRangeKm=60.0, light=True, errorCode=0, errorStatus=0, protocol="102", fresh=True,
            canControl=True)


def detail():
    n = 240
    xs = [1760600000000 + i * 8000 for i in range(n)]
    speed = [18 + 6 * math.sin(i / 20.0) for i in range(n)]
    rider = [80 + 50 * math.sin(i / 15.0) for i in range(n)]
    motor = [150 + 90 * math.sin(i / 17.0) for i in range(n)]
    soc = [90 - i * 0.12 for i in range(n)]
    alt = [40 + 25 * math.sin(i / 40.0) for i in range(n)]
    lats = [60.17 + 0.01 * math.sin(i / 30.0) + 0.0001 * i for i in range(n)]
    lons = [24.94 + 0.02 * math.cos(i / 30.0) for i in range(n)]
    return dict(id=3, startMs=1760600000000, name="Commute", deviceName="iWoc 7A21", recording=False,
                distanceKm=12.4, durationS=2100.0, movingS=1900.0, avgKmh=21.3, maxKmh=31.2, ascentM=84.0,
                hasEnergy=True, energyWh=38.0, whPerKm=3.1, socStart=90, socEnd=61, hasPower=True,
                avgMotorW=160.0, avgRiderW=95.0, maxRiderW=210.0, hasTrack=True, odoStartKm=1200.0,
                odoEndKm=1212.4, assistSeconds=[0, 120, 900, 700, 180, 0, 0, 0],
                xs=xs, speed=speed, riderW=rider, motorW=motor, soc=soc, alt=alt, lats=lats, lons=lons)


class PageStack(QObject):
    @Slot("QVariant", "QVariant")
    def push(self, url, props=None):
        pass

    @Slot()
    def pop(self):
        pass


def _never():
    pass


class App(QObject):
    liveChanged = Signal()
    linkChanged = Signal()
    bikeChanged = Signal()
    scanningChanged = Signal()
    recordingChanged = Signal()
    recordingInfoChanged = Signal()
    gpsChanged = Signal()
    settingsChanged = Signal()
    logChanged = Signal()
    frameLogChanged = Signal()
    message = Signal(str)

    def __init__(self, **state):
        super().__init__()
        self.s = dict(live=LIVE, linkState=3, status="Connected", hasBike=True, bikeName="iWoc 7A21",
                      bikeAddress="00:1E:C0:12:34:56", vin="WMH12345678901234", protocolVersion="1.02",
                      scanning=False, recording=False, recordingInfo={}, gpsStatus="",
                      imperial=False, autoConnect=True, controlEnabled=False, autoRecord=False, useGps=True,
                      keepScreenOn=False,
                      logText="08:15:01 Connecting (attempt 1)\n08:15:03 Connected\n08:15:04 VIN: WMH12345678901234",
                      frameLogText="08:15:04.100 <- motor      24 6d 24 5a 23 ...  $m$Z#...\n08:15:04.300 -> vin?       24 53 24 56 23 40  $S$V#@")
        self.s.update(state)
        self._bikes = ListModel(BIKE_ROLES, BIKES)
        self._rides = ListModel(RIDE_ROLES, RIDES if self.s.get("rideCount", 3) else [])
        self.detail = detail()

    linkState = Property(int, lambda s: s.s["linkState"], constant=True)
    status = Property(str, lambda s: s.s["status"], constant=True)
    live = Property("QVariantMap", lambda s: s.s["live"], constant=True)
    hasBike = Property(bool, lambda s: s.s["hasBike"], constant=True)
    bikeName = Property(str, lambda s: s.s["bikeName"], constant=True)
    bikeAddress = Property(str, lambda s: s.s["bikeAddress"], constant=True)
    vin = Property(str, lambda s: s.s["vin"], constant=True)
    protocolVersion = Property(str, lambda s: s.s["protocolVersion"], constant=True)
    scanning = Property(bool, lambda s: s.s["scanning"], constant=True)
    recording = Property(bool, lambda s: s.s["recording"], constant=True)
    recordingInfo = Property("QVariantMap", lambda s: s.s["recordingInfo"], constant=True)
    gpsStatus = Property(str, lambda s: s.s["gpsStatus"], constant=True)
    imperial = Property(bool, lambda s: s.s["imperial"], lambda s, v: None, notify=settingsChanged)
    autoConnect = Property(bool, lambda s: s.s["autoConnect"], lambda s, v: None, notify=settingsChanged)
    controlEnabled = Property(bool, lambda s: s.s["controlEnabled"], lambda s, v: None, notify=settingsChanged)
    autoRecord = Property(bool, lambda s: s.s["autoRecord"], lambda s, v: None, notify=settingsChanged)
    useGps = Property(bool, lambda s: s.s["useGps"], lambda s, v: None, notify=settingsChanged)
    keepScreenOn = Property(bool, lambda s: s.s["keepScreenOn"], lambda s, v: None, notify=settingsChanged)
    traceFrames = Property(bool, lambda s: s.s.get("traceFrames", False), lambda s, v: None, notify=settingsChanged)
    frameTracePath = Property(str, lambda s: "/home/user/.local/share/io.github.jarnose/mahlebike/frames.log", constant=True)
    logText = Property(str, lambda s: s.s["logText"], constant=True)
    frameLogText = Property(str, lambda s: s.s["frameLogText"], constant=True)
    version = Property(str, lambda s: "0.1.0", constant=True)
    exportDirectory = Property(str, lambda s: "/home/user/Documents/MahleBike", constant=True)
    bikes = Property(QObject, lambda s: s._bikes, constant=True)
    rides = Property(QObject, lambda s: s._rides, constant=True)

    @Slot()
    def startScan(self): pass
    @Slot()
    def stopScan(self): pass
    @Slot(str, str)
    def selectBike(self, a, n): pass
    @Slot()
    def forgetBike(self): pass
    @Slot()
    def connectNow(self): pass
    @Slot()
    def disconnectNow(self): pass
    @Slot(str)
    def pairBike(self, a): pass
    @Slot(int, result=bool)
    def setAssistLevel(self, n): return True
    @Slot(int, result=bool)
    def changeAssistLevel(self, n): return True
    @Slot(result=bool)
    def resetTrip(self): return True
    @Slot(result=bool)
    def syncClock(self): return True
    @Slot(result=bool)
    def startRecording(self): return True
    @Slot()
    def stopRecording(self): pass
    @Slot("qlonglong", result="QVariantMap")
    def rideDetail(self, i): return self.detail
    @Slot("qlonglong", str, result=bool)
    def renameRide(self, i, n): return True
    @Slot("qlonglong", result=bool)
    def deleteRide(self, i): return True
    @Slot("qlonglong", str, result=str)
    def exportRide(self, i, f): return "/home/user/Documents/MahleBike/ride-20251016-0833." + f
    @Slot()
    def clearLog(self): pass


def LIVE_WITH(**kw):
    d = dict(LIVE)
    d.update(kw)
    return d


CASES = [
    ("main", "pages/MainPage.qml", {}, {}),
    ("main_land", "pages/MainPage.qml", {}, {"_land": True}),
    ("main_control", "pages/MainPage.qml", {"controlEnabled": True}, {}),
    ("main_imperial", "pages/MainPage.qml", {"imperial": True}, {}),
    ("main_recording", "pages/MainPage.qml", {"recording": True, "gpsStatus": "GPS fix",
                                              "recordingInfo": {"elapsedS": 754, "distanceKm": 3.42}}, {}),
    ("main_nobike", "pages/MainPage.qml", {"hasBike": False, "bikeName": "", "linkState": 0, "status": "No bike chosen",
                                           "live": {}}, {}),
    ("main_waiting", "pages/MainPage.qml", {"linkState": 0, "live": {},
                                             "status": "Could not connect to the bike: timeout\nTrying again in 10 s"}, {}),
    ("main_connecting", "pages/MainPage.qml", {"linkState": 1, "live": {}, "status": "Connecting to iWoc 7A21…"}, {}),
    ("main_fault", "pages/MainPage.qml", {"live": LIVE_WITH(errorCode=204, errorDevice="internal battery",
                                                             battery2={"soc": 88, "remainingWh": 310.0,
                                                                       "nominalWh": 360.0, "voltage": 37.9,
                                                                       "temp": 20})}, {}),
    ("main_x35", "pages/MainPage.qml", {"live": {"speedKmh": 0.0, "assist": 1, "soc": 84, "voltage": 40.3,
                                                  "remainingWh": 181.8, "nominalWh": 216.7, "batteryTemp": 23,
                                                  "motorCurrentA": 0.0, "torquePct": 0, "odometerKm": 568.1,
                                                  "rangeKm": 78.2, "light": False, "errorCode": 0,
                                                  "assistLow": 0, "assistHigh": 5}}, {}),
    ("bikes", "pages/BikesPage.qml", {"scanning": True}, {}),
    ("rides", "pages/RidesPage.qml", {}, {}),
    ("rides_empty", "pages/RidesPage.qml", {"rideCount": 0}, {}),
    ("ride", "pages/RidePage.qml", {}, {"rideId": 3, "_tall": True}),
    ("settings", "pages/SettingsPage.qml", {"controlEnabled": True}, {"_tall": True}),
    ("log", "pages/LogPage.qml", {}, {}),
    ("about", "pages/AboutPage.qml", {}, {}),
    ("cover", "cover/CoverPage.qml", {}, {}),
    ("cover_idle", "cover/CoverPage.qml", {"linkState": 0, "live": {}, "status": "Disconnected"}, {}),
]


def strip_attached(text):
    """Removes `EnterKey.*` bindings: Silica's attached property has no stand-in here."""
    import re
    out, i = [], 0
    lines = text.split("\n")
    while i < len(lines):
        line = lines[i]
        if re.match(r"\s*EnterKey\.", line):
            depth = line.count("{") - line.count("}")
            while depth > 0:
                i += 1
                depth += lines[i].count("{") - lines[i].count("}")
            i += 1
            continue
        out.append(line)
        i += 1
    return "\n".join(out)


def prepared_qml():
    """A copy of qml/ with the unstubbable bits stripped; the real files are untouched."""
    import shutil
    dst = os.path.join(OUT, "qml")
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(os.path.join(ROOT, "qml"), dst)
    for base, _, names in os.walk(dst):
        for n in names:
            if n.endswith(".qml"):
                p = os.path.join(base, n)
                text = strip_attached(open(p).read())
                open(p, "w").write(text)
    return dst


def main():
    app = QGuiApplication(sys.argv)
    qml_root = prepared_qml()
    failures = 0
    keep = []
    for name, path, state, props in CASES:
        props = dict(props)
        before = len(messages)
        controller = App(**state)

        view = QQuickView()
        view.setColor(QColor('#1b2430'))
        view.engine().addImportPath(os.path.join(HERE, "stubs"))
        view.engine().addImportPath(qml_root)
        view.engine().addImageProvider("theme", ThemeIcons())
        view.rootContext().setContextProperty("AppController", controller)
        view.rootContext().setContextProperty("pageStack", PageStack())
        wide = path.startswith("cover")
        land = props.pop("_land", False)
        tall = props.pop("_tall", False)
        w, h = (400, 640) if wide else ((960, 540) if land else (540, 2600 if tall else 1100))
        host = os.path.join(OUT, "_host.qml")
        with open(host, "w") as f:
            f.write('import QtQuick 2.0\nItem { width: %d; height: %d\n  Loader { id: l; anchors.fill: parent }\n}\n' % (w, h))
        view.setSource(QUrl.fromLocalFile(host))
        view.resize(w, h)
        view.show()
        loader = view.rootObject().children()[0]
        loader.setProperty("source", QUrl.fromLocalFile(os.path.join(qml_root, path)))
        for _ in range(8):
            app.processEvents()
        item = loader.property("item")
        if item is None:
            print("FAIL %s: the page did not load" % name)
            failures += 1
            continue
        for k, v in props.items():
            item.setProperty(k, v)
        end = time.time() + 0.5
        while time.time() < end:
            app.processEvents()
            time.sleep(0.02)
        view.grabWindow().save(os.path.join(OUT, name + ".png"))
        new = [m for m in messages[before:] if "deprecated" not in m]
        bad = [m for m in new if any(w_ in m for w_ in (
            "Error", "error", "TypeError", "ReferenceError", "is not a type", "Unable to assign", "Cannot read",
            "not defined", "Binding loop", "undefined", "is not installed", "Cannot assign"))]
        print("%-22s %s" % (name, "FAIL" if bad else "ok"))
        if bad:
            failures += 1
        loader.setProperty("source", QUrl())
        for _ in range(4):
            app.processEvents()
        view.close()
        keep.append((controller, view))
        del messages[before + len(new):]
    print("%d of %d pages failed" % (failures, len(CASES)))
    return 1 if failures else 0


sys.exit(main())

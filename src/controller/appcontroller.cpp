#include "appcontroller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGeoPositionInfoSource>
#include <QStandardPaths>
#include <QTextStream>

#include <cmath>

namespace {

constexpr int kLiveIntervalMs = 200;     // updates of the dashboard are batched to about 5 per second
constexpr int kStaleFrameMs = 5000;      // no frame for this long: the link is up but the bike is silent
constexpr int kMaxLogLines = 400;
constexpr int kMaxFrameLines = 160;
constexpr int kGpsMaxAgeMs = 10000;      // a fix older than this is not attached to a sample
constexpr int kRecordLinkLossStopS = 600; // a ride with no bike data for this long has ended
constexpr int kAutoStartKmh = 3;
constexpr int kAutoStartSeconds = 3;
constexpr int kAutoStopIdleS = 300;
constexpr int kFlushEverySeconds = 15;
constexpr int kAssistVerifyMs = 3500;
constexpr int kMinCommandGapMs = 400;
constexpr int kMapReplyTimeoutMs = 3500;   // maps asked for, bike silent
constexpr int kMapAckTimeoutMs = 5000;     // the official app waits 5 s for a write's OK, three tries
constexpr int kMapWriteTries = 3;
constexpr int kMapMinPercent = 30;         // the official app warns below this
constexpr double kMapStandstillKmh = 1.0;
// A bike that is off is looked for ever less often: every 30 s, then once a minute.
const int kRetryDelaysS[] = { 3, 5, 10, 20, 30, 30, 30, 60 };
constexpr int kSilentHintAfterS = 12;   // connected, but not one data frame yet

QString hexOf(const QByteArray &data)
{
    // QByteArray::toHex(char separator) is Qt 5.9; Sailfish's Qt is 5.6.
    const QByteArray plain = data.toHex();
    QString out;
    for (int i = 0; i < plain.size(); i += 2) {
        if (i > 0)
            out += QLatin1Char(' ');
        out += QString::fromLatin1(plain.mid(i, 2));
    }
    return out;
}

double toDisplayNumber(double v)
{
    return std::round(v * 10.0) / 10.0;
}

} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<BikeScanner::Device>();

    const QString dataDir = appDataDir();
    QDir().mkpath(dataDir);
    m_logPath = dataDir + QStringLiteral("/mahlebike.log");

    // QStandardPaths::AppConfigLocation is exactly ~/.config/<Org>/<App>, which Sailjail grants. A
    // default-constructed QSettings writes elsewhere and fails silently.
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    m_settings.reset(new QSettings(configDir + QStringLiteral("/settings.ini"), QSettings::IniFormat));
    loadSettings();

    QString error;
    if (!m_store.open(dataDir + QStringLiteral("/rides.sqlite"), &error))
        logEvent(tr("Could not open the ride database: %1").arg(error));
    else
        m_store.recoverUnfinished();
    refreshRides();

    connect(&m_link, &BikeLink::opened, this, &AppController::onOpened);
    connect(&m_link, &BikeLink::failed, this, &AppController::onFailed);
    connect(&m_link, &BikeLink::lost, this, &AppController::onLost);
    connect(&m_link, &BikeLink::frame, this, &AppController::onFrame);
    connect(&m_link, &BikeLink::button, this, &AppController::onButton);
    connect(&m_link, &BikeLink::writeFailed, this, [this](const QString &text) {
        logEvent(tr("Write to the bike failed: %1").arg(text));
    });
    connect(&m_link, &BikeLink::pairFinished, this, [this](const QString &err) {
        if (err.isEmpty()) {
            logEvent(tr("Paired with the bike"));
            emit message(tr("Paired"));
        } else {
            logEvent(err);
            emit message(err);
        }
    });
    connect(&m_scanner, &BikeScanner::deviceUpdated, this, &AppController::onDeviceUpdated);
    connect(&m_scanner, &BikeScanner::errorOccurred, this, [this](const QString &text) {
        logEvent(text);
        emit message(text);
    });

    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &AppController::tryConnect);

    m_liveTimer.setSingleShot(true);
    m_liveTimer.setInterval(kLiveIntervalMs);
    connect(&m_liveTimer, &QTimer::timeout, this, &AppController::publishLive);

    m_mapTimer.setSingleShot(true);
    connect(&m_mapTimer, &QTimer::timeout, this, &AppController::mapsOnTimeout);

    m_demoTimer.setInterval(1000);
    connect(&m_demoTimer, &QTimer::timeout, this, &AppController::demoTick);

    m_assistCheck.setSingleShot(true);
    m_assistCheck.setInterval(kAssistVerifyMs);
    connect(&m_assistCheck, &QTimer::timeout, this, [this]() {
        if (m_pendingAssist < 0)
            return;
        const auto &motor = m_parser.motor();
        const int wanted = m_pendingAssist;
        m_pendingAssist = -1;
        if (motor && motor->assistLevel == wanted)
            return;
        const QString text = tr("The bike did not confirm assist level %1").arg(wanted);
        logEvent(text + (motor ? QStringLiteral(" (it reports %1)").arg(motor->assistLevel) : QString()));
        emit message(text);
    });

    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &AppController::onTick);
    m_tick.start();

    m_sinceCommand.start();
    setLink(0, hasBike() ? tr("Not connected") : tr("No bike chosen"));

    if (hasBike() && m_autoConnect) {
        m_wantConnected = true;
        QTimer::singleShot(1500, this, &AppController::tryConnect);
    }
}

AppController::~AppController()
{
    if (recording())
        stopRecording();
    m_link.close(false);
    m_scanner.stopDiscovery();
}

QString AppController::version() const
{
#ifdef APP_VERSION
    return QStringLiteral(APP_VERSION);
#else
    return QStringLiteral("dev");
#endif
}

QString AppController::bikeName() const
{
    return m_demo ? tr("Demo bike") : m_bikeName;
}

QString AppController::vin() const
{
    return m_demo ? QString::fromStdString(Mahle::Demo::Bike::vin()) : m_vin;
}

QString AppController::protocolVersion() const
{
    if (m_demo)
        return QStringLiteral("1.02");
    // An iWoc bike never answers the protocol request; its short frames (no MOSFET temperature in the
    // battery frame) are the ebikemotion "EBM" layout, which the integration also falls back to.
    if (m_protocolVersion.isEmpty() && m_gotFrame && m_parser.battery() && !m_parser.battery()->mosfetTemperature)
        return tr("EBM (the bike does not answer)");
    return m_protocolVersion;
}

QString AppController::appDataDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString AppController::exportDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/MahleBike");
}

// ---- settings --------------------------------------------------------------------------------

void AppController::loadSettings()
{
    m_bikeAddress = m_settings->value(QStringLiteral("bike/address")).toString();
    m_bikeName = m_settings->value(QStringLiteral("bike/name")).toString();
    m_vin = m_settings->value(QStringLiteral("bike/vin")).toString();
    m_protocolVersion = m_settings->value(QStringLiteral("bike/protocol")).toString();
    m_parser.setProtocolVersion(m_protocolVersion.toStdString());
    m_imperial = m_settings->value(QStringLiteral("ui/imperial"), false).toBool();
    m_autoConnect = m_settings->value(QStringLiteral("bike/autoConnect"), true).toBool();
    m_controlEnabled = m_settings->value(QStringLiteral("bike/controlEnabled"), false).toBool();
    m_autoRecord = m_settings->value(QStringLiteral("rides/autoRecord"), false).toBool();
    m_useGps = m_settings->value(QStringLiteral("rides/useGps"), true).toBool();
    m_keepScreenOn = m_settings->value(QStringLiteral("ui/keepScreenOn"), false).toBool();
    m_traceFrames = m_settings->value(QStringLiteral("debug/traceFrames"), false).toBool();
    m_bikes.setSelectedAddress(m_bikeAddress);
}

void AppController::saveSetting(const QString &key, const QVariant &value)
{
    m_settings->setValue(key, value);
    m_settings->sync();
}

void AppController::setImperial(bool on)
{
    if (m_imperial == on)
        return;
    m_imperial = on;
    saveSetting(QStringLiteral("ui/imperial"), on);
    emit settingsChanged();
}

void AppController::setAutoConnect(bool on)
{
    if (m_autoConnect == on)
        return;
    m_autoConnect = on;
    saveSetting(QStringLiteral("bike/autoConnect"), on);
    emit settingsChanged();
}

void AppController::setControlEnabled(bool on)
{
    if (m_controlEnabled == on)
        return;
    m_controlEnabled = on;
    saveSetting(QStringLiteral("bike/controlEnabled"), on);
    logEvent(on ? tr("Bike controls enabled") : tr("Bike controls disabled"));
    emit settingsChanged();
    scheduleLive();
}

void AppController::setAutoRecord(bool on)
{
    if (m_autoRecord == on)
        return;
    m_autoRecord = on;
    saveSetting(QStringLiteral("rides/autoRecord"), on);
    emit settingsChanged();
}

void AppController::setUseGps(bool on)
{
    if (m_useGps == on)
        return;
    m_useGps = on;
    saveSetting(QStringLiteral("rides/useGps"), on);
    emit settingsChanged();
    if (recording()) {
        if (on)
            startPositioning();
        else
            stopPositioning();
    }
}

void AppController::setKeepScreenOn(bool on)
{
    if (m_keepScreenOn == on)
        return;
    m_keepScreenOn = on;
    saveSetting(QStringLiteral("ui/keepScreenOn"), on);
    emit settingsChanged();
}

void AppController::setTraceFrames(bool on)
{
    if (m_traceFrames == on)
        return;
    m_traceFrames = on;
    saveSetting(QStringLiteral("debug/traceFrames"), on);
    logEvent(on ? tr("Writing every frame to %1").arg(frameTracePath()) : tr("Frame tracing off"));
    emit settingsChanged();
}

QString AppController::frameTracePath() const
{
    return appDataDir() + QStringLiteral("/frames.log");
}

// ---- log -------------------------------------------------------------------------------------

void AppController::logEvent(const QString &text)
{
    const QString line = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")) + QLatin1Char(' ') + text;
    m_log.append(line);
    while (m_log.size() > kMaxLogLines)
        m_log.removeFirst();
    emit logChanged();

    QFileInfo info(m_logPath);
    if (info.exists() && info.size() > 512 * 1024) {
        QFile::remove(m_logPath + QStringLiteral(".old"));
        QFile::rename(m_logPath, m_logPath + QStringLiteral(".old"));
    }
    QFile f(m_logPath);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&f);
        out << QDateTime::currentDateTime().toString(Qt::ISODate) << ' ' << text << '\n';
    }
}

void AppController::logFrame(const char *direction, const QByteArray &data, const char *kind)
{
    const Mahle::Bytes bytes(data.begin(), data.end());
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString line = stamp + QLatin1Char(' ') + QLatin1String(direction) + QLatin1Char(' ')
                         + QString::fromLatin1(kind).leftJustified(10) + QLatin1Char(' ') + hexOf(data)
                         + QLatin1String("  ") + QString::fromStdString(Mahle::printable(bytes));

    if (m_traceFrames) {
        QFileInfo info(frameTracePath());
        if (info.exists() && info.size() > 2 * 1024 * 1024) {
            QFile::remove(frameTracePath() + QStringLiteral(".old"));
            QFile::rename(frameTracePath(), frameTracePath() + QStringLiteral(".old"));
        }
        QFile f(frameTracePath());
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&f);
            out << QDateTime::currentDateTime().toString(Qt::ISODate) << ' ' << line << '\n';
        }
    }

    // Battery, motor and trip frames arrive several times a second and would push everything else
    // out of the log in half a minute. The page shows the first few of each, then only the newest
    // of each kind; replies, commands and anything unrecognised are all kept.
    const QString k = QString::fromLatin1(kind);
    const bool broadcast = (k == QLatin1String("battery") || k == QLatin1String("motor") || k == QLatin1String("ebm"))
                           && direction[0] == '<';
    if (broadcast) {
        if (m_broadcastShown < 6) {
            ++m_broadcastShown;
            m_frameLog.append(line);
        }
        m_latest.insert(k, line);
    } else {
        m_frameLog.append(line);
    }
    while (m_frameLog.size() > kMaxFrameLines)
        m_frameLog.removeFirst();
    emit frameLogChanged();
}

QString AppController::frameLogText() const
{
    QStringList lines = m_frameLog;
    if (!m_latest.isEmpty()) {
        lines.append(QStringLiteral("--- newest frame of each kind ---"));
        for (auto it = m_latest.constBegin(); it != m_latest.constEnd(); ++it)
            lines.append(it.value());
    }
    return lines.join(QLatin1Char('\n'));
}

void AppController::clearLog()
{
    m_log.clear();
    m_frameLog.clear();
    m_latest.clear();
    m_broadcastShown = 0;
    QFile::remove(m_logPath);
    emit logChanged();
    emit frameLogChanged();
}

// ---- bikes -----------------------------------------------------------------------------------

void AppController::setLink(int state, const QString &status)
{
    if (m_linkState == state && m_status == status)
        return;
    m_linkState = state;
    m_status = status;
    emit linkChanged();
}

void AppController::startScan()
{
    m_bikes.clear();
    if (hasBike()) {
        BikeScanner::Device known;
        known.address = m_bikeAddress;
        known.name = m_bikeName.isEmpty() ? m_bikeAddress : m_bikeName;
        known.objectPath = m_scanner.devicePathForAddress(m_bikeAddress);
        m_bikes.upsert(known);
    }
    m_bikes.setSelectedAddress(m_bikeAddress);
    m_scanner.startDiscovery();
    m_scanner.refresh();
    emit scanningChanged();
}

void AppController::stopScan()
{
    m_scanner.stopDiscovery();
    emit scanningChanged();
}

void AppController::onDeviceUpdated(const BikeScanner::Device &device)
{
    m_bikes.upsert(device);
}

void AppController::selectBike(const QString &address, const QString &name)
{
    if (m_demo)
        stopDemo();
    const bool changed = address.compare(m_bikeAddress, Qt::CaseInsensitive) != 0;
    m_bikeAddress = address;
    m_bikeName = name;
    if (changed) {
        m_vin.clear();
        m_protocolVersion.clear();
        m_parser.reset();
        m_live.clear();
        emit liveChanged();
    }
    saveSetting(QStringLiteral("bike/address"), m_bikeAddress);
    saveSetting(QStringLiteral("bike/name"), m_bikeName);
    saveSetting(QStringLiteral("bike/vin"), m_vin);
    saveSetting(QStringLiteral("bike/protocol"), m_protocolVersion);
    m_bikes.setSelectedAddress(m_bikeAddress);
    emit bikeChanged();
    logEvent(tr("Bike chosen: %1 (%2)").arg(name, address));
    stopScan();
    connectNow();
}

void AppController::forgetBike()
{
    disconnectNow();
    m_bikeAddress.clear();
    m_bikeName.clear();
    m_vin.clear();
    m_protocolVersion.clear();
    m_parser.reset();
    m_live.clear();
    for (const char *key : { "bike/address", "bike/name", "bike/vin", "bike/protocol" })
        m_settings->remove(QLatin1String(key));
    m_settings->sync();
    m_bikes.setSelectedAddress(QString());
    setLink(0, tr("No bike chosen"));
    emit bikeChanged();
    emit liveChanged();
}

void AppController::connectNow()
{
    if (m_demo)
        stopDemo();
    if (m_bikeAddress.isEmpty())
        return;
    m_wantConnected = true;
    m_attempt = 0;
    m_retry.stop();
    tryConnect();
}

void AppController::disconnectNow()
{
    if (m_demo) {
        stopDemo();
        return;
    }
    m_wantConnected = false;
    m_retry.stop();
    m_pendingAssist = -1;
    m_scanner.stopDiscovery();
    mapsReset();
    m_link.close(true);
    m_gotFrame = false;
    setLink(0, hasBike() ? tr("Disconnected") : tr("No bike chosen"));
    logEvent(tr("Disconnected"));
    scheduleLive();
}

void AppController::pairBike(const QString &address)
{
    const QString path = m_scanner.devicePathForAddress(address);
    if (path.isEmpty()) {
        emit message(tr("The bike is not visible to Bluetooth right now"));
        return;
    }
    logEvent(tr("Pairing with %1").arg(address));
    m_link.pair(path);
}

void AppController::tryConnect()
{
    if (!m_wantConnected || m_bikeAddress.isEmpty() || m_demo)
        return;
    if (m_link.state() != BikeLink::Closed)
        return;
    const QString path = m_scanner.devicePathForAddress(m_bikeAddress);
    if (path.isEmpty()) {
        // BlueZ forgets an unpaired device a few minutes after it was last seen. Scanning brings it back.
        setLink(1, tr("Looking for the bike…"));
        m_scanner.startDiscovery();
        ++m_attempt;
        scheduleRetry(tr("The bike is not visible to Bluetooth. Switch it on and keep it close."));
        return;
    }
    m_scanner.stopDiscovery();
    ++m_attempt;
    setLink(1, tr("Connecting to %1…").arg(m_bikeName.isEmpty() ? m_bikeAddress : m_bikeName));
    logEvent(tr("Connecting (attempt %1)").arg(m_attempt));
    m_link.open(path);
}

void AppController::scheduleRetry(const QString &because)
{
    if (!m_wantConnected)
        return;
    // Without "connect automatically" a bike that cannot be reached is given up on after a few tries
    // instead of being chased for ever.
    if (!m_autoConnect && m_attempt >= 3) {
        m_wantConnected = false;
        m_scanner.stopDiscovery();
        setLink(0, because);
        return;
    }
    const int index = std::min<int>(std::max(0, m_attempt - 1), int(sizeof kRetryDelaysS / sizeof kRetryDelaysS[0]) - 1);
    const int delay = kRetryDelaysS[index];
    setLink(0, because + QLatin1Char('\n') + tr("Trying again in %1 s").arg(delay));
    m_retry.start(delay * 1000);
}

void AppController::onOpened()
{
    mapsReset();
    m_assistCap = -1;
    m_attempt = 0;
    m_gotFrame = false;
    m_framesSinceOpen = 0;
    m_noDataSeconds = 0;
    m_broadcastShown = 0;
    m_sinceFrame.invalidate();
    setLink(2, tr("Connected, waiting for the bike's data…"));
    logEvent(tr("Connected"));
    handshake();
}

void AppController::onFailed(const QString &text)
{
    mapsReset();
    logEvent(text);
    m_gotFrame = false;
    scheduleRetry(text);
}

void AppController::onLost()
{
    mapsReset();
    logEvent(tr("The bike disconnected"));
    m_gotFrame = false;
    m_pendingAssist = -1;
    scheduleLive();
    // A bike that switches itself off (about five minutes after its last use) lands here.
    m_attempt = 0;
    if (!m_autoConnect)
        m_wantConnected = false;
    if (m_wantConnected)
        scheduleRetry(tr("The bike disconnected."));
    else
        setLink(0, tr("Disconnected"));
}

void AppController::handshake()
{
    // The bike broadcasts its battery, motor and trip frames by itself once notifications are on.
    // These two questions are the ones the integration also asks, 200 ms apart: the VIN, for the
    // device page, and the protocol version, which decides how the 20-byte frames are read.
    m_link.write(QByteArray("$S$V#@"));
    logFrame("->", QByteArray("$S$V#@"), "vin?");
    QTimer::singleShot(250, this, [this]() {
        if (!m_link.isOpen())
            return;
        m_link.write(QByteArray("$S$P#@"));
        logFrame("->", QByteArray("$S$P#@"), "protocol?");
    });
    // The first request can be lost while the bike is still waking up: ask once more.
    QTimer::singleShot(4000, this, [this]() {
        if (!m_link.isOpen())
            return;
        if (m_parser.vin().empty()) {
            m_link.write(QByteArray("$S$V#@"));
            logFrame("->", QByteArray("$S$V#@"), "vin?");
        }
        if (m_parser.protocol() == Mahle::Protocol::Unknown) {
            m_link.write(QByteArray("$S$P#@"));
            logFrame("->", QByteArray("$S$P#@"), "protocol?");
        }
    });
}

bool AppController::sendCommand(const Mahle::Bytes &bytes, const char *what, bool ignoreGap)
{
    if (!linkOpen()) {
        emit message(tr("Not connected to the bike"));
        return false;
    }
    if (!ignoreGap && m_sinceCommand.isValid() && m_sinceCommand.elapsed() < kMinCommandGapMs) {
        emit message(tr("One moment…"));
        return false;
    }
    const QByteArray data(reinterpret_cast<const char *>(bytes.data()), int(bytes.size()));
    if (m_demo) {
        m_sinceCommand.restart();
        logFrame("->", data, what);
        for (const Mahle::Bytes &reply : m_demoBike.receive(bytes))
            handleFrame(QByteArray(reinterpret_cast<const char *>(reply.data()), int(reply.size())));
        return true;
    }
    if (!m_link.write(data)) {
        emit message(tr("Could not send to the bike"));
        return false;
    }
    m_sinceCommand.restart();
    logFrame("->", data, what);
    return true;
}

void AppController::startDemo()
{
    if (m_demo)
        return;
    m_wantConnected = false;
    m_retry.stop();
    m_scanner.stopDiscovery();
    m_link.close(true);
    m_parser.reset();
    m_live.clear();
    m_demo = true;
    mapsReset();
    m_demoBike = Mahle::Demo::Bike();
    m_gotFrame = false;
    m_framesSinceOpen = 0;
    m_broadcastShown = 0;
    m_sinceFrame.invalidate();
    logEvent(tr("Demo started: a pretend bike"));
    setLink(2, tr("Demo: a pretend bike"));
    emit bikeChanged();
    // The handshake the real bike gets.
    sendCommand(Mahle::cmdVin(), "vin?");
    m_sinceCommand.invalidate();
    sendCommand(Mahle::cmdProtocol(), "protocol?");
    m_sinceCommand.invalidate();
    m_demoTimer.start();
    demoTick();
}

void AppController::stopDemo()
{
    if (!m_demo)
        return;
    m_mapTimer.stop();
    m_mapState = MapIdle;
    m_mapReadThisSession = false;
    m_demoTimer.stop();
    if (recording())
        stopRecording();
    m_demo = false;
    m_gotFrame = false;
    m_pendingAssist = -1;
    m_parser.reset();
    m_parser.setProtocolVersion(m_protocolVersion.toStdString());
    m_live.clear();
    m_latest.clear();
    logEvent(tr("Demo stopped"));
    setLink(0, m_bikeAddress.isEmpty() ? tr("No bike chosen") : tr("Not connected"));
    emit bikeChanged();
    emit liveChanged();
}

void AppController::demoTick()
{
    for (const Mahle::Bytes &f : m_demoBike.step(1.0))
        handleFrame(QByteArray(reinterpret_cast<const char *>(f.data()), int(f.size())));
}

// ---- frames ----------------------------------------------------------------------------------

void AppController::onButton(const QByteArray &data)
{
    // Not decoded: it is logged so that a capture can show what the handlebar remote sends.
    logFrame("<-", data, "button");
}

void AppController::onFrame(const QByteArray &data)
{
    if (m_demo)
        return; // a late frame of the real bike while the pretend one is on
    handleFrame(data);
}

void AppController::handleFrame(const QByteArray &data)
{
    const Mahle::Kind kind = m_parser.feed(reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()));
    // Battery, motor and trip frames arrive several times a second; the log keeps the last ones only.
    logFrame("<-", data, Mahle::kindName(kind));

    ++m_framesSinceOpen;
    m_sinceFrame.restart();
    if (!m_gotFrame && (kind == Mahle::Kind::Battery || kind == Mahle::Kind::Motor || kind == Mahle::Kind::Ebm)) {
        m_gotFrame = true;
        setLink(3, m_demo ? tr("Demo: a pretend bike") : tr("Connected"));
    }

    bool bikeChangedNow = false;
    if (kind == Mahle::Kind::Vin) {
        const QString vin = QString::fromStdString(m_parser.vin());
        if (!m_demo && vin != m_vin) {
            m_vin = vin;
            saveSetting(QStringLiteral("bike/vin"), m_vin);
            bikeChangedNow = true;
            logEvent(tr("VIN: %1").arg(m_vin));
        }
    } else if (kind == Mahle::Kind::ProtocolReply) {
        const QString version = QString::fromStdString(m_parser.protocolVersion());
        if (!m_demo && version != m_protocolVersion) {
            m_protocolVersion = version;
            saveSetting(QStringLiteral("bike/protocol"), m_protocolVersion);
            bikeChangedNow = true;
            logEvent(tr("Protocol version: %1").arg(m_protocolVersion));
        }
    } else if (kind == Mahle::Kind::Assist) {
        const auto &a = m_parser.assist();
        logEvent(tr("Assist levels %1 to %2, now %3").arg(a->min).arg(a->max).arg(a->current));
    } else if (kind == Mahle::Kind::AssistAck) {
        const QString answer = QString::fromStdString(m_parser.lastAck());
        logEvent(tr("Assist answer: %1").arg(answer));
        if (answer == QLatin1String("ER") && m_pendingAssist >= 0) {
            // The bike refuses a level it does not have; the range it sends by itself may not have
            // arrived yet. Remember the limit so the + button stops there.
            const int refused = m_pendingAssist;
            m_pendingAssist = -1;
            m_assistCheck.stop();
            const auto &motor = m_parser.motor();
            if (motor && refused > motor->assistLevel)
                m_assistCap = refused - 1;
            emit message(tr("The bike does not have assist level %1").arg(refused));
        }
    }
    if (bikeChangedNow)
        emit bikeChanged();

    if (kind == Mahle::Kind::MapData) {
        // A bike that answers a write with the map itself instead of an OK counts as accepting it.
        if (m_mapState == MapWriting && m_mapAckPending && !m_mapQueue.isEmpty()) {
            const auto &got = m_parser.maps();
            const auto it = got.find(m_mapQueue.first().first);
            if (it != got.end() && it->second.writeFlag
                && it->second.percent == Mahle::mapSafePercent(m_mapQueue.first().second)) {
                m_mapAckPending = false;
                m_mapTimer.stop();
                logEvent(tr("The bike accepted map %1").arg(m_mapQueue.first().first));
                m_mapQueue.removeFirst();
                m_mapTries = 0;
                mapsWriteStep();
                scheduleLive();
                return;
            }
        }
        if (m_mapState == MapReading && m_parser.maps().size() >= 3)
            mapsFinishRead();
        else if (m_mapState == MapVerifying && m_parser.maps().size() >= int(m_mapExpected.size()))
            mapsFinishRead();
        else
            mapsPublish();
    } else if (kind == Mahle::Kind::MapAck) {
        m_mapAck = QString::fromStdString(m_parser.lastMapAck());
        if (m_mapState == MapWriting && m_mapAckPending) {
            m_mapAckPending = false;
            m_mapTimer.stop();
            if (m_mapAck == QLatin1String("OK")) {
                logEvent(tr("The bike accepted map %1").arg(m_mapQueue.first().first));
                m_mapQueue.removeFirst();
                m_mapTries = 0;
                mapsWriteStep();
            } else {
                mapsAbort(tr("The bike refused map %1 (%2)").arg(m_mapQueue.first().first).arg(m_mapAck));
            }
        }
    }

    scheduleLive();
}

void AppController::scheduleLive()
{
    if (!m_liveTimer.isActive())
        m_liveTimer.start();
}

void AppController::publishLive()
{
    m_live = buildLive();
    emit liveChanged();
}

QVariantMap AppController::buildLive() const
{
    QVariantMap m;
    const auto &motor = m_parser.motor();
    const auto &battery = m_parser.battery();
    const auto &ebm = m_parser.ebm();

    if (motor) {
        m.insert(QStringLiteral("speedKmh"), motor->speedKmh);
        m.insert(QStringLiteral("assist"), motor->assistLevel);
        if (motor->temperatureValid())
            m.insert(QStringLiteral("motorTemp"), motor->temperature);
        if (motor->motorPowerW)
            m.insert(QStringLiteral("motorPowerW"), *motor->motorPowerW);
        if (motor->riderPowerW)
            m.insert(QStringLiteral("riderPowerW"), *motor->riderPowerW);
        if (motor->currentAmp)
            m.insert(QStringLiteral("motorCurrentA"), *motor->currentAmp);
        if (motor->torquePct)
            m.insert(QStringLiteral("torquePct"), *motor->torquePct);
        if (motor->wheelRpm)
            m.insert(QStringLiteral("wheelRpm"), *motor->wheelRpm);
    }
    if (const auto &range = m_parser.assist()) {
        m.insert(QStringLiteral("assistMin"), range->min);
        m.insert(QStringLiteral("assistMax"), range->max);
    }
    if (battery) {
        m.insert(QStringLiteral("soc"), battery->soc);
        m.insert(QStringLiteral("voltage"), battery->voltage);
        m.insert(QStringLiteral("remainingWh"), battery->remainingWh);
        m.insert(QStringLiteral("nominalWh"), battery->nominalCapacityWh);
        m.insert(QStringLiteral("batteryTemp"), battery->temperature);
        if (battery->mosfetTemperature)
            m.insert(QStringLiteral("mosTemp"), *battery->mosfetTemperature);
        if (battery->cycles)
            m.insert(QStringLiteral("cycles"), *battery->cycles);
        m.insert(QStringLiteral("charging"), battery->charging);
        m.insert(QStringLiteral("batteryCurrent"), battery->current);
    }
    if (m_parser.hasSecondBattery()) {
        const Mahle::Battery &b = *m_parser.battery2();
        QVariantMap two;
        two.insert(QStringLiteral("soc"), b.soc);
        two.insert(QStringLiteral("voltage"), b.voltage);
        two.insert(QStringLiteral("remainingWh"), b.remainingWh);
        two.insert(QStringLiteral("nominalWh"), b.nominalCapacityWh);
        two.insert(QStringLiteral("temp"), b.temperature);
        if (b.cycles)
            two.insert(QStringLiteral("cycles"), *b.cycles);
        m.insert(QStringLiteral("battery2"), two);
    }
    if (ebm) {
        if (ebm->lifetimeKnown) {
            m.insert(QStringLiteral("odometerKm"), ebm->odometerKm);
            m.insert(QStringLiteral("rangeKm"), ebm->rangeKm);
        }
        if (ebm->tripKm)
            m.insert(QStringLiteral("tripKm"), *ebm->tripKm);
        if (ebm->tripRangeKm)
            m.insert(QStringLiteral("tripRangeKm"), *ebm->tripRangeKm);
        m.insert(QStringLiteral("light"), ebm->light);
        m.insert(QStringLiteral("errorCode"), ebm->errorCode);
        m.insert(QStringLiteral("errorStatus"), ebm->status);
        if (m_parser.protocol() == Mahle::Protocol::V200 && ebm->errorCode >= 100)
            m.insert(QStringLiteral("errorDevice"), QString::fromLatin1(Mahle::errorDeviceName(ebm->errorCode)));
        if (ebm->remoteSoc)
            m.insert(QStringLiteral("remoteSoc"), *ebm->remoteSoc);
    }
    m.insert(QStringLiteral("protocol"), QString::fromLatin1(Mahle::protocolName(m_parser.protocol())));
    m.insert(QStringLiteral("fresh"), fresh(kStaleFrameMs));

    // What the assist buttons may do, so the page need not know the rules.
    int lo = 0, hi = 5;
    if (const auto &range = m_parser.assist()) {
        lo = range->min;
        hi = range->max;
    } else if (m_assistCap >= 0) {
        hi = m_assistCap;
    }
    m.insert(QStringLiteral("assistLow"), lo);
    m.insert(QStringLiteral("assistHigh"), hi);
    m.insert(QStringLiteral("canControl"), m_controlEnabled && linkOpen());
    return m;
}

bool AppController::fresh(int maxAgeMs) const
{
    return m_gotFrame && m_sinceFrame.isValid() && m_sinceFrame.elapsed() < maxAgeMs;
}

// ---- controls --------------------------------------------------------------------------------

bool AppController::setAssistLevel(int level)
{
    if (!m_controlEnabled) {
        emit message(tr("Bike controls are off. Turn them on in Settings."));
        return false;
    }
    int lo = 0, hi = 5;
    if (const auto &range = m_parser.assist()) {
        lo = range->min;
        hi = range->max;
    } else if (m_assistCap >= 0) {
        hi = m_assistCap;
    }
    if (level < lo || level > hi)
        return false;
    if (!sendCommand(Mahle::cmdAssistSet(level), "assist"))
        return false;
    logEvent(tr("Assist level %1 requested").arg(level));
    m_pendingAssist = level;
    m_assistCheck.start();
    return true;
}

bool AppController::changeAssistLevel(int delta)
{
    const auto &motor = m_parser.motor();
    if (!motor || !fresh(kStaleFrameMs)) {
        emit message(tr("The bike's assist level is not known yet"));
        return false;
    }
    return setAssistLevel(motor->assistLevel + delta);
}

bool AppController::resetTrip()
{
    if (!m_controlEnabled) {
        emit message(tr("Bike controls are off. Turn them on in Settings."));
        return false;
    }
    if (!sendCommand(Mahle::cmdResetTrip(), "reset-trip"))
        return false;
    logEvent(tr("Trip reset requested"));
    emit message(tr("Trip reset sent"));
    return true;
}

bool AppController::syncClock()
{
    if (!m_controlEnabled) {
        emit message(tr("Bike controls are off. Turn them on in Settings."));
        return false;
    }
    const QTime now = QTime::currentTime();
    if (!sendCommand(Mahle::cmdSetClock(now.hour(), now.minute()), "clock"))
        return false;
    logEvent(tr("Clock set to %1").arg(now.toString(QStringLiteral("HH:mm"))));
    emit message(tr("Bike clock set to %1").arg(now.toString(QStringLiteral("HH:mm"))));
    return true;
}

// ---- motor maps ------------------------------------------------------------------------------

QVariantList AppController::mapsStored(const QString &which) const
{
    return m_settings->value(QStringLiteral("maps/%1/%2").arg(m_bikeAddress.isEmpty() ? QStringLiteral("demo") : m_bikeAddress, which))
            .toList();
}

void AppController::mapsStore(const QString &which, const QVariantList &percents)
{
    saveSetting(QStringLiteral("maps/%1/%2").arg(m_bikeAddress.isEmpty() ? QStringLiteral("demo") : m_bikeAddress, which),
                percents);
}

void AppController::mapsPublish(const QString &message)
{
    QVariantMap info;
    static const char *const states[] = { "idle", "reading", "writing", "verifying" };
    info.insert(QStringLiteral("state"), QLatin1String(states[m_mapState]));
    QVariantList list;
    bool percentLayout = true;
    for (const auto &e : m_parser.maps()) {
        QVariantMap one;
        one.insert(QStringLiteral("map"), e.second.map);
        one.insert(QStringLiteral("percent"), e.second.percent);
        one.insert(QStringLiteral("isPercent"), e.second.isPercent());
        list.append(one);
        if (!e.second.isPercent())
            percentLayout = false;
    }
    info.insert(QStringLiteral("maps"), list);
    info.insert(QStringLiteral("known"), m_mapReadThisSession && !list.isEmpty());
    info.insert(QStringLiteral("percentLayout"), percentLayout && !list.isEmpty());
    QString why;
    info.insert(QStringLiteral("writable"), mapsWritable(&why));
    info.insert(QStringLiteral("whyNot"), why);
    info.insert(QStringLiteral("previous"), mapsStored(QStringLiteral("previous")));
    info.insert(QStringLiteral("original"), mapsStored(QStringLiteral("original")));
    info.insert(QStringLiteral("message"), message.isEmpty() ? m_mapsInfo.value(QStringLiteral("message")) : QVariant(message));
    m_mapsInfo = info;
    emit motorMapsChanged();
}

// Whether a write may be attempted right now, and if not, why not.
bool AppController::mapsWritable(QString *why) const
{
    auto no = [&](const QString &text) {
        if (why)
            *why = text;
        return false;
    };
    if (!m_controlEnabled)
        return no(tr("Bike controls are off. Turn them on in Settings."));
    if (!linkOpen())
        return no(tr("Not connected to the bike"));
    if (m_mapState != MapIdle)
        return no(tr("Working…"));
    const auto &maps = m_parser.maps();
    if (!m_mapReadThisSession || maps.size() != 3)
        return no(tr("Read the bike's maps first"));
    int expect = maps.begin()->first;
    for (const auto &e : maps) {
        if (!e.second.isPercent())
            return no(tr("This bike's maps are not simple percentages, so the app does not write them."));
        if (e.first != expect++)
            return no(tr("The bike's maps are not numbered one after another."));
    }
    const auto &motor = m_parser.motor();
    if (motor && fresh(3000) && motor->speedKmh >= kMapStandstillKmh)
        return no(tr("Stop the bike first"));
    return true;
}

void AppController::mapsReset()
{
    m_mapTimer.stop();
    m_mapState = MapIdle;
    m_mapQueue.clear();
    m_mapExpected.clear();
    m_mapAckPending = false;
    m_mapReadThisSession = false;
    m_parser.clearMaps();
    mapsPublish(QString());
}

void AppController::readMotorMaps()
{
    if (!linkOpen()) {
        emit message(tr("Not connected to the bike"));
        return;
    }
    if (m_mapState != MapIdle)
        return;
    m_parser.clearMaps();
    m_mapReadThisSession = false;
    m_mapFallbackTried = false;
    m_mapState = MapReading;
    logEvent(tr("Reading the motor maps"));
    mapsPublish(tr("Reading…"));
    if (!sendCommand(Mahle::cmdMapRead(0), "map-read", true)) {
        m_mapState = MapIdle;
        mapsPublish();
        return;
    }
    m_mapTimer.start(kMapReplyTimeoutMs);
}

void AppController::mapsFinishRead()
{
    m_mapTimer.stop();
    const bool verifying = m_mapState == MapVerifying;
    m_mapState = MapIdle;
    m_mapReadThisSession = !m_parser.maps().empty();
    if (!verifying) {
        logEvent(tr("The bike has %1 motor maps").arg(int(m_parser.maps().size())));
        mapsPublish(m_parser.maps().empty() ? tr("The bike did not answer. This bike may not have motor maps this app understands.")
                                            : tr("Maps read"));
        return;
    }
    // Verification: every map must hold what was written (the bike's 34 for a 35, and so on).
    QStringList bad;
    for (const auto &want : m_mapExpected) {
        const auto it = m_parser.maps().find(want.first);
        if (it == m_parser.maps().end() || it->second.percent != Mahle::mapSafePercent(want.second))
            bad.append(QString::number(want.first));
    }
    m_mapExpected.clear();
    if (bad.isEmpty()) {
        logEvent(tr("Motor maps written and checked"));
        mapsPublish(tr("Written and checked"));
        emit message(tr("Motor maps written"));
    } else {
        const QString text = tr("Read back, but map %1 does not hold what was written").arg(bad.join(QStringLiteral(", ")));
        logEvent(text);
        mapsPublish(text);
        emit message(text);
    }
}

void AppController::mapsAbort(const QString &why)
{
    m_mapTimer.stop();
    m_mapState = MapIdle;
    m_mapQueue.clear();
    m_mapAckPending = false;
    logEvent(why);
    mapsPublish(why + QLatin1Char(' ') + tr("The previous values are saved: use Restore."));
    emit message(why);
}

void AppController::mapsOnTimeout()
{
    if (m_mapState == MapReading) {
        if (m_parser.maps().size() >= 3 || m_mapFallbackTried) {
            mapsFinishRead();
            return;
        }
        // The official app asks for map 0 and gets every map. A bike that answers that with fewer is
        // asked for each one (reading is harmless).
        m_mapFallbackTried = true;
        logEvent(tr("Asking for the maps one by one"));
        for (int n = 1; n <= 3; ++n)
            QTimer::singleShot(250 * n, this, [this, n]() {
                if (m_mapState == MapReading)
                    sendCommand(Mahle::cmdMapRead(n), "map-read", true);
            });
        m_mapTimer.start(kMapReplyTimeoutMs);
    } else if (m_mapState == MapVerifying) {
        mapsFinishRead();
    } else if (m_mapState == MapWriting && m_mapAckPending) {
        if (++m_mapTries >= kMapWriteTries) {
            mapsAbort(tr("The bike did not answer map %1").arg(m_mapQueue.first().first));
            return;
        }
        logEvent(tr("No answer to map %1, trying again").arg(m_mapQueue.first().first));
        mapsWriteStep();
    }
}

bool AppController::writeMotorMaps(const QVariantList &percents)
{
    QString why;
    if (!mapsWritable(&why)) {
        emit message(why);
        return false;
    }
    if (percents.size() != 3) {
        emit message(tr("Three values are needed, one per assist level"));
        return false;
    }
    QList<int> values;
    for (const QVariant &v : percents) {
        const int p = v.toInt();
        if (p < kMapMinPercent || p > 100) {
            emit message(tr("Each value must be between %1 and 100 %").arg(kMapMinPercent));
            return false;
        }
        values.append(p);
    }
    return startMapWrite(values);
}

bool AppController::startMapWrite(const QList<int> &values)
{
    // What the bike holds now is kept before anything changes: "previous" is the state before this
    // write, "original" the very first state ever seen, never overwritten.
    QVariantList now;
    QList<int> numbers;
    for (const auto &e : m_parser.maps()) {
        now.append(e.second.percent);
        numbers.append(e.first);
    }
    mapsStore(QStringLiteral("previous"), now);
    if (mapsStored(QStringLiteral("original")).isEmpty())
        mapsStore(QStringLiteral("original"), now);

    m_mapQueue.clear();
    for (int i = 0; i < 3; ++i)
        m_mapQueue.append(qMakePair(numbers.at(i), values.at(i)));
    m_mapExpected = m_mapQueue;
    m_mapTries = 0;
    m_mapState = MapWriting;
    logEvent(tr("Writing motor maps %1 / %2 / %3 %").arg(values.at(0)).arg(values.at(1)).arg(values.at(2)));
    mapsPublish(tr("Writing…"));
    mapsWriteStep();
    return true;
}

void AppController::mapsWriteStep()
{
    if (m_mapQueue.isEmpty()) {
        // Everything is acknowledged: read the maps back to see that they hold.
        m_mapState = MapVerifying;
        m_parser.clearMaps();
        mapsPublish(tr("Checking…"));
        if (!sendCommand(Mahle::cmdMapRead(0), "map-read", true)) {
            mapsAbort(tr("Could not read the maps back"));
            return;
        }
        m_mapTimer.start(kMapReplyTimeoutMs);
        return;
    }
    const auto step = m_mapQueue.first();
    m_mapAckPending = true;
    m_mapAck.clear();
    if (!sendCommand(Mahle::cmdMapWritePercent(step.first, step.second), "map-write", true)) {
        mapsAbort(tr("Could not send map %1").arg(step.first));
        return;
    }
    m_mapTimer.start(kMapAckTimeoutMs);
}

bool AppController::restoreMotorMaps(const QString &which)
{
    const QVariantList stored = mapsStored(which);
    if (stored.size() != 3) {
        emit message(tr("There is nothing saved to restore"));
        return false;
    }
    QString why;
    if (!mapsWritable(&why)) {
        emit message(why);
        return false;
    }
    QList<int> values;
    for (const QVariant &v : stored)
        values.append(qBound(0, v.toInt(), 100));
    return startMapWrite(values);
}

// ---- recording -------------------------------------------------------------------------------

void AppController::onTick()
{
    if (linkOpen() && !m_demo) {
        if (!m_gotFrame) {
            // Notifications are on but the bike has said nothing: say so after a while, so that the
            // page does not wait for ever on "waiting for data".
            if (++m_noDataSeconds == kSilentHintAfterS) {
                logEvent(tr("Connected for %1 s without a data frame").arg(kSilentHintAfterS));
                setLink(2, tr("Connected, but the bike sends no data. Make sure no other phone is connected "
                              "to it, or switch the bike off and on."));
            }
        } else if (!fresh(kStaleFrameMs)) {
            // Frames came, then stopped: do not show frozen numbers as live.
            if (m_linkState == 3) {
                setLink(2, tr("Connected, but the bike has stopped sending data"));
                scheduleLive();
            }
        } else if (m_linkState == 2) {
            setLink(3, tr("Connected"));
        }
    }

    const auto &motor = m_parser.motor();
    const bool liveNow = fresh(3000) && motor;
    maybeAutoRecord(liveNow ? motor->speedKmh : 0.0);
    if (recording())
        sampleTick();
}

void AppController::maybeAutoRecord(double speedKmh)
{
    if (recording()) {
        // An automatically started ride ends by itself after a long stand-still.
        if (m_recordingAuto) {
            m_idleSeconds = speedKmh < 1.0 ? m_idleSeconds + 1 : 0;
            if (m_idleSeconds >= kAutoStopIdleS) {
                logEvent(tr("Ride stopped: standing still for %1 minutes").arg(kAutoStopIdleS / 60));
                stopRecording();
            }
        }
        return;
    }
    if (!m_autoRecord) {
        m_movingSeconds = 0;
        return;
    }
    m_movingSeconds = speedKmh >= kAutoStartKmh ? m_movingSeconds + 1 : 0;
    if (m_movingSeconds >= kAutoStartSeconds) {
        m_movingSeconds = 0;
        m_recordingAuto = true;
        if (!startRecording())
            m_recordingAuto = false;
    }
}

bool AppController::startRecording()
{
    if (recording())
        return true;
    if (!m_store.isOpen()) {
        emit message(tr("The ride database is not available"));
        return false;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    double odo = -1;
    if (const auto &ebm = m_parser.ebm(); ebm && ebm->lifetimeKnown)
        odo = ebm->odometerKm;
    m_recordingId = m_store.beginRide(now, m_bikeName, m_vin, odo);
    if (m_recordingId == 0) {
        emit message(tr("Could not start the ride"));
        return false;
    }
    m_pending.clear();
    m_recStartMs = now;
    m_lastSampleMs = 0;
    m_recDistanceKm = 0;
    m_recLastSpeed = 0;
    m_recFirstWh = NAN;
    m_recLastWh = NAN;
    m_recOdoStart = odo;
    m_recSampleCount = 0;
    m_idleSeconds = 0;
    m_linkLossSeconds = 0;
    m_flushCounter = 0;
    logEvent(m_recordingAuto ? tr("Ride started automatically") : tr("Ride started"));
    if (m_useGps)
        startPositioning();
    refreshRides();
    m_recordingInfo = QVariantMap();
    emit recordingChanged();
    emit recordingInfoChanged();
    return true;
}

void AppController::stopRecording()
{
    if (!recording())
        return;
    flushSamples();
    const qint64 id = m_recordingId;
    m_recordingId = 0;
    m_recordingAuto = false;
    stopPositioning();

    const std::vector<Ride::Sample> samples = m_store.samples(id);
    if (samples.size() < 10) {
        // A few seconds of standing is not a ride.
        m_store.deleteRide(id);
        logEvent(tr("Ride discarded: too short"));
        emit message(tr("Ride discarded: too short"));
    } else {
        double odoEnd = -1;
        if (const auto &ebm = m_parser.ebm(); ebm && ebm->lifetimeKnown)
            odoEnd = ebm->odometerKm;
        const Ride::Stats stats = Ride::computeStats(samples);
        m_store.finishRide(id, stats, odoEnd);
        logEvent(tr("Ride saved: %1 km in %2 s").arg(stats.distanceKm, 0, 'f', 2).arg(qint64(stats.durationS)));
        emit message(tr("Ride saved"));
    }
    m_recordingInfo = QVariantMap();
    refreshRides();
    emit recordingChanged();
    emit recordingInfoChanged();
}

Ride::Sample AppController::makeSample(qint64 nowMs) const
{
    Ride::Sample s;
    s.tMs = nowMs;
    if (const auto &motor = m_parser.motor()) {
        s.speedKmh = motor->speedKmh;
        s.assist = motor->assistLevel;
        if (motor->motorPowerW)
            s.motorW = *motor->motorPowerW;
        if (motor->riderPowerW)
            s.riderW = *motor->riderPowerW;
        if (motor->temperatureValid())
            s.motorTemp = motor->temperature;
    }
    if (const auto &battery = m_parser.battery()) {
        s.soc = battery->soc;
        s.remainingWh = battery->remainingWh;
        s.batteryTemp = battery->temperature;
    }
    if (m_haveFix && m_sinceFix.isValid() && m_sinceFix.elapsed() < kGpsMaxAgeMs) {
        const QGeoCoordinate c = m_lastFix.coordinate();
        if (c.isValid()) {
            s.lat = c.latitude();
            s.lon = c.longitude();
            if (!std::isnan(c.altitude()))
                s.alt = c.altitude();
        }
    }
    return s;
}

void AppController::sampleTick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!fresh(3000) || !m_parser.motor()) {
        // No data from the bike: leave a gap, which the statistics do not bridge.
        ++m_linkLossSeconds;
        if (m_linkLossSeconds >= kRecordLinkLossStopS) {
            logEvent(tr("Ride stopped: no data from the bike for %1 minutes").arg(kRecordLinkLossStopS / 60));
            stopRecording();
        }
        return;
    }
    m_linkLossSeconds = 0;

    const Ride::Sample s = makeSample(now);
    if (m_lastSampleMs > 0) {
        const double dt = (now - m_lastSampleMs) / 1000.0;
        if (dt > 0 && dt <= Ride::kMaxGapS)
            m_recDistanceKm += (m_recLastSpeed + s.speedKmh) / 2.0 * dt / 3600.0;
    }
    m_recLastSpeed = s.speedKmh;
    m_lastSampleMs = now;
    if (!std::isnan(s.remainingWh)) {
        if (std::isnan(m_recFirstWh))
            m_recFirstWh = s.remainingWh;
        m_recLastWh = s.remainingWh;
    }
    m_pending.push_back(s);
    ++m_recSampleCount;
    if (++m_flushCounter >= kFlushEverySeconds)
        flushSamples();

    // Satellites take a while, and a phone with location switched off never finds any: say so once.
    if (m_useGps && !m_haveFix && !m_gpsWarned && (now - m_recStartMs) > 60000) {
        m_gpsWarned = true;
        m_gpsStatus = tr("No GPS fix yet. Is Location switched on in Settings?");
        logEvent(m_gpsStatus);
        emit gpsChanged();
    }

    QVariantMap info;
    info.insert(QStringLiteral("rideId"), m_recordingId);
    info.insert(QStringLiteral("elapsedS"), (now - m_recStartMs) / 1000);
    info.insert(QStringLiteral("distanceKm"), toDisplayNumber(m_recDistanceKm));
    info.insert(QStringLiteral("auto"), m_recordingAuto);
    info.insert(QStringLiteral("gps"), s.hasFix());
    if (!std::isnan(m_recFirstWh) && !std::isnan(m_recLastWh))
        info.insert(QStringLiteral("energyWh"), std::max(0.0, m_recFirstWh - m_recLastWh));
    m_recordingInfo = info;
    emit recordingInfoChanged();
}

void AppController::flushSamples()
{
    m_flushCounter = 0;
    if (m_pending.empty() || !recording())
        return;
    m_store.appendSamples(m_recordingId, m_pending);
    m_pending.clear();
}

void AppController::startPositioning()
{
    if (!m_position) {
        m_position = QGeoPositionInfoSource::createDefaultSource(this);
        if (!m_position) {
            m_gpsStatus = tr("No positioning source on this device");
            logEvent(m_gpsStatus);
            emit gpsChanged();
            return;
        }
        m_position->setUpdateInterval(1000);
        m_position->setPreferredPositioningMethods(QGeoPositionInfoSource::SatellitePositioningMethods);
        connect(m_position, &QGeoPositionInfoSource::positionUpdated, this, &AppController::onPosition);
        connect(m_position, static_cast<void (QGeoPositionInfoSource::*)(QGeoPositionInfoSource::Error)>(
                                    &QGeoPositionInfoSource::error),
                this, [this](QGeoPositionInfoSource::Error e) {
                    m_gpsStatus = tr("Positioning error %1").arg(int(e));
                    logEvent(m_gpsStatus);
                    emit gpsChanged();
                });
    }
    m_haveFix = false;
    m_gpsStatus = tr("Waiting for a GPS fix…");
    emit gpsChanged();
    m_position->startUpdates();
}

void AppController::stopPositioning()
{
    if (m_position)
        m_position->stopUpdates();
    m_haveFix = false;
    m_gpsStatus.clear();
    emit gpsChanged();
}

void AppController::onPosition(const QGeoPositionInfo &info)
{
    if (!info.isValid())
        return;
    m_lastFix = info;
    m_sinceFix.restart();
    const bool first = !m_haveFix;
    m_haveFix = true;
    if (first) {
        m_gpsStatus = tr("GPS fix");
        emit gpsChanged();
    }
}

// ---- ride history ----------------------------------------------------------------------------

void AppController::refreshRides()
{
    m_rides.setRides(m_store.rides());
    emit ridesChanged();
}

QVariantMap AppController::rideDetail(qint64 id)
{
    QVariantMap out;
    RideStore::Summary s;
    if (!m_store.ride(id, &s))
        return out;
    // The ride being recorded has only the samples that were flushed so far.
    if (id == m_recordingId)
        flushSamples();
    const std::vector<Ride::Sample> samples = m_store.samples(id);
    const Ride::Stats stats = Ride::computeStats(samples);

    out.insert(QStringLiteral("id"), s.id);
    out.insert(QStringLiteral("startMs"), s.startMs);
    out.insert(QStringLiteral("name"), s.name);
    out.insert(QStringLiteral("deviceName"), s.deviceName);
    out.insert(QStringLiteral("recording"), s.endMs == 0);
    out.insert(QStringLiteral("distanceKm"), stats.distanceKm);
    out.insert(QStringLiteral("durationS"), stats.durationS);
    out.insert(QStringLiteral("movingS"), stats.movingS);
    out.insert(QStringLiteral("avgKmh"), stats.avgSpeedKmh);
    out.insert(QStringLiteral("maxKmh"), stats.maxSpeedKmh);
    out.insert(QStringLiteral("ascentM"), stats.ascentM);
    out.insert(QStringLiteral("hasEnergy"), stats.hasEnergy);
    out.insert(QStringLiteral("energyWh"), stats.energyWh);
    out.insert(QStringLiteral("whPerKm"), stats.whPerKm);
    out.insert(QStringLiteral("socStart"), stats.socStart);
    out.insert(QStringLiteral("socEnd"), stats.socEnd);
    out.insert(QStringLiteral("hasPower"), stats.hasPower);
    out.insert(QStringLiteral("avgMotorW"), stats.avgMotorW);
    out.insert(QStringLiteral("avgRiderW"), stats.avgRiderW);
    out.insert(QStringLiteral("maxRiderW"), stats.maxRiderW);
    out.insert(QStringLiteral("hasTrack"), stats.hasTrack);
    out.insert(QStringLiteral("odoStartKm"), s.odoStartKm);
    out.insert(QStringLiteral("odoEndKm"), s.odoEndKm);
    QVariantList assist;
    for (int i = 0; i < Ride::kMaxAssistLevels; ++i)
        assist.append(stats.assistSeconds[i]);
    out.insert(QStringLiteral("assistSeconds"), assist);

    // Chart series, averaged into at most kPoints buckets so that a long ride stays light.
    const size_t kPoints = 240;
    const size_t n = samples.size();
    const size_t step = std::max<size_t>(1, (n + kPoints - 1) / kPoints);
    QVariantList xs, speed, rider, motor, soc, alt;
    for (size_t i = 0; i < n; i += step) {
        const size_t end = std::min(n, i + step);
        double sp = 0, rw = 0, mw = 0, so = 0, al = 0;
        int cr = 0, cm = 0, cs = 0, ca = 0;
        for (size_t j = i; j < end; ++j) {
            const Ride::Sample &x = samples[j];
            sp += x.speedKmh;
            if (!std::isnan(x.riderW)) { rw += x.riderW; ++cr; }
            if (!std::isnan(x.motorW)) { mw += x.motorW; ++cm; }
            if (x.soc >= 0) { so += x.soc; ++cs; }
            if (!std::isnan(x.alt)) { al += x.alt; ++ca; }
        }
        xs.append(qint64(samples[i].tMs));
        speed.append(sp / double(end - i));
        rider.append(cr ? QVariant(rw / cr) : QVariant());
        motor.append(cm ? QVariant(mw / cm) : QVariant());
        soc.append(cs ? QVariant(so / cs) : QVariant());
        alt.append(ca ? QVariant(al / ca) : QVariant());
    }
    out.insert(QStringLiteral("xs"), xs);
    out.insert(QStringLiteral("speed"), speed);
    out.insert(QStringLiteral("riderW"), rider);
    out.insert(QStringLiteral("motorW"), motor);
    out.insert(QStringLiteral("soc"), soc);
    out.insert(QStringLiteral("alt"), alt);

    // The route as a list of [lat, lon], thinned to a few hundred points.
    QVariantList lats, lons;
    size_t fixes = 0;
    for (const Ride::Sample &x : samples)
        if (x.hasFix())
            ++fixes;
    const size_t trackStep = std::max<size_t>(1, fixes / 400);
    size_t seen = 0;
    for (const Ride::Sample &x : samples) {
        if (!x.hasFix())
            continue;
        if (seen++ % trackStep == 0) {
            lats.append(x.lat);
            lons.append(x.lon);
        }
    }
    out.insert(QStringLiteral("lats"), lats);
    out.insert(QStringLiteral("lons"), lons);
    return out;
}

bool AppController::renameRide(qint64 id, const QString &name)
{
    const bool ok = m_store.renameRide(id, name.trimmed());
    if (ok)
        refreshRides();
    return ok;
}

bool AppController::deleteRide(qint64 id)
{
    if (id == m_recordingId)
        return false;
    const bool ok = m_store.deleteRide(id);
    if (ok)
        refreshRides();
    return ok;
}

QString AppController::exportRide(qint64 id, const QString &format)
{
    RideStore::Summary s;
    if (!m_store.ride(id, &s))
        return QString();
    const std::vector<Ride::Sample> samples = m_store.samples(id);
    const QDateTime start = QDateTime::fromMSecsSinceEpoch(s.startMs);
    const QString title = s.name.isEmpty() ? tr("Ride %1").arg(start.toString(QStringLiteral("yyyy-MM-dd HH:mm")))
                                           : s.name;
    std::string body;
    QString extension;
    if (format == QLatin1String("gpx")) {
        body = Ride::toGpx(samples, title.toStdString());
        extension = QStringLiteral("gpx");
        if (body.empty()) {
            emit message(tr("This ride has no GPS track. Export it as CSV instead."));
            return QString();
        }
    } else {
        body = Ride::toCsv(samples);
        extension = QStringLiteral("csv");
    }

    const QString dir = exportDirectory();
    if (!QDir().mkpath(dir)) {
        emit message(tr("Could not create %1").arg(dir));
        return QString();
    }
    const QString path = dir + QStringLiteral("/ride-") + start.toString(QStringLiteral("yyyyMMdd-HHmm"))
                         + QLatin1Char('.') + extension;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(body.data(), qint64(body.size())) != qint64(body.size())) {
        emit message(tr("Could not write %1").arg(path));
        return QString();
    }
    logEvent(tr("Exported %1").arg(path));
    return path;
}

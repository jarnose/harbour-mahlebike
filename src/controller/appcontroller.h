#pragma once

#include "ble/bikelink.h"
#include "ble/bikescanner.h"
#include "controller/models.h"
#include "protocol/demo.h"
#include "protocol/mahle.h"
#include "ride/ride.h"
#include "store/ridestore.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QGeoPositionInfo>
#include <QObject>
#include <QSettings>
#include <QMap>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <memory>

class QGeoPositionInfoSource;

// Everything the QML side talks to: the bike link, what the bike says, the ride recorder, the ride
// history and the settings. One object, exposed as AppController.
class AppController : public QObject
{
    Q_OBJECT

    // The bike's current readings as one map; keys that the bike does not report are absent.
    Q_PROPERTY(QVariantMap live READ live NOTIFY liveChanged)
    // 0 not connected, 1 connecting, 2 connected and waiting for data, 3 live
    Q_PROPERTY(int linkState READ linkState NOTIFY linkChanged)
    Q_PROPERTY(QString status READ status NOTIFY linkChanged)

    Q_PROPERTY(bool hasBike READ hasBike NOTIFY bikeChanged)
    // A pretend bike is connected instead of a real one.
    Q_PROPERTY(bool demo READ demo NOTIFY bikeChanged)
    Q_PROPERTY(QString bikeName READ bikeName NOTIFY bikeChanged)
    Q_PROPERTY(QString bikeAddress READ bikeAddress NOTIFY bikeChanged)
    Q_PROPERTY(QString vin READ vin NOTIFY bikeChanged)
    Q_PROPERTY(QString protocolVersion READ protocolVersion NOTIFY bikeChanged)

    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(BikeListModel *bikes READ bikes CONSTANT)
    Q_PROPERTY(RideListModel *rides READ rides CONSTANT)

    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(QVariantMap recordingInfo READ recordingInfo NOTIFY recordingInfoChanged)
    Q_PROPERTY(QString gpsStatus READ gpsStatus NOTIFY gpsChanged)

    Q_PROPERTY(bool imperial READ imperial WRITE setImperial NOTIFY settingsChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY settingsChanged)
    Q_PROPERTY(bool controlEnabled READ controlEnabled WRITE setControlEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool autoRecord READ autoRecord WRITE setAutoRecord NOTIFY settingsChanged)
    Q_PROPERTY(bool useGps READ useGps WRITE setUseGps NOTIFY settingsChanged)
    Q_PROPERTY(bool keepScreenOn READ keepScreenOn WRITE setKeepScreenOn NOTIFY settingsChanged)
    Q_PROPERTY(bool traceFrames READ traceFrames WRITE setTraceFrames NOTIFY settingsChanged)

    Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
    Q_PROPERTY(QString frameLogText READ frameLogText NOTIFY frameLogChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString exportDirectory READ exportDirectory CONSTANT)
    Q_PROPERTY(QString frameTracePath READ frameTracePath CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    QVariantMap live() const { return m_live; }
    int linkState() const { return m_linkState; }
    QString status() const { return m_status; }
    bool hasBike() const { return m_demo || !m_bikeAddress.isEmpty(); }
    bool demo() const { return m_demo; }
    QString bikeName() const;
    QString bikeAddress() const { return m_demo ? QString() : m_bikeAddress; }
    QString vin() const;
    QString protocolVersion() const;
    bool scanning() const { return m_scanner.discovering(); }
    BikeListModel *bikes() { return &m_bikes; }
    RideListModel *rides() { return &m_rides; }
    bool recording() const { return m_recordingId != 0; }
    QVariantMap recordingInfo() const { return m_recordingInfo; }
    QString gpsStatus() const { return m_gpsStatus; }
    bool imperial() const { return m_imperial; }
    bool autoConnect() const { return m_autoConnect; }
    bool controlEnabled() const { return m_controlEnabled; }
    bool autoRecord() const { return m_autoRecord; }
    bool useGps() const { return m_useGps; }
    bool keepScreenOn() const { return m_keepScreenOn; }
    bool traceFrames() const { return m_traceFrames; }
    QString logText() const { return m_log.join(QLatin1Char('\n')); }
    QString frameLogText() const;
    QString version() const;
    QString exportDirectory() const;
    QString frameTracePath() const;

    void setImperial(bool on);
    void setAutoConnect(bool on);
    void setControlEnabled(bool on);
    void setAutoRecord(bool on);
    void setUseGps(bool on);
    void setKeepScreenOn(bool on);
    void setTraceFrames(bool on);

    // Bikes
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE void selectBike(const QString &address, const QString &name);
    Q_INVOKABLE void forgetBike();
    Q_INVOKABLE void connectNow();
    Q_INVOKABLE void disconnectNow();
    Q_INVOKABLE void pairBike(const QString &address);
    Q_INVOKABLE void startDemo();
    Q_INVOKABLE void stopDemo();

    // Controls. Each returns false, with a message, when it cannot be sent.
    Q_INVOKABLE bool setAssistLevel(int level);
    Q_INVOKABLE bool changeAssistLevel(int delta);
    Q_INVOKABLE bool resetTrip();
    Q_INVOKABLE bool syncClock();

    // Rides
    Q_INVOKABLE bool startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE QVariantMap rideDetail(qint64 id);
    Q_INVOKABLE bool renameRide(qint64 id, const QString &name);
    Q_INVOKABLE bool deleteRide(qint64 id);
    // format: "gpx" or "csv". Returns the file's path, or an empty string.
    Q_INVOKABLE QString exportRide(qint64 id, const QString &format);

    Q_INVOKABLE void clearLog();

signals:
    void liveChanged();
    void linkChanged();
    void bikeChanged();
    void scanningChanged();
    void recordingChanged();
    void recordingInfoChanged();
    void gpsChanged();
    void settingsChanged();
    void logChanged();
    void frameLogChanged();
    // A short message for a Notice on whatever page is showing.
    void message(const QString &text);

private slots:
    void onOpened();
    void onFailed(const QString &message);
    void onLost();
    void onFrame(const QByteArray &data);
    void onButton(const QByteArray &data);
    void onDeviceUpdated(const BikeScanner::Device &device);
    void onPosition(const QGeoPositionInfo &info);
    void onTick();
    void publishLive();
    void tryConnect();
    void demoTick();

private:
    void setLink(int state, const QString &status);
    void logEvent(const QString &text);
    void logFrame(const char *direction, const QByteArray &data, const char *kind);
    void loadSettings();
    void saveSetting(const QString &key, const QVariant &value);
    void scheduleLive();
    void scheduleRetry(const QString &because);
    void handshake();
    QVariantMap buildLive() const;
    bool sendCommand(const Mahle::Bytes &bytes, const char *what);
    void sampleTick();
    void maybeAutoRecord(double speedKmh);
    void flushSamples();
    void startPositioning();
    void stopPositioning();
    void refreshRides();
    bool fresh(int maxAgeMs) const;
    bool linkOpen() const { return m_demo || m_link.isOpen(); }
    void handleFrame(const QByteArray &data);
    Ride::Sample makeSample(qint64 nowMs) const;
    QString appDataDir() const;

    // Link
    BikeLink m_link;
    BikeScanner m_scanner;
    BikeListModel m_bikes;
    RideListModel m_rides;
    Mahle::Parser m_parser;
    QString m_bikeAddress;
    QString m_bikeName;
    QString m_vin;
    QString m_protocolVersion;
    int m_linkState = 0;
    QString m_status;
    bool m_wantConnected = false; // the user wants the bike connected: reconnect when it drops
    int m_attempt = 0;
    QTimer m_retry;
    QElapsedTimer m_sinceFrame;
    bool m_gotFrame = false;
    int m_framesSinceOpen = 0;

    // The pretend bike
    bool m_demo = false;
    Mahle::Demo::Bike m_demoBike;
    QTimer m_demoTimer;

    // Live values
    QVariantMap m_live;
    QTimer m_liveTimer;
    bool m_livePending = false;
    QTimer m_tick;
    int m_noDataSeconds = 0;

    // Controls
    QElapsedTimer m_sinceCommand;
    int m_pendingAssist = -1;
    QTimer m_assistCheck;

    // Recorder
    RideStore m_store;
    qint64 m_recordingId = 0;
    bool m_recordingAuto = false;
    std::vector<Ride::Sample> m_pending;
    qint64 m_lastSampleMs = 0;
    double m_recDistanceKm = 0;
    double m_recLastSpeed = 0;
    qint64 m_recStartMs = 0;
    double m_recFirstWh = NAN;
    double m_recLastWh = NAN;
    double m_recOdoStart = -1;
    int m_recSampleCount = 0;
    int m_idleSeconds = 0;
    int m_movingSeconds = 0;
    int m_linkLossSeconds = 0;
    QVariantMap m_recordingInfo;
    int m_flushCounter = 0;

    // Positioning
    QGeoPositionInfoSource *m_position = nullptr;
    QGeoPositionInfo m_lastFix;
    QElapsedTimer m_sinceFix;
    bool m_haveFix = false;
    QString m_gpsStatus;

    // Settings
    std::unique_ptr<QSettings> m_settings;
    bool m_imperial = false;
    bool m_autoConnect = true;
    bool m_controlEnabled = false;
    bool m_autoRecord = false;
    bool m_useGps = true;
    bool m_keepScreenOn = false;

    bool m_traceFrames = false;

    QStringList m_log;
    QStringList m_frameLog;           // everything but the steady broadcast
    QMap<QString, QString> m_latest;  // the newest broadcast frame of each kind
    int m_broadcastShown = 0;
    QString m_logPath;
};

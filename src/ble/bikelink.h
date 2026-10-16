#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

// The bike's GATT link over BlueZ's D-Bus API. Unlike a one-shot sync, a bike is watched for as
// long as the app runs, so everything here is asynchronous: open() returns at once and the outcome
// arrives as opened() or failed(); notifications arrive as frame().
//
// The link is closed on purpose in only two places, close() and the destructor, and both end with
// StopNotify. BlueZ emits one PropertiesChanged per *notifying client*, so a session that let go
// without StopNotify makes the next one see every frame twice.
class BikeLink : public QObject
{
    Q_OBJECT
public:
    enum State { Closed, Connecting, Subscribing, Open };
    Q_ENUM(State)

    explicit BikeLink(QObject *parent = nullptr);
    ~BikeLink() override;

    State state() const { return m_state; }
    bool isOpen() const { return m_state == Open; }
    QString devicePath() const { return m_devicePath; }

    // Connects, waits for the GATT services, finds the characteristics and starts notifications.
    void open(const QString &devicePath);

    // Stops notifications and optionally drops the connection. Emits nothing: the caller asked.
    void close(bool disconnectDevice);

    // Writes to the bike. False when the link is not open.
    bool write(const QByteArray &data);

    // Bonds with the device. Normal monitoring needs no bond; this is for a bike that refuses
    // notifications on an unencrypted link.
    void pair(const QString &devicePath);

signals:
    void opened();
    void failed(const QString &message);   // open() did not succeed
    void lost();                           // an open link went away on its own
    void frame(const QByteArray &data);    // FFD1: the ASCII frames
    void button(const QByteArray &data);   // FFE1: the handlebar buttons, not decoded
    void writeFailed(const QString &message);
    void pairFinished(const QString &error); // empty on success
    void stateChanged();

private slots:
    void onDevicePropertiesChanged(const QString &interface, const QVariantMap &changed,
                                   const QStringList &invalidated);
    void onFrameChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void onButtonChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void setState(State s);
    void fail(const QString &message);
    QVariant deviceProperty(const QString &name) const;
    void refreshFlags();
    void proceedWhenReady();
    QString discoverCharacteristics();
    void subscribe();
    void unhookAll();

    State m_state = Closed;
    QString m_devicePath;
    QString m_writePath;
    QString m_notifyPath;
    QString m_buttonPath;
    bool m_writeNoResponse = false;
    bool m_deviceHooked = false;
    bool m_frameHooked = false;
    bool m_buttonHooked = false;
    bool m_frameSubscribed = false;
    bool m_buttonSubscribed = false;
    bool m_connected = false;
    bool m_servicesResolved = false;
    bool m_connectCallDone = false;
    // Bumped by every open() and close(): a late D-Bus reply for an older attempt checks it.
    int m_generation = 0;
    QTimer m_timeout;
};

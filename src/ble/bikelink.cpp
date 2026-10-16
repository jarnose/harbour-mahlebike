#include "bikelink.h"

#include "bluezcall.h"
#include "bluezmanagedobjects.h"
#include "protocol/mahle.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>

namespace {

const QString kBluezService = QStringLiteral("org.bluez");
const QString kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kDeviceInterface = QStringLiteral("org.bluez.Device1");
const QString kServiceInterface = QStringLiteral("org.bluez.GattService1");
const QString kCharInterface = QStringLiteral("org.bluez.GattCharacteristic1");
const QString kObjectManagerInterface = QStringLiteral("org.freedesktop.DBus.ObjectManager");

const QString kErrInProgress = QStringLiteral("org.bluez.Error.InProgress");
const QString kErrAlreadyConnected = QStringLiteral("org.bluez.Error.AlreadyConnected");
const QString kErrAlreadyExists = QStringLiteral("org.bluez.Error.AlreadyExists");

constexpr int kOpenTimeoutMs = 60000;
constexpr int kConnectCallTimeoutMs = 45000;

} // namespace

BikeLink::BikeLink(QObject *parent)
    : QObject(parent)
{
    registerBluezManagedObjectTypes();
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(kOpenTimeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        if (m_state == Connecting || m_state == Subscribing)
            fail(tr("The bike did not answer in time. Is it switched on and nearby, and not connected to "
                    "another phone?"));
    });
}

BikeLink::~BikeLink()
{
    close(false);
}

void BikeLink::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

QVariant BikeLink::deviceProperty(const QString &name) const
{
    if (m_devicePath.isEmpty())
        return QVariant();
    QDBusInterface props(kBluezService, m_devicePath, kPropertiesInterface, QDBusConnection::systemBus());
    QDBusReply<QDBusVariant> reply = props.call(QStringLiteral("Get"), kDeviceInterface, name);
    return reply.isValid() ? reply.value().variant() : QVariant();
}

void BikeLink::refreshFlags()
{
    m_connected = deviceProperty(QStringLiteral("Connected")).toBool();
    m_servicesResolved = deviceProperty(QStringLiteral("ServicesResolved")).toBool();
}

void BikeLink::open(const QString &devicePath)
{
    // Every open is a fresh session: close() stops notifications and drops the hooks, so a
    // reconnect never inherits a doubled subscription.
    close(false);
    ++m_generation;
    m_devicePath = devicePath;
    setState(Connecting);
    m_timeout.start();

    QDBusConnection::systemBus().connect(
            kBluezService, m_devicePath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
            SLOT(onDevicePropertiesChanged(QString, QVariantMap, QStringList)));
    m_deviceHooked = true;

    refreshFlags();
    m_connectCallDone = m_connected;
    if (m_connected) {
        proceedWhenReady();
        return;
    }

    const int generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(
            BluezCall::async(m_devicePath, kDeviceInterface, QStringLiteral("Connect"), {}, kConnectCallTimeoutMs),
            this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (generation != m_generation || m_state != Connecting)
            return;
        // InProgress and AlreadyConnected mean "it is coming" and "it is there".
        if (reply.isError() && reply.error().name() != kErrInProgress
            && reply.error().name() != kErrAlreadyConnected) {
            fail(tr("Could not connect to the bike: %1").arg(reply.error().message()));
            return;
        }
        m_connectCallDone = true;
        refreshFlags();
        proceedWhenReady();
    });
}

void BikeLink::proceedWhenReady()
{
    if (m_state != Connecting || !m_connected || !m_servicesResolved)
        return;
    const QString found = discoverCharacteristics();
    if (!found.isEmpty()) {
        fail(found);
        return;
    }
    subscribe();
}

QString BikeLink::discoverCharacteristics()
{
    QDBusInterface manager(kBluezService, QStringLiteral("/"), kObjectManagerInterface,
                           QDBusConnection::systemBus());
    QDBusReply<BluezObjectMap> reply = manager.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid())
        return tr("Could not list Bluetooth services: %1").arg(reply.error().message());

    const QString devicePrefix = m_devicePath + QLatin1Char('/');
    const BluezObjectMap objects = reply.value();

    QString servicePath;
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QString path = it.key().path();
        if (!path.startsWith(devicePrefix) || !it.value().contains(kServiceInterface))
            continue;
        const QString uuid = it.value().value(kServiceInterface).value(QStringLiteral("UUID")).toString();
        if (uuid.compare(QLatin1String(Mahle::kServiceUuid), Qt::CaseInsensitive) == 0) {
            servicePath = path;
            break;
        }
    }
    if (servicePath.isEmpty())
        return tr("This device does not offer the Mahle bike service (FFE0).");

    m_writePath.clear();
    m_notifyPath.clear();
    m_buttonPath.clear();
    m_writeNoResponse = false;
    const QString servicePrefix = servicePath + QLatin1Char('/');
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QString path = it.key().path();
        if (!path.startsWith(servicePrefix) || !it.value().contains(kCharInterface))
            continue;
        const QVariantMap props = it.value().value(kCharInterface);
        const QString uuid = props.value(QStringLiteral("UUID")).toString();
        if (uuid.compare(QLatin1String(Mahle::kWriteCharUuid), Qt::CaseInsensitive) == 0) {
            m_writePath = path;
            m_writeNoResponse =
                    props.value(QStringLiteral("Flags")).toStringList().contains(QStringLiteral("write-without-response"));
        } else if (uuid.compare(QLatin1String(Mahle::kNotifyCharUuid), Qt::CaseInsensitive) == 0) {
            m_notifyPath = path;
        } else if (uuid.compare(QLatin1String(Mahle::kButtonCharUuid), Qt::CaseInsensitive) == 0) {
            m_buttonPath = path;
        }
    }
    if (m_writePath.isEmpty() || m_notifyPath.isEmpty())
        return tr("The bike service is missing its read or write characteristic.");
    return QString();
}

void BikeLink::subscribe()
{
    setState(Subscribing);

    // The hook must exist exactly once: connect() adds another instead of recognising the one
    // already there, and a doubled hook delivers every notification twice.
    QDBusConnection::systemBus().connect(
            kBluezService, m_notifyPath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
            SLOT(onFrameChanged(QString, QVariantMap, QStringList)));
    m_frameHooked = true;

    const int generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(
            BluezCall::async(m_notifyPath, kCharInterface, QStringLiteral("StartNotify")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (generation != m_generation || m_state != Subscribing)
            return;
        // "Already notifying" from an earlier session of ours is fine as well.
        if (reply.isError() && reply.error().name() != kErrInProgress) {
            if (reply.error().name() == QLatin1String("org.bluez.Error.NotPermitted")
                || reply.error().message().contains(QLatin1String("nsufficient"), Qt::CaseInsensitive))
                fail(tr("The bike refused notifications because the link is not encrypted. Pair the bike first."));
            else
                fail(tr("Could not subscribe to the bike's notifications: %1").arg(reply.error().message()));
            return;
        }
        m_frameSubscribed = true;
        m_timeout.stop();

        // The button characteristic is optional and best-effort: the official app subscribes to
        // it, and a bike that wants that has not been seen to need it for the data to flow.
        if (!m_buttonPath.isEmpty()) {
            QDBusConnection::systemBus().connect(
                    kBluezService, m_buttonPath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                    SLOT(onButtonChanged(QString, QVariantMap, QStringList)));
            m_buttonHooked = true;
            auto *bw = new QDBusPendingCallWatcher(
                    BluezCall::async(m_buttonPath, kCharInterface, QStringLiteral("StartNotify")), this);
            connect(bw, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *b) {
                const QDBusPendingReply<> r = *b;
                b->deleteLater();
                if (generation == m_generation && !r.isError())
                    m_buttonSubscribed = true;
            });
        }
        setState(Open);
        emit opened();
    });
}

void BikeLink::fail(const QString &message)
{
    // A half-made connection is dropped, so that the next attempt starts clean.
    close(m_connected);
    emit failed(message);
}

void BikeLink::unhookAll()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (m_frameHooked) {
        bus.disconnect(kBluezService, m_notifyPath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                       SLOT(onFrameChanged(QString, QVariantMap, QStringList)));
        m_frameHooked = false;
    }
    if (m_buttonHooked) {
        bus.disconnect(kBluezService, m_buttonPath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                       SLOT(onButtonChanged(QString, QVariantMap, QStringList)));
        m_buttonHooked = false;
    }
    if (m_deviceHooked) {
        bus.disconnect(kBluezService, m_devicePath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                       SLOT(onDevicePropertiesChanged(QString, QVariantMap, QStringList)));
        m_deviceHooked = false;
    }
}

void BikeLink::close(bool disconnectDevice)
{
    ++m_generation;
    m_timeout.stop();
    unhookAll();

    // Stopping notifications is not optional: dropping our signal hook only stops us listening.
    // BlueZ still counts this connection as a notifying client and emits one PropertiesChanged per
    // client. Errors are ignored: "not notifying" is what we want.
    if (m_frameSubscribed && !m_notifyPath.isEmpty())
        BluezCall::call(m_notifyPath, kCharInterface, QStringLiteral("StopNotify"), {}, 3000);
    if (m_buttonSubscribed && !m_buttonPath.isEmpty())
        BluezCall::call(m_buttonPath, kCharInterface, QStringLiteral("StopNotify"), {}, 3000);
    m_frameSubscribed = false;
    m_buttonSubscribed = false;

    if (disconnectDevice && !m_devicePath.isEmpty())
        BluezCall::async(m_devicePath, kDeviceInterface, QStringLiteral("Disconnect"));

    m_writePath.clear();
    m_notifyPath.clear();
    m_buttonPath.clear();
    m_devicePath.clear();
    m_connected = false;
    m_servicesResolved = false;
    setState(Closed);
}

bool BikeLink::write(const QByteArray &data)
{
    if (m_state != Open || m_writePath.isEmpty())
        return false;
    QVariantMap options;
    options.insert(QStringLiteral("type"),
                   m_writeNoResponse ? QStringLiteral("command") : QStringLiteral("request"));
    const int generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(
            BluezCall::async(m_writePath, kCharInterface, QStringLiteral("WriteValue"), { data, options }), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (generation == m_generation && reply.isError())
            emit writeFailed(reply.error().message());
    });
    return true;
}

void BikeLink::pair(const QString &devicePath)
{
    // Trusted first: BlueZ then reconnects the device on its own.
    QDBusInterface props(kBluezService, devicePath, kPropertiesInterface, QDBusConnection::systemBus());
    props.call(QStringLiteral("Set"), kDeviceInterface, QStringLiteral("Trusted"),
               QVariant::fromValue(QDBusVariant(true)));

    auto *watcher = new QDBusPendingCallWatcher(
            BluezCall::async(devicePath, kDeviceInterface, QStringLiteral("Pair"), {}, 60000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (reply.isError() && reply.error().name() != kErrAlreadyExists)
            emit pairFinished(tr("Pairing failed: %1\n\nPair the bike in Settings > Bluetooth instead, then "
                                 "come back here.").arg(reply.error().message()));
        else
            emit pairFinished(QString());
    });
}

void BikeLink::onDevicePropertiesChanged(const QString &interface, const QVariantMap &changed,
                                         const QStringList &invalidated)
{
    Q_UNUSED(invalidated);
    if (interface != kDeviceInterface)
        return;
    if (changed.contains(QStringLiteral("Connected")))
        m_connected = changed.value(QStringLiteral("Connected")).toBool();
    if (changed.contains(QStringLiteral("ServicesResolved")))
        m_servicesResolved = changed.value(QStringLiteral("ServicesResolved")).toBool();

    if (m_state == Open && !m_connected) {
        close(false);
        emit lost();
        return;
    }
    if ((m_state == Connecting || m_state == Subscribing) && changed.contains(QStringLiteral("Connected"))
        && !m_connected && m_connectCallDone) {
        fail(tr("The bike dropped the connection while it was being set up."));
        return;
    }
    if (m_state == Connecting)
        proceedWhenReady();
}

void BikeLink::onFrameChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    Q_UNUSED(invalidated);
    if (interface != kCharInterface || !changed.contains(QStringLiteral("Value")))
        return;
    emit frame(changed.value(QStringLiteral("Value")).toByteArray());
}

void BikeLink::onButtonChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    Q_UNUSED(invalidated);
    if (interface != kCharInterface || !changed.contains(QStringLiteral("Value")))
        return;
    emit button(changed.value(QStringLiteral("Value")).toByteArray());
}

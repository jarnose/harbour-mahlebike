#include "bikescanner.h"

#include "bluezcall.h"
#include "protocol/mahle.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>

namespace {

const QString kBluezService = QStringLiteral("org.bluez");
const QString kObjectManagerInterface = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString kAdapterInterface = QStringLiteral("org.bluez.Adapter1");
const QString kDeviceInterface = QStringLiteral("org.bluez.Device1");

} // namespace

BikeScanner::BikeScanner(QObject *parent)
    : QObject(parent)
{
    registerBluezManagedObjectTypes();
    qRegisterMetaType<BikeScanner::Device>();

    QDBusConnection::systemBus().connect(
            kBluezService, QString(), kObjectManagerInterface, QStringLiteral("InterfacesAdded"), this,
            SLOT(onInterfacesAdded(QDBusObjectPath, BluezInterfaceMap)));

    m_poll.setInterval(2000);
    connect(&m_poll, &QTimer::timeout, this, &BikeScanner::refresh);
}

bool BikeScanner::looksLikeBike(const QVariantMap &props)
{
    const QStringList uuids = props.value(QStringLiteral("UUIDs")).toStringList();
    for (const QString &uuid : uuids)
        if (uuid.compare(QLatin1String(Mahle::kServiceUuid), Qt::CaseInsensitive) == 0)
            return true;

    const QString address = props.value(QStringLiteral("Address")).toString();
    for (const char *prefix : Mahle::kMacPrefixes)
        if (address.startsWith(QLatin1String(prefix), Qt::CaseInsensitive))
            return true;

    const QString name = props.value(QStringLiteral("Alias"), props.value(QStringLiteral("Name"))).toString();
    return name.startsWith(QLatin1String("iWoc"), Qt::CaseInsensitive)
           || name.startsWith(QLatin1String("HUS"), Qt::CaseInsensitive);
}

QString BikeScanner::adapterPath()
{
    if (!m_adapterPath.isEmpty())
        return m_adapterPath;

    QDBusInterface manager(kBluezService, QStringLiteral("/"), kObjectManagerInterface,
                           QDBusConnection::systemBus());
    QDBusReply<BluezObjectMap> reply = manager.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid()) {
        emit errorOccurred(tr("Could not reach Bluetooth (org.bluez): %1").arg(reply.error().message()));
        return QString();
    }
    const BluezObjectMap objects = reply.value();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        if (it.value().contains(kAdapterInterface)) {
            m_adapterPath = it.key().path();
            break;
        }
    }
    if (m_adapterPath.isEmpty())
        emit errorOccurred(tr("No Bluetooth adapter found"));
    return m_adapterPath;
}

void BikeScanner::emitIfBike(const QString &objectPath, const BluezInterfaceMap &interfaces)
{
    if (!interfaces.contains(kDeviceInterface))
        return;
    const QVariantMap props = interfaces.value(kDeviceInterface);
    if (!looksLikeBike(props))
        return;

    Device d;
    d.objectPath = objectPath;
    d.address = props.value(QStringLiteral("Address")).toString();
    d.name = props.value(QStringLiteral("Alias"), props.value(QStringLiteral("Name"), d.address)).toString();
    d.paired = props.value(QStringLiteral("Paired")).toBool();
    d.connected = props.value(QStringLiteral("Connected")).toBool();
    d.trusted = props.value(QStringLiteral("Trusted")).toBool();
    d.rssi = props.value(QStringLiteral("RSSI")).toInt();
    emit deviceUpdated(d);
}

void BikeScanner::refresh()
{
    QDBusInterface manager(kBluezService, QStringLiteral("/"), kObjectManagerInterface,
                           QDBusConnection::systemBus());
    QDBusReply<BluezObjectMap> reply = manager.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid()) {
        emit errorOccurred(tr("Could not list Bluetooth devices: %1").arg(reply.error().message()));
        return;
    }
    const BluezObjectMap objects = reply.value();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it)
        emitIfBike(it.key().path(), it.value());
}

void BikeScanner::onInterfacesAdded(const QDBusObjectPath &path, const BluezInterfaceMap &interfaces)
{
    emitIfBike(path.path(), interfaces);
}

void BikeScanner::startDiscovery()
{
    const QString path = adapterPath();
    if (path.isEmpty())
        return;
    m_discovering = true;
    m_poll.start();

    auto *watcher = new QDBusPendingCallWatcher(
            BluezCall::async(path, kAdapterInterface, QStringLiteral("StartDiscovery")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        // InProgress means someone else (Settings) is already scanning, which serves us just as well.
        if (reply.isError() && reply.error().name() != QLatin1String("org.bluez.Error.InProgress"))
            emit errorOccurred(tr("Could not start scanning: %1").arg(reply.error().message()));
        w->deleteLater();
    });
}

void BikeScanner::stopDiscovery()
{
    m_poll.stop();
    if (!m_discovering)
        return;
    m_discovering = false;
    const QString path = adapterPath();
    if (path.isEmpty())
        return;
    BluezCall::async(path, kAdapterInterface, QStringLiteral("StopDiscovery"));
}

QString BikeScanner::devicePathForAddress(const QString &address)
{
    QDBusInterface manager(kBluezService, QStringLiteral("/"), kObjectManagerInterface,
                           QDBusConnection::systemBus());
    QDBusReply<BluezObjectMap> reply = manager.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid())
        return QString();
    const BluezObjectMap objects = reply.value();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QVariantMap props = it.value().value(kDeviceInterface);
        if (!props.isEmpty()
            && props.value(QStringLiteral("Address")).toString().compare(address, Qt::CaseInsensitive) == 0)
            return it.key().path();
    }
    return QString();
}

bool BikeScanner::removeDevice(const QString &objectPath, QString *error)
{
    const QString path = adapterPath();
    if (path.isEmpty()) {
        if (error)
            *error = tr("No Bluetooth adapter found");
        return false;
    }
    QDBusReply<void> reply(BluezCall::call(path, kAdapterInterface, QStringLiteral("RemoveDevice"),
                                           { QVariant::fromValue(QDBusObjectPath(objectPath)) }));
    if (!reply.isValid()) {
        if (error)
            *error = reply.error().message();
        return false;
    }
    return true;
}

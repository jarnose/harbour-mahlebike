#pragma once

#include "bluezmanagedobjects.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

// Finds Mahle bikes through BlueZ's own D-Bus API (org.bluez.*), the way harbour-ourasync and
// harbour-suuntosync reach their devices: Sailfish OS 5.1's SDK target has no Qt Bluetooth.
//
// A bike is recognised the way the official app's own scan filter does it: the FFE0 service in its
// advertisement, or a MAC address from one of Mahle's two vendor ranges. The advertised names
// ("iWoc..." on X25/X35+, "HUS..." on X20) are accepted too, because a bonded device often
// advertises no service list at all.
class BikeScanner : public QObject
{
    Q_OBJECT
public:
    struct Device
    {
        QString objectPath;
        QString address;
        QString name;
        bool paired = false;
        bool connected = false;
        bool trusted = false;
        int rssi = 0; // dBm, 0 when BlueZ has no reading
    };

    explicit BikeScanner(QObject *parent = nullptr);

    // Re-reads every device BlueZ knows and emits deviceUpdated() for the bikes.
    void refresh();

    // Scanning also polls refresh() every couple of seconds: RSSI and the connected flag change
    // without a new object appearing.
    void startDiscovery();
    void stopDiscovery();
    bool discovering() const { return m_discovering; }

    // Object path of the BlueZ device with this MAC address, or an empty string.
    QString devicePathForAddress(const QString &address);

    // Makes BlueZ forget the device and its bond.
    bool removeDevice(const QString &objectPath, QString *error = nullptr);

    static bool looksLikeBike(const QVariantMap &deviceProperties);

signals:
    void deviceUpdated(const BikeScanner::Device &device);
    void errorOccurred(const QString &message);

private slots:
    void onInterfacesAdded(const QDBusObjectPath &path, const BluezInterfaceMap &interfaces);

private:
    QString adapterPath();
    void emitIfBike(const QString &objectPath, const BluezInterfaceMap &interfaces);

    QString m_adapterPath;
    QTimer m_poll;
    bool m_discovering = false;
};

Q_DECLARE_METATYPE(BikeScanner::Device)

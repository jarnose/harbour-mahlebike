#pragma once

#include "ble/bikescanner.h"
#include "store/ridestore.h"

#include <QAbstractListModel>
#include <QList>

// Bikes found while scanning (and the remembered one), for the "Choose a bike" page.
class BikeListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { NameRole = Qt::UserRole + 1, AddressRole, RssiRole, PairedRole, ConnectedRole, SelectedRole };

    explicit BikeListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void upsert(const BikeScanner::Device &device);
    void setSelectedAddress(const QString &address);
    void clear();
    int count() const { return m_items.size(); }

private:
    QList<BikeScanner::Device> m_items;
    QString m_selected;
};

// The ride history.
class RideListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        StartRole,
        NameRole,
        DistanceRole,
        DurationRole,
        AvgSpeedRole,
        EnergyRole,
        HasEnergyRole,
        HasTrackRole,
        SectionRole,
        RecordingRole
    };

    explicit RideListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRides(const QList<RideStore::Summary> &rides);

private:
    QList<RideStore::Summary> m_items;
};

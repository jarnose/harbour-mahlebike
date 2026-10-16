#include "models.h"

#include <QDateTime>

int BikeListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant BikeListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return QVariant();
    const BikeScanner::Device &d = m_items.at(index.row());
    switch (role) {
    case NameRole: return d.name;
    case AddressRole: return d.address;
    case RssiRole: return d.rssi;
    case PairedRole: return d.paired;
    case ConnectedRole: return d.connected;
    case SelectedRole: return !m_selected.isEmpty() && d.address.compare(m_selected, Qt::CaseInsensitive) == 0;
    }
    return QVariant();
}

QHash<int, QByteArray> BikeListModel::roleNames() const
{
    return { { NameRole, "name" },     { AddressRole, "address" },     { RssiRole, "rssi" },
             { PairedRole, "paired" }, { ConnectedRole, "connected" }, { SelectedRole, "selected" } };
}

void BikeListModel::upsert(const BikeScanner::Device &device)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).address.compare(device.address, Qt::CaseInsensitive) != 0)
            continue;
        const BikeScanner::Device old = m_items.at(i);
        // A scan refresh repeats the same values every two seconds: only a real change touches the view.
        if (old.name == device.name && old.paired == device.paired && old.connected == device.connected
            && old.rssi == device.rssi && old.objectPath == device.objectPath)
            return;
        m_items[i] = device;
        emit dataChanged(index(i), index(i));
        return;
    }
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    m_items.append(device);
    endInsertRows();
}

void BikeListModel::setSelectedAddress(const QString &address)
{
    m_selected = address;
    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(m_items.size() - 1), { SelectedRole });
}

void BikeListModel::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
}

int RideListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant RideListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return QVariant();
    const RideStore::Summary &r = m_items.at(index.row());
    switch (role) {
    case IdRole: return r.id;
    case StartRole: return r.startMs;
    case NameRole: return r.name;
    case DistanceRole: return r.distanceKm;
    case DurationRole: return r.durationS;
    case AvgSpeedRole: return r.avgKmh;
    case EnergyRole: return r.energyWh;
    case HasEnergyRole: return r.hasEnergy;
    case HasTrackRole: return r.hasTrack;
    case SectionRole: return QDateTime::fromMSecsSinceEpoch(r.startMs).toString(QStringLiteral("yyyy-MM"));
    case RecordingRole: return r.endMs == 0;
    }
    return QVariant();
}

QHash<int, QByteArray> RideListModel::roleNames() const
{
    return { { IdRole, "rideId" },          { StartRole, "startMs" },     { NameRole, "name" },
             { DistanceRole, "distanceKm" }, { DurationRole, "durationS" }, { AvgSpeedRole, "avgKmh" },
             { EnergyRole, "energyWh" },     { HasEnergyRole, "hasEnergy" }, { HasTrackRole, "hasTrack" },
             { SectionRole, "section" },     { RecordingRole, "recording" } };
}

void RideListModel::setRides(const QList<RideStore::Summary> &rides)
{
    beginResetModel();
    m_items = rides;
    endResetModel();
}

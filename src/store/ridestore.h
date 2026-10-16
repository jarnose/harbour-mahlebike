#pragma once

#include "ride/ride.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>

#include <vector>

// The rides and their samples, in a SQLite file under the app's data directory (the one place
// Sailjail keeps for the app).
class RideStore
{
public:
    struct Summary
    {
        qint64 id = 0;
        qint64 startMs = 0;
        qint64 endMs = 0; // 0 while the ride is still being recorded
        QString name;
        QString deviceName;
        double distanceKm = 0;
        double durationS = 0;
        double movingS = 0;
        double avgKmh = 0;
        double maxKmh = 0;
        double ascentM = 0;
        double energyWh = 0;
        double whPerKm = 0;
        int socStart = -1;
        int socEnd = -1;
        double avgMotorW = 0;
        double avgRiderW = 0;
        double maxRiderW = 0;
        bool hasEnergy = false;
        bool hasPower = false;
        bool hasTrack = false;
        double odoStartKm = -1; // the bike's lifetime odometer, -1 when unknown
        double odoEndKm = -1;
    };

    RideStore();
    ~RideStore();

    bool open(const QString &path, QString *error = nullptr);
    bool isOpen() const { return m_open; }

    // Rows left open by a crash or a power loss are closed from their samples.
    void recoverUnfinished();

    qint64 beginRide(qint64 startMs, const QString &deviceName, const QString &vin, double odoStartKm);
    bool appendSamples(qint64 rideId, const std::vector<Ride::Sample> &samples);
    bool finishRide(qint64 rideId, const Ride::Stats &stats, double odoEndKm);

    QList<Summary> rides() const;
    bool ride(qint64 id, Summary *out) const;
    std::vector<Ride::Sample> samples(qint64 id) const;
    bool renameRide(qint64 id, const QString &name);
    bool deleteRide(qint64 id);
    // Count, distance (km), time (s) and battery (Wh) over all finished rides.
    QVariantMap totals() const;
    // Samples written so far, for a ride that is being recorded.
    int sampleCount(qint64 id) const;

private:
    static Summary fromQuery(class QSqlQuery &q);

    QSqlDatabase m_db;
    QString m_connection;
    bool m_open = false;
};

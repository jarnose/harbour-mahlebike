#include "ridestore.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <cmath>

namespace {

QVariant dbl(double v)
{
    return std::isnan(v) ? QVariant(QVariant::Double) : QVariant(v);
}

double fromDbl(const QVariant &v)
{
    return v.isNull() ? NAN : v.toDouble();
}

} // namespace

RideStore::RideStore() = default;

RideStore::~RideStore()
{
    if (m_open) {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connection);
    }
}

bool RideStore::open(const QString &path, QString *error)
{
    m_connection = QStringLiteral("rides-") + QUuid::createUuid().toString();
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    const bool ok =
            q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS rides ("
                                  "id INTEGER PRIMARY KEY AUTOINCREMENT, start_ms INTEGER NOT NULL, end_ms INTEGER NOT NULL DEFAULT 0,"
                                  "name TEXT NOT NULL DEFAULT '', device_name TEXT NOT NULL DEFAULT '', vin TEXT NOT NULL DEFAULT '',"
                                  "distance_km REAL NOT NULL DEFAULT 0, duration_s REAL NOT NULL DEFAULT 0, moving_s REAL NOT NULL DEFAULT 0,"
                                  "avg_kmh REAL NOT NULL DEFAULT 0, max_kmh REAL NOT NULL DEFAULT 0, ascent_m REAL NOT NULL DEFAULT 0,"
                                  "energy_wh REAL NOT NULL DEFAULT 0, wh_per_km REAL NOT NULL DEFAULT 0,"
                                  "soc_start INTEGER NOT NULL DEFAULT -1, soc_end INTEGER NOT NULL DEFAULT -1,"
                                  "avg_motor_w REAL NOT NULL DEFAULT 0, avg_rider_w REAL NOT NULL DEFAULT 0, max_rider_w REAL NOT NULL DEFAULT 0,"
                                  "has_energy INTEGER NOT NULL DEFAULT 0, has_power INTEGER NOT NULL DEFAULT 0, has_track INTEGER NOT NULL DEFAULT 0,"
                                  "odo_start_km REAL NOT NULL DEFAULT -1, odo_end_km REAL NOT NULL DEFAULT -1)"))
            && q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS samples ("
                                     "ride_id INTEGER NOT NULL, t_ms INTEGER NOT NULL, speed REAL NOT NULL, assist INTEGER NOT NULL,"
                                     "soc INTEGER NOT NULL, rem_wh REAL, motor_w REAL, rider_w REAL,"
                                     "motor_temp INTEGER NOT NULL, batt_temp INTEGER NOT NULL, lat REAL, lon REAL, alt REAL)"))
            && q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS samples_by_ride ON samples (ride_id, t_ms)"));
    if (!ok) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    m_open = true;
    return true;
}

qint64 RideStore::beginRide(qint64 startMs, const QString &deviceName, const QString &vin, double odoStartKm)
{
    if (!m_open)
        return 0;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO rides (start_ms, device_name, vin, odo_start_km) VALUES (?, ?, ?, ?)"));
    q.addBindValue(startMs);
    q.addBindValue(deviceName);
    q.addBindValue(vin);
    q.addBindValue(odoStartKm);
    if (!q.exec()) {
        qWarning() << "beginRide:" << q.lastError().text();
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool RideStore::appendSamples(qint64 rideId, const std::vector<Ride::Sample> &samples)
{
    if (!m_open || samples.empty())
        return m_open;
    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO samples (ride_id, t_ms, speed, assist, soc, rem_wh, motor_w, rider_w,"
                             "motor_temp, batt_temp, lat, lon, alt) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    for (const Ride::Sample &s : samples) {
        q.addBindValue(rideId);
        q.addBindValue(static_cast<qint64>(s.tMs));
        q.addBindValue(s.speedKmh);
        q.addBindValue(s.assist);
        q.addBindValue(s.soc);
        q.addBindValue(dbl(s.remainingWh));
        q.addBindValue(dbl(s.motorW));
        q.addBindValue(dbl(s.riderW));
        q.addBindValue(s.motorTemp);
        q.addBindValue(s.batteryTemp);
        q.addBindValue(dbl(s.lat));
        q.addBindValue(dbl(s.lon));
        q.addBindValue(dbl(s.alt));
        if (!q.exec()) {
            qWarning() << "appendSamples:" << q.lastError().text();
            m_db.rollback();
            return false;
        }
    }
    return m_db.commit();
}

bool RideStore::finishRide(qint64 rideId, const Ride::Stats &s, double odoEndKm)
{
    if (!m_open)
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
            "UPDATE rides SET end_ms=?, distance_km=?, duration_s=?, moving_s=?, avg_kmh=?, max_kmh=?, ascent_m=?,"
            "energy_wh=?, wh_per_km=?, soc_start=?, soc_end=?, avg_motor_w=?, avg_rider_w=?, max_rider_w=?,"
            "has_energy=?, has_power=?, has_track=?, odo_end_km=? WHERE id=?"));
    q.addBindValue(static_cast<qint64>(s.endMs > 0 ? s.endMs : 1));
    q.addBindValue(s.distanceKm);
    q.addBindValue(s.durationS);
    q.addBindValue(s.movingS);
    q.addBindValue(s.avgSpeedKmh);
    q.addBindValue(s.maxSpeedKmh);
    q.addBindValue(s.ascentM);
    q.addBindValue(s.energyWh);
    q.addBindValue(s.whPerKm);
    q.addBindValue(s.socStart);
    q.addBindValue(s.socEnd);
    q.addBindValue(s.avgMotorW);
    q.addBindValue(s.avgRiderW);
    q.addBindValue(s.maxRiderW);
    q.addBindValue(s.hasEnergy ? 1 : 0);
    q.addBindValue(s.hasPower ? 1 : 0);
    q.addBindValue(s.hasTrack ? 1 : 0);
    q.addBindValue(odoEndKm);
    q.addBindValue(rideId);
    if (!q.exec()) {
        qWarning() << "finishRide:" << q.lastError().text();
        return false;
    }
    return true;
}

RideStore::Summary RideStore::fromQuery(QSqlQuery &q)
{
    Summary s;
    int i = 0;
    s.id = q.value(i++).toLongLong();
    s.startMs = q.value(i++).toLongLong();
    s.endMs = q.value(i++).toLongLong();
    s.name = q.value(i++).toString();
    s.deviceName = q.value(i++).toString();
    s.distanceKm = q.value(i++).toDouble();
    s.durationS = q.value(i++).toDouble();
    s.movingS = q.value(i++).toDouble();
    s.avgKmh = q.value(i++).toDouble();
    s.maxKmh = q.value(i++).toDouble();
    s.ascentM = q.value(i++).toDouble();
    s.energyWh = q.value(i++).toDouble();
    s.whPerKm = q.value(i++).toDouble();
    s.socStart = q.value(i++).toInt();
    s.socEnd = q.value(i++).toInt();
    s.avgMotorW = q.value(i++).toDouble();
    s.avgRiderW = q.value(i++).toDouble();
    s.maxRiderW = q.value(i++).toDouble();
    s.hasEnergy = q.value(i++).toInt() != 0;
    s.hasPower = q.value(i++).toInt() != 0;
    s.hasTrack = q.value(i++).toInt() != 0;
    s.odoStartKm = q.value(i++).toDouble();
    s.odoEndKm = q.value(i++).toDouble();
    s.vin = q.value(i++).toString();
    return s;
}

namespace {
const char *kSummaryColumns =
        "id, start_ms, end_ms, name, device_name, distance_km, duration_s, moving_s, avg_kmh, max_kmh, ascent_m,"
        "energy_wh, wh_per_km, soc_start, soc_end, avg_motor_w, avg_rider_w, max_rider_w, has_energy, has_power,"
        "has_track, odo_start_km, odo_end_km, vin";
}

QList<RideStore::Summary> RideStore::rides() const
{
    QList<Summary> out;
    if (!m_open)
        return out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT %1 FROM rides ORDER BY start_ms DESC").arg(QLatin1String(kSummaryColumns))))
        return out;
    while (q.next())
        out.append(fromQuery(q));
    return out;
}

bool RideStore::ride(qint64 id, Summary *out) const
{
    if (!m_open)
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM rides WHERE id=?").arg(QLatin1String(kSummaryColumns)));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return false;
    *out = fromQuery(q);
    return true;
}

std::vector<Ride::Sample> RideStore::samples(qint64 id) const
{
    std::vector<Ride::Sample> out;
    if (!m_open)
        return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT t_ms, speed, assist, soc, rem_wh, motor_w, rider_w, motor_temp, batt_temp, lat, lon, alt "
                             "FROM samples WHERE ride_id=? ORDER BY t_ms"));
    q.addBindValue(id);
    if (!q.exec())
        return out;
    while (q.next()) {
        Ride::Sample s;
        s.tMs = q.value(0).toLongLong();
        s.speedKmh = q.value(1).toDouble();
        s.assist = q.value(2).toInt();
        s.soc = q.value(3).toInt();
        s.remainingWh = fromDbl(q.value(4));
        s.motorW = fromDbl(q.value(5));
        s.riderW = fromDbl(q.value(6));
        s.motorTemp = q.value(7).toInt();
        s.batteryTemp = q.value(8).toInt();
        s.lat = fromDbl(q.value(9));
        s.lon = fromDbl(q.value(10));
        s.alt = fromDbl(q.value(11));
        out.push_back(s);
    }
    return out;
}

QVariantMap RideStore::totals() const
{
    QVariantMap out;
    out.insert(QStringLiteral("count"), 0);
    out.insert(QStringLiteral("distanceKm"), 0.0);
    out.insert(QStringLiteral("durationS"), 0.0);
    out.insert(QStringLiteral("energyWh"), 0.0);
    if (!m_open)
        return out;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT COUNT(*), COALESCE(SUM(distance_km),0), COALESCE(SUM(duration_s),0),"
                              "COALESCE(SUM(energy_wh),0) FROM rides WHERE end_ms > 0"))
        && q.next()) {
        out.insert(QStringLiteral("count"), q.value(0).toInt());
        out.insert(QStringLiteral("distanceKm"), q.value(1).toDouble());
        out.insert(QStringLiteral("durationS"), q.value(2).toDouble());
        out.insert(QStringLiteral("energyWh"), q.value(3).toDouble());
    }
    return out;
}

int RideStore::sampleCount(qint64 id) const
{
    if (!m_open)
        return 0;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM samples WHERE ride_id=?"));
    q.addBindValue(id);
    return (q.exec() && q.next()) ? q.value(0).toInt() : 0;
}

bool RideStore::renameRide(qint64 id, const QString &name)
{
    if (!m_open)
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE rides SET name=? WHERE id=?"));
    q.addBindValue(name);
    q.addBindValue(id);
    return q.exec();
}

bool RideStore::deleteRide(qint64 id)
{
    if (!m_open)
        return false;
    m_db.transaction();
    QSqlQuery a(m_db), b(m_db);
    a.prepare(QStringLiteral("DELETE FROM samples WHERE ride_id=?"));
    a.addBindValue(id);
    b.prepare(QStringLiteral("DELETE FROM rides WHERE id=?"));
    b.addBindValue(id);
    if (!a.exec() || !b.exec()) {
        m_db.rollback();
        return false;
    }
    return m_db.commit();
}

void RideStore::recoverUnfinished()
{
    if (!m_open)
        return;
    QList<qint64> open;
    {
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral("SELECT id FROM rides WHERE end_ms=0")))
            return;
        while (q.next())
            open.append(q.value(0).toLongLong());
    }
    for (qint64 id : open) {
        const std::vector<Ride::Sample> s = samples(id);
        if (s.size() < 2) { // nothing worth keeping
            deleteRide(id);
            continue;
        }
        finishRide(id, Ride::computeStats(s), -1);
    }
}

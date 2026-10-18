// Ride statistics, GPX and CSV.
#include "hex.h"
#include "ride/ride.h"

#include <cmath>

using namespace Ride;

static bool near(double a, double b, double eps) { return std::fabs(a - b) < eps; }

static Sample at(int64_t seconds, double kmh, int assist = 1)
{
    Sample s;
    s.tMs = 1760600000000LL + seconds * 1000;
    s.speedKmh = kmh;
    s.assist = assist;
    return s;
}

static void iso()
{
    CHECK_EQ(isoUtc(0), std::string("1970-01-01T00:00:00.000Z"));
    CHECK_EQ(isoUtc(1760600000000LL), std::string("2025-10-16T07:33:20.000Z"));
    CHECK_EQ(isoUtc(951782400123LL), std::string("2000-02-29T00:00:00.123Z"));
    CHECK_EQ(isoUtc(-1000), std::string("1969-12-31T23:59:59.000Z"));
}

static void distanceAndSpeed()
{
    // One hour at a steady 18 km/h is 18 km; a stop in the middle counts as time, not as moving time.
    std::vector<Sample> v;
    for (int t = 0; t <= 600; ++t)
        v.push_back(at(t, 18.0));
    for (int t = 601; t <= 900; ++t)
        v.push_back(at(t, 0.0));
    Stats s = computeStats(v);
    CHECK(near(s.durationS, 900, 0.01));
    CHECK(near(s.distanceKm, 18.0 * 600 / 3600, 0.01)); // 3 km
    CHECK(near(s.movingS, 600, 1.5));
    CHECK(near(s.avgSpeedKmh, 18.0, 0.3));
    CHECK(near(s.maxSpeedKmh, 18.0, 0.001));

    // A gap longer than five seconds is not integrated over.
    std::vector<Sample> g = { at(0, 36), at(1, 36), at(60, 36), at(61, 36) };
    CHECK(near(computeStats(g).distanceKm, 36.0 * 2 / 3600, 0.0005));

    CHECK(near(computeStats({}).distanceKm, 0, 1e-9));
}

static void assistTime()
{
    std::vector<Sample> v;
    for (int t = 0; t <= 60; ++t)
        v.push_back(at(t, 20, t < 40 ? 2 : 4));
    Stats s = computeStats(v);
    CHECK(near(s.assistSeconds[2], 40, 1.01));
    CHECK(near(s.assistSeconds[4], 20, 1.01));
    CHECK(near(s.assistSeconds[0], 0, 0.001));
}

static void energy()
{
    std::vector<Sample> v;
    for (int t = 0; t <= 600; ++t) {
        Sample x = at(t, 24);
        x.remainingWh = 300 - 0.05 * t; // 30 Wh over 4 km
        x.soc = 80 - t / 100;
        v.push_back(x);
    }
    Stats s = computeStats(v);
    CHECK(s.hasEnergy);
    CHECK(near(s.energyWh, 30.0, 0.01));
    CHECK(near(s.whPerKm, 30.0 / 4.0, 0.05));
    CHECK_EQ(s.socStart, 80);
    CHECK_EQ(s.socEnd, 74);

    // Charging during a ride is not negative consumption.
    std::vector<Sample> c = { at(0, 10), at(1, 10) };
    c[0].remainingWh = 100;
    c[1].remainingWh = 120;
    CHECK(near(computeStats(c).energyWh, 0, 1e-9));
    CHECK(!computeStats({ at(0, 1) }).hasEnergy);
}

static void power()
{
    std::vector<Sample> v;
    for (int t = 0; t <= 100; ++t) {
        Sample x = at(t, 20);
        x.motorW = 200;
        x.riderW = t < 50 ? 100 : 150;
        v.push_back(x);
    }
    Stats s = computeStats(v);
    CHECK(s.hasPower);
    CHECK(near(s.avgMotorW, 200, 0.5));
    CHECK(near(s.avgRiderW, 125, 3));
    CHECK(near(s.maxRiderW, 150, 0.001));
    CHECK(!computeStats({ at(0, 20), at(1, 20) }).hasPower);
}

static void ascent()
{
    // Climb 50 m, descend 20, climb 10: 60 m up. GPS noise of a metre must not add anything.
    std::vector<Sample> v;
    double h = 100;
    for (int t = 0; t < 200; ++t) {
        if (t < 50) h += 1.0;
        else if (t < 70) h -= 1.0;
        else if (t < 80) h += 1.0;
        Sample x = at(t, 20);
        x.alt = h + ((t % 2) ? 0.4 : -0.4);
        v.push_back(x);
    }
    CHECK(near(computeStats(v).ascentM, 60.0, 8.0));
    // Flat but noisy.
    std::vector<Sample> f;
    for (int t = 0; t < 100; ++t) {
        Sample x = at(t, 20);
        x.alt = 100 + ((t % 2) ? 1.2 : -1.2);
        f.push_back(x);
    }
    CHECK(near(computeStats(f).ascentM, 0, 0.001));
}

static void altitudeCleaning()
{
    // The start of a real ride: a stale 128 m, then a GPS settling down while the rider still stood
    // (the altitude fell to 40 m and came back to about 100 m), then the true level of about 110 m.
    const double start[] = { 128.3, 128.3, 128.3, 128.3, 128.3, 62.0, 52.9, 39.5, 45.5, 62.4, 59.4, 99.9, 96.1, 93.0,
                             97.6, 100.2, 104.0, 108.0, 110.5, 111.0, 110.2, 109.8 };
    std::vector<Sample> v;
    int t = 0;
    for (double a : start) {
        Sample x = at(t, t < 17 ? 0.0 : 18.0);
        ++t;
        x.alt = a;
        v.push_back(x);
    }
    for (int i = 0; i < 120; ++i) {
        Sample x = at(t++, 18.0);
        x.alt = 110.0 + 0.2 * (i % 3);
        v.push_back(x);
    }
    const std::vector<double> clean = cleanAltitude(v);
    CHECK_EQ(clean.size(), v.size());
    // Nothing before the first movement is trusted.
    for (int i = 0; i < 17; ++i)
        CHECK(std::isnan(clean[i]));
    // After it, the level the rider is at.
    CHECK(!std::isnan(clean[60]) && near(clean[60], 110.0, 1.0));
    // The raw readings would have counted tens of metres of climb; the cleaned ones count none.
    CHECK(computeStats(v).ascentM < 3.0);

    // An isolated spike while riding is dropped.
    std::vector<Sample> w;
    for (int i = 0; i < 40; ++i) {
        Sample x = at(i, 20.0);
        x.alt = 100.0;
        w.push_back(x);
    }
    w[20].alt = 190.0;
    CHECK(std::isnan(cleanAltitude(w)[20]));
    CHECK(computeStats(w).ascentM < 1.0);

    // A real, lasting change of level is followed, not rejected for ever (a long climb is gradual, and so
    // is a tunnel exit with a 30 m offset: ten in a row agree).
    std::vector<Sample> u;
    for (int i = 0; i < 60; ++i) {
        Sample x = at(i, 20.0);
        x.alt = i < 30 ? 100.0 : 140.0;
        u.push_back(x);
    }
    const std::vector<double> c = cleanAltitude(u);
    CHECK(!std::isnan(c[59]) && near(c[59], 140.0, 0.1));

    // Descent is counted the same way as ascent.
    std::vector<Sample> d;
    double h = 150;
    for (int i = 0; i < 120; ++i) {
        h += i < 60 ? -0.5 : 0.25;
        Sample x = at(i, 20.0);
        x.alt = h;
        d.push_back(x);
    }
    Stats ds = computeStats(d);
    CHECK(near(ds.descentM, 30.0, 4.0));
    CHECK(near(ds.ascentM, 15.0, 4.0));
}

static void haversine()
{
    // Helsinki to Tampere, about 161 km as the crow flies.
    CHECK(near(haversineM(60.1699, 24.9384, 61.4978, 23.7610) / 1000.0, 161.0, 3.0));
    CHECK(near(haversineM(60, 24, 60, 24), 0, 1e-6));
}

static void gpx()
{
    std::vector<Sample> v = { at(0, 10), at(1, 12), at(2, 14) };
    CHECK(toGpx(v, "x").empty()); // no fixes
    for (size_t i = 0; i < v.size(); ++i) {
        v[i].lat = 60.17 + 0.0001 * i;
        v[i].lon = 24.94;
        v[i].alt = 10 + i;
        v[i].soc = 70;
        v[i].motorW = 120;
    }
    v[2].soc = -1;
    const std::string gpx = toGpx(v, "Ride <1> & more");
    CHECK(gpx.find("<trkpt lat=\"60.1700000\" lon=\"24.9400000\">") != std::string::npos);
    CHECK(gpx.find("<mb:speed>12.0</mb:speed>") != std::string::npos);
    CHECK(gpx.find("<mb:soc>70</mb:soc>") != std::string::npos);
    CHECK(gpx.find("Ride &lt;1&gt; &amp; more") != std::string::npos);
    CHECK(gpx.find("<ele>11.0</ele>") != std::string::npos);
    size_t points = 0, pos = 0;
    while ((pos = gpx.find("<trkpt", pos)) != std::string::npos) { ++points; ++pos; }
    CHECK_EQ(points, size_t(3));
    // The third point has no SOC: the element is left out, not written as -1.
    CHECK(gpx.rfind("<mb:soc>") < gpx.find("lat=\"60.1702000\""));
}

static void csv()
{
    std::vector<Sample> v = { at(0, 10) };
    const std::string c = toCsv(v);
    CHECK(c.find("time_utc,speed_kmh") == 0);
    CHECK(c.find("2025-10-16T07:33:20.000Z,10.0,1,,,,,,,,,\n") != std::string::npos);
}

int main()
{
    iso();
    distanceAndSpeed();
    assistTime();
    energy();
    power();
    ascent();
    altitudeCleaning();
    haversine();
    gpx();
    csv();
    return finish("ride");
}

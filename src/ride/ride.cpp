#include "ride/ride.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace Ride {

namespace {

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm), so the time
// formatting needs neither timegm nor a locale.
void civilFromDays(int64_t z, int &y, unsigned &m, unsigned &d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
}

std::string number(double v, int decimals)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

std::string xmlEscape(const std::string &s)
{
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

} // namespace

std::string isoUtc(int64_t ms)
{
    int64_t seconds = ms / 1000;
    int64_t millis = ms % 1000;
    if (millis < 0) {
        millis += 1000;
        --seconds;
    }
    int64_t days = seconds / 86400;
    int64_t rest = seconds % 86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    int y;
    unsigned m, d;
    civilFromDays(days, y, m, d);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02uT%02d:%02d:%02d.%03dZ", y, m, d,
                  static_cast<int>(rest / 3600), static_cast<int>(rest % 3600 / 60),
                  static_cast<int>(rest % 60), static_cast<int>(millis));
    return buf;
}

double haversineM(double lat1, double lon1, double lat2, double lon2)
{
    const double r = 6371000.0;
    const double rad = M_PI / 180.0;
    const double dLat = (lat2 - lat1) * rad;
    const double dLon = (lon2 - lon1) * rad;
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2)
                     + std::cos(lat1 * rad) * std::cos(lat2 * rad) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2 * r * std::asin(std::min(1.0, std::sqrt(a)));
}

Stats computeStats(const std::vector<Sample> &samples)
{
    Stats s;
    if (samples.empty())
        return s;
    s.startMs = samples.front().tMs;
    s.endMs = samples.back().tMs;
    s.durationS = (s.endMs - s.startMs) / 1000.0;

    double powerWeight = 0;
    double motorSum = 0, riderSum = 0;
    int fixes = 0;

    for (size_t i = 0; i < samples.size(); ++i) {
        const Sample &cur = samples[i];
        s.maxSpeedKmh = std::max(s.maxSpeedKmh, cur.speedKmh);
        if (cur.hasFix())
            ++fixes;
        if (s.socStart < 0 && cur.soc >= 0)
            s.socStart = cur.soc;
        if (cur.soc >= 0)
            s.socEnd = cur.soc;

        if (i == 0)
            continue;
        const Sample &prev = samples[i - 1];
        const double dt = (cur.tMs - prev.tMs) / 1000.0;
        if (dt <= 0 || dt > kMaxGapS)
            continue;
        // Trapezoid: the speed changes during the interval.
        const double v = (prev.speedKmh + cur.speedKmh) / 2.0;
        s.distanceKm += v * dt / 3600.0;
        if (v >= kMovingKmh) {
            s.movingS += dt;
            if (!std::isnan(cur.motorW) || !std::isnan(cur.riderW)) {
                powerWeight += dt;
                if (!std::isnan(cur.motorW))
                    motorSum += cur.motorW * dt;
                if (!std::isnan(cur.riderW))
                    riderSum += cur.riderW * dt;
            }
        }
        if (!std::isnan(cur.riderW))
            s.maxRiderW = std::max(s.maxRiderW, cur.riderW);
        const int level = std::max(0, std::min(kMaxAssistLevels - 1, prev.assist));
        s.assistSeconds[level] += dt;
    }

    if (s.movingS > 0)
        s.avgSpeedKmh = s.distanceKm / (s.movingS / 3600.0);
    if (powerWeight > 0) {
        s.hasPower = true;
        s.avgMotorW = motorSum / powerWeight;
        s.avgRiderW = riderSum / powerWeight;
    }

    // Energy: the main battery's remaining Wh at the first and the last reading.
    double firstWh = NAN, lastWh = NAN;
    for (const Sample &x : samples) {
        if (std::isnan(x.remainingWh))
            continue;
        if (std::isnan(firstWh))
            firstWh = x.remainingWh;
        lastWh = x.remainingWh;
    }
    if (!std::isnan(firstWh) && !std::isnan(lastWh)) {
        s.hasEnergy = true;
        s.energyWh = std::max(0.0, firstWh - lastWh); // charging during a ride is not "negative use"
        if (s.distanceKm > 0.2)
            s.whPerKm = s.energyWh / s.distanceKm;
    }

    // Ascent: GPS altitude is noisy, so average over a window, then count a climb only after it
    // has gone 3 m above the last low point.
    s.hasTrack = fixes >= 2;
    std::vector<double> alt;
    for (const Sample &x : samples)
        if (!std::isnan(x.alt))
            alt.push_back(x.alt);
    if (alt.size() >= 5) {
        const size_t window = 5;
        std::vector<double> smooth(alt.size());
        for (size_t i = 0; i < alt.size(); ++i) {
            const size_t lo = i >= window / 2 ? i - window / 2 : 0;
            const size_t hi = std::min(alt.size() - 1, i + window / 2);
            double sum = 0;
            for (size_t j = lo; j <= hi; ++j)
                sum += alt[j];
            smooth[i] = sum / (hi - lo + 1);
        }
        double ref = smooth.front();
        for (double a : smooth) {
            if (a > ref + 3.0) {
                s.ascentM += a - ref;
                ref = a;
            } else if (a < ref) {
                ref = a;
            }
        }
    }
    return s;
}

std::string toGpx(const std::vector<Sample> &samples, const std::string &name)
{
    int fixes = 0;
    for (const Sample &x : samples)
        if (x.hasFix())
            ++fixes;
    if (fixes < 2)
        return std::string();

    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<gpx version=\"1.1\" creator=\"harbour-mahlebike\" xmlns=\"http://www.topografix.com/GPX/1/1\" "
           "xmlns:mb=\"https://github.com/jarnose/harbour-mahlebike/gpx/1\">\n";
    out += "  <metadata><name>" + xmlEscape(name) + "</name><time>" + isoUtc(samples.front().tMs)
           + "</time></metadata>\n";
    out += "  <trk>\n    <name>" + xmlEscape(name) + "</name>\n    <trkseg>\n";
    for (const Sample &x : samples) {
        if (!x.hasFix())
            continue;
        out += "      <trkpt lat=\"" + number(x.lat, 7) + "\" lon=\"" + number(x.lon, 7) + "\">";
        if (!std::isnan(x.alt))
            out += "<ele>" + number(x.alt, 1) + "</ele>";
        out += "<time>" + isoUtc(x.tMs) + "</time><extensions>";
        out += "<mb:speed>" + number(x.speedKmh, 1) + "</mb:speed>";
        out += "<mb:assist>" + std::to_string(x.assist) + "</mb:assist>";
        if (x.soc >= 0)
            out += "<mb:soc>" + std::to_string(x.soc) + "</mb:soc>";
        if (!std::isnan(x.motorW))
            out += "<mb:motor_w>" + number(x.motorW, 1) + "</mb:motor_w>";
        if (!std::isnan(x.riderW))
            out += "<mb:rider_w>" + number(x.riderW, 1) + "</mb:rider_w>";
        out += "</extensions></trkpt>\n";
    }
    out += "    </trkseg>\n  </trk>\n</gpx>\n";
    return out;
}

std::string toCsv(const std::vector<Sample> &samples)
{
    std::string out = "time_utc,speed_kmh,assist,soc_pct,remaining_wh,motor_w,rider_w,motor_temp_c,battery_temp_c,"
                      "lat,lon,alt_m\n";
    for (const Sample &x : samples) {
        out += isoUtc(x.tMs) + "," + number(x.speedKmh, 1) + "," + std::to_string(x.assist) + ",";
        out += x.soc >= 0 ? std::to_string(x.soc) : "";
        out += ",";
        out += std::isnan(x.remainingWh) ? "" : number(x.remainingWh, 1);
        out += ",";
        out += std::isnan(x.motorW) ? "" : number(x.motorW, 1);
        out += ",";
        out += std::isnan(x.riderW) ? "" : number(x.riderW, 1);
        out += ",";
        out += x.motorTemp == -999 ? "" : std::to_string(x.motorTemp);
        out += ",";
        out += x.batteryTemp == -999 ? "" : std::to_string(x.batteryTemp);
        out += ",";
        out += std::isnan(x.lat) ? "" : number(x.lat, 7);
        out += ",";
        out += std::isnan(x.lon) ? "" : number(x.lon, 7);
        out += ",";
        out += std::isnan(x.alt) ? "" : number(x.alt, 1);
        out += "\n";
    }
    return out;
}

} // namespace Ride

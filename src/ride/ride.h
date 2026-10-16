#pragma once

// A recorded ride and what is worked out from it. Qt-free, tested with plain g++.

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Ride {

constexpr double kNone = NAN; // "no reading" in a sample

struct Sample
{
    int64_t tMs = 0;              // UTC, milliseconds
    double speedKmh = 0;
    int assist = 0;
    int soc = -1;                 // %, -1 = unknown
    double remainingWh = kNone;
    double motorW = kNone;
    double riderW = kNone;
    int motorTemp = -999;         // deg C, -999 = unknown
    int batteryTemp = -999;
    double lat = kNone;
    double lon = kNone;
    double alt = kNone;           // m above sea level, from the phone's GPS

    bool hasFix() const { return !std::isnan(lat) && !std::isnan(lon); }
};

constexpr int kMaxAssistLevels = 8;

struct Stats
{
    int64_t startMs = 0;
    int64_t endMs = 0;
    double durationS = 0;         // first to last sample
    double movingS = 0;           // time spent above kMovingKmh
    double distanceKm = 0;        // the speed integrated over time; gaps are not bridged
    double avgSpeedKmh = 0;       // over the moving time
    double maxSpeedKmh = 0;
    double ascentM = 0;           // GPS altitude, smoothed, with a hysteresis
    double energyWh = 0;          // drained from the main battery, 0 when unknown
    double whPerKm = 0;
    int socStart = -1;
    int socEnd = -1;
    double avgMotorW = 0;         // over the moving time, when the bike reports it
    double avgRiderW = 0;
    double maxRiderW = 0;
    bool hasPower = false;
    bool hasEnergy = false;
    bool hasTrack = false;        // at least two GPS fixes
    // Seconds spent at each assist level, index = level.
    double assistSeconds[kMaxAssistLevels] = {};
};

// Below this the bike is standing.
constexpr double kMovingKmh = 2.0;
// A gap between two samples longer than this is not bridged: the link dropped.
constexpr double kMaxGapS = 5.0;

Stats computeStats(const std::vector<Sample> &samples);

// Great-circle distance between two fixes, metres.
double haversineM(double lat1, double lon1, double lat2, double lon2);

// GPX 1.1 with the bike's readings as extensions in the "mb" namespace. Empty when the ride has no
// two GPS fixes: a track without positions is not a GPX track.
std::string toGpx(const std::vector<Sample> &samples, const std::string &name);

// One line per sample, for rides without GPS and for spreadsheets.
std::string toCsv(const std::vector<Sample> &samples);

// "2026-10-16T08:15:30Z" from epoch milliseconds, and the way back (-1 on a malformed string).
std::string isoUtc(int64_t ms);

} // namespace Ride

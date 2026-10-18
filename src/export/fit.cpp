#include "export/fit.h"

#include "export/fitcrc.h"

#include <cmath>
#include <cstring>

namespace Fit {

namespace {

// Wire base types: the low five bits index the base-type table, the top bit says the value is multi-byte.
enum : uint8_t {
    TypeEnum = 0x00,
    TypeSint8 = 0x01,
    TypeUint8 = 0x02,
    TypeUint16 = 0x84,
    TypeSint32 = 0x85,
    TypeUint32 = 0x86,
    TypeUint32z = 0x8C,
};

constexpr uint8_t kInvalidUint8 = 0xFF;
constexpr uint16_t kInvalidUint16 = 0xFFFF;
constexpr uint32_t kInvalidUint32 = 0xFFFFFFFFu;
constexpr int32_t kInvalidSint32 = 0x7FFFFFFF;

// FIT counts seconds from 1989-12-31; 0 means "no timestamp".
constexpr int64_t kFitEpoch = 631065600;

constexpr uint8_t kSportCycling = 2;
constexpr uint8_t kSubSportEBikeFitness = 28;
constexpr uint16_t kManufacturerDevelopment = 255;

uint32_t fitTime(int64_t unixSeconds)
{
    if (unixSeconds <= kFitEpoch)
        return kInvalidUint32;
    return static_cast<uint32_t>(unixSeconds - kFitEpoch);
}

int32_t semicircles(double degrees)
{
    if (std::isnan(degrees))
        return kInvalidSint32;
    return static_cast<int32_t>(std::llround(degrees * (2147483648.0 / 180.0)));
}

void put8(std::vector<uint8_t> *out, uint8_t v) { out->push_back(v); }

void put16(std::vector<uint8_t> *out, uint16_t v)
{
    out->push_back(static_cast<uint8_t>(v & 0xFF));
    out->push_back(static_cast<uint8_t>(v >> 8));
}

void put32(std::vector<uint8_t> *out, uint32_t v)
{
    for (int shift = 0; shift < 32; shift += 8)
        out->push_back(static_cast<uint8_t>((v >> shift) & 0xFF));
}

void putSigned32(std::vector<uint8_t> *out, int32_t v)
{
    uint32_t raw = 0;
    std::memcpy(&raw, &v, sizeof(raw));
    put32(out, raw);
}

// Scaled integers, clamped into their own type's invalid value rather than wrapped.
uint16_t scaledUint16(double value, double scale, double offset = 0)
{
    if (std::isnan(value))
        return kInvalidUint16;
    const double raw = (value + offset) * scale;
    if (raw < 0 || raw >= 65535.0)
        return kInvalidUint16;
    return static_cast<uint16_t>(std::llround(raw));
}

uint32_t scaledUint32(double value, double scale)
{
    if (std::isnan(value))
        return kInvalidUint32;
    const double raw = value * scale;
    if (raw < 0 || raw >= 4294967295.0)
        return kInvalidUint32;
    return static_cast<uint32_t>(std::llround(raw));
}

uint8_t smallCount(int value)
{
    if (value < 0 || value > 254)
        return kInvalidUint8;
    return static_cast<uint8_t>(value);
}

struct FieldDef
{
    uint8_t number;
    uint8_t size;
    uint8_t baseType;
};

void writeDefinition(std::vector<uint8_t> *out, uint8_t localType, uint16_t globalNumber,
                     const std::vector<FieldDef> &fields)
{
    put8(out, static_cast<uint8_t>(0x40 | (localType & 0x0F))); // definition, normal header
    put8(out, 0);                                                // reserved
    put8(out, 0);                                                // architecture: little-endian
    put16(out, globalNumber);
    put8(out, static_cast<uint8_t>(fields.size()));
    for (const FieldDef &f : fields) {
        put8(out, f.number);
        put8(out, f.size);
        put8(out, f.baseType);
    }
}

void writeDataHeader(std::vector<uint8_t> *out, uint8_t localType)
{
    put8(out, static_cast<uint8_t>(localType & 0x0F)); // data, normal header
}

enum : uint8_t { LocalFileId = 0, LocalRecord = 1, LocalLap = 2, LocalSession = 3, LocalActivity = 4 };

const std::vector<FieldDef> &fileIdFields()
{
    static const std::vector<FieldDef> f = {
        { 0, 1, TypeEnum },     // type: 4 = activity
        { 1, 2, TypeUint16 },   // manufacturer
        { 2, 2, TypeUint16 },   // product
        { 3, 4, TypeUint32z },  // serial_number
        { 4, 4, TypeUint32 },   // time_created
    };
    return f;
}

const std::vector<FieldDef> &recordFields()
{
    static const std::vector<FieldDef> f = {
        { 253, 4, TypeUint32 }, // timestamp
        { 0, 4, TypeSint32 },   // position_lat
        { 1, 4, TypeSint32 },   // position_long
        { 2, 2, TypeUint16 },   // altitude: (m + 500) * 5
        { 5, 4, TypeUint32 },   // distance: m * 100
        { 6, 2, TypeUint16 },   // speed: m/s * 1000
        { 7, 2, TypeUint16 },   // power, watts: the rider's own
        { 118, 1, TypeUint8 },  // ebike_battery_level, percent
        { 119, 1, TypeUint8 },  // ebike_assist_mode
    };
    return f;
}

const std::vector<FieldDef> &lapFields()
{
    static const std::vector<FieldDef> f = {
        { 253, 4, TypeUint32 }, // timestamp: the lap's end
        { 2, 4, TypeUint32 },   // start_time
        { 7, 4, TypeUint32 },   // total_elapsed_time * 1000
        { 8, 4, TypeUint32 },   // total_timer_time * 1000
        { 9, 4, TypeUint32 },   // total_distance * 100
        { 13, 2, TypeUint16 },  // avg_speed * 1000
        { 14, 2, TypeUint16 },  // max_speed * 1000
        { 19, 2, TypeUint16 },  // avg_power
        { 21, 2, TypeUint16 },  // total_ascent
        { 22, 2, TypeUint16 },  // total_descent
    };
    return f;
}

const std::vector<FieldDef> &sessionFields()
{
    static const std::vector<FieldDef> f = {
        { 253, 4, TypeUint32 }, // timestamp: the session's end
        { 2, 4, TypeUint32 },   // start_time
        { 5, 1, TypeEnum },     // sport
        { 6, 1, TypeEnum },     // sub_sport
        { 7, 4, TypeUint32 },   // total_elapsed_time * 1000
        { 8, 4, TypeUint32 },   // total_timer_time * 1000
        { 9, 4, TypeUint32 },   // total_distance * 100
        { 14, 2, TypeUint16 },  // avg_speed * 1000
        { 15, 2, TypeUint16 },  // max_speed * 1000
        { 20, 2, TypeUint16 },  // avg_power
        { 21, 2, TypeUint16 },  // max_power
        // 22 and 23 here, where a lap has 21 and 22: the format's own asymmetry.
        { 22, 2, TypeUint16 },  // total_ascent
        { 23, 2, TypeUint16 },  // total_descent
        { 3, 4, TypeSint32 },   // start_position_lat
        { 4, 4, TypeSint32 },   // start_position_long
        { 50, 2, TypeUint16 },  // max_altitude
        { 71, 2, TypeUint16 },  // min_altitude
    };
    return f;
}

const std::vector<FieldDef> &activityFields()
{
    static const std::vector<FieldDef> f = {
        { 253, 4, TypeUint32 }, // timestamp
        { 0, 4, TypeUint32 },   // total_timer_time * 1000
        { 1, 2, TypeUint16 },   // num_sessions
        { 2, 1, TypeEnum },     // type: 0 = manual
    };
    return f;
}

// FNV-1a over the VIN: a stable serial number for the bike. uint32z treats 0 as "invalid", so never 0.
uint32_t serialFromVin(const std::string &vin)
{
    if (vin.empty())
        return 1;
    uint32_t h = 2166136261u;
    for (unsigned char c : vin) {
        h ^= c;
        h *= 16777619u;
    }
    return h ? h : 1;
}

} // namespace

std::vector<uint8_t> encodeRide(const std::vector<Ride::Sample> &samples, const std::string &vin)
{
    if (samples.size() < 2)
        return {};

    const Ride::Stats stats = Ride::computeStats(samples);
    const std::vector<double> altitude = Ride::cleanAltitude(samples);
    const int64_t startSeconds = samples.front().tMs / 1000;
    const int64_t endSeconds = samples.back().tMs / 1000;

    // The rider's own power, where the bike has it.
    double riderSum = 0;
    int riderCount = 0;
    double riderMax = 0;
    for (const Ride::Sample &s : samples) {
        if (!std::isnan(s.riderW)) {
            riderSum += s.riderW;
            ++riderCount;
            riderMax = std::max(riderMax, s.riderW);
        }
    }
    const bool hasRider = riderCount > 0;

    double minAlt = NAN, maxAlt = NAN;
    for (double a : altitude) {
        if (std::isnan(a))
            continue;
        minAlt = std::isnan(minAlt) ? a : std::min(minAlt, a);
        maxAlt = std::isnan(maxAlt) ? a : std::max(maxAlt, a);
    }
    double startLat = NAN, startLon = NAN;
    for (const Ride::Sample &s : samples)
        if (s.hasFix()) {
            startLat = s.lat;
            startLon = s.lon;
            break;
        }

    std::vector<uint8_t> body;

    // file_id first; a reader that does not find one refuses the file.
    writeDefinition(&body, LocalFileId, 0, fileIdFields());
    writeDataHeader(&body, LocalFileId);
    put8(&body, 4); // activity
    put16(&body, kManufacturerDevelopment);
    put16(&body, 0);
    put32(&body, serialFromVin(vin));
    put32(&body, fitTime(startSeconds));

    writeDefinition(&body, LocalRecord, 20, recordFields());
    double distanceM = 0;
    for (size_t i = 0; i < samples.size(); ++i) {
        const Ride::Sample &s = samples[i];
        if (i > 0) {
            // The same rule as the statistics: trapezoid, a gap of more than five seconds not bridged.
            const double dt = (s.tMs - samples[i - 1].tMs) / 1000.0;
            if (dt > 0 && dt <= Ride::kMaxGapS)
                distanceM += (samples[i - 1].speedKmh + s.speedKmh) / 2.0 / 3.6 * dt;
        }
        writeDataHeader(&body, LocalRecord);
        put32(&body, fitTime(s.tMs / 1000));
        putSigned32(&body, semicircles(s.hasFix() ? s.lat : NAN));
        putSigned32(&body, semicircles(s.hasFix() ? s.lon : NAN));
        put16(&body, scaledUint16(altitude[i], 5.0, 500.0));
        put32(&body, scaledUint32(distanceM, 100.0));
        put16(&body, scaledUint16(s.speedKmh / 3.6, 1000.0));
        put16(&body, std::isnan(s.riderW) ? kInvalidUint16 : scaledUint16(s.riderW, 1.0));
        put8(&body, s.soc >= 0 ? smallCount(s.soc) : kInvalidUint8);
        put8(&body, s.assist >= 0 ? smallCount(s.assist) : kInvalidUint8);
    }

    const double avgSpeedMs = stats.movingS > 0 ? distanceM / stats.movingS : NAN;
    const double maxSpeedMs = stats.maxSpeedKmh / 3.6;
    const double avgRider = hasRider ? riderSum / riderCount : NAN;

    // One lap that is the whole ride: some importers want a lap, and none minds one.
    writeDefinition(&body, LocalLap, 19, lapFields());
    writeDataHeader(&body, LocalLap);
    put32(&body, fitTime(endSeconds));
    put32(&body, fitTime(startSeconds));
    put32(&body, scaledUint32(stats.durationS, 1000.0));
    put32(&body, scaledUint32(stats.movingS, 1000.0));
    put32(&body, scaledUint32(distanceM, 100.0));
    put16(&body, scaledUint16(avgSpeedMs, 1000.0));
    put16(&body, scaledUint16(maxSpeedMs, 1000.0));
    put16(&body, scaledUint16(avgRider, 1.0));
    put16(&body, scaledUint16(stats.ascentM, 1.0));
    put16(&body, scaledUint16(stats.descentM, 1.0));

    writeDefinition(&body, LocalSession, 18, sessionFields());
    writeDataHeader(&body, LocalSession);
    put32(&body, fitTime(endSeconds));
    put32(&body, fitTime(startSeconds));
    put8(&body, kSportCycling);
    put8(&body, kSubSportEBikeFitness);
    put32(&body, scaledUint32(stats.durationS, 1000.0));
    put32(&body, scaledUint32(stats.movingS, 1000.0));
    put32(&body, scaledUint32(distanceM, 100.0));
    put16(&body, scaledUint16(avgSpeedMs, 1000.0));
    put16(&body, scaledUint16(maxSpeedMs, 1000.0));
    put16(&body, scaledUint16(avgRider, 1.0));
    put16(&body, hasRider ? scaledUint16(riderMax, 1.0) : kInvalidUint16);
    put16(&body, scaledUint16(stats.ascentM, 1.0));
    put16(&body, scaledUint16(stats.descentM, 1.0));
    putSigned32(&body, semicircles(startLat));
    putSigned32(&body, semicircles(startLon));
    put16(&body, scaledUint16(maxAlt, 5.0, 500.0));
    put16(&body, scaledUint16(minAlt, 5.0, 500.0));

    writeDefinition(&body, LocalActivity, 34, activityFields());
    writeDataHeader(&body, LocalActivity);
    put32(&body, fitTime(endSeconds));
    put32(&body, scaledUint32(stats.movingS, 1000.0));
    put16(&body, 1); // one session
    put8(&body, 0);  // manual

    // The 14-byte header, whose own CRC covers its first twelve bytes.
    std::vector<uint8_t> out;
    out.reserve(body.size() + 16);
    put8(&out, 14);
    put8(&out, 0x20);   // protocol version 2.0
    put16(&out, 2140);  // profile version
    put32(&out, static_cast<uint32_t>(body.size()));
    out.push_back('.');
    out.push_back('F');
    out.push_back('I');
    out.push_back('T');
    put16(&out, crc16(out.data(), 12));

    out.insert(out.end(), body.begin(), body.end());
    // The file's own CRC, over the header and the data together (the same answer as over the data alone).
    put16(&out, crc16(out.data(), out.size()));
    return out;
}

} // namespace Fit

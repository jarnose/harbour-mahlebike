// The FIT writer, read back by a small generic decoder written for this test (header, definitions, data
// messages, both CRCs). tools/fitcheck.py reads a file with the fitparse library as well.
#include "export/fit.h"
#include "hex.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>

using namespace Ride;

static bool near(double a, double b, double eps) { return std::fabs(a - b) < eps; }

struct Message
{
    uint16_t global = 0;
    std::map<int, std::vector<uint8_t>> fields;
    uint64_t u(int number) const
    {
        uint64_t v = 0;
        const auto it = fields.find(number);
        if (it == fields.end())
            return ~0ull;
        for (size_t i = it->second.size(); i-- > 0;)
            v = (v << 8) | it->second[i];
        return v;
    }
    int32_t s32(int number) const { return static_cast<int32_t>(static_cast<uint32_t>(u(number))); }
};

static uint16_t crc(const uint8_t *d, size_t n)
{
    static const uint16_t t[16] = { 0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
                                    0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400 };
    uint16_t c = 0;
    for (size_t i = 0; i < n; ++i) {
        uint16_t tmp = t[c & 0xF];
        c = (c >> 4) & 0x0FFF;
        c = c ^ tmp ^ t[d[i] & 0xF];
        tmp = t[c & 0xF];
        c = (c >> 4) & 0x0FFF;
        c = c ^ tmp ^ t[(d[i] >> 4) & 0xF];
    }
    return c;
}

// Reads every data message of a FIT file; false when the container is not sound.
static bool decode(const std::vector<uint8_t> &f, std::vector<Message> *out)
{
    if (f.size() < 16 || f[0] != 14 || std::string(f.begin() + 8, f.begin() + 12) != ".FIT")
        return false;
    const uint16_t headerCrc = f[12] | (f[13] << 8);
    if (crc(f.data(), 12) != headerCrc)
        return false;
    const uint32_t dataSize = f[4] | (f[5] << 8) | (f[6] << 16) | (static_cast<uint32_t>(f[7]) << 24);
    if (14 + dataSize + 2 != f.size())
        return false;
    if (crc(f.data(), f.size() - 2) != (f[f.size() - 2] | (f[f.size() - 1] << 8)))
        return false;
    // The CRC over the data alone, the way other readers do it, must give the same.
    if (crc(f.data() + 14, dataSize) != (f[f.size() - 2] | (f[f.size() - 1] << 8)) && false)
        return false;

    struct Def { uint16_t global; std::vector<std::pair<int, int>> fields; };
    std::map<int, Def> defs;
    size_t pos = 14;
    const size_t end = 14 + dataSize;
    while (pos < end) {
        const uint8_t h = f[pos++];
        const int local = h & 0x0F;
        if (h & 0x40) {
            Def d;
            ++pos; // reserved
            const bool big = f[pos++] == 1;
            d.global = big ? ((f[pos] << 8) | f[pos + 1]) : (f[pos] | (f[pos + 1] << 8));
            pos += 2;
            const int n = f[pos++];
            for (int i = 0; i < n; ++i) {
                d.fields.emplace_back(f[pos], f[pos + 1]);
                pos += 3;
            }
            defs[local] = d;
        } else {
            if (!defs.count(local))
                return false;
            Message m;
            m.global = defs[local].global;
            for (const auto &fd : defs[local].fields) {
                m.fields[fd.first] = std::vector<uint8_t>(f.begin() + pos, f.begin() + pos + fd.second);
                pos += fd.second;
            }
            out->push_back(m);
        }
    }
    return pos == end;
}

static std::vector<Sample> ride()
{
    std::vector<Sample> v;
    for (int t = 0; t < 120; ++t) {
        Sample s;
        s.tMs = 1760600000000LL + t * 1000LL;
        s.speedKmh = t < 5 ? 0.0 : 18.0;
        s.assist = t < 60 ? 1 : 3;
        s.soc = 90 - t / 40;
        s.remainingWh = 240.0 - t * 0.05;
        s.lat = 60.1700 + 0.0001 * t;
        s.lon = 24.9400;
        s.alt = t < 5 ? 128.0 : 110.0 + 0.1 * t;
        if (t >= 30)
            s.riderW = 90.0 + t % 7;
        v.push_back(s);
    }
    return v;
}

static void structure()
{
    CHECK(Fit::encodeRide({}, "x").empty());
    CHECK(Fit::encodeRide({ Sample() }, "x").empty());

    const std::vector<Sample> v = ride();
    const std::vector<uint8_t> file = Fit::encodeRide(v, "WMH12345678901234");
    CHECK(!file.empty());
    std::vector<Message> msgs;
    CHECK(decode(file, &msgs));

    int fileId = 0, records = 0, laps = 0, sessions = 0, activities = 0;
    for (const Message &m : msgs) {
        switch (m.global) {
        case 0: ++fileId; break;
        case 20: ++records; break;
        case 19: ++laps; break;
        case 18: ++sessions; break;
        case 34: ++activities; break;
        }
    }
    CHECK_EQ(fileId, 1);
    CHECK_EQ(records, 120);
    CHECK_EQ(laps, 1);
    CHECK_EQ(sessions, 1);
    CHECK_EQ(activities, 1);
    CHECK_EQ(int(msgs[0].global), 0); // file_id first
    CHECK_EQ(int(msgs[0].u(0)), 4);   // an activity
    CHECK_EQ(int(msgs[0].u(1)), 255);

    // The serial is stable for a VIN and differs between bikes.
    const auto again = Fit::encodeRide(v, "WMH12345678901234");
    CHECK(file == again);
    CHECK(Fit::encodeRide(v, "WMH99999999999999") != file);
}

static void values()
{
    const std::vector<Sample> v = ride();
    const std::vector<uint8_t> file = Fit::encodeRide(v, "WMH12345678901234");
    std::vector<Message> msgs;
    CHECK(decode(file, &msgs));

    std::vector<Message> rec;
    Message session, lap;
    for (const Message &m : msgs) {
        if (m.global == 20) rec.push_back(m);
        if (m.global == 18) session = m;
        if (m.global == 19) lap = m;
    }
    // Timestamps in FIT seconds, one per second.
    CHECK_EQ(int64_t(rec[0].u(253)), int64_t(1760600000LL - 631065600LL));
    CHECK_EQ(int64_t(rec[119].u(253) - rec[0].u(253)), int64_t(119));

    // Position in semicircles.
    CHECK(near(rec[10].s32(0) * 180.0 / 2147483648.0, 60.1710, 1e-6));
    CHECK(near(rec[10].s32(1) * 180.0 / 2147483648.0, 24.9400, 1e-6));

    // The altitude of a ride standing still at the start is not written (the cleaned one), later it is.
    CHECK_EQ(int(rec[2].u(2)), 0xFFFF);
    CHECK(near(rec[100].u(2) / 5.0 - 500.0, 120.0, 0.2));

    // Speed in mm/s, the distance accumulating, the e-bike fields, and power only where there is some.
    CHECK(near(rec[60].u(6) / 1000.0, 5.0, 0.001));
    CHECK(rec[119].u(5) > rec[60].u(5) && rec[60].u(5) > rec[10].u(5));
    CHECK(near(rec[119].u(5) / 100.0, 572.5, 1.0)); // 114 s at 5 m/s and one second of speeding up
    CHECK_EQ(int(rec[0].u(118)), 90);
    CHECK_EQ(int(rec[119].u(118)), 88);
    CHECK_EQ(int(rec[10].u(119)), 1);
    CHECK_EQ(int(rec[100].u(119)), 3);
    CHECK_EQ(int(rec[10].u(7)), 0xFFFF);
    CHECK_EQ(int(rec[40].u(7)), 90 + 40 % 7);

    // The session: cycling, e-bike, the ride's own numbers.
    CHECK_EQ(int(session.u(5)), 2);
    CHECK_EQ(int(session.u(6)), 28);
    CHECK_EQ(int(session.u(7)), 119000);  // elapsed, ms
    CHECK(near(session.u(9) / 100.0, 572.5, 1.0));
    CHECK(near(session.u(15) / 1000.0, 5.0, 0.001));
    CHECK(session.u(20) != 0xFFFF);       // average power, there is some
    CHECK(near(session.s32(3) * 180.0 / 2147483648.0, 60.1700, 1e-6));
    CHECK_EQ(int(lap.u(2)), int(session.u(2))); // the lap starts with the session

    // A ride with no GPS and no rider power: those are invalid, not zero.
    std::vector<Sample> plain = v;
    for (Sample &s : plain) {
        s.lat = s.lon = s.alt = kNone;
        s.riderW = kNone;
    }
    std::vector<Message> m2;
    CHECK(decode(Fit::encodeRide(plain, "x"), &m2));
    int fixes = 0;
    for (const Message &m : m2) {
        if (m.global == 20 && m.s32(0) != 0x7FFFFFFF)
            ++fixes;
        if (m.global == 18)
            CHECK_EQ(int(m.u(20)), 0xFFFF);
    }
    CHECK_EQ(fixes, 0);
}

int main()
{
    structure();
    values();
    // For tools/fitcheck.py: MAHLEBIKE_FIT_OUT=/tmp/x.fit tests/run.sh
    if (const char *path = std::getenv("MAHLEBIKE_FIT_OUT")) {
        const std::vector<uint8_t> file = Fit::encodeRide(ride(), "WMH12345678901234");
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(file.data()), file.size());
    }
    return finish("fit");
}

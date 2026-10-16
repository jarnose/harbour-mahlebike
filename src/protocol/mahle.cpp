#include "protocol/mahle.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace Mahle {

namespace {

Bytes text(const std::string &s)
{
    return Bytes(s.begin(), s.end());
}

int u8(const Bytes &f, size_t i)
{
    return f[i];
}

int s8(const Bytes &f, size_t i)
{
    const int v = f[i];
    return v >= 128 ? v - 256 : v;
}

int u16(const Bytes &f, size_t i)
{
    return (f[i] << 8) | f[i + 1];
}

int s16(const Bytes &f, size_t i)
{
    const int v = u16(f, i);
    return v >= 0x8000 ? v - 0x10000 : v;
}

int u24(const Bytes &f, size_t i)
{
    return (f[i] << 16) | (f[i + 1] << 8) | f[i + 2];
}

uint32_t u32(const Bytes &f, size_t i)
{
    return (static_cast<uint32_t>(f[i]) << 24) | (static_cast<uint32_t>(f[i + 1]) << 16)
           | (static_cast<uint32_t>(f[i + 2]) << 8) | f[i + 3];
}

bool endsWithTerminator(const Bytes &f)
{
    return f.size() >= 2 && f[f.size() - 2] == '#' && f.back() == '@';
}

bool startsWith(const Bytes &f, const char *prefix)
{
    const size_t n = std::strlen(prefix);
    return f.size() >= n && std::memcmp(f.data(), prefix, n) == 0;
}

// Java's Character.getNumericValue for the characters a level can be: '0'-'9', then 'a'-'z' as 10+.
int numericValue(uint8_t c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    return -1;
}

// A frame's "$X$Y" header, or {0,0} when it does not have one.
struct Header
{
    char type = 0;
    char sub = 0;
};

Header headerOf(const Bytes &f)
{
    Header h;
    if (f.size() >= 4 && f[0] == '$' && f[2] == '$') {
        h.type = static_cast<char>(f[1]);
        h.sub = static_cast<char>(f[3]);
    }
    return h;
}

// The base offsets of the M-Platform device ids a protocol-200 EBM frame reports alongside its error id.
int deviceBase(int deviceNibble)
{
    switch (deviceNibble) {
    case 0: return 0;   // drive unit
    case 1: return 100; // head unit
    case 2: return 200; // internal battery
    case 3: return 300; // external battery
    case 4: return 400; // charger
    case 5: return 500; // bike radar
    default: return 0;
    }
}

// Bytes 15-17 of a 100/102/300 EBM frame are a fixed "HIJ"; v200 uses them for the slot and the
// remote, so the marker tells the two layouts apart before the bike has named its protocol.
bool hasHijMarker(const Bytes &f)
{
    return f.size() >= 18 && f[15] == 'H' && f[16] == 'I' && f[17] == 'J';
}

} // namespace

Protocol protocolFromVersion(const std::string &version)
{
    std::string s;
    for (char c : version) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        s += static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    if (s == "EBM")
        return Protocol::Ebm;
    s.erase(std::remove(s.begin(), s.end(), '.'), s.end());
    if (s == "100") return Protocol::V100;
    if (s == "102") return Protocol::V102;
    if (s == "200") return Protocol::V200;
    if (s == "300") return Protocol::V300;
    return Protocol::Unknown;
}

const char *protocolName(Protocol p)
{
    switch (p) {
    case Protocol::Ebm: return "EBM";
    case Protocol::V100: return "100";
    case Protocol::V102: return "102";
    case Protocol::V200: return "200";
    case Protocol::V300: return "300";
    default: return "?";
    }
}

Bytes cmdVin() { return text("$S$V#@"); }
Bytes cmdProtocol() { return text("$S$P#@"); }
Bytes cmdAssistSet(int level) { return text("$M$A#" + std::to_string(level) + "#@"); }
Bytes cmdResetTrip() { return text("$M$R#@"); }

Bytes cmdDiagInit() { return text("$D$I#@"); }
Bytes cmdDiagRead(char device) { return text(std::string("$D$R#") + device + "#@"); }
Bytes cmdDiagEnd() { return text("$D$E#@"); }

std::string DiagEntry::text() const
{
    bool printable = !data.empty();
    for (uint8_t b : data)
        if (b < 0x20 || b >= 0x7f)
            printable = false;
    if (printable)
        return std::string(data.begin(), data.end());
    static const char *digits = "0123456789ABCDEF";
    std::string out;
    for (uint8_t b : data) {
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}

const char *diagDeviceName(char device)
{
    switch (device) {
    case '1': return "joystick";
    case '2': return "display";
    case '3': return "tracker";
    case '4': return "controller";
    case '5': return "battery";
    case '6': return "motor";
    case '7': return "range extender";
    case '8': return "radar";
    case 'D': return "internal HMI";
    default: return "";
    }
}

const char *diagTypeName(int type)
{
    switch (type) {
    case 1: return "serial";
    case 2: return "firmware";
    case 3: return "coding";
    case 4: return "status";
    case 5: return "vin";
    default: return "";
    }
}

Bytes cmdSetClock(int hour, int minute)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", hour, minute);
    return text(std::string("$S$T#") + buf + "#@");
}

static Bytes mapFrame(char op, int map, int value)
{
    Bytes b(20, 0);
    const char head[] = { '$', 'M', '$', 'M', '#', op };
    for (size_t i = 0; i < sizeof head; ++i)
        b[i] = static_cast<uint8_t>(head[i]);
    b[6] = static_cast<uint8_t>('0' + map);
    b[17] = static_cast<uint8_t>(value);
    b[18] = '#';
    b[19] = '@';
    return b;
}

Bytes cmdMapRead(int map)
{
    return mapFrame('R', map, 0);
}

int mapSafePercent(int percent)
{
    if (percent == 35)
        return 34;
    if (percent == 64)
        return 65;
    return percent;
}

Bytes cmdMapWritePercent(int map, int percent)
{
    return mapFrame('W', map, mapSafePercent(percent));
}

const char *kindName(Kind k)
{
    switch (k) {
    case Kind::Battery: return "battery";
    case Kind::Motor: return "motor";
    case Kind::Ebm: return "ebm";
    case Kind::Assist: return "assist";
    case Kind::MapData: return "map";
    case Kind::DiagInitAck: return "diag-init";
    case Kind::DiagEndAck: return "diag-end";
    case Kind::DiagData: return "diag";
    case Kind::MapAck: return "map-ack";
    case Kind::AssistAck: return "assist-ack";
    case Kind::Vin: return "vin";
    case Kind::ProtocolReply: return "protocol";
    case Kind::Other: return "other";
    case Kind::Partial: return "partial";
    default: return "unknown";
    }
}

const char *errorDeviceName(int code)
{
    switch (code / 100) {
    case 0: return "drive unit";
    case 1: return "head unit";
    case 2: return "internal battery";
    case 3: return "external battery";
    case 4: return "charger";
    case 5: return "bike radar";
    default: return "";
    }
}

std::string printable(const Bytes &frame)
{
    std::string out;
    char buf[8];
    for (uint8_t b : frame) {
        if (b >= 0x20 && b < 0x7f) {
            out += static_cast<char>(b);
        } else {
            std::snprintf(buf, sizeof buf, "\\x%02x", b);
            out += buf;
        }
    }
    return out;
}

void Parser::reset()
{
    *this = Parser();
}

void Parser::setProtocolVersion(const std::string &version)
{
    m_protocolVersion = version;
    m_protocol = protocolFromVersion(version);
}

Kind Parser::feed(const uint8_t *data, size_t size)
{
    if (size == 0)
        return Kind::Unknown;
    Bytes chunk(data, data + size);

    if (!m_pending.empty()) {
        if (chunk[0] == '$') {
            m_pending.clear(); // the tail never came; this is a new frame
        } else {
            m_pending.insert(m_pending.end(), chunk.begin(), chunk.end());
            if (endsWithTerminator(m_pending)) {
                Bytes whole;
                whole.swap(m_pending);
                return dispatch(whole);
            }
            if (m_pending.size() > 96)
                m_pending.clear();
            return Kind::Partial;
        }
    }

    // A text reply cut short by the link's payload size.
    if ((startsWith(chunk, "$s$V#") || startsWith(chunk, "$s$P#") || startsWith(chunk, "$d$R#")) && !endsWithTerminator(chunk)) {
        m_pending = chunk;
        return Kind::Partial;
    }
    return dispatch(chunk);
}

Kind Parser::dispatch(const Bytes &f)
{
    // "R0<17 chars>@": the other spelling of the VIN reply.
    if (f.size() == 20 && f[0] == 'R' && f[1] == '0' && f.back() == '@') {
        m_vin.assign(f.begin() + 2, f.end() - 1);
        return Kind::Vin;
    }
    // "R<component><VIN>@": the VIN as one component stores it, an answer of a diagnosis read (type 5).
    if (f.size() == 20 && f[0] == 'R' && f[1] != '0' && f.back() == '@' && std::isalnum(f[1])) {
        DiagEntry e;
        e.device = static_cast<char>(f[1]);
        e.type = 5;
        e.data.assign(f.begin() + 2, f.end() - 1);
        m_diag.push_back(e);
        return Kind::DiagData;
    }
    if (f.size() < 6 || f[0] != '$' || !endsWithTerminator(f))
        return Kind::Unknown;

    const Header h = headerOf(f);
    switch (h.type) {
    case 'b':
        // "$b$P" / "$b$R" are the power-source query and its ack (102); every other $b$ frame is a broadcast.
        if (h.sub == 'P' || h.sub == 'R')
            return Kind::Other;
        if (f.size() == 20)
            parseBattery20(f);
        else if (f.size() >= 17)
            parseBatteryShort(f);
        else
            return Kind::Unknown;
        return Kind::Battery;
    case 'm':
        if (h.sub == 'Z') {
            if (f.size() >= 20) {
                if (m_protocol == Protocol::V100)
                    parseMotorV100(f);
                else
                    parseMotor20(f);
            } else if (f.size() >= 18) {
                parseMotorShort(f);
            } else {
                return Kind::Unknown;
            }
            return Kind::Motor;
        }
        if (h.sub == 'A') {
            if (f.size() == 10) {
                const int lo = numericValue(f[5]), hi = numericValue(f[6]), cur = numericValue(f[7]);
                if (lo < 0 || hi < 0 || cur < 0)
                    return Kind::Unknown;
                m_assist = AssistRange{ lo, hi, cur };
                return Kind::Assist;
            }
            if (f.size() == 9) {
                m_lastAck.assign(f.begin() + 5, f.begin() + 7);
                return Kind::AssistAck;
            }
            return Kind::Unknown;
        }
        if (h.sub == 'M') {
            if (f.size() == 9) {
                m_lastMapAck.assign(f.begin() + 5, f.begin() + 7);
                return Kind::MapAck;
            }
            if (f.size() >= 20) {
                MapEntry e;
                e.raw = f;
                e.writeFlag = f[5] == 'W';
                if (f[6] < '0' || f[6] > '9')
                    return Kind::Unknown;
                e.map = f[6] - '0';
                // Pairs at 7..15; a power byte of 255 means "no point". A percentage map has none.
                for (size_t i = 7; i <= 15; i += 2)
                    if (f[i + 1] < 255)
                        e.points.emplace_back(f[i], f[i + 1]);
                e.percent = f[17];
                m_maps[e.map] = e;
                return Kind::MapData;
            }
        }
        return Kind::Other; // trip reset ack, traction control
    case 'j':
        if (h.sub != 'Z')
            return Kind::Unknown;
        if (f.size() >= 20) {
            parseEbm20(f, ebmIsV200(f));
        } else if (f.size() >= 17) {
            parseEbmShort(f);
        } else {
            return Kind::Unknown;
        }
        return Kind::Ebm;
    case 's':
        if (h.sub == 'V') {
            if (f.size() == 24) {
                m_vin.assign(f.begin() + 5, f.end() - 2);
                return Kind::Vin;
            }
            return Kind::Unknown;
        }
        if (h.sub == 'P') {
            if (f.size() < 7)
                return Kind::Unknown;
            const std::string version(f.begin() + 5, f.end() - 2);
            if (version.empty() || version == "ER")
                return Kind::Unknown;
            setProtocolVersion(version);
            return Kind::ProtocolReply;
        }
        return Kind::Other; // blinking lights ($s$L), clock ack ($s$T)
    case 'd':
        if (h.sub == 'I' || h.sub == 'E') {
            // "$d$I#OK#@" / "$d$E#OK#@": the answer is the two characters at 5..6.
            if (f.size() != 9)
                return Kind::Unknown;
            const bool ok = f[5] == 'O' && f[6] == 'K';
            (h.sub == 'I' ? m_diagInit : m_diagEnd) = ok ? 1 : 0;
            return h.sub == 'I' ? Kind::DiagInitAck : Kind::DiagEndAck;
        }
        if (h.sub == 'R') {
            // "$d$R#<device><type>#<data>#@"; a refusal has "ER" from byte 7 on.
            if (f.size() < 9)
                return Kind::Unknown;
            DiagEntry e;
            e.device = static_cast<char>(f[5]);
            if (f[6] < '0' || f[6] > '9')
                return Kind::Unknown;
            e.type = f[6] - '0';
            if (f.size() == 11 && f[7] == 'E' && f[8] == 'R') {
                e.error = true;
            } else if (f.size() > 10) {
                e.data.assign(f.begin() + 8, f.end() - 2);
            }
            m_diag.push_back(e);
            return Kind::DiagData;
        }
        return Kind::Other;
    case 'f': case 'i': case 'M':
        return Kind::Other;
    default:
        return Kind::Unknown;
    }
}

void Parser::storeBattery(Battery b)
{
    if (b.number == 2) {
        m_primaryRun = 0;
        m_secondBatteryAlive = true;
        m_battery2 = b;
        return;
    }
    // Anything that is not an explicit second pack is the main one: a single-battery bike whose
    // firmware leaves the field at 0 would otherwise show nothing.
    b.number = 1;
    m_battery = b;
    if (++m_primaryRun >= 4)
        m_secondBatteryAlive = false;
}

void Parser::parseBatteryShort(const Bytes &f)
{
    Battery b;
    b.voltage = u16(f, 5) / 10.0;
    b.soc = u8(f, 7);
    b.temperature = s8(f, 8);
    b.current = u16(f, 9) / 10.0; // unsigned in this layout, as the SDK reads it
    b.nominalCapacityWh = u16(f, 11) / 10.0;
    b.remainingWh = u16(f, 13) / 10.0;
    int number = 1;
    if (f.size() >= 19) {
        const int combined = u16(f, 15);
        if (combined) {
            number = combined / 10000;
            b.cycles = combined % 10000;
        }
    }
    b.number = number;
    storeBattery(b);
}

void Parser::parseBattery20(const Bytes &f)
{
    Battery b;
    b.voltage = u16(f, 5) / 100.0;
    b.temperature = s8(f, 8);
    b.mosfetTemperature = s8(f, 15);
    b.current = s16(f, 9) / 10.0;
    b.nominalCapacityWh = u16(f, 11) / 10.0;
    b.remainingWh = u16(f, 13) / 10.0;
    const int combined = u16(f, 16);
    b.cycles = combined % 10000;
    b.number = combined / 10000;

    const int socRaw = u8(f, 7);
    if (m_protocol == Protocol::V200) {
        // Number 0 is the auxiliary (DC/DC) current, not a pack: letting it through would overwrite the real one.
        if (b.number == 0)
            return;
        b.soc = socRaw & 0x7f;
        b.charging = (socRaw & 0x80) && b.number == 1;
    } else {
        b.soc = socRaw; // read whole: these protocols have no charging flag
    }
    storeBattery(b);
}

void Parser::parseMotorShort(const Bytes &f)
{
    Motor m;
    m.assistLevel = s8(f, 5);
    m.temperature = s8(f, 6);
    m.currentAmp = u16(f, 7) / 10.0;
    m.speedKmh = u16(f, 9) / 10.0;
    m.wheelRpm = u8(f, 11);
    m.torquePct = s8(f, 12);
    m.maxCurrentAmp = u16(f, 13) / 10.0;
    m.maxTorque = s8(f, 15);
    m_motor = m;
}

void Parser::parseMotorV100(const Bytes &f)
{
    Motor m;
    m.assistLevel = s8(f, 5);
    m.temperature = s8(f, 6);
    m.currentAmp = u16(f, 7) / 10.0;
    m.speedKmh = u16(f, 9) / 10.0;
    m.wheelRpm = u8(f, 11);
    m.torquePct = s16(f, 12) / 100; // truncates toward zero, like the SDK's integer division
    m.maxCurrentAmp = u16(f, 14) / 10.0;
    m.maxTorque = u16(f, 16);
    m_motor = m;
}

void Parser::parseMotor20(const Bytes &f)
{
    Motor m;
    m.assistLevel = s8(f, 5);
    m.temperature = s8(f, 6);
    m.motorPowerW = u16(f, 7) / 100.0;
    m.speedKmh = u16(f, 9) / 10.0;
    m.maxCurrentAmp = u16(f, 14) / 10.0;
    m.maxTorque = u16(f, 16);
    // The torque doubles as the validity flag of the wheel speed: 0 means no data.
    if (m.maxTorque != 0)
        m.wheelRpm = u8(f, 11);
    m.riderPowerW = u16(f, 12) / 10.0;
    m_motor = m;
}

bool Parser::ebmIsV200(const Bytes &f) const
{
    if (m_protocol != Protocol::Unknown)
        return m_protocol == Protocol::V200;
    return !hasHijMarker(f);
}

void Parser::parseEbmShort(const Bytes &f)
{
    Ebm e;
    e.lifetimeKnown = true;
    e.odometerKm = u32(f, 5) / 10000.0;
    e.rangeKm = u32(f, 9) / 10000.0;
    e.light = f[13] == 1;
    e.status = u8(f, 14);
    e.errorCode = e.status;
    m_ebm = e;
}

void Parser::parseEbm20(const Bytes &f, bool v200)
{
    const double odo = u24(f, 5) / 10.0;
    const double range = u16(f, 8) / 10.0;
    const int slot = v200 ? u8(f, 15) : u8(f, 14);

    // Consecutive frames alternate: slot 2 carries trip A, anything else the lifetime totals.
    // Whichever set a frame does not carry is kept from the previous one.
    if (!m_ebm)
        m_ebm = Ebm();
    Ebm &e = *m_ebm;
    if (slot == 2) {
        e.tripKm = odo;
        e.tripRangeKm = range;
    } else {
        e.lifetimeKnown = true;
        e.odometerKm = odo;
        e.rangeKm = range;
    }
    e.light = f[10] == 1;
    if (v200) {
        const int errorId = u8(f, 11) & 0x1f;
        e.status = errorId;
        e.errorCode = deviceBase(u8(f, 12) & 0x0f) + errorId;
        e.accelZ = s8(f, 13);
        e.accelY = s8(f, 14);
        const bool remote = !(f[16] == 0xff && f[17] == 0xff);
        e.remoteConnected = remote;
        e.remoteSoc = remote ? std::optional<int>(u8(f, 17)) : std::nullopt;
    } else {
        e.status = u8(f, 11);
        e.errorCode = e.status;
        e.accelZ = s8(f, 12);
        e.accelY = s8(f, 13);
        e.remoteConnected = std::nullopt;
        e.remoteSoc = std::nullopt;
    }
}

} // namespace Mahle

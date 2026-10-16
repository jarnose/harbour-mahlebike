#pragma once

// The Mahle SmartBike / ebikemotion BLE protocol, as spoken by the iWoc* and HUS* drive systems.
//
// Qt-free on purpose: this is built and tested with plain g++ (tests/run.sh), because a decoder is
// exactly the kind of code where "it compiles" proves nothing and the bike is a slow place to find
// out.
//
// Sources: the layouts follow the Home Assistant integration ReneNulschDE/ha-mysmartbike_ble, and
// were checked against the decompiled Mahle SDK inside the official My SmartBike app 3.0.5
// (com.mahle.protocol.*: the commands, the frame parsers per protocol version, and the GATT
// service in com.mahle.refactored.gatt). docs/protocol.md lists what is verified and what is not.
//
// Every frame is ASCII-framed: "$" <type> "$" <subtype> "#" <payload> "#@". Frames the bike
// broadcasts use an upper-case Z subtype with a lower-case type ("$b$Z#", "$m$Z#", "$j$Z#"); the
// replies to our commands echo the command with a lower-case type ("$s$V#" answers "$S$V#@").
// All multi-byte numbers are big-endian.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <string>
#include <vector>

namespace Mahle {

using Bytes = std::vector<uint8_t>;

// GATT. Service FFE0: FFE2 is written, FFD1 notifies the frames, FFE1 notifies the handlebar
// buttons ("EBM Button Read" in the SDK; its payload is not decoded here).
constexpr const char *kServiceUuid = "0000ffe0-0000-1000-8000-00805f9b34fb";
constexpr const char *kWriteCharUuid = "0000ffe2-0000-1000-8000-00805f9b34fb";
constexpr const char *kNotifyCharUuid = "0000ffd1-0000-1000-8000-00805f9b34fb";
constexpr const char *kButtonCharUuid = "0000ffe1-0000-1000-8000-00805f9b34fb";

// The official app's own scan filter: a device is a bike when it advertises FFE0 or its MAC begins
// with one of these two vendor prefixes. Names start with "iWoc" (X25/X35+) or "HUS" (X20).
constexpr const char *kMacPrefixes[] = { "00:1E:C0", "D8:80:39" };

// The protocol generation the bike reports for "$S$P#@". It selects the layout of the broadcast
// frames: length alone does not identify it, because 100, 102, 200 and 300 all broadcast 20-byte
// frames that read some bytes differently.
enum class Protocol { Unknown, Ebm, V100, V102, V200, V300 };

// "1.02" and "102" are the same protocol; anything unrecognised is Unknown.
Protocol protocolFromVersion(const std::string &version);
const char *protocolName(Protocol p);

// ---- commands we may write -----------------------------------------------------------------

Bytes cmdVin();                       // $S$V#@
Bytes cmdProtocol();                  // $S$P#@
// $M$A#<level>#@. The official app sends the level it wants (its +/- buttons send current +- 1) and the
// bike answers with $m$A#<min><max><current>#@, or $m$A#OK#@. There is deliberately no "read only" form:
// the SDK has one command for both, and a level of 0 is a real level, so asking with 0 could switch the
// assistance off. The current level is in every motor frame anyway.
Bytes cmdAssistSet(int level);
Bytes cmdResetTrip();                 // $M$R#@

// Motor maps ("Eco", "Sport" ...). On an X35-type bike there is one map per assist level 1..3 and each is
// a single power percentage. Both commands are 20 bytes: "$M$M#" + R or W + the map number as a digit,
// ten zero bytes, a value byte (zero in a read) and "#@". A read of map 0 makes the bike send every
// map; the official app only ever asks for 0. The bike answers a write with "$m$M#OK#@" (or "ER").
Bytes cmdMapRead(int map);
Bytes cmdMapWritePercent(int map, int percent);
// What the bike is actually told for a percentage: the official app swaps 35 and 64, which are the
// characters '#' and '@' and would be read as the end of the frame, for 34 and 65.
int mapSafePercent(int percent);
Bytes cmdSetClock(int hour, int minute); // $S$T#HH:mm#@

// The official app's end-of-session command. Starting a diagnosis session ($D$I#@) is not
// something this app does; see docs/protocol.md.

// ---- decoded data --------------------------------------------------------------------------

struct Battery
{
    double voltage = 0;       // V
    int soc = 0;              // %
    int temperature = 0;      // deg C
    std::optional<int> mosfetTemperature; // deg C, 20-byte frames only
    double current = 0;       // A, positive = discharging as far as the bike's sign convention goes
    double nominalCapacityWh = 0;
    double remainingWh = 0;
    std::optional<int> cycles;
    bool charging = false;    // protocol 200 only
    int number = 1;           // 1 = main pack, 2 = the range extender
};

struct Motor
{
    int assistLevel = 0;
    int temperature = 0;      // deg C; -40 is "no sensor reading yet"
    bool temperatureValid() const { return temperature != -40; }
    std::optional<double> currentAmp;     // EBM and 100
    std::optional<double> motorPowerW;    // 102 and later
    double speedKmh = 0;
    std::optional<int> wheelRpm;
    std::optional<int> torquePct;         // EBM and 100
    std::optional<double> riderPowerW;    // 102 and later
    double maxCurrentAmp = 0;
    int maxTorque = 0;
};

struct Ebm
{
    bool lifetimeKnown = false; // a lifetime frame has been seen (20-byte frames alternate with trip A)
    double odometerKm = 0;
    double rangeKm = 0;
    std::optional<double> tripKm;         // trip A, 20-byte frames only
    std::optional<double> tripRangeKm;
    bool light = false;
    int status = 0;           // the error id the bike reports, 0 = fine
    int errorCode = 0;        // the number the official app shows: device base + id
    std::optional<int> accelY;
    std::optional<int> accelZ;
    std::optional<bool> remoteConnected;
    std::optional<int> remoteSoc;
};

// One motor map as the bike reports it.
struct MapEntry
{
    int map = 0;
    bool writeFlag = false;     // the reply's R / W byte
    int percent = 0;            // byte 17: the power percentage of an X35-type map
    // (speed km/h, power %) pairs of a curve-type map; empty for a percentage map.
    std::vector<std::pair<int, int>> points;
    Bytes raw;
    bool isPercent() const { return points.empty(); }
};

struct AssistRange
{
    int min = 0;
    int max = 0;
    int current = 0;
};

// What a frame turned out to be.
enum class Kind {
    Unknown,
    Battery,
    Motor,
    Ebm,
    Assist,       // $m$A#<min><max><cur>#@
    MapData,      // $m$M#: one motor map
    MapAck,       // $m$M#OK#@ or $m$M#ER#@
    AssistAck,    // $m$A#OK#@ (or another word: see ackOk)
    Vin,
    ProtocolReply,
    Other,        // recognised, deliberately not decoded (diagnosis, coding, trio, ...)
    Partial,      // the first part of a text reply; the rest comes in the next notification
};

// A short text for the diagnostics page: "battery", "motor", "ebm", ...
const char *kindName(Kind k);

// Names of the device a protocol-200 error belongs to, from the number the app shows.
const char *errorDeviceName(int code);

// Turns a stream of notifications into state.
//
// A notification is normally one frame, but a reply longer than the link's payload (the 24-byte
// VIN reply on a link that has not negotiated a larger MTU) arrives in pieces: a text reply that
// does not end in "#@" is held and completed by the following notification.
class Parser
{
public:
    // Feeds one notification. Returns what it decoded; `Kind::Unknown` for noise.
    Kind feed(const uint8_t *data, size_t size);
    Kind feed(const Bytes &data) { return feed(data.data(), data.size()); }

    void reset();

    Protocol protocol() const { return m_protocol; }
    // The version text exactly as the bike reported it.
    const std::string &protocolVersion() const { return m_protocolVersion; }
    void setProtocolVersion(const std::string &version);
    const std::string &vin() const { return m_vin; }

    const std::optional<Battery> &battery() const { return m_battery; }
    const std::optional<Battery> &battery2() const { return m_battery2; }
    const std::optional<Motor> &motor() const { return m_motor; }
    const std::optional<Ebm> &ebm() const { return m_ebm; }
    const std::optional<AssistRange> &assist() const { return m_assist; }
    // The last $m$A#xx#@ acknowledgement, empty until one arrives.
    const std::string &lastAck() const { return m_lastAck; }

    // Motor maps received since the last clearMaps(), by map number.
    const std::map<int, MapEntry> &maps() const { return m_maps; }
    void clearMaps() { m_maps.clear(); m_lastMapAck.clear(); }
    // The last "$m$M#OK#@" / "ER" answer to a map write, empty until one arrives.
    const std::string &lastMapAck() const { return m_lastMapAck; }

    // Whether a second battery is installed. A battery_number == 2 frame says yes; four main-pack
    // frames in a row without one say no.
    bool hasSecondBattery() const { return m_battery2.has_value() && m_secondBatteryAlive; }

private:
    Kind dispatch(const Bytes &frame);
    void parseBatteryShort(const Bytes &f);
    void parseBattery20(const Bytes &f);
    void storeBattery(Battery b);
    void parseMotorShort(const Bytes &f);
    void parseMotorV100(const Bytes &f);
    void parseMotor20(const Bytes &f);
    void parseEbmShort(const Bytes &f);
    void parseEbm20(const Bytes &f, bool v200);
    bool ebmIsV200(const Bytes &f) const;

    Protocol m_protocol = Protocol::Unknown;
    std::string m_protocolVersion;
    std::string m_vin;
    std::optional<Battery> m_battery;
    std::optional<Battery> m_battery2;
    std::optional<Motor> m_motor;
    std::optional<Ebm> m_ebm;
    std::optional<AssistRange> m_assist;
    std::string m_lastAck;
    std::map<int, MapEntry> m_maps;
    std::string m_lastMapAck;
    int m_primaryRun = 0;
    bool m_secondBatteryAlive = false;
    Bytes m_pending; // the start of a text reply still waiting for its tail
};

// The bytes of a frame as text, with anything unprintable shown as \xNN. For logs.
std::string printable(const Bytes &frame);

} // namespace Mahle

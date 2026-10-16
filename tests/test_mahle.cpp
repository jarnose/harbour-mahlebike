// The Mahle frame decoders and commands. The byte vectors are real captures published in the test
// suite of ReneNulschDE/ha-mysmartbike_ble (an Orbea Vibe, a Schindelhauer Arthur IX and a HUS/X20
// bike); the expected numbers are the ones that suite checked against the official app.
#include "hex.h"
#include "protocol/mahle.h"

#include <cmath>

using namespace Mahle;

static bool near(double a, double b, double eps = 0.011) { return std::fabs(a - b) < eps; }

static std::string str(const Bytes &b) { return std::string(b.begin(), b.end()); }

static Parser parserFor(const char *version)
{
    Parser p;
    p.setProtocolVersion(version);
    return p;
}

static void versions()
{
    CHECK(protocolFromVersion("1.02") == Protocol::V102);
    CHECK(protocolFromVersion("102") == Protocol::V102);
    CHECK(protocolFromVersion("1.00") == Protocol::V100);
    CHECK(protocolFromVersion("2.00") == Protocol::V200);
    CHECK(protocolFromVersion("3.00") == Protocol::V300);
    CHECK(protocolFromVersion("ebm") == Protocol::Ebm);
    CHECK(protocolFromVersion("EBM") == Protocol::Ebm);
    CHECK(protocolFromVersion("") == Protocol::Unknown);
    CHECK(protocolFromVersion("CONF") == Protocol::Unknown);
    CHECK(protocolFromVersion("9.99") == Protocol::Unknown);

    Parser p;
    CHECK(p.feed(fromHex("24732450" "23" "312e3032" "2340")) == Kind::ProtocolReply); // $s$P#1.02#@
    CHECK(p.protocol() == Protocol::V102);
    CHECK_EQ(p.protocolVersion(), std::string("1.02"));
    // "ER" is the bike's refusal, not a version.
    Parser q;
    CHECK(q.feed(fromHex("24732450" "23" "4552" "2340")) == Kind::Unknown);
    CHECK(q.protocol() == Protocol::Unknown);
}

static void commands()
{
    CHECK_EQ(toHex(cmdVin()), std::string("245324562340"));        // $S$V#@
    CHECK_EQ(toHex(cmdProtocol()), std::string("245324502340"));   // $S$P#@
    CHECK_EQ(str(cmdAssistSet(3)), std::string("$M$A#3#@"));
    CHECK_EQ(str(cmdResetTrip()), std::string("$M$R#@"));
    const Bytes clock = cmdSetClock(7, 5);
    CHECK_EQ(str(clock), std::string("$S$T#07:05#@"));
}

static void batteryShort()
{
    Parser p;
    CHECK(p.feed(fromHex("2462245a230193541700000877071a27342340")) == Kind::Battery);
    const Battery &b = *p.battery();
    CHECK(near(b.voltage, 40.3));
    CHECK_EQ(b.soc, 84);
    CHECK_EQ(b.temperature, 23);
    CHECK(near(b.current, 0));
    CHECK(near(b.nominalCapacityWh, 216.7));
    CHECK(near(b.remainingWh, 181.8));
    CHECK(b.cycles.has_value() && *b.cycles == 36);
    CHECK(!b.mosfetTemperature.has_value());
    CHECK_EQ(b.number, 1);

    // The temperature byte is signed; the current in this layout is not.
    Bytes f = fromHex("2462245a230193541700000877071a27342340");
    f[8] = 0xf6;
    f[9] = 0xff;
    f[10] = 0xec;
    Parser q;
    q.feed(f);
    CHECK_EQ(q.battery()->temperature, -10);
    CHECK(near(q.battery()->current, 6551.6, 0.1));
}

static void battery20()
{
    const Bytes frame = fromHex("2462245a230ec2391600000dc807d31827152340");
    CHECK_EQ(frame.size(), size_t(20));
    Parser p;
    CHECK(p.feed(frame) == Kind::Battery);
    const Battery &b = *p.battery();
    CHECK(near(b.voltage, 37.78));
    CHECK_EQ(b.soc, 57);
    CHECK(!b.charging);
    CHECK_EQ(b.temperature, 22);
    CHECK(near(b.nominalCapacityWh, 352.8));
    CHECK(near(b.remainingWh, 200.3));
    CHECK(b.mosfetTemperature.has_value() && *b.mosfetTemperature == 24);
    CHECK(b.cycles.has_value() && *b.cycles == 5);
    CHECK_EQ(b.number, 1);

    // The 20-byte current is signed.
    Bytes f = frame;
    f[9] = 0xff;
    f[10] = 0xec;
    Parser q;
    q.feed(f);
    CHECK(near(q.battery()->current, -2.0, 0.001));

    // Bit 7 of the SOC byte is a charging flag on 200 only; the other protocols read the byte whole.
    f = frame;
    f[7] = 0xb9; // 0x80 | 57
    Parser v200 = parserFor("200");
    v200.feed(f);
    CHECK_EQ(v200.battery()->soc, 57);
    CHECK(v200.battery()->charging);
    Parser v102 = parserFor("102");
    v102.feed(f);
    CHECK_EQ(v102.battery()->soc, 0xb9);
    CHECK(!v102.battery()->charging);

    // Battery number 0 on 200 is the auxiliary current: it must not replace the main pack.
    Bytes aux = frame;
    aux[16] = 0x00;
    aux[17] = 0x05;
    Parser v200b = parserFor("200");
    v200b.feed(frame);
    v200b.feed(aux);
    CHECK_EQ(v200b.battery()->soc, 57);
}

static void secondBattery()
{
    Parser p;
    const Bytes main = fromHex("2462245a230ec2391600000dc807d31827152340");
    Bytes second = main;
    second[16] = 0x4e; // 20005 -> pack 2, 5 cycles
    second[17] = 0x25;
    p.feed(second);
    CHECK(p.hasSecondBattery());
    for (int i = 0; i < 4; ++i)
        p.feed(main);
    CHECK(!p.hasSecondBattery());
    p.feed(second);
    CHECK(p.hasSecondBattery());
}

static void motorShort()
{
    Parser p;
    CHECK(p.feed(fromHex("246d245a230117000000000000004f642340")) == Kind::Motor);
    const Motor &m = *p.motor();
    CHECK_EQ(m.assistLevel, 1);
    CHECK_EQ(m.temperature, 23);
    CHECK(m.currentAmp.has_value());
    CHECK(!m.motorPowerW.has_value());

    Bytes f = fromHex("246d245a230117000000000000004f642340");
    f[6] = 0xd8;
    f[12] = 0xff;
    f[15] = 0xfb;
    Parser q;
    q.feed(f);
    CHECK_EQ(q.motor()->temperature, -40);
    CHECK(!q.motor()->temperatureValid());
    CHECK_EQ(*q.motor()->torquePct, -1);
    CHECK_EQ(q.motor()->maxTorque, -5);
}

static void motor20()
{
    const Bytes frame = fromHex("246d245a23011600000000000000005a03ff2340");
    Parser p;
    CHECK(p.feed(frame) == Kind::Motor);
    const Motor &m = *p.motor();
    CHECK_EQ(m.assistLevel, 1);
    CHECK_EQ(m.temperature, 22);
    CHECK(near(m.speedKmh, 0));
    CHECK(m.motorPowerW.has_value() && near(*m.motorPowerW, 0));
    CHECK(m.riderPowerW.has_value());
    CHECK(near(m.maxCurrentAmp, 9.0));
    CHECK_EQ(m.maxTorque, 0x03ff);
    CHECK(m.wheelRpm.has_value() && *m.wheelRpm == 0);
    CHECK(!m.currentAmp.has_value());

    // No torque, no wheel speed.
    Bytes f = frame;
    f[16] = 0;
    f[17] = 0;
    f[11] = 0x2a;
    Parser q = parserFor("102");
    q.feed(f);
    CHECK(!q.motor()->wheelRpm.has_value());

    // 102, 200 and 300 read it the same way.
    Parser a = parserFor("102"), b = parserFor("200"), c = parserFor("300");
    a.feed(frame);
    b.feed(frame);
    c.feed(frame);
    CHECK(near(a.motor()->maxCurrentAmp, b.motor()->maxCurrentAmp));
    CHECK(near(b.motor()->maxCurrentAmp, c.motor()->maxCurrentAmp));
}

static void motorV100()
{
    const Bytes frame = fromHex("246d245a2301160096000000ff38005a03ff2340");
    Parser p = parserFor("100");
    p.feed(frame);
    const Motor &m = *p.motor();
    CHECK(m.currentAmp.has_value() && near(*m.currentAmp, 15.0));
    CHECK(!m.motorPowerW.has_value());
    CHECK(m.torquePct.has_value() && *m.torquePct == -2); // -200 / 100
    CHECK(!m.riderPowerW.has_value());
    CHECK(near(m.maxCurrentAmp, 9.0));
    CHECK_EQ(m.maxTorque, 0x03ff);

    // The same bytes under 102 are a power of 1.5 W: the version has to decide, not the length.
    Parser q = parserFor("102");
    q.feed(frame);
    CHECK(near(*q.motor()->motorPowerW, 1.5));

    // And v100 does not gate the wheel speed on the torque.
    Bytes f = frame;
    f[11] = 0x2a;
    f[16] = 0;
    f[17] = 0;
    Parser r = parserFor("100");
    r.feed(f);
    CHECK(r.motor()->wheelRpm.has_value() && *r.motor()->wheelRpm == 42);
}

static void ebmShort()
{
    Parser p;
    CHECK(p.feed(fromHex("246a245a230056af68000bef6f00002340")) == Kind::Ebm);
    const Ebm &e = *p.ebm();
    CHECK(e.lifetimeKnown);
    CHECK(near(e.odometerKm, 568.1, 0.1));
    CHECK(near(e.rangeKm, 78.2, 0.1));
    CHECK(!e.light);
    CHECK_EQ(e.errorCode, 0);
}

static void ebm20()
{
    const Bytes life = fromHex("246a245a2300008402e1000001f50148494a2340");
    const Bytes trip = fromHex("246a245a2300008402e1000001f50248494a2340");
    Parser p = parserFor("102");
    p.feed(life);
    CHECK(near(p.ebm()->odometerKm, 13.2, 0.05));
    CHECK(near(p.ebm()->rangeKm, 73.7, 0.05));
    CHECK(!p.ebm()->light);
    CHECK(!p.ebm()->tripKm.has_value());
    CHECK_EQ(*p.ebm()->accelZ, 1);
    CHECK_EQ(*p.ebm()->accelY, -11);

    // A trip slot updates the trip only, and a lifetime slot keeps the trip.
    Bytes t = trip;
    t[5] = 0;
    t[6] = 0;
    t[7] = 0x50;
    p.feed(t);
    CHECK(near(*p.ebm()->tripKm, 8.0, 0.001));
    CHECK(near(p.ebm()->odometerKm, 13.2, 0.05));
    p.feed(life);
    CHECK(near(*p.ebm()->tripKm, 8.0, 0.001));

    // A trip frame first leaves the lifetime unknown instead of showing 0 km.
    Parser q = parserFor("102");
    q.feed(trip);
    CHECK(!q.ebm()->lifetimeKnown);
    CHECK(near(*q.ebm()->tripKm, 13.2, 0.05));

    // Lights, and a bare error byte on 100/102/300.
    Bytes f = life;
    f[10] = 1;
    f[11] = 0x2c;
    Parser r = parserFor("102");
    r.feed(f);
    CHECK(r.ebm()->light);
    CHECK_EQ(r.ebm()->status, 0x2c);
    CHECK_EQ(r.ebm()->errorCode, 0x2c);
    CHECK(!r.ebm()->remoteConnected.has_value());
}

static void ebmV200()
{
    const Bytes life = fromHex("246a245a2300008402e100242201f501ffff2340");
    const Bytes trip = fromHex("246a245a2300008402e100242201f502ffff2340");
    Parser p = parserFor("200");
    p.feed(life);
    CHECK(near(p.ebm()->odometerKm, 13.2, 0.05));
    CHECK(near(p.ebm()->rangeKm, 73.7, 0.05));
    CHECK_EQ(p.ebm()->status, 4);     // 0x24 & 0x1f
    CHECK_EQ(p.ebm()->errorCode, 204); // device 2 = internal battery
    CHECK_EQ(std::string(errorDeviceName(p.ebm()->errorCode)), std::string("internal battery"));
    CHECK_EQ(*p.ebm()->accelZ, 1);
    CHECK_EQ(*p.ebm()->accelY, -11);
    CHECK(p.ebm()->remoteConnected.has_value() && !*p.ebm()->remoteConnected);
    p.feed(trip);
    CHECK(p.ebm()->tripKm.has_value());

    // Device 0 is the drive unit: the bare id.
    Bytes f = life;
    f[12] = 0x20;
    Parser q = parserFor("200");
    q.feed(f);
    CHECK_EQ(q.ebm()->errorCode, 4);

    // A remote pairs: the second byte is its charge.
    f = life;
    f[16] = 0x00;
    f[17] = 0x55;
    Parser r = parserFor("200");
    r.feed(f);
    CHECK(*r.ebm()->remoteConnected);
    CHECK_EQ(*r.ebm()->remoteSoc, 0x55);
}

static void ebmLayoutBeforeTheHandshake()
{
    // Before the bike has named its protocol the HIJ marker tells the layouts apart.
    Parser a;
    a.feed(fromHex("246a245a2300008402e1000001f50248494a2340")); // marker: 100/102/300, slot 2 = trip
    CHECK(a.ebm()->tripKm.has_value());
    Parser b;
    b.feed(fromHex("246a245a2300008402e100242201f502ffff2340")); // no marker: 200, slot 2 = trip
    CHECK(b.ebm()->tripKm.has_value());
    CHECK_EQ(b.ebm()->status, 4);
}

static void assist()
{
    Parser p;
    CHECK(p.feed(fromHex("246d2441233033312340")) == Kind::Assist); // $m$A#031#@
    CHECK_EQ(p.assist()->min, 0);
    CHECK_EQ(p.assist()->max, 3);
    CHECK_EQ(p.assist()->current, 1);
    CHECK(p.feed(fromHex("246d2441234f4b2340")) == Kind::AssistAck); // $m$A#OK#@
    CHECK_EQ(p.lastAck(), std::string("OK"));
}

static void vin()
{
    Parser p;
    const std::string vin = "WMH12345678901234"; // 17 characters
    std::string frame = "$s$V#" + vin + "#@";
    Bytes b(frame.begin(), frame.end());
    CHECK_EQ(b.size(), size_t(24));
    CHECK(p.feed(b) == Kind::Vin);
    CHECK_EQ(p.vin(), vin);

    // A link that has not negotiated a bigger payload splits it in two.
    Parser q;
    CHECK(q.feed(Bytes(b.begin(), b.begin() + 20)) == Kind::Partial);
    CHECK(q.feed(Bytes(b.begin() + 20, b.end())) == Kind::Vin);
    CHECK_EQ(q.vin(), vin);

    // A new frame discards a tail that never came.
    Parser r;
    r.feed(Bytes(b.begin(), b.begin() + 20));
    CHECK(r.feed(fromHex("246d245a230117000000000000004f642340")) == Kind::Motor);

    // The other spelling.
    Parser s;
    const std::string alt = "R0" + vin + "@";
    CHECK(s.feed(Bytes(alt.begin(), alt.end())) == Kind::Vin);
    CHECK_EQ(s.vin(), vin);
}

static void motorMaps()
{
    // The commands are 20 bytes: "$M$M#" R/W, the map digit, ten zeros, the value, "#@".
    CHECK_EQ(toHex(cmdMapRead(0)), std::string("244d244d2352300000000000000000000000" "2340"));
    CHECK_EQ(toHex(cmdMapWritePercent(2, 42)), std::string("244d244d235732" "00000000000000000000" "2a" "2340"));
    CHECK_EQ(cmdMapRead(3).size(), size_t(20));
    // 35 and 64 are '#' and '@': the official app writes 34 and 65.
    CHECK_EQ(mapSafePercent(35), 34);
    CHECK_EQ(mapSafePercent(64), 65);
    CHECK_EQ(mapSafePercent(30), 30);
    CHECK_EQ(cmdMapWritePercent(1, 35)[17], uint8_t(34));
    CHECK_EQ(cmdMapWritePercent(1, 64)[17], uint8_t(65));

    // A percentage map: no curve points (power bytes 255), the value in byte 17.
    Bytes reply(20, 0xff);
    reply[0] = '$'; reply[1] = 'm'; reply[2] = '$'; reply[3] = 'M'; reply[4] = '#';
    reply[5] = 'R'; reply[6] = '2'; reply[17] = 70; reply[18] = '#'; reply[19] = '@';
    Parser p;
    CHECK(p.feed(reply) == Kind::MapData);
    CHECK_EQ(p.maps().size(), size_t(1));
    CHECK(p.maps().at(2).isPercent());
    CHECK_EQ(p.maps().at(2).percent, 70);
    CHECK(!p.maps().at(2).writeFlag);

    // A curve map has (speed, power) pairs.
    Bytes curve = reply;
    curve[7] = 10; curve[8] = 50; curve[9] = 25; curve[10] = 100;
    Parser q;
    q.feed(curve);
    CHECK(!q.maps().at(2).isPercent());
    CHECK_EQ(q.maps().at(2).points.size(), size_t(2));
    CHECK_EQ(q.maps().at(2).points[1].first, 25);

    CHECK(p.feed(fromHex("246d244d234f4b2340")) == Kind::MapAck);
    CHECK_EQ(p.lastMapAck(), std::string("OK"));
    CHECK(p.feed(fromHex("246d244d2345522340")) == Kind::MapAck);
    CHECK_EQ(p.lastMapAck(), std::string("ER"));
    p.clearMaps();
    CHECK(p.maps().empty() && p.lastMapAck().empty());

    // A map number that is not a digit is not a map.
    Bytes bad = reply;
    bad[6] = 'x';
    CHECK(Parser().feed(bad) == Kind::Unknown);
}

static void noise()
{
    Parser p;
    CHECK(p.feed(Bytes()) == Kind::Unknown);
    CHECK(p.feed(fromHex("00")) == Kind::Unknown);
    CHECK(p.feed(fromHex("2462245a2301")) == Kind::Unknown);       // no terminator
    CHECK(p.feed(fromHex("2462245a232340")) == Kind::Unknown);     // too short for a battery
    CHECK(p.feed(fromHex("2442245a2340")) == Kind::Unknown);       // unknown type
    CHECK(p.feed(fromHex("24642449" "23" "2340")) == Kind::Other); // $d$I: diagnosis, not decoded
    CHECK(p.feed(fromHex("24622450" "23" "2340")) == Kind::Other); // $b$P: power source, not a battery
    CHECK(!p.battery().has_value());
    CHECK_EQ(printable(fromHex("24ff00")), std::string("$\\xff\\x00"));
}

int main()
{
    versions();
    commands();
    batteryShort();
    battery20();
    secondBattery();
    motorShort();
    motor20();
    motorV100();
    ebmShort();
    ebm20();
    ebmV200();
    ebmLayoutBeforeTheHandshake();
    assist();
    motorMaps();
    vin();
    noise();
    return finish("mahle");
}

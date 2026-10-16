// The pretend bike: every frame it makes must decode back to what it meant, and its answers must
// be the ones the real bike gives.
#include "hex.h"
#include "protocol/demo.h"

#include <cmath>

using namespace Mahle;

static bool near(double a, double b, double eps = 0.06) { return std::fabs(a - b) < eps; }

static void encoders()
{
    Parser p;
    p.setProtocolVersion("1.02");
    CHECK(p.feed(Demo::encodeBattery20(37.78, 57, 22, 24, -2.0, 352.8, 200.3, 1, 5)) == Kind::Battery);
    CHECK(near(p.battery()->voltage, 37.78, 0.006));
    CHECK_EQ(p.battery()->soc, 57);
    CHECK(near(p.battery()->current, -2.0));
    CHECK(near(p.battery()->remainingWh, 200.3));
    CHECK_EQ(*p.battery()->cycles, 5);
    CHECK_EQ(*p.battery()->mosfetTemperature, 24);

    // The encoder reproduces the real capture byte for byte.
    CHECK_EQ(toHex(Demo::encodeBattery20(37.78, 57, 22, 24, 0.0, 352.8, 200.3, 1, 5)),
             std::string("2462245a230ec2391600000dc807d31827152340"));
    CHECK_EQ(toHex(Demo::encodeMotor20(1, 22, 0, 0, 0, 0, 9.0, 0x03ff)),
             std::string("246d245a23011600000000000000005a03ff2340"));
    CHECK_EQ(toHex(Demo::encodeEbm20(13.2, 73.7, false, 0, 1)),
             std::string("246a245a2300008402e1000001f50148494a2340"));

    CHECK(p.feed(Demo::encodeMotor20(3, 31, 180.5, 23.4, 120, 96.0, 9.0, 0x0200)) == Kind::Motor);
    CHECK_EQ(p.motor()->assistLevel, 3);
    CHECK(near(*p.motor()->motorPowerW, 180.5, 0.006));
    CHECK(near(p.motor()->speedKmh, 23.4));
    CHECK(near(*p.motor()->riderPowerW, 96.0));

    CHECK(p.feed(Demo::encodeVin(Demo::Bike::vin())) == Kind::Vin);
    CHECK_EQ(p.vin(), Demo::Bike::vin());
    CHECK(p.feed(Demo::encodeAssistRange(0, 5, 2)) == Kind::Assist);
    CHECK_EQ(p.assist()->max, 5);
}

static void ride()
{
    Demo::Bike bike;
    Parser p;
    for (const Bytes &f : bike.receive(cmdVin()))
        p.feed(f);
    for (const Bytes &f : bike.receive(cmdProtocol()))
        p.feed(f);
    CHECK_EQ(p.vin(), Demo::Bike::vin());
    CHECK(p.protocol() == Protocol::V102);

    double maxSpeed = 0;
    for (int t = 0; t < 600; ++t) {
        for (const Bytes &f : bike.step(1.0))
            CHECK(p.feed(f) != Kind::Unknown);
        maxSpeed = std::max(maxSpeed, p.motor()->speedKmh);
    }
    CHECK(maxSpeed > 15 && maxSpeed < 26);
    CHECK(bike.distanceKm() > 1.0);
    CHECK(p.battery()->soc < 100 && p.battery()->soc > 50);
    CHECK(p.ebm()->lifetimeKnown && p.ebm()->tripKm.has_value());
    CHECK(near(p.ebm()->odometerKm, 1234.5 + bike.distanceKm(), 0.3));
    CHECK(p.battery()->voltage > 30 && p.battery()->voltage < 42);
}

static void commands()
{
    Demo::Bike bike;
    std::vector<Bytes> reply = bike.receive(cmdAssistSet(4));
    CHECK_EQ(bike.assist(), 4);
    CHECK_EQ(reply.size(), size_t(1));
    Parser p;
    CHECK(p.feed(reply[0]) == Kind::Assist);
    CHECK_EQ(p.assist()->current, 4);

    // Out of range is ignored, as the app never sends it.
    CHECK(bike.receive(cmdAssistSet(9)).empty());
    CHECK_EQ(bike.assist(), 4);

    for (int i = 0; i < 200; ++i)
        bike.step(1.0);
    bike.receive(cmdResetTrip());
    Parser q;
    q.setProtocolVersion("1.02");
    for (int i = 0; i < 2; ++i)
        for (const Bytes &f : bike.step(0.001))
            q.feed(f);
    CHECK(q.ebm()->tripKm.has_value() && *q.ebm()->tripKm < 0.01);
}

static void maps()
{
    Demo::Bike bike;
    Parser p;
    // Asking for map 0 gets all three, in the layout the parser calls a percentage map.
    std::vector<Bytes> reply = bike.receive(cmdMapRead(0));
    CHECK_EQ(reply.size(), size_t(3));
    for (const Bytes &f : reply)
        CHECK(p.feed(f) == Kind::MapData);
    CHECK_EQ(p.maps().size(), size_t(3));
    CHECK(p.maps().at(1).isPercent());
    CHECK_EQ(p.maps().at(1).percent, 40);
    CHECK_EQ(p.maps().at(3).percent, 90);

    // A write is acknowledged and sticks; reading one map gets one frame.
    reply = bike.receive(cmdMapWritePercent(2, 42));
    CHECK_EQ(reply.size(), size_t(1));
    CHECK(p.feed(reply[0]) == Kind::MapAck);
    CHECK_EQ(p.lastMapAck(), std::string("OK"));
    CHECK_EQ(bike.mapPercent(2), 42);
    reply = bike.receive(cmdMapRead(2));
    CHECK_EQ(reply.size(), size_t(1));
    p.feed(reply[0]);
    CHECK_EQ(p.maps().at(2).percent, 42);

    // The 35 % the official app cannot send is stored as 34.
    bike.receive(cmdMapWritePercent(3, 35));
    CHECK_EQ(bike.mapPercent(3), 34);
    // Nonsense is refused.
    CHECK(p.feed(bike.receive(cmdMapWritePercent(9, 50))[0]) == Kind::MapAck);
    CHECK_EQ(p.lastMapAck(), std::string("ER"));
}

int main()
{
    encoders();
    ride();
    commands();
    maps();
    return finish("demo");
}

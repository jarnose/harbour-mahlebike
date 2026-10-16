#pragma once

// A pretend bike: a protocol 102 bike that rides a loop, answers the commands and sends frames
// exactly as the real one is documented to (the encoders here are the inverse of the decoders in
// mahle.cpp, and the tests check the round trip). It lets the whole app be tried, and a ride recorded
// and exported, with no bike in reach.
//
// Qt-free, like the rest of the core.

#include "protocol/mahle.h"

#include <vector>

namespace Mahle {
namespace Demo {

// The frame encoders. The values are in the units the decoders return.
Bytes encodeBattery20(double voltage, int soc, int temperature, int mosfetTemperature, double currentAmp,
                      double nominalWh, double remainingWh, int pack, int cycles);
Bytes encodeMotor20(int assist, int temperature, double motorPowerW, double speedKmh, int wheelRpm,
                    double riderPowerW, double maxCurrentAmp, int maxTorque);
Bytes encodeEbm20(double odometerKm, double rangeKm, bool light, int errorId, int slot);
Bytes encodeVin(const std::string &vin);
Bytes encodeProtocol(const std::string &version);
Bytes encodeAssistRange(int min, int max, int current);
// A motor-map reply of an X35-type bike: no curve points (power bytes 255), the percentage in byte 17.
Bytes encodeMapPercent(char op, int map, int percent);
Bytes encodeMapAck(bool ok);

class Bike
{
public:
    Bike();

    // Advances the pretend ride by dt seconds and returns this tick's broadcast frames.
    std::vector<Bytes> step(double dtSeconds);

    // Answers a command the app wrote. Returns the frames the bike would send back.
    std::vector<Bytes> receive(const Bytes &command);

    int assist() const { return m_assist; }
    int mapPercent(int map) const { return map >= 1 && map <= 3 ? m_maps[map - 1] : -1; }
    double speedKmh() const { return m_speed; }
    double distanceKm() const { return m_distanceKm; }
    double remainingWh() const { return m_remainingWh; }

    static std::string vin() { return "DEMO0000000000001"; }

private:
    double m_t = 0;
    int m_assist = 2;
    int m_slot = 1;
    double m_speed = 0;
    double m_distanceKm = 0;
    double m_odometerKm = 1234.5;
    double m_tripKm = 0;
    double m_remainingWh = 300.0;
    double m_motorTemp = 22;
    double m_motorW = 0;
    double m_riderW = 0;
    double m_currentA = 0;
    bool m_light = false;
    int m_maps[3] = { 40, 70, 90 };
};

} // namespace Demo
} // namespace Mahle

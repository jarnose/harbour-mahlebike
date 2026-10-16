#include "protocol/demo.h"

#include <algorithm>
#include <cmath>

namespace Mahle {
namespace Demo {

namespace {

void put16(Bytes &b, size_t at, int v)
{
    b[at] = static_cast<uint8_t>((v >> 8) & 0xff);
    b[at + 1] = static_cast<uint8_t>(v & 0xff);
}

void put24(Bytes &b, size_t at, int v)
{
    b[at] = static_cast<uint8_t>((v >> 16) & 0xff);
    put16(b, at + 1, v & 0xffff);
}

Bytes frame20(char type)
{
    Bytes b(20, 0);
    b[0] = '$';
    b[1] = static_cast<uint8_t>(type);
    b[2] = '$';
    b[3] = 'Z';
    b[4] = '#';
    b[18] = '#';
    b[19] = '@';
    return b;
}

int clampi(long v, int lo, int hi)
{
    return static_cast<int>(std::max<long>(lo, std::min<long>(hi, v)));
}

} // namespace

Bytes encodeBattery20(double voltage, int soc, int temperature, int mosfetTemperature, double currentAmp,
                      double nominalWh, double remainingWh, int pack, int cycles)
{
    Bytes b = frame20('b');
    put16(b, 5, clampi(std::lround(voltage * 100), 0, 0xffff));
    b[7] = static_cast<uint8_t>(clampi(soc, 0, 255));
    b[8] = static_cast<uint8_t>(temperature & 0xff);
    put16(b, 9, static_cast<int>(std::lround(currentAmp * 10)) & 0xffff);
    put16(b, 11, clampi(std::lround(nominalWh * 10), 0, 0xffff));
    put16(b, 13, clampi(std::lround(remainingWh * 10), 0, 0xffff));
    b[15] = static_cast<uint8_t>(mosfetTemperature & 0xff);
    put16(b, 16, clampi(pack * 10000L + cycles, 0, 0xffff));
    return b;
}

Bytes encodeMotor20(int assist, int temperature, double motorPowerW, double speedKmh, int wheelRpm,
                    double riderPowerW, double maxCurrentAmp, int maxTorque)
{
    Bytes b = frame20('m');
    b[5] = static_cast<uint8_t>(assist & 0xff);
    b[6] = static_cast<uint8_t>(temperature & 0xff);
    put16(b, 7, clampi(std::lround(motorPowerW * 100), 0, 0xffff));
    put16(b, 9, clampi(std::lround(speedKmh * 10), 0, 0xffff));
    b[11] = static_cast<uint8_t>(clampi(wheelRpm, 0, 255));
    put16(b, 12, clampi(std::lround(riderPowerW * 10), 0, 0xffff));
    put16(b, 14, clampi(std::lround(maxCurrentAmp * 10), 0, 0xffff));
    put16(b, 16, clampi(maxTorque, 0, 0xffff));
    return b;
}

Bytes encodeEbm20(double odometerKm, double rangeKm, bool light, int errorId, int slot)
{
    Bytes b = frame20('j');
    put24(b, 5, clampi(std::lround(odometerKm * 10), 0, 0xffffff));
    put16(b, 8, clampi(std::lround(rangeKm * 10), 0, 0xffff));
    b[10] = light ? 1 : 0;
    b[11] = static_cast<uint8_t>(errorId & 0xff);
    b[12] = 1;
    b[13] = 0xf5;
    b[14] = static_cast<uint8_t>(slot);
    b[15] = 'H';
    b[16] = 'I';
    b[17] = 'J';
    return b;
}

Bytes encodeVin(const std::string &vin)
{
    const std::string s = "$s$V#" + vin + "#@";
    return Bytes(s.begin(), s.end());
}

Bytes encodeProtocol(const std::string &version)
{
    const std::string s = "$s$P#" + version + "#@";
    return Bytes(s.begin(), s.end());
}

Bytes encodeAssistRange(int min, int max, int current)
{
    const std::string s = std::string("$m$A#") + char('0' + min) + char('0' + max) + char('0' + current) + "#@";
    return Bytes(s.begin(), s.end());
}

Bike::Bike() = default;

std::vector<Bytes> Bike::step(double dt)
{
    m_t += dt;

    // The rider sets off, rides a loop with some variation, and stops now and then.
    const double cycle = std::fmod(m_t, 240.0);
    const bool standing = cycle < 8.0 || (cycle > 150.0 && cycle < 165.0);
    static const double kTarget[] = { 16, 19, 22, 24, 25, 25.4, 25.4, 25.4 };
    const double target = standing ? 0.0 : kTarget[std::min(m_assist, 7)] * (0.9 + 0.1 * std::sin(m_t / 7.0));
    // The bike speeds up and slows down at a bounded rate (km/h per second).
    const double step = 2.2 * dt;
    if (m_speed < target)
        m_speed = std::min(target, m_speed + step);
    else
        m_speed = std::max(target, m_speed - 2 * step);

    const bool pedalling = m_speed > 3.0;
    m_riderW = pedalling ? 70 + 60 * std::sin(m_t / 5.0) + m_speed * 2 : 0;
    m_riderW = std::max(0.0, m_riderW);
    // The motor adds a multiple of the rider's power by assist level, capped at the drive's 250 W.
    static const double kFactor[] = { 0.0, 0.5, 1.0, 1.6, 2.4, 3.2, 3.2, 3.2 };
    m_motorW = pedalling ? std::min(250.0, m_riderW * kFactor[std::min(m_assist, 7)]) : 0.0;
    m_currentA = m_motorW / 37.0;

    const double km = m_speed * dt / 3600.0;
    m_distanceKm += km;
    m_odometerKm += km;
    m_tripKm += km;
    m_remainingWh = std::max(0.0, m_remainingWh - m_motorW * dt / 3600.0 * 1.08);
    m_motorTemp += ((22 + m_motorW / 12.0) - m_motorTemp) * 0.01 * dt;
    if (std::fmod(m_t, 120.0) < dt)
        m_light = !m_light;

    const double nominal = 352.8;
    const int soc = clampi(std::lround(m_remainingWh / nominal * 100), 0, 100);
    const double voltage = 32.0 + soc * 0.078 - m_currentA * 0.15;

    std::vector<Bytes> out;
    out.push_back(encodeMotor20(m_assist, static_cast<int>(std::lround(m_motorTemp)), m_motorW, m_speed,
                                static_cast<int>(m_speed * 1000.0 / 3600.0 / 2.1 * 60.0 / 3.0), m_riderW, 9.0,
                                pedalling ? 0x0200 : 0x03ff));
    out.push_back(encodeBattery20(voltage, soc, 22, 24, m_currentA, nominal, m_remainingWh, 1, 12));
    // The EBM frame alternates between the lifetime totals and trip A.
    const double range = m_remainingWh / 4.2;
    if (m_slot == 1)
        out.push_back(encodeEbm20(m_odometerKm, range, m_light, 0, 1));
    else
        out.push_back(encodeEbm20(m_tripKm, range, m_light, 0, 2));
    m_slot = m_slot == 1 ? 2 : 1;
    return out;
}

std::vector<Bytes> Bike::receive(const Bytes &c)
{
    const std::string s(c.begin(), c.end());
    std::vector<Bytes> out;
    if (s == "$S$V#@") {
        out.push_back(encodeVin(vin()));
    } else if (s == "$S$P#@") {
        out.push_back(encodeProtocol("1.02"));
    } else if (s.compare(0, 5, "$M$A#") == 0 && s.size() >= 8 && s.compare(s.size() - 2, 2, "#@") == 0) {
        const int level = std::atoi(s.substr(5, s.size() - 7).c_str());
        if (level >= 0 && level <= 5) {
            m_assist = level;
            out.push_back(encodeAssistRange(0, 5, m_assist));
        }
    } else if (s == "$M$R#@") {
        m_tripKm = 0;
    }
    return out;
}

} // namespace Demo
} // namespace Mahle

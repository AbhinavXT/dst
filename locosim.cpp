#include "locosim.h"

#include <QtEndian>
#include <cmath>

namespace LocoSim {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

double pulsesPerMetre(const Wheel &wheel)
{
    if (wheel.diameterMm <= 0) return 0;
    return wheel.countScale * wheel.pulsesPerRev / (kPi * wheel.diameterMm / 1000.0);
}

QByteArray speedMessage(int sensor, quint8 direction, quint32 tachometer1, quint32 tachometer2)
{
    QByteArray message(kSpeedMessageSize, '\0');
    uchar *bytes = reinterpret_cast<uchar *>(message.data());
    bytes[0] = kSrcSpeedSim;
    bytes[1] = kDestKavach;
    bytes[2] = kMsgSpeedInfo;
    qToLittleEndian<quint16>(kSpeedMessageSize, bytes + 3);
    bytes[5] = quint8(sensor);
    bytes[6] = direction;
    qToLittleEndian<quint32>(tachometer1, bytes + 7);
    qToLittleEndian<quint32>(tachometer2, bytes + 11);
    return message;                           // crc (15-18) stays 0
}

QString tagFaultName(TagFault fault)
{
    switch (fault) {
    case TagFault::None:   return QString();
    case TagFault::Late:   return QStringLiteral("late");
    case TagFault::Twice:  return QStringLiteral("sent twice");
    case TagFault::BadCrc: return QStringLiteral("bad CRC");
    }
    return QString();
}

QByteArray badCrcTag(const QByteArray &tag)
{
    QByteArray bad = tag;
    if (bad.size() == 16) bad[15] = char(bad.at(15) ^ 0x01);
    return bad;
}

QByteArray tagFrame(int reader, const QByteArray &tag)
{
    if (tag.size() != 16) return QByteArray();
    QByteArray frame;
    frame.reserve(kTagFrameSize);
    frame.append(char(0xAA)).append(char(0xAA)).append(char(kTagFrameSize)).append(char(reader));
    frame.append(tag);
    frame.append(char(0xBB)).append(char(0xBB));
    return frame;
}

double brakeDeceleration(const BrakeRates &rates, double bpKgcm2, double bcKgcm2)
{
    if (bcKgcm2 <= rates.releasedBcKgcm2 || rates.fullBcKgcm2 <= 0) return 0;
    // BC alone: up to the NB rate at NB's BC, then on to full service.
    double base = rates.fullServiceMps2;
    if (bcKgcm2 < rates.fullBcKgcm2) {
        if (rates.nbBcKgcm2 <= 0 || rates.nbBcKgcm2 >= rates.fullBcKgcm2)
            base = rates.fullServiceMps2 * bcKgcm2 / rates.fullBcKgcm2;
        else if (bcKgcm2 <= rates.nbBcKgcm2)
            base = rates.normalMps2 * bcKgcm2 / rates.nbBcKgcm2;
        else
            base = rates.normalMps2 + (rates.fullServiceMps2 - rates.normalMps2) * (bcKgcm2 - rates.nbBcKgcm2)
                                          / (rates.fullBcKgcm2 - rates.nbBcKgcm2);
    }
    // BP under the FSB level: toward the emergency rate as it falls to 0.
    const double belowFsb = rates.fsbBpKgcm2 > 0 ? qBound(0.0, (rates.fsbBpKgcm2 - bpKgcm2) / rates.fsbBpKgcm2, 1.0) : 0.0;
    const double boost = rates.fullServiceMps2 > 0 ? (rates.emergencyMps2 / rates.fullServiceMps2 - 1) * belowFsb : 0.0;
    return qMax(0.0, base * (1 + boost));
}

void Motion::setSpeed(double kmh)
{
    m_speed = qBound(0.0, kmh, m_maxSpeed);
}

void Motion::setMaxSpeed(double kmh)
{
    m_maxSpeed = qMax(0.0, kmh);
    setSpeed(m_speed);
}

void Motion::resetCounts()
{
    for (auto &sensor : m_carry)
        for (double &fraction : sensor) fraction = 0;
}

void Motion::setBrake(double mps2)
{
    const bool was = braking();
    m_brake = qMax(0.0, mps2);
    if (was != braking()) m_drive = Drive::Hold;
}

Step Motion::step(double seconds)
{
    Step out;
    if (seconds <= 0) return out;

    const double startSpeed = m_speed;
    if (braking()) setSpeed(m_speed - m_brake * 3.6 * seconds);
    else if (m_drive == Drive::Accelerate) setSpeed(m_speed + m_rate * seconds);
    else if (m_drive == Drive::Decelerate) setSpeed(m_speed - m_rate * seconds);
    // The speed ramps through the step: its mean is what the wheel turned at.
    const double meanSpeed = (startSpeed + m_speed) / 2;

    out.metres = meanSpeed / 3.6 * seconds;
    m_position += m_direction == Reverse ? -out.metres : out.metres;

    const double perMetre = pulsesPerMetre(m_wheel);
    for (int sensor = 0; sensor < 2; ++sensor) {
        for (int channel = 0; channel < 2; ++channel) {
            const double own = m_channelSpeed[sensor][channel];
            const double metres = own < 0 ? out.metres : own / 3.6 * seconds;
            const double counted = metres * perMetre + m_carry[sensor][channel];
            const double whole = std::floor(counted);
            m_carry[sensor][channel] = counted - whole;
            out.pulses[sensor][channel] = quint32(whole);
        }
    }
    return out;
}

}  // namespace LocoSim

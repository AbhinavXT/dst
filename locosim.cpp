#include "locosim.h"

#include "biuprotocol.h"

#include <QtEndian>
#include <algorithm>
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

QByteArray simulatorTagFrame(int reader, const QString &pageX, const QString &pageY)
{
    const QString hex = pageY + pageX;
    if (hex.size() != 32) return QByteArray();
    for (const QChar c : hex)
        if (!c.isDigit() && !(c.toLower() >= QLatin1Char('a') && c.toLower() <= QLatin1Char('f'))) return QByteArray();
    QByteArray payload = QByteArray::fromHex(hex.toLatin1());
    std::reverse(payload.begin(), payload.end());
    return tagFrame(reader, payload);
}

QByteArray healthFrame(int reader, bool ok)
{
    QByteArray frame;
    frame.reserve(kHealthFrameSize);
    frame.append(char(0xAA)).append(char(0xAA)).append(char(kHealthFrameSize)).append(char(reader)).append(char(ok ? 1 : 2));
    frame.append(char(0xBB)).append(char(0xBB));
    return frame;
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

QString brakeTypeName(BrakeType type)
{
    switch (type) {
    case BrakeType::Normal:      return QStringLiteral("NB");
    case BrakeType::FullService: return QStringLiteral("FSB");
    case BrakeType::Emergency:   return QStringLiteral("EB");
    case BrakeType::LightEngine: return QStringLiteral("LE");
    case BrakeType::Released:    break;
    }
    return QStringLiteral("released");
}

BrakeType commandedBrake(const BrakeRates &rates, double bpKgcm2, double bcKgcm2)
{
    if (bpKgcm2 < rates.fsbBpKgcm2 / 2) return BrakeType::Emergency;
    if (bpKgcm2 < (rates.fsbBpKgcm2 + rates.nbBpKgcm2) / 2) return BrakeType::FullService;
    if (bpKgcm2 < (rates.nbBpKgcm2 + rates.releasedBpKgcm2) / 2) return BrakeType::Normal;
    return bcKgcm2 > rates.releasedBcKgcm2 ? BrakeType::LightEngine : BrakeType::Released;
}

double brakeTypeRate(const BrakeRates &rates, BrakeType type)
{
    switch (type) {
    case BrakeType::Normal:      return rates.normalMps2;
    case BrakeType::FullService:
    case BrakeType::LightEngine: return rates.fullServiceMps2;
    case BrakeType::Emergency:   return rates.emergencyMps2;
    case BrakeType::Released:    break;
    }
    return 0;
}

bool biuCommandTargets(const QByteArray &data, const BrakeRates &rates, CommandTargets *out)
{
    Biu::Command c;
    if (!out || data.size() != 8 || !Biu::decodeCommand(data, &c)) return false;
    out->isolated = c.bit1(Biu::CmdBiuIsolate);
    out->bp = !out->isolated && c.a9Valid() ? c.a9() : rates.releasedBpKgcm2;
    out->bc = !out->isolated && c.sa9Valid() ? c.sa9() : 0.0;
    out->a9Above  = c.a9Valid() && c.a9() > Biu::kA9ValidMax + 1e-9;
    out->sa9Above = c.sa9Valid() && c.sa9() > Biu::kSa9ValidMax + 1e-9;
    return true;
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

void Motion::setBrake(double mps2)
{
    const bool was = braking();
    m_brake = qMax(0.0, mps2);
    if (was != braking()) {
        m_drive = Drive::Hold;
        m_brakeSeconds = 0;
    }
}

double Motion::brakeEffort() const
{
    if (!braking()) return 0;
    return m_buildUp > 0 ? qMin(1.0, m_brakeSeconds / m_buildUp) : 1.0;
}

Step Motion::step(double seconds)
{
    Step out;
    if (seconds <= 0) return out;

    const double startSpeed = m_speed;
    // Session 418: the brake as built up at the step's end, as the simulator's.
    if (braking()) m_brakeSeconds += seconds;
    if (braking()) setSpeed(m_speed - brakeNow() * 3.6 * seconds);
    else if (m_drive == Drive::Accelerate) setSpeed(m_speed + m_rate * seconds);
    else if (m_drive == Drive::Decelerate) setSpeed(m_speed - m_rate * seconds);
    // The speed ramps through the step: its mean is what the wheel turned at.
    const double meanSpeed = (startSpeed + m_speed) / 2;

    out.metres = meanSpeed / 3.6 * seconds;
    m_position += m_direction == Reverse ? -out.metres : out.metres;

    // Session 408: one interval at the speed now, truncated, as the simulator.
    const double perMetre = pulsesPerMetre(m_wheel);
    for (int sensor = 0; sensor < 2; ++sensor) {
        for (int channel = 0; channel < 2; ++channel) {
            const double own = m_channelSpeed[sensor][channel];
            const double kmh = own < 0 ? m_speed : own;
            out.pulses[sensor][channel] = quint32(std::floor(kmh / 3.6 * m_pulseInterval * perMetre + 1e-9));
        }
    }
    return out;
}

}  // namespace LocoSim

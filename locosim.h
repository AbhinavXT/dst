#ifndef LOCOSIM_H
#define LOCOSIM_H
// =============================================================================
//  LocoSim (session 391) -- the loco's motion for Tools > Simulators > Loco
//  Simulator, and the speed sensor message it sends the VCC.
//
//  Step 1 of bringing LocoTcasSimulator into DLConsole: speed and the two speed
//  sensors. The RFID readers (step 2) and the braking (step 3) drive the same
//  position, so it lives here and not in the window.
//
//  THE MESSAGE (LocoTcasSimulator's LPIM_LKAVACH_INFO_MSG, 19 B, packed LE)
//    0     src_id      20 (CSCI_SPEED_SIM)
//    1     dest_id      2 (CSCI_LTCAS)
//    2     message_id   1 (LPIM_LKAVACH_SPEED_INFO_MSG)
//    3-4   length      19
//    5     speed_sensor_reader  1 or 2
//    6     movement_dir         1 forward, 2 reverse
//    7-10  pulse_from_tachometer_1 (channel 1)
//    11-14 pulse_from_tachometer_2 (channel 2)
//    15-18 crc          0, as the simulator sends it
//  The simulator's message, not session 67's 14-byte @speed struct: it is
//  the one the VCC on the bench takes today (Abhinav, 2026-10-29).
//
//  THE PULSES
//    A message carries the pulses counted since the last one, times the
//    count scale: metres x scale x pulses-per-rev / (pi x wheel diameter).
//    The simulator's defaults (950 mm, 30, x1000) with the real pi (it used
//    3.14 for pulses and 3.1428571 for distance, 0.09 % apart). The fraction
//    a message cannot carry is kept for the next, and the time is the clock's,
//    not a nominal 100 ms, so the pulses add up to the distance travelled.
//
//  THE TAG FRAME (session 395, step 2: the RFID readers over serial)
//    LocoTcasSimulator's LPIM_LTACS_NORMAL_TAG_PKT, 22 B packed, 38400 8N1:
//    0-1   start_byte   AA AA (0xAAAA LE)
//    2     packet_length 22
//    3     rfid_reader  1 or 2
//    4-19  the tag's 16 bytes: its buildTagPacket() reverses page_y + page_x,
//          which gives them back in the order the tag holds them
//    20-21 end_byte     BB BB
//
//  THE BRAKE (session 398, step 3: braking from the BIU's command)
//    The VCC commands the BIU; the Brake Test Bench's air (brakeplant.h)
//    moves toward it; the loco slows by what the air does. Deceleration is
//    proportional to BC up to BC at full service (formation FSB / EB: 2.4,
//    Annexure-4), and BP picks the ceiling: the full-service rate at the FSB
//    BP (3.5) or above, rising to the emergency rate as BP falls to 0. NB
//    (BC 1.2, BP 4.4) is half the full-service rate; FSB (2.4, 3.5) the full
//    service rate; EB (2.4, 0) the emergency rate. BC at or under 0.1 (the
//    released tolerance) is no brake. The rates are settings, not loco data:
//    no per-loco-type figures are known yet.
//    While a brake acts it wins over the drive; when it comes on and when it
//    goes off the drive goes to Hold, so the loco keeps the speed it slowed to.
// =============================================================================
#include <QByteArray>
#include <QtGlobal>

namespace LocoSim {

constexpr quint8 kSrcSpeedSim = 20;
constexpr quint8 kDestKavach  = 2;
constexpr quint8 kMsgSpeedInfo = 1;
constexpr int    kSpeedMessageSize = 19;
constexpr int    kTagFrameSize = 22;
constexpr int    kTagBaud = 38400;

enum Direction : quint8 { Forward = 1, Reverse = 2 };

struct Wheel {
    double diameterMm  = 950.0;
    int    pulsesPerRev = 30;
    int    countScale   = 1000;
};

double     pulsesPerMetre(const Wheel &wheel);
QByteArray speedMessage(int sensor, quint8 direction, quint32 tachometer1, quint32 tachometer2);
// Empty unless `tag` is 16 bytes (the simulator sends nothing then either).
QByteArray tagFrame(int reader, const QByteArray &tag);

struct BrakeRates {
    double fullServiceMps2 = 0.6;   // m/s^2 at BC full service, BP at FSB or above
    double emergencyMps2   = 0.9;   // m/s^2 at BC full service, BP 0
    double fullBcKgcm2     = 2.4;   // BC at full service (formation FSB / EB)
    double fsbBpKgcm2      = 3.5;   // BP at FSB
    double releasedBcKgcm2 = 0.1;   // BC at or under: no brake
};
// m/s^2 the air in the brake gives, 0 for none.
double brakeDeceleration(const BrakeRates &rates, double bpKgcm2, double bcKgcm2);

// What one step sent: pulses[sensor 0/1][channel 0/1], and the metres moved.
struct Step {
    quint32 pulses[2][2] = { { 0, 0 }, { 0, 0 } };
    double  metres = 0;
};

class Motion
{
public:
    enum class Drive { Hold, Accelerate, Decelerate };

    void   setWheel(const Wheel &wheel) { m_wheel = wheel; }
    Wheel  wheel() const { return m_wheel; }
    void   setSpeed(double kmh);                 // held to 0 .. max
    double speed() const { return m_speed; }
    void   setMaxSpeed(double kmh);
    double maxSpeed() const { return m_maxSpeed; }
    void   setRate(double kmhPerSecond) { m_rate = qMax(0.0, kmhPerSecond); }
    double rate() const { return m_rate; }
    void   setDrive(Drive drive) { m_drive = drive; }
    Drive  drive() const { return m_drive; }
    void   setDirection(Direction direction) { m_direction = direction; }
    Direction direction() const { return m_direction; }
    // A channel at its own speed (slip, skid, a dead channel); below 0 = the loco's.
    void   setChannelSpeed(int sensor, int channel, double kmh) { m_channelSpeed[sensor][channel] = kmh; }
    double channelSpeed(int sensor, int channel) const { return m_channelSpeed[sensor][channel]; }
    // Metres along the line; forward counts up, reverse down.
    void   setPosition(double metres) { m_position = metres; }
    double position() const { return m_position; }
    void   resetCounts();                        // drop the fractions carried
    // The brake's deceleration, m/s^2 (0 none). It wins over the drive while
    // it acts; coming on or going off sets the drive to Hold.
    void   setBrake(double mps2);
    double brake() const { return m_brake; }
    bool   braking() const { return m_brake > 0; }

    Step step(double seconds);

private:
    Wheel     m_wheel;
    double    m_speed = 0;
    double    m_maxSpeed = 120;
    double    m_rate = 1;
    Drive     m_drive = Drive::Hold;
    Direction m_direction = Forward;
    double    m_channelSpeed[2][2] = { { -1, -1 }, { -1, -1 } };
    double    m_carry[2][2] = { { 0, 0 }, { 0, 0 } };
    double    m_position = 0;
    double    m_brake = 0;
};

}  // namespace LocoSim

#endif  // LOCOSIM_H

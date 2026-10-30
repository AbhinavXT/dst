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
//    A message carries one interval's pulses at the channel's speed now, times
//    the count scale: speed x interval x scale x pulses-per-rev / (pi x wheel
//    diameter), truncated, as LocoTcasSimulator computes them (0.1 s x v x
//    ppr / (pi x d) x 1000, into a UINT_32). So at a steady speed every message
//    carries the same count (session 408: Abhinav, "if I set the speed as 80
//    why are the pulse counts changing"). The interval is the configured one
//    (100 ms), not the clock's time since the last message: a late timer does
//    not change the count, and no fraction is carried. The simulator's
//    defaults (950 mm, 30, x1000) with the real pi (it used 3.14 for pulses).
//    The loco's position still moves by the clock's time.
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
//  READER HEALTH (session 409, LocoTcasSimulator's LPIM_LTCAS_RFID_READER_HEALTH)
//    0-1 start_byte AA AA, 2 packet_length 7, 3 rfid_reader 1 / 2,
//    4 health_status 1 OK / 2 fail, 5-6 end_byte BB BB. Reader 1's every
//    1 s, reader 2's every 2 s, while the reader is on, whether the loco runs
//    or not; reader 2's on port B when it has its own port. A reader at Fail
//    sends its health (2) and no tags, as the simulator.
//
//  TAG FAULTS (session 405): per tag, for reader 1, reader 2 or both, each
//    time the loco passes: Late (sent N ms after the reader would have),
//    Twice (the frame twice, back to back), Bad CRC (badCrcTag).
//
//  THE BRAKE (session 398, step 3: braking from the BIU's command)
//    The VCC commands the BIU; the Brake Test Bench's air (brakeplant.h)
//    moves toward it; the loco slows by what the air does. BC sets the rate:
//    0 at the released tolerance (0.1) and under, the NB rate at NB's BC
//    (1.2, Annexure-4), the full-service rate at BC full service (formation
//    FSB / EB: 2.4) and above, straight lines between (session 399: the NB
//    point has its own rate, so NB can be set without moving FSB or EB).
//    BP under the FSB level (3.5) raises it, up to emergency / full service
//    times at BP 0. So NB (BC 1.2, BP 4.4) gives the NB rate, FSB (2.4, 3.5)
//    the full-service rate, EB (2.4, 0) the emergency rate. The rates are
//    settings, not loco data: no per-loco-type figures are known yet.
//    While a brake acts it wins over the drive; when it comes on and when it
//    goes off the drive goes to Hold, so the loco keeps the speed it slowed to.
//
//  THE BRAKE FROM THE BIU'S COMMAND (session 418, LocoTcasSimulator's way)
//    The command, not the air, picks the brake: what A9 (the BP target) and
//    SA9 (the BC target) ask for is NB, FSB, EB or the light engine brake
//    (commandedBrake), and that brake's rate acts at once, as the simulator's
//    fixed rate per brake type does. A build-up time grows it from 0 to the
//    full rate (the simulator's "Brake build-up time"; 0 = at once). It acts
//    until the VCC commands the release (A9 5.0, SA9 0). The air way above
//    stays, for a test of the plant's faults.
//    (session 420) The command can also come from the VCC's log: its
//    @biu_cmd copies of what it put on CAN (docs/biu-firmware,
//    biu_dlconsole_mirror), read as the BIU reads them (biuCommandTargets).
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
constexpr int    kHealthFrameSize = 7;
// Session 409: how often each reader's health goes out (the simulator's timers).
constexpr int    kHealthPeriodMs[2] = { 1000, 2000 };

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
// Session 410: the simulator's buildTagPacket (structures.h): page_y + page_x,
// exactly 32 hex digits, as bytes, reversed, in the 22-byte tag frame. Empty
// when the digits are not 32 (the simulator refuses them too).
QByteArray simulatorTagFrame(int reader, const QString &pageX, const QString &pageY);
// Session 409: LPIM_LTCAS_RFID_READER_HEALTH, 7 B packed: AA AA, length 7,
// rfid_reader, health_status (1 OK, 2 fail), BB BB.
QByteArray healthFrame(int reader, bool ok);

struct BrakeRates {
    double normalMps2      = 0.3;   // m/s^2 at NB's BC, BP at FSB or above
    double fullServiceMps2 = 0.6;   // m/s^2 at BC full service, BP at FSB or above
    double emergencyMps2   = 0.9;   // m/s^2 at BC full service, BP 0
    double nbBcKgcm2       = 1.2;   // BC at NB
    double fullBcKgcm2     = 2.4;   // BC at full service (formation FSB / EB)
    double fsbBpKgcm2      = 3.5;   // BP at FSB
    double releasedBcKgcm2 = 0.1;   // BC at or under: no brake
    // Session 418: the BIU's command.
    double nbBpKgcm2       = 4.4;   // BP at NB
    double releasedBpKgcm2 = 5.0;   // BP released
    double buildUpSeconds  = 0;     // 0 to the full rate (0: at once)
};
// m/s^2 the air in the brake gives, 0 for none.
double brakeDeceleration(const BrakeRates &rates, double bpKgcm2, double bcKgcm2);

// Session 418: what slows the loco: the BIU's command, or the bench's air.
// Session 420: or the VCC's own @biu_cmd copies (a real BIU on CAN, no bench).
enum class BrakeSource { Command, Air, VccLog };
// The brake a command asks for, from its BP target (A9) and BC target (SA9),
// each when valid: BP half way down to FSB (1.75) or lower is EB, half way to
// NB (3.95) FSB, half way to released (4.7) NB; BP charged with BC over the
// released tolerance is the light engine brake.
enum class BrakeType { Released, Normal, FullService, Emergency, LightEngine };
QString   brakeTypeName(BrakeType type);   // "released", "NB", "FSB", "EB", "LE"
BrakeType commandedBrake(const BrakeRates &rates, double bpKgcm2, double bcKgcm2);
// m/s^2: NB the NB rate, FSB and LE full service, EB emergency, released 0.
double    brakeTypeRate(const BrakeRates &rates, BrakeType type);

// Session 420: the BP and BC a BIU command asks for, as the BIU follows it:
// A9 / SA9 when valid, else the released BP / 0; nothing while the command
// isolates the BIU (the loco pilot's brakes then). False for anything that
// is not an 8-byte 0x190 data field. A target marked valid above its range
// (A9 6.0, SA9 4.0) is still followed, as the bench's BIU follows it
// (BiuSim notes it), and flagged here so the Drive tab can say so.
struct CommandTargets {
    double bp = 0, bc = 0;
    bool   isolated = false;
    bool   a9Above = false, sa9Above = false;
};
bool biuCommandTargets(const QByteArray &data, const BrakeRates &rates, CommandTargets *out);

// Session 405: a tag frame's fault, set per tag on the RFID tab.
enum class TagFault { None, Late, Twice, BadCrc };
QString tagFaultName(TagFault fault);          // "late", "sent twice", "bad CRC"
// The tag with its CRC-30 no longer matching: the last byte's lowest bit,
// inside the CRC, flipped. Everything the loco decodes stays as it was.
QByteArray badCrcTag(const QByteArray &tag);

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
    // Session 408: the interval one message's pulses are for (s; 0.1 by default).
    void   setPulseInterval(double seconds) { m_pulseInterval = qMax(0.001, seconds); }
    double pulseInterval() const { return m_pulseInterval; }
    // The brake's deceleration, m/s^2 (0 none). It wins over the drive while
    // it acts; coming on or going off sets the drive to Hold.
    void   setBrake(double mps2);
    double brake() const { return m_brake; }
    bool   braking() const { return m_brake > 0; }
    // Session 418: the brake grows from 0 to brake() over this time after it
    // comes on (0: at once); brakeNow() is what it is at, as of the last step.
    void   setBrakeBuildUp(double seconds) { m_buildUp = qMax(0.0, seconds); }
    double brakeBuildUp() const { return m_buildUp; }
    double brakeEffort() const;                  // 0 .. 1
    double brakeNow() const { return m_brake * brakeEffort(); }

    Step step(double seconds);

private:
    Wheel     m_wheel;
    double    m_speed = 0;
    double    m_maxSpeed = 120;
    double    m_rate = 1;
    Drive     m_drive = Drive::Hold;
    Direction m_direction = Forward;
    double    m_channelSpeed[2][2] = { { -1, -1 }, { -1, -1 } };
    double    m_pulseInterval = 0.1;
    double    m_position = 0;
    double    m_brake = 0;
    double    m_buildUp = 0;
    double    m_brakeSeconds = 0;     // how long the brake has acted
};

}  // namespace LocoSim

#endif  // LOCOSIM_H

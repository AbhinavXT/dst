#ifndef BIUPROTOCOL_H
#define BIUPROTOCOL_H
// =============================================================================
//  BIU CAN protocol (session 360) -- RDSO MP.0.01.00.31 Rev.05, Annexure-1.
//
//  The Brake Interface Unit and KAVACH talk CAN: KAVACH sends a command
//  frame every 250 ms (speed, discrete bits, the A9 / SA9 reference
//  pressures it wants), the BIU answers with three status frames (its
//  pressures, its codes, its discrete bits), and both send a heartbeat every
//  500 ms. The BIU is the network manager: it starts KAVACH with NMT.
//
//  The VCC reaches the CAN bus through a UDP gateway; one CAN frame per
//  datagram:  [COB-ID lo][COB-ID hi][3 + n][n data bytes]  (the firmware's
//  brake_pressure_interface_user.c, session 360).
//
//  Pure functions and values only: the simulator is biusim.h.
// =============================================================================
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Biu {

// ---- node IDs and COB-IDs (Annexure-1 1.2, 2.2) ---------------------------------
constexpr quint8  kBiuNodeKavach = 0x10;              // BIU Node 1: its KAVACH side
constexpr quint8  kKavachNode    = 0x20;
constexpr quint16 kCobNmt              = 0x000;
constexpr quint16 kCobCommand          = 0x180 + kBiuNodeKavach;   // 0x190, KAVACH -> BIU
constexpr quint16 kCobPressures        = 0x200 + kKavachNode;      // 0x220, BIU -> KAVACH
constexpr quint16 kCobCodes            = 0x300 + kKavachNode;      // 0x320
constexpr quint16 kCobDiscretes        = 0x400 + kKavachNode;      // 0x420
constexpr quint16 kCobHeartbeatBiu     = 0x700 + kBiuNodeKavach;   // 0x710
constexpr quint16 kCobHeartbeatKavach  = 0x700 + kKavachNode;      // 0x720
constexpr int     kDataLen = 8;

// Heartbeat states and NMT command specifiers
enum NmtState : quint8 { BootUp = 0x00, Stopped = 0x04, Operational = 0x05, PreOperational = 0x7F };
enum NmtCommand : quint8 { NmtStart = 0x01, NmtStop = 0x02, NmtPreOp = 0x80, NmtResetNode = 0x81, NmtResetComm = 0x82 };
QString nmtStateName(int state);       // "Operational", "Pre-operational", "0x12"
QString nmtCommandName(int cs);        // "Start remote node", ...

// ---- the gateway's datagram -------------------------------------------------------
struct Frame {
    quint16    cob = 0;
    QByteArray data;
};
QByteArray toDatagram(const Frame &f);
// One frame from a datagram; false (and why) for anything else.
bool fromDatagram(const QByteArray &datagram, Frame *out, QString *why = nullptr);

// Every pressure byte, both ways: 0.05 kg/cm^2 per bit, rounded.
constexpr double kKgPerBit = 0.05;
quint8 kgToByte(double kgcm2);
double byteToKg(quint8 raw);

// ---- KAVACH -> BIU command (0x190) --------------------------------------------------
//  byte 0-1 speed 0.01 m/s (lower byte first), 2 discrete 1, 3 discrete 2,
//  4 A9 reference (BP target), 5 SA9 reference (BC target), 6-7 spare.
enum CommandBit1 {
    CmdBrakeTestRequest = 0, CmdHealth, CmdBpChargingCutout, CmdBiuIsolate,
    CmdSpeedValid, CmdLightEngine, CmdRemoteLoco, CmdDynamicBraking
};
enum CommandBit2 { CmdFaultAck = 0, CmdDisplayAck, CmdA9Valid, CmdSa9Valid };
constexpr double kA9ValidMax  = 6.0;    // 2.2.2 Discrete Byte2: valid 0 to 6.0
constexpr double kSa9ValidMax = 4.0;    // 0 to 4.0

struct Command {
    quint16 speedRaw = 0;               // 0.01 m/s
    quint8  d1 = 0, d2 = 0;
    quint8  a9Raw = 0, sa9Raw = 0;
    quint8  spare6 = 0, spare7 = 0;
    double  speedMs() const { return speedRaw / 100.0; }
    double  a9() const  { return byteToKg(a9Raw); }
    double  sa9() const { return byteToKg(sa9Raw); }
    bool    bit1(int b) const { return (d1 >> b) & 1; }
    bool    bit2(int b) const { return (d2 >> b) & 1; }
    bool    a9Valid() const  { return bit2(CmdA9Valid); }
    bool    sa9Valid() const { return bit2(CmdSa9Valid); }
};
bool       decodeCommand(const QByteArray &data, Command *out);   // 8 bytes
QByteArray encodeCommand(const Command &c);
QStringList commandBitNames(int byte);   // byte 1: 8 names, byte 2: 4

// ---- BIU -> KAVACH status (0x220, 0x320, 0x420) -------------------------------------
enum PressureId { Bp = 0, Bc, Mr, A9Ref, Sa9Ref, AirFlow, FeedPipe, kPressures };
QString pressureName(int id);            // "BP", "BC", "MR", "A9 ref", ...
QString pressureLongName(int id);        // "Brake pipe", ...
double  validLimit(int id);              // the validity byte's limit: BP 6.0, BC 4.0, MR 10.5 ...

enum StatusBit1 {
    StHealthy = 0, StCmdOverrideByPilot, StPilotOverrideByBiu, StManualIsolate,
    StBpChargingCutout, StEmergencyValve, StPowerCutoffRelay
};
enum StatusBit2 { StPanelTestSuccess = 0, StPanelTestFail, StPanelTestProgress };
enum StatusBit3 {
    StBpCutoutValveOn = 0, StBpSelectValveOn, StBcSelectValveOn, StEmergencyValveOn,
    StBpCutoutValveOk, StBpSelectValveOk, StBcSelectValveOk, StEmergencyValveOk
};
QStringList statusBitNames(int byte);    // byte 1: 7, byte 2: 3, byte 3: 8
// What a status bit's 1 means ("cut out", "healthy", ...), for tooltips.
QString statusBitMeaning(int byte, int bit);

struct Status {
    quint8  raw[kPressures] = {};        // 0.05 kg/cm^2
    bool    valid[kPressures] = {};
    quint16 isolationCounter = 0;
    quint16 faultCode = 0;
    quint16 displayCode = 0;
    quint8  d1 = 0, d2 = 0, d3 = 0;
};
QByteArray encodePressures(const Status &s);
QByteArray encodeCodes(const Status &s);
QByteArray encodeDiscretes(const Status &s);
// Each fills only its own part of `out`.
bool decodePressures(const QByteArray &data, Status *out);
bool decodeCodes(const QByteArray &data, Status *out);
bool decodeDiscretes(const QByteArray &data, Status *out);

// ---- fault and display codes (Annexure-1 5, 6) --------------------------------------
struct Code { quint16 code; QString text; };
QVector<Code> faultCodes();
QVector<Code> displayCodes();
QString codeText(quint16 code);          // "BIU ISOLATED", "Spare", "" for 0

// ---- for the log ------------------------------------------------------------------
QString frameName(quint16 cob);          // "Command", "Pressures", "KAVACH heartbeat", ...
QString describe(const Frame &f);        // one line: what the bytes say
QString hexBytes(const QByteArray &b);   // "90 01 0B ..."

}  // namespace Biu

#endif  // BIUPROTOCOL_H

#ifndef CABIO_H
#define CABIO_H
// =============================================================================
//  cabio.{h,cpp} -- the VCC <-> IOA card serial protocol, for the Cab I/O
//  Simulator (session 324). No UI and no port here: the layouts, the input
//  bank the simulator toggles, and the frames, built and found in a stream.
//
//  THE FRAMES (Abhinav, session 324)
//
//      struct PACK {                   // STRUCT_INTERNAL_MESSAGE_HEADER, 6 B
//          uint8_t  message_length;
//          uint8_t  src_id;            // VCC = 1
//          uint8_t  dest_id;
//          uint8_t  message_id;
//          uint16_t seq_num;           // little-endian
//      };
//
//      VCC -> input card request    AA AA  header                 crc  BB BB  14 B
//      input card -> VCC data       AF AF  header data(4) health  crc  FA FA  19 B
//      VCC -> output card data      CC CC  header byte1 byte2     crc  DD DD  16 B
//      VCC -> output readback req.  CC CC  header                 crc  DD DD  14 B
//      output card -> VCC readback  AF AF  header pins(16) health crc  FA FA  31 B
//
//    Card ids: input controllers 5-8 carry STRUCT_CAB_INPUTS_SIFA_EXTENDED_SET1,
//    9-12 _SET2. Output data goes to 13 / 14; the readback request goes to
//    17 / 18 and is answered by 13 / 14 (the id less 4). message_id: 0x15
//    input request, 0x51 input data, 0x2D output data, 0x1D readback
//    request, 0xD1 readback, 0xF1 analog.
//
//  CONFIRMED BY REAL FRAMES (session 325: Abhinav's bench capture, 44 frames,
//  tests/fixtures/cabio_frames_20261023.txt)
//    - CRC: JAMCRC init 0 (the DMI frame's), over the header and payload --
//      everything between the start pair and the CRC -- stored little-endian.
//    - message_length counts the whole frame; frames are packed.
//    - A reply carries its request's seq_num; vic_health is 0x13.
//    - The readback's 16 pin bytes are the @dop1 log's (a, b, fault, trans,
//      output LSB-first); with output data 0x03E1 the card answered 0x0C for
//      each pin whose bit was 1 and 0x17 for each pin whose bit was 0.
//    The Wire format settings stay in the window, defaulting to these.
//
//  ANALOG CARD (Abhinav, session 346: the firmware's Process_Analog_Card_Data_CAN)
//
//      analog card -> VCC data      header ch1..ch6 crc        34 B
//
//    No start or end pair (session 347): the bare struct, message_length 34.
//
//    src 15 (top) / 16 (bottom), dest 1, message 0xF1. Each channel is a
//    uint32 sent big-endian (the VCC PP_HTONLs it). The VCC reads
//    mA = raw / 2^31 * (2500/33) -- integer division in C, so * 75 -- then
//    current_to_pressure(): < 3.5 mA open, > 20.5 mA short, else
//    (mA - 4) / 16 clamped to 0..1, * full scale (10 or 16 bar) * 1.02, in
//    kg/cm^2. Channels 1-3 are BP, BC, MR. No real frame captured yet.
// =============================================================================
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CabIo {

// ---- ids --------------------------------------------------------------------
constexpr quint8 kVccId = 1;

constexpr quint8 kMsgInputToVcc   = 0x51;
constexpr quint8 kMsgOutputToVcc  = 0xD1;   // output readback
constexpr quint8 kMsgAnalogToVcc  = 0xF1;
constexpr quint8 kMsgVccInputReq  = 0x15;
constexpr quint8 kMsgVccOutputReq = 0x1D;
constexpr quint8 kMsgVccOutputData = 0x2D;

constexpr quint8 kInputSof  = 0xAA, kInputEof  = 0xBB;   // to an input card
constexpr quint8 kVccRxSof  = 0xAF, kVccRxEof  = 0xFA;   // to the VCC
constexpr quint8 kOutputSof = 0xCC, kOutputEof = 0xDD;   // to an output card

constexpr quint8 kHealthy = 0x13;        // vic_health in every real reply (the struct's comment says 0x03)

// A readback pin byte, as the real output card answered (and @dop1 logs).
constexpr quint8 kPinBitSet   = 0x0C;    // a=0 b=0 fault=1 trans=1 output=0
constexpr quint8 kPinBitClear = 0x17;    // a=1 b=1 fault=1 trans=0 output=1
constexpr int    kPins = 16;

constexpr quint8 kAnalogTopId = 15, kAnalogBottomId = 16;
constexpr int    kAnalogChannels = 6;

constexpr int kHeaderSize = 6;
constexpr int kCrcSize    = 4;
constexpr int kMaxFrame   = 64;          // longest frame looked for

QString msgName(quint8 msgId);           // "input request (0x15)"
QString unitName(quint8 id);             // "VCC", "input 5", "output 13", ...
quint8  readbackCard(quint8 requestDest); // 17 -> 13, 18 -> 14

// ---- the cab type and its layouts ---------------------------------------------
enum class Cab { Sifa, Old };

// One switch on the simulator. A plain input is one bit; a contact with NO
// and NC feedbacks is two: Off = NO 0 NC 1 (at rest), On = NO 1 NC 0
// (operated), and the faults both 0 and both 1 (written NO NC: 01 10 00 11).
struct Input {
    QString name;          // "dmi1_sos" for a pair, "cab1_active" for a bit
    QString group;         // "DMI 1"
    int bit   = -1;        // a plain input
    int noBit = -1;        // a pair
    int ncBit = -1;
    bool isPair() const { return noBit >= 0; }
    quint32 mask() const;  // the bit(s) it owns
};

struct InputLayout {
    QString key;           // "sifa1", "sifa2", "old"
    QString title;         // "Set 1"
    QString structName;    // "STRUCT_CAB_INPUTS_SIFA_EXTENDED_SET1"
    QVector<quint8> ids;   // the card ids answering with it by default
    QVector<Input>  inputs;
};

// Every bit the layout's inputs own (the padding is not one of them).
quint32 layoutMask(const InputLayout &l);

const InputLayout &sifaSet1();
const InputLayout &sifaSet2();
const InputLayout &oldCabInputs();
QVector<const InputLayout *> inputLayouts(Cab cab);

struct OutputBit {
    QString name;          // STRUCT_LOCO_OUTPUT's field
    QString group;
    int     bit = 0;       // 0..15: data_byte1 bit 0 is 0, data_byte2 bit 0 is 8
    QString lamp;          // UiColor::signalLamp() name: red, yellow, green, white
    // Abhinav (session 327): in the SIFA layout the buzzers, the brakes and
    // the SIFA valve act on 0 -- 0 is the buzzer sounding, the brake
    // applied, the valve open. Every other output, and every old-cab one,
    // acts on 1 (relay energised).
    bool    activeLow = false;
    QString activeWord;    // "Applied", "Sounding", "Open", "Energised"
    QString idleWord;      // "Released", "Silent", "Closed", "De-energised"
};
const QVector<OutputBit> &outputBits(Cab cab);
// Is `o` acting (lamp lit) in the output word `word`?
bool outputActive(const OutputBit &o, quint16 word);
QString outputStructName(Cab cab);

// "dmi1_sos" -> "DMI1 SOS", "kavach_isolation_fb" -> "Kavach isolation FB",
// "dmi1_leading_non_leading" -> "DMI1 lead/non-lead"
QString displayName(const QString &name);

// ---- the input bank: what one set of controllers answers with ------------------
//
// `base` is what the operator set. Flickering inputs are XORed with their
// own mask on alternate phases: a plain input inverts, a pair goes to its
// complement (01 <-> 10, 00 <-> 11). Stopping a flicker returns to `base`.
class InputBank
{
public:
    explicit InputBank(const InputLayout *layout = nullptr) : m_layout(layout) {}
    const InputLayout *layout() const { return m_layout; }
    int count() const { return m_layout ? m_layout->inputs.size() : 0; }
    const Input &input(int i) const { return m_layout->inputs.at(i); }

    quint32 base() const { return m_base; }
    void setBase(quint32 w) { m_base = w; }
    quint32 word() const;                      // what is sent now

    // A plain input: 0/1. A pair: (NO << 1) | NC, i.e. 0..3 read as "NO NC".
    int  state(int i) const;                   // of `base`
    int  liveState(int i) const;               // of word()
    void setState(int i, int s);
    // Plain: invert. Pair: Off (01) <-> On (10); a fault (00 / 11) goes to Off.
    void toggle(int i);
    static QString stateText(const Input &in, int s);   // "1", "01"

    bool flickering(int i) const;
    void setFlicker(int i, bool on);
    int  flickerCount() const;
    void stopFlicker() { m_flicker = 0; m_phase = false; }
    void tick() { m_phase = !m_phase; }        // one flicker half-period
    bool phase() const { return m_phase; }

    void reset();                              // all 0, pairs released (NO 0 NC 1), no flicker

private:
    const InputLayout *m_layout = nullptr;
    quint32 m_base = 0;
    quint32 m_flicker = 0;                     // OR of flickering inputs' masks
    bool    m_phase = false;
};

// ---- wire format -------------------------------------------------------------------
enum class Crc { DmiJam, Crc32, Jam, None };
QString crcName(Crc c);
QString crcShortName(Crc c);           // "CRC-32", for a table cell
quint32 crcOf(Crc c, const QByteArray &data);
QVector<Crc> allCrcs();

enum class LengthRule { WholeFrame, HeaderAndPayload, Payload };
QString lengthRuleName(LengthRule r);

struct WireFormat {
    Crc        crc = Crc::DmiJam;              // the DMI frame's (capturedecoder jamcrc)
    LengthRule length = LengthRule::WholeFrame;
    bool       echoSeq = true;                 // a reply carries its request's seq_num
};

struct Header {
    quint8  length = 0;
    quint8  src = 0;
    quint8  dest = 0;
    quint8  msgId = 0;
    quint16 seq = 0;
};

struct Frame {
    quint8     sof = 0;          // the start byte (AA, AF, CC)
    Header     h;
    QByteArray payload;          // between the header and the CRC
    quint32    crc = 0;          // as on the wire
    QByteArray raw;              // the whole frame
    QVector<Crc> crcMatches;     // every algorithm that gives `crc`
    bool known = false;          // a size this file knows, not found by its end pair

    bool isInputRequest() const { return sof == kInputSof && h.msgId == kMsgVccInputReq; }
    bool isInputReply()   const { return sof == kVccRxSof && h.msgId == kMsgInputToVcc; }
    bool isOutputData()   const { return sof == kOutputSof && h.msgId == kMsgVccOutputData && payload.size() == 2; }
    bool isOutputRequest() const { return sof == kOutputSof && h.msgId == kMsgVccOutputReq; }
    bool isReadback() const { return sof == kVccRxSof && h.msgId == kMsgOutputToVcc && payload.size() == kPins + 1; }
    bool isAnalog() const { return h.msgId == kMsgAnalogToVcc && payload.size() == 4 * kAnalogChannels; }
    quint32 analogRaw(int ch) const;   // channel 0..5, as the VCC reads it (big-endian)
    QByteArray pins() const { return isReadback() ? payload.left(kPins) : QByteArray(); }
    quint32 inputWord() const;   // an input reply's input_data
    quint8  health() const;      // an input reply's or readback's health byte (the last)
    quint16 outputWord() const;  // output data: byte1 | byte2 << 8
};

quint8 eofFor(quint8 sof);       // 0 when not a start byte

// A frame, put together: SOF SOF header payload crc EOF EOF. message_length
// follows `wf.length`; h.length is ignored.
QByteArray build(quint8 sof, Header h, const QByteArray &payload, const WireFormat &wf);
QByteArray inputRequest(quint8 dest, quint16 seq, const WireFormat &wf);
QByteArray inputReply(quint8 src, quint8 dest, quint16 seq, quint32 word, quint8 health,
                      const WireFormat &wf);
QByteArray outputData(quint8 dest, quint16 seq, quint16 word, const WireFormat &wf);
QByteArray outputRequest(quint8 dest, quint16 seq, const WireFormat &wf);   // to 17 / 18
QByteArray readback(quint8 src, quint8 dest, quint16 seq, const QByteArray &pins, quint8 health,
                    const WireFormat &wf);
// The 16 pin bytes the output card answered for `word` (kPinBitSet for a 1).
QByteArray readbackPins(quint16 word);

// STRUCT_OUTPUT_PIN_READBACK: a, b, fault, trans, output, LSB-first, 3 pad.
struct PinReadback {
    bool a = false, b = false, fault = false, trans = false, output = false;
};
quint8      pinByte(const PinReadback &p);
PinReadback pinFromByte(quint8 byte);
// Abhinav (session 329): a loco output bit of 1 reads back a=0 b=0 fault=1
// trans=1 output=0 (0x0C); a 0 reads back a=1 b=1 fault=1 trans=0 output=1
// (0x17). The real cards answer exactly that (session 325).
quint8 autoPinByte(bool commanded);

// What the simulator reads back, pin by pin: Auto follows the commanded bit
// (autoPinByte), Manual sends a byte the operator set -- a fault to inject.
class ReadbackSim
{
public:
    bool   isManual(int pin) const { return m_manual.value(pin, -1) >= 0; }
    quint8 manualByte(int pin) const { return quint8(qMax(0, m_manual.value(pin, 0))); }
    void   setManual(int pin, quint8 byte) { if (pin >= 0 && pin < kPins) m_manual[pin] = byte; }
    void   setAuto(int pin) { if (pin >= 0 && pin < kPins) m_manual[pin] = -1; }
    void   allAuto() { m_manual.fill(-1); }
    int    manualCount() const;
    quint8 pinFor(int pin, quint16 word) const;
    QByteArray pins(quint16 word) const;     // the 16 bytes sent

private:
    QVector<int> m_manual = QVector<int>(kPins, -1);
};
// A readback request's answer from the card it is for: readbackCard(dest),
// to the VCC, its seq_num, `pins`.
QByteArray readbackTo(const Frame &request, const QByteArray &pins, quint8 health, const WireFormat &wf);

// ---- the analog card ----------------------------------------------------------------
// The VCC's reading of a channel: raw / 2^31 * 75 mA (the firmware's
// (double)(2500/33) is 75), and the raw value that reads as `mA`.
double  analogMa(quint32 raw);
quint32 analogRawFor(double mA);
// current_to_pressure() as the VCC runs it: kg/cm^2, set only when Ok.
struct PressureReading {
    enum Status { Ok, Open, Short } status = Ok;
    double kgcm2 = 0;
};
PressureReading currentToPressure(double mA, double fullScaleBar);
QString analogChannelName(int ch);   // "BP", "BC", "MR", "Channel 4"...
// header ch1..ch6 crc, no start or end pair; message_length follows wf.length.
QByteArray analogData(quint8 src, quint16 seq, const QVector<quint32> &raw, const WireFormat &wf);
// Exactly one analog frame (34 B, message 0xF1); false otherwise. The Scanner
// finds framed messages only, so this is how the Frames log reads one.
bool parseAnalog(const QByteArray &bytes, Frame *out);

// The reply of the card `request` is addressed to: src and dest swapped,
// seq_num echoed (or `ownSeq` when wf.echoSeq is off).
QByteArray replyTo(const Frame &request, quint32 word, quint8 health, const WireFormat &wf,
                   quint16 ownSeq);

// One frame from `bytes` (exactly a frame); false when it is not one.
bool parse(const QByteArray &bytes, Frame *out);

// Frames found in a byte stream that arrives in pieces. Bytes before a
// start pair, and a start pair whose end pair is not where it should be, are
// skipped and counted.
class Scanner
{
public:
    QVector<Frame> feed(const QByteArray &bytes);
    int  skippedBytes() const { return m_skipped; }
    int  pending() const { return m_buf.size(); }
    void clear() { m_buf.clear(); m_skipped = 0; }

private:
    QByteArray m_buf;
    int m_skipped = 0;
};

// "AA AA 0E 01 05 15 07 00 ..."
QString hex(const QByteArray &b);

}  // namespace CabIo

#endif  // CABIO_H

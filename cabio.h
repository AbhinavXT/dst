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
//      VCC -> input card request   AA AA  header            crc32  BB BB  14 B
//      input card -> VCC reply     AF AF  header data health crc32  FA FA  19 B
//      VCC -> output card data     CC CC  header byte1 byte2 crc32  DD DD  16 B
//
//    Card ids: input controllers 5-8 carry STRUCT_CAB_INPUTS_SIFA_EXTENDED_SET1,
//    9-12 _SET2; output data goes to 13 / 14; output readback comes from
//    17 / 18. message_id: 0x15 request, 0x51 input reply, 0x1D output
//    request, 0x2D output data, 0xD1 output readback, 0xF1 analog.
//
//  PROVISIONAL UNTIL SAMPLE FRAMES ARRIVE
//    Three things the structs do not say, so the operator picks them (Wire
//    format, in the window) instead of the tool guessing silently: the CRC
//    algorithm, what message_length counts, and that the frames are packed
//    (the sizes above are the packed sizes). The CRC covers the header and
//    the payload -- everything between the start pair and the CRC -- and is
//    stored little-endian. A received frame's CRC is checked against every
//    algorithm, so the first real VCC frame says which one it is.
//
//    The readback (0xD1) and analog (0xF1) payloads are not known: those
//    frames are found and shown, never built.
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

constexpr quint8 kHealthy = 0x03;        // vic_health in the sample struct

constexpr int kHeaderSize = 6;
constexpr int kCrcSize    = 4;
constexpr int kMaxFrame   = 64;          // longest frame looked for

QString msgName(quint8 msgId);           // "input request (0x15)"
QString unitName(quint8 id);             // "VCC", "input 5", "output 13", ...

// ---- the cab type and its layouts ---------------------------------------------
enum class Cab { Sifa, Old };

// One switch on the simulator. A plain input is one bit; a contact with NO
// and NC feedbacks is two, with four states (00 01 10 11, written NO NC).
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

const InputLayout &sifaSet1();
const InputLayout &sifaSet2();
const InputLayout &oldCabInputs();
QVector<const InputLayout *> inputLayouts(Cab cab);

struct OutputBit {
    QString name;          // STRUCT_LOCO_OUTPUT's field
    QString group;
    int     bit = 0;       // 0..15: data_byte1 bit 0 is 0, data_byte2 bit 0 is 8
    QString lamp;          // UiColor::signalLamp() name: red, yellow, green, white
};
const QVector<OutputBit> &outputBits(Cab cab);
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
    void toggle(int i);                        // plain: invert; pair: next of 00 01 10 11
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
    bool isOutputRequest() const { return h.msgId == kMsgVccOutputReq; }
    quint32 inputWord() const;   // an input reply's input_data
    quint8  health() const;      // an input reply's vic_health
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

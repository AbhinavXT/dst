#include "cabio.h"

#include "capturedecoder.h"

#include <QSet>

namespace CabIo {

// ---- names ------------------------------------------------------------------------

QString msgName(quint8 msgId)
{
    const QString code = QStringLiteral("0x") + QString::number(msgId, 16).toUpper().rightJustified(2, QLatin1Char('0'));
    switch (msgId) {
    case kMsgVccInputReq:   return QStringLiteral("input request (%1)").arg(code);
    case kMsgInputToVcc:    return QStringLiteral("input data (%1)").arg(code);
    case kMsgVccOutputReq:  return QStringLiteral("readback request (%1)").arg(code);
    case kMsgVccOutputData: return QStringLiteral("output data (%1)").arg(code);
    case kMsgOutputToVcc:   return QStringLiteral("readback data (%1)").arg(code);
    case kMsgAnalogToVcc:   return QStringLiteral("analog data (%1)").arg(code);
    default:                return QStringLiteral("message %1").arg(code);
    }
}

QString unitName(quint8 id)
{
    if (id == kVccId)              return QStringLiteral("VCC");
    if (id >= 5 && id <= 12)       return QStringLiteral("input %1").arg(id);
    if (id == 13 || id == 14)      return QStringLiteral("output %1").arg(id);
    if (id == kAnalogTopId)        return QStringLiteral("analog top 15");
    if (id == kAnalogBottomId)     return QStringLiteral("analog bottom 16");
    if (id == 17 || id == 18)      return QStringLiteral("readback %1").arg(id);
    return QStringLiteral("unit %1").arg(id);
}

quint8 readbackCard(quint8 requestDest) { return quint8(requestDest - 4); }

QString displayName(const QString &name)
{
    static const QSet<QString> upper{
        QStringLiteral("dmi"), QStringLiteral("dmi1"), QStringLiteral("dmi2"), QStringLiteral("sos"),
        QStringLiteral("nc"), QStringLiteral("no"), QStringLiteral("biu"), QStringLiteral("mr"),
        QStringLiteral("bp"), QStringLiteral("em"), QStringLiteral("nb"), QStringLiteral("fsb"),
        QStringLiteral("eb"), QStringLiteral("le"), QStringLiteral("veb"), QStringLiteral("pvef"),
        QStringLiteral("qrv"), QStringLiteral("pg"), QStringLiteral("bl"), QStringLiteral("fb"),
        QStringLiteral("sifa"), QStringLiteral("led") };
    QString n = name;
    n.replace(QLatin1String("leading_non_leading"), QLatin1String("lead/non-lead"));
    QStringList parts = n.split(QLatin1Char('_'), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        QString &p = parts[i];
        if (upper.contains(p.toLower())) p = p.toUpper();
        else if (i == 0) p[0] = p[0].toUpper();
    }
    return parts.join(QLatin1Char(' '));
}

// ---- layouts ----------------------------------------------------------------------

quint32 Input::mask() const
{
    if (isPair()) return (1u << noBit) | (1u << ncBit);
    return bit >= 0 ? (1u << bit) : 0u;
}

namespace {

Input bitIn(const char *name, const char *group, int bit)
{
    Input in;
    in.name = QString::fromLatin1(name);
    in.group = QString::fromLatin1(group);
    in.bit = bit;
    return in;
}

Input pairIn(const char *name, const char *group, int noBit, int ncBit)
{
    Input in;
    in.name = QString::fromLatin1(name);
    in.group = QString::fromLatin1(group);
    in.noBit = noBit;
    in.ncBit = ncBit;
    return in;
}

}  // namespace

// STRUCT_CAB_INPUTS_SIFA_EXTENDED_SET1: bit i is the struct's i-th bitfield,
// LSB-first (as kavach.xml's DIP1). The NC bit comes before the NO bit here.
const InputLayout &sifaSet1()
{
    static const InputLayout l = [] {
        InputLayout x;
        x.key = QStringLiteral("sifa1");
        x.title = QStringLiteral("Set 1");
        x.structName = QStringLiteral("STRUCT_CAB_INPUTS_SIFA_EXTENDED_SET1");
        x.ids = { 5, 6, 7, 8 };
        x.inputs = {
            pairIn("dmi1_sos",                 "DMI 1", 1, 0),
            pairIn("dmi1_cancel",              "DMI 1", 3, 2),
            pairIn("dmi1_common",              "DMI 1", 5, 4),
            pairIn("dmi1_leading_non_leading", "DMI 1", 7, 6),
            pairIn("dmi2_sos",                 "DMI 2", 9, 8),
            pairIn("dmi2_cancel",              "DMI 2", 11, 10),
            pairIn("dmi2_common",              "DMI 2", 13, 12),
            pairIn("dmi2_leading_non_leading", "DMI 2", 15, 14),
            bitIn("cab1_active",               "Cab 1", 23),
            bitIn("cab1_forward",              "Cab 1", 24),
            bitIn("cab1_reverse",              "Cab 1", 25),
            bitIn("cab2_active",               "Cab 2", 26),
            bitIn("cab2_forward",              "Cab 2", 27),
            bitIn("cab2_reverse",              "Cab 2", 28),
            bitIn("horn1_fb_cock_no",          "Horns", 19),
            bitIn("horn1_solenoid_no",         "Horns", 20),
            bitIn("horn2_fb_cock_no",          "Horns", 21),
            bitIn("horn2_solenoid_no",         "Horns", 22),
            bitIn("pvef_disabled_fb",          "Feedbacks", 16),
            bitIn("neutral_section_fb",        "Feedbacks", 17),
            bitIn("fouling_mark_entry_fb",     "Feedbacks", 18),
            bitIn("traction_cutoff_fb",        "Feedbacks", 30),
            bitIn("train_intigrity",           "Feedbacks", 29),
        };
        return x;
    }();
    return l;
}

// STRUCT_CAB_INPUTS_SIFA_EXTENDED_SET2. NO before NC, except the SIFA/VEB
// relay at 28/29 (swapped in the firmware: NC is bit 28).
const InputLayout &sifaSet2()
{
    static const InputLayout l = [] {
        InputLayout x;
        x.key = QStringLiteral("sifa2");
        x.title = QStringLiteral("Set 2");
        x.structName = QStringLiteral("STRUCT_CAB_INPUTS_SIFA_EXTENDED_SET2");
        x.ids = { 9, 10, 11, 12 };
        x.inputs = {
            pairIn("kavach_isolation_fb",     "Isolation", 0, 1),
            pairIn("biu_isolation_fb",        "Isolation", 2, 3),
            bitIn("mr_cock_fb_no",            "Cocks", 4),
            bitIn("bp_cock_fb_no",            "Cocks", 5),
            pairIn("fsb_relay_fb",            "Brake relays", 20, 21),
            pairIn("nb_relay_fb",             "Brake relays", 22, 23),
            pairIn("eb_relay_fb",             "Brake relays", 24, 25),
            pairIn("le_relay_fb",             "Brake relays", 26, 27),
            pairIn("sifa_valve_fb",           "SIFA", 6, 7),
            pairIn("sifa_isolation_cock_fb",  "SIFA", 8, 9),
            pairIn("sifa_or_veb_relay_fb",    "SIFA", 29, 28),
        };
        return x;
    }();
    return l;
}

// STRUCT_CAB_INPUT (the old cab). Bit 17 is padding.
const InputLayout &oldCabInputs()
{
    static const InputLayout l = [] {
        InputLayout x;
        x.key = QStringLiteral("old");
        x.title = QStringLiteral("Cab inputs");
        x.structName = QStringLiteral("STRUCT_CAB_INPUT");
        x.ids = { 5, 6, 7, 8, 9, 10, 11, 12 };      // all eight controllers (Abhinav, session 327)
        x.inputs = {
            bitIn("cab1_input",               "Cab 1", 12),
            bitIn("bl_key_cab1",              "Cab 1", 21),
            bitIn("cab1_leading_non_leading", "Cab 1", 18),
            bitIn("sos_cab1",                 "Cab 1", 31),
            bitIn("cancel_cab1",              "Cab 1", 29),
            bitIn("comm_cab1",                "Cab 1", 30),
            bitIn("cab2_input",               "Cab 2", 13),
            bitIn("bl_key_cab2",              "Cab 2", 22),
            bitIn("cab2_leading_non_leading", "Cab 2", 19),
            bitIn("sos_cab2",                 "Cab 2", 26),
            bitIn("cancel_cab2",              "Cab 2", 24),
            bitIn("comm_cab2",                "Cab 2", 25),
            bitIn("run_forward",              "Direction", 14),
            bitIn("run_reverse",              "Direction", 15),
            bitIn("pg_direction",             "Direction", 16),
            bitIn("biu_status",               "Cocks and isolation", 0),
            bitIn("mr_cock",                  "Cocks and isolation", 1),
            bitIn("bp_cock",                  "Cocks and isolation", 2),
            bitIn("em_cock",                  "Cocks and isolation", 3),
            bitIn("em_cock_fail",             "Cocks and isolation", 23),
            bitIn("kavach_isolation",         "Cocks and isolation", 20),
            bitIn("mr_pressure_fdbk",         "Pressure feedback", 4),
            bitIn("bp_pressure_fdbk",         "Pressure feedback", 5),
            bitIn("nb_pressure_fdbk",         "Pressure feedback", 6),
            bitIn("fsb_pressure_fdbk",        "Pressure feedback", 7),
            bitIn("eb_pressure_fdbk",         "Pressure feedback", 8),
            bitIn("nb_command_fdbk",          "Brake command feedback", 9),
            bitIn("fsb_command_fdbk",         "Brake command feedback", 10),
            bitIn("eb_command_fdbk",          "Brake command feedback", 11),
            bitIn("ack",                      "Other", 27),
            bitIn("qrv_fb",                   "Other", 28),
        };
        return x;
    }();
    return l;
}

quint32 layoutMask(const InputLayout &l)
{
    quint32 m = 0;
    for (const Input &in : l.inputs) m |= in.mask();
    return m;
}

QVector<const InputLayout *> inputLayouts(Cab cab)
{
    if (cab == Cab::Old) return { &oldCabInputs() };
    return { &sifaSet1(), &sifaSet2() };
}

const QVector<OutputBit> &outputBits(Cab cab)
{
    // The words follow what the output drives; `low` is set for the SIFA
    // layout's active-low outputs.
    auto ob = [](const char *n, const char *g, int bit, const char *lamp, bool low = false) {
        OutputBit o;
        o.name = QString::fromLatin1(n);
        o.group = QString::fromLatin1(g);
        o.bit = bit;
        o.lamp = QString::fromLatin1(lamp);
        o.activeLow = low;
        const QString name = o.name;
        if (name.contains(QLatin1String("buzzer"))) {
            o.activeWord = QStringLiteral("Sounding"); o.idleWord = QStringLiteral("Silent");
        } else if (name.endsWith(QLatin1String("_brake"))) {
            o.activeWord = QStringLiteral("Applied"); o.idleWord = QStringLiteral("Released");
        } else if (name == QLatin1String("sifa_valve")) {
            o.activeWord = QStringLiteral("Open"); o.idleWord = QStringLiteral("Closed");
        } else {
            o.activeWord = QStringLiteral("Energised"); o.idleWord = QStringLiteral("De-energised");
        }
        return o;
    };
    // The SIFA-extended STRUCT_LOCO_OUTPUT (data_byte1 = bits 0-7).
    static const QVector<OutputBit> sifa{
        ob("dmi_sos_red",        "DMI", 0, "red"),
        ob("dmi_sos_green",      "DMI", 1, "green"),
        ob("dmi1_buzzer",        "DMI", 2, "yellow", true),
        ob("dmi2_buzzer",        "DMI", 3, "yellow", true),
        ob("horn1",              "Horns", 4, "yellow"),
        ob("horn2",              "Horns", 5, "yellow"),
        ob("light_engine_brake", "Brakes", 10, "red", true),
        ob("full_service_brake", "Brakes", 11, "red", true),
        ob("normal_brake",       "Brakes", 12, "red", true),
        ob("emergency_brake",    "Brakes", 13, "red", true),
        ob("traction_on_off",    "Traction and track", 6, "yellow"),
        ob("pvef_disable",       "Traction and track", 7, "yellow"),
        ob("nuteral_section",    "Traction and track", 8, "yellow"),
        ob("fouling_mark",       "Traction and track", 9, "yellow"),
        ob("sifa_valve",         "SIFA", 14, "yellow", true),
        ob("spare",              "Spare", 15, "white"),
    };
    // The old cab's STRUCT_LOCO_OUTPUT: ten outputs and spare: 6.
    static const QVector<OutputBit> old{
        ob("kavach_ok",          "Kavach", 0, "green"),
        ob("kavach_not_ok",      "Kavach", 1, "red"),
        ob("sos",                "SOS", 2, "red"),
        ob("no_sos_green_led",   "SOS", 8, "green"),
        ob("buzzer",             "Alerts", 3, "yellow"),
        ob("horn",               "Alerts", 4, "yellow"),
        ob("light_engine_brake", "Brakes", 9, "red"),
        ob("normal_brake",       "Brakes", 5, "red"),
        ob("full_service_brake", "Brakes", 6, "red"),
        ob("emergency_brake",    "Brakes", 7, "red"),
        ob("spare_10",           "Spare", 10, "white"),
        ob("spare_11",           "Spare", 11, "white"),
        ob("spare_12",           "Spare", 12, "white"),
        ob("spare_13",           "Spare", 13, "white"),
        ob("spare_14",           "Spare", 14, "white"),
        ob("spare_15",           "Spare", 15, "white"),
    };
    return cab == Cab::Old ? old : sifa;
}

bool outputActive(const OutputBit &o, quint16 word)
{
    const bool set = (word >> o.bit) & 1u;
    return o.activeLow ? !set : set;
}

QString outputStructName(Cab cab)
{
    return cab == Cab::Old ? QStringLiteral("STRUCT_LOCO_OUTPUT (old cab)")
                           : QStringLiteral("STRUCT_LOCO_OUTPUT (SIFA extended)");
}

// ---- the input bank ----------------------------------------------------------------

quint32 InputBank::word() const
{
    return m_phase ? (m_base ^ m_flicker) : m_base;
}

static int stateOf(const Input &in, quint32 w)
{
    if (in.isPair())
        return (((w >> in.noBit) & 1u) << 1) | ((w >> in.ncBit) & 1u);
    return in.bit >= 0 ? int((w >> in.bit) & 1u) : 0;
}

int InputBank::state(int i) const { return stateOf(input(i), m_base); }
int InputBank::liveState(int i) const { return stateOf(input(i), word()); }

void InputBank::setState(int i, int s)
{
    const Input &in = input(i);
    m_base &= ~in.mask();
    if (in.isPair()) {
        if (s & 2) m_base |= 1u << in.noBit;
        if (s & 1) m_base |= 1u << in.ncBit;
    } else if (in.bit >= 0 && (s & 1)) {
        m_base |= 1u << in.bit;
    }
}

void InputBank::toggle(int i)
{
    const Input &in = input(i);
    setState(i, in.isPair() ? (state(i) == 1 ? 2 : 1) : (state(i) ^ 1));
}

QString InputBank::stateText(const Input &in, int s)
{
    if (in.isPair()) return QStringLiteral("%1%2").arg((s >> 1) & 1).arg(s & 1);
    return QString::number(s & 1);
}

bool InputBank::flickering(int i) const { return (m_flicker & input(i).mask()) != 0; }

void InputBank::setFlicker(int i, bool on)
{
    if (on) m_flicker |= input(i).mask();
    else    m_flicker &= ~input(i).mask();
    if (!m_flicker) m_phase = false;
}

int InputBank::flickerCount() const
{
    int n = 0;
    for (int i = 0; i < count(); ++i) n += flickering(i) ? 1 : 0;
    return n;
}

void InputBank::reset()
{
    m_base = 0;
    m_flicker = 0;
    m_phase = false;
    for (int i = 0; i < count(); ++i)
        if (input(i).isPair()) setState(i, 1);     // NO 0, NC 1: the contact at rest
}

// ---- CRC ---------------------------------------------------------------------------

QVector<Crc> allCrcs() { return { Crc::DmiJam, Crc::Crc32, Crc::Jam, Crc::None }; }

QString crcName(Crc c)
{
    switch (c) {
    case Crc::DmiJam: return QStringLiteral("JAMCRC, init 0 (as the DMI frame)");
    case Crc::Crc32:  return QStringLiteral("CRC-32 (zlib)");
    case Crc::Jam:    return QStringLiteral("JAMCRC (CRC-32 without final XOR)");
    case Crc::None:   return QStringLiteral("None (zero)");
    }
    return QString();
}

QString crcShortName(Crc c)
{
    switch (c) {
    case Crc::DmiJam: return QStringLiteral("JAMCRC/0");
    case Crc::Crc32:  return QStringLiteral("CRC-32");
    case Crc::Jam:    return QStringLiteral("JAMCRC");
    case Crc::None:   return QStringLiteral("none");
    }
    return QString();
}

static quint32 reflected(const QByteArray &d, quint32 init, quint32 xorOut)
{
    quint32 c = init;
    for (char ch : d) {
        c ^= quint8(ch);
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ xorOut;
}

quint32 crcOf(Crc c, const QByteArray &data)
{
    switch (c) {
    case Crc::DmiJam: return CaptureDecoder::jamcrc(data, 0, data.size());
    case Crc::Crc32:  return reflected(data, 0xFFFFFFFFu, 0xFFFFFFFFu);
    case Crc::Jam:    return reflected(data, 0xFFFFFFFFu, 0);
    case Crc::None:   return 0;
    }
    return 0;
}

QString lengthRuleName(LengthRule r)
{
    switch (r) {
    case LengthRule::WholeFrame:       return QStringLiteral("Whole frame (start to end bytes)");
    case LengthRule::HeaderAndPayload: return QStringLiteral("Header + payload");
    case LengthRule::Payload:          return QStringLiteral("Payload only");
    }
    return QString();
}

// ---- frames --------------------------------------------------------------------------

quint8 eofFor(quint8 sof)
{
    switch (sof) {
    case kInputSof:  return kInputEof;
    case kVccRxSof:  return kVccRxEof;
    case kOutputSof: return kOutputEof;
    default:         return 0;
    }
}

static int knownSize(quint8 sof, quint8 msgId)
{
    const int fixed = 2 + kHeaderSize + kCrcSize + 2;
    if (sof == kInputSof && msgId == kMsgVccInputReq)    return fixed;
    if (sof == kVccRxSof && msgId == kMsgInputToVcc)     return fixed + 5;
    if (sof == kOutputSof && msgId == kMsgVccOutputData) return fixed + 2;
    if (sof == kOutputSof && msgId == kMsgVccOutputReq)  return fixed;
    if (sof == kVccRxSof && msgId == kMsgOutputToVcc)    return fixed + kPins + 1;
    if (sof == kVccRxSof && msgId == kMsgAnalogToVcc)    return fixed + 4 * kAnalogChannels;
    return 0;
}

static void putLe(QByteArray *b, quint32 v, int n)
{
    for (int i = 0; i < n; ++i) b->append(char((v >> (8 * i)) & 0xFF));
}

static quint32 getLe(const QByteArray &b, int at, int n)
{
    quint32 v = 0;
    for (int i = 0; i < n; ++i) v |= quint32(quint8(b.at(at + i))) << (8 * i);
    return v;
}

QByteArray build(quint8 sof, Header h, const QByteArray &payload, const WireFormat &wf)
{
    const int whole = 2 + kHeaderSize + payload.size() + kCrcSize + 2;
    switch (wf.length) {
    case LengthRule::WholeFrame:       h.length = quint8(whole); break;
    case LengthRule::HeaderAndPayload: h.length = quint8(kHeaderSize + payload.size()); break;
    case LengthRule::Payload:          h.length = quint8(payload.size()); break;
    }
    QByteArray body;
    body.append(char(h.length));
    body.append(char(h.src));
    body.append(char(h.dest));
    body.append(char(h.msgId));
    putLe(&body, h.seq, 2);
    body.append(payload);

    QByteArray f;
    f.append(char(sof)).append(char(sof));
    f.append(body);
    putLe(&f, crcOf(wf.crc, body), 4);
    const char e = char(eofFor(sof));
    f.append(e).append(e);
    return f;
}

QByteArray inputRequest(quint8 dest, quint16 seq, const WireFormat &wf)
{
    Header h;
    h.src = kVccId;
    h.dest = dest;
    h.msgId = kMsgVccInputReq;
    h.seq = seq;
    return build(kInputSof, h, QByteArray(), wf);
}

QByteArray inputReply(quint8 src, quint8 dest, quint16 seq, quint32 word, quint8 health,
                      const WireFormat &wf)
{
    Header h;
    h.src = src;
    h.dest = dest;
    h.msgId = kMsgInputToVcc;
    h.seq = seq;
    QByteArray p;
    putLe(&p, word, 4);
    p.append(char(health));
    return build(kVccRxSof, h, p, wf);
}

QByteArray outputData(quint8 dest, quint16 seq, quint16 word, const WireFormat &wf)
{
    Header h;
    h.src = kVccId;
    h.dest = dest;
    h.msgId = kMsgVccOutputData;
    h.seq = seq;
    QByteArray p;
    putLe(&p, word, 2);
    return build(kOutputSof, h, p, wf);
}

QByteArray outputRequest(quint8 dest, quint16 seq, const WireFormat &wf)
{
    Header h;
    h.src = kVccId;
    h.dest = dest;
    h.msgId = kMsgVccOutputReq;
    h.seq = seq;
    return build(kOutputSof, h, QByteArray(), wf);
}

QByteArray readback(quint8 src, quint8 dest, quint16 seq, const QByteArray &pins, quint8 health,
                    const WireFormat &wf)
{
    Header h;
    h.src = src;
    h.dest = dest;
    h.msgId = kMsgOutputToVcc;
    h.seq = seq;
    QByteArray p = pins.left(kPins);
    p.append(QByteArray(kPins - p.size(), char(kPinBitClear)));
    p.append(char(health));
    return build(kVccRxSof, h, p, wf);
}

double analogMa(quint32 raw)
{
    return raw / 2147483648.0 * 75.0;
}

quint32 analogRawFor(double mA)
{
    const double r = mA / 75.0 * 2147483648.0;
    return r <= 0 ? 0u : r >= 4294967295.0 ? 0xFFFFFFFFu : quint32(r + 0.5);
}

PressureReading currentToPressure(double mA, double fullScaleBar)
{
    // The firmware takes a float: compare as it does.
    const float ma = float(mA);
    PressureReading r;
    if (ma < 3.5f)  { r.status = PressureReading::Open;  return r; }
    if (ma > 20.5f) { r.status = PressureReading::Short; return r; }
    float fraction = (ma - 4.0f) / 16.0f;
    fraction = qBound(0.0f, fraction, 1.0f);
    r.kgcm2 = float(fraction * float(fullScaleBar) * 1.02);
    return r;
}

QString analogChannelName(int ch)
{
    static const char *const names[3] = { "BP", "BC", "MR" };
    return ch >= 0 && ch < 3 ? QString::fromLatin1(names[ch]) : QStringLiteral("Channel %1").arg(ch + 1);
}

QByteArray analogData(quint8 src, quint16 seq, const QVector<quint32> &raw, const WireFormat &wf)
{
    Header h;
    h.src = src;
    h.dest = kVccId;
    h.msgId = kMsgAnalogToVcc;
    h.seq = seq;
    QByteArray p;
    for (int ch = 0; ch < kAnalogChannels; ++ch) {
        const quint32 v = raw.value(ch);
        for (int i = 3; i >= 0; --i) p.append(char((v >> (8 * i)) & 0xFF));
    }
    return build(kVccRxSof, h, p, wf);
}

QByteArray readbackPins(quint16 word)
{
    return ReadbackSim().pins(word);
}

quint8 pinByte(const PinReadback &p)
{
    return quint8((p.a ? 0x01 : 0) | (p.b ? 0x02 : 0) | (p.fault ? 0x04 : 0) | (p.trans ? 0x08 : 0)
                  | (p.output ? 0x10 : 0));
}

PinReadback pinFromByte(quint8 byte)
{
    PinReadback p;
    p.a = byte & 0x01;
    p.b = byte & 0x02;
    p.fault = byte & 0x04;
    p.trans = byte & 0x08;
    p.output = byte & 0x10;
    return p;
}

quint8 autoPinByte(bool commanded) { return commanded ? kPinBitSet : kPinBitClear; }

int ReadbackSim::manualCount() const
{
    int n = 0;
    for (int m : m_manual) n += m >= 0 ? 1 : 0;
    return n;
}

quint8 ReadbackSim::pinFor(int pin, quint16 word) const
{
    return isManual(pin) ? manualByte(pin) : autoPinByte((word >> pin) & 1u);
}

QByteArray ReadbackSim::pins(quint16 word) const
{
    QByteArray b;
    for (int i = 0; i < kPins; ++i) b.append(char(pinFor(i, word)));
    return b;
}

QByteArray readbackTo(const Frame &request, const QByteArray &pins, quint8 health, const WireFormat &wf)
{
    return readback(readbackCard(request.h.dest), request.h.src, request.h.seq, pins, health, wf);
}

QByteArray replyTo(const Frame &request, quint32 word, quint8 health, const WireFormat &wf,
                   quint16 ownSeq)
{
    return inputReply(request.h.dest, request.h.src, wf.echoSeq ? request.h.seq : ownSeq,
                      word, health, wf);
}

quint32 Frame::inputWord() const { return payload.size() >= 4 ? getLe(payload, 0, 4) : 0; }
quint8  Frame::health() const
{
    return (isInputReply() || isReadback()) && !payload.isEmpty() ? quint8(payload.at(payload.size() - 1)) : 0;
}
quint16 Frame::outputWord() const { return payload.size() >= 2 ? quint16(getLe(payload, 0, 2)) : 0; }
quint32 Frame::analogRaw(int ch) const
{
    if (!isAnalog() || ch < 0 || ch >= kAnalogChannels) return 0;
    quint32 v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | quint8(payload.at(4 * ch + i));
    return v;
}

static Frame makeFrame(const QByteArray &raw, bool known)
{
    Frame f;
    f.raw = raw;
    f.known = known;
    f.sof = quint8(raw.at(0));
    f.h.length = quint8(raw.at(2));
    f.h.src    = quint8(raw.at(3));
    f.h.dest   = quint8(raw.at(4));
    f.h.msgId  = quint8(raw.at(5));
    f.h.seq    = quint16(getLe(raw, 6, 2));
    const int n = raw.size();
    f.payload = raw.mid(2 + kHeaderSize, n - (2 + kHeaderSize) - kCrcSize - 2);
    f.crc = getLe(raw, n - 2 - kCrcSize, 4);
    const QByteArray covered = raw.mid(2, n - 4 - kCrcSize);
    for (Crc c : allCrcs())
        if (crcOf(c, covered) == f.crc) f.crcMatches << c;
    return f;
}

QVector<Frame> Scanner::feed(const QByteArray &bytes)
{
    m_buf.append(bytes);
    QVector<Frame> out;
    const int minFrame = 2 + kHeaderSize + kCrcSize + 2;
    for (;;) {
        // The first start pair.
        int at = -1;
        for (int i = 0; i + 1 < m_buf.size(); ++i) {
            const quint8 b = quint8(m_buf.at(i));
            if (eofFor(b) && quint8(m_buf.at(i + 1)) == b) { at = i; break; }
        }
        if (at < 0) {
            // Keep a last byte that may start a pair.
            const int keep = (!m_buf.isEmpty() && eofFor(quint8(m_buf.at(m_buf.size() - 1)))) ? 1 : 0;
            m_skipped += m_buf.size() - keep;
            m_buf = m_buf.right(keep);
            return out;
        }
        if (at > 0) {
            m_skipped += at;
            m_buf.remove(0, at);
        }
        if (m_buf.size() < 2 + kHeaderSize) return out;

        const quint8 sof = quint8(m_buf.at(0));
        const quint8 eof = eofFor(sof);
        auto endsAt = [&](int size) {
            return size <= m_buf.size() && quint8(m_buf.at(size - 2)) == eof
                   && quint8(m_buf.at(size - 1)) == eof;
        };
        const int known = knownSize(sof, quint8(m_buf.at(5)));
        if (known) {
            if (m_buf.size() < known) return out;
            if (endsAt(known)) {
                out << makeFrame(m_buf.left(known), true);
                m_buf.remove(0, known);
                continue;
            }
        }
        // A size this file does not know (or the known one did not end
        // right): the first end pair after the shortest possible frame.
        int size = 0;
        for (int s = minFrame; s <= qMin(int(m_buf.size()), kMaxFrame); ++s)
            if (endsAt(s)) { size = s; break; }
        if (size) {
            out << makeFrame(m_buf.left(size), false);
            m_buf.remove(0, size);
            continue;
        }
        if (m_buf.size() < kMaxFrame) return out;     // its end may still come
        m_skipped += 1;                                // not a frame: past this start
        m_buf.remove(0, 1);
    }
}

bool parse(const QByteArray &bytes, Frame *out)
{
    Scanner s;
    const QVector<Frame> f = s.feed(bytes);
    if (f.size() != 1 || f.first().raw != bytes) return false;
    if (out) *out = f.first();
    return true;
}

QString hex(const QByteArray &b)
{
    QString s;
    s.reserve(b.size() * 3);
    for (int i = 0; i < b.size(); ++i) {
        if (i) s += QLatin1Char(' ');
        s += QStringLiteral("%1").arg(quint8(b.at(i)), 2, 16, QLatin1Char('0')).toUpper();
    }
    return s;
}

}  // namespace CabIo

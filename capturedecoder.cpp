#include "capturedecoder.h"
#include "schema/schemaencoder.h"
#include "sessionkeystore.h"
#include "schema/schemadecoder.h"   // global scope: declares namespace Schema + pulls in QtXml

#include <QStringList>

namespace {

// 57 NMS health events, generated from g_event_table (nms_data_manager.c).
const NmsEventDef kNmsEventTable[] = {
    {   1, 1, false, "RADIO_1_HEALTH" },
    {   2, 1, false, "RADIO_2_HEALTH" },
    {   3, 1, false, "RADIO_1_INPUT_SUPPLY" },
    {   4, 1, false, "RADIO_2_INPUT_SUPPLY" },
    {   5, 1,  true, "RADIO_1_TEMP" },
    {   6, 1,  true, "RADIO_2_TEMP" },
    {   7, 1, false, "RADIO_1_PA_TEMP" },
    {   8, 1, false, "RADIO_2_PA_TEMP" },
    {   9, 1, false, "RADIO_1_PA_SUPPLY_VOLTAGE" },
    {  10, 1, false, "RADIO_2_PA_SUPPLY_VOLTAGE" },
    {  11, 1, false, "RADIO_1_TX_PA_CURRENT" },
    {  12, 1, false, "RADIO_2_TX_PA_CURRENT" },
    {  13, 1, false, "RADIO_1_REVERSE_POWER" },
    {  14, 1, false, "RADIO_2_REVERSE_POWER" },
    {  15, 1, false, "RADIO_1_FORWARD_POWER" },
    {  16, 1, false, "RADIO_2_FORWARD_POWER" },
    {  17, 2, false, "SKAVACH_REGULAR_PKT_OFFSET" },
    {  18, 1, false, "ACTIVE_GPS_NUMBER" },
    {  19, 1, false, "GPS_1_VIEW_STATUS" },
    {  20, 1, false, "GPS_2_VIEW_STATUS" },
    {  21, 1, false, "GPS_1_SECONDS" },
    {  22, 1, false, "GPS_2_SECONDS" },
    {  23, 1, false, "GPS_1_SATELLITE_IN_VIEW" },
    {  24, 1, false, "GPS_1_CNO" },
    {  25, 1, false, "GPS_2_SATELLITE_IN_VIEW" },
    {  26, 1, false, "GPS_2_CNO" },
    {  27, 2, false, "GPS_1_LINK_STATUS" },
    {  28, 2, false, "GPS_2_LINK_STATUS" },
    {  29, 1, false, "GSM_1_RSSI" },
    {  30, 1, false, "GSM_2_RSSI" },
    {  31, 1, false, "CURRENT_RUNNING_KEY" },
    {  32, 1, false, "REMAINING_KEY_NUMBERS" },
    {  33, 2, false, "SESSION_KEY_CHECKSUM" },
    {  34, 2, false, "DMI_1_LINK_STATUS" },
    {  35, 2, false, "DMI_2_LINK_STATUS" },
    {  36, 2, false, "RFID_READER_1_LINK_STATUS" },
    {  37, 2, false, "RFID_READER_2_LINK_STATUS" },
    {  38, 2, false, "DUPLICATE_MISSING_RFID" },
    {  39, 4, false, "MISSING_LINKED_RFID" },
    {  40, 4, false, "COMPUTED_TLM_STATUS" },
    {  41, 1, false, "TRAIN_CONFIG_CHANGE" },
    {  42, 1, false, "BOOTUP_SEQUENCE_ERROR" },
    {  43, 1, false, "SELECTED_TRAIN_FORMATION" },
    {  44, 1, false, "SELECTED_CAB" },
    {  45, 1, false, "BRAKE_APPLICATION_REASON" },
    {  46, 3, false, "STATION_GENERAL_SOS" },
    {  47, 3, false, "STATION_LOCO_SPECIFIC_SOS" },
    {  48, 4, false, "COLLISION_DETECTION" },
    {  49, 1, false, "LOCO_SELF_SOS" },
    {  50, 1, false, "KAVACH_CONNECTION" },
    {  51, 1, false, "BIU_ISOLATED" },
    {  52, 1, false, "EB_BYPASSED" },
    {  53, 1, false, "KAVACH_TERRITORY" },
    {  54, 1, false, "BRAKE_INTERFACE_ERROR" },
    {  55, 2, false, "ONBOARD_KAVACH_MODULES_HEALTH" },
    {  56, 2, false, "CONFLICT_ROUTE_RFID" },
    {  57, 4, false, "TRAIN_CONFIG_DATA_CHECKSUM" },
};
const int kNmsEventCount = 57;

quint8  reflect8 (quint8 v) {
    quint8 r = 0;
    for (int i = 0; i < 8; ++i) { if (v & (1u << i)) r |= (quint8)(1u << (7 - i)); }
    return r;
}
quint32 reflect32(quint32 v) {
    quint32 r = 0;
    for (int i = 0; i < 32; ++i) { if (v & (1u << i)) r |= (1u << (31 - i)); }
    return r;
}

// Every accessor below indexes a QByteArray with an offset derived from
// frame contents. QByteArray::operator[] does not bounds-check, and with
// QT_NO_DEBUG_OUTPUT set in the .pro the Q_ASSERT inside at() is compiled
// out of release builds — so an out-of-range read was silent rather than
// loud. These wrappers make the failure mode "zero" instead of "whatever
// was next in the heap". Callers that already validate lose nothing; the
// next decoder someone adds gets the guard for free.
inline bool inRange(const QByteArray &b, int i, int len) {
    return i >= 0 && len >= 0 && len <= b.size() && i <= b.size() - len;
}
inline quint8 byteAt(const QByteArray &b, int i) {
    return inRange(b, i, 1) ? quint8(b[i]) : quint8(0);
}

quint32 beU16(const QByteArray &b, int i) {
    if (!inRange(b, i, 2)) return 0;
    return (quint32(quint8(b[i])) << 8) | quint32(quint8(b[i + 1]));
}
quint32 leU16(const QByteArray &b, int i) {
    if (!inRange(b, i, 2)) return 0;
    return quint32(quint8(b[i])) | (quint32(quint8(b[i + 1])) << 8);
}
QString hexRange(const QByteArray &b, int from, int len) {
    // mid() already clamps, but a negative `from` shifts the window; make
    // the clamp explicit so the output length matches what callers expect.
    if (from < 0 || len <= 0 || from >= b.size()) return QString();
    return QString::fromLatin1(b.mid(from, qMin(len, b.size() - from))
                                   .toHex(' ')).toUpper();
}
// ---- Anx-D RFID helpers (LSB-first words + firmware CRC-30) ----
quint64 rfidLeWord(const QByteArray &b, int off) {
    if (!inRange(b, off, 8)) return 0;
    quint64 v = 0;
    for (int i = 0; i < 8; ++i) { v |= (quint64)(quint8(b[off + i])) << (8 * i); }
    return v;
}
quint64 rfidBf(quint64 w, int lo, int hi) {
    const int n = hi - lo + 1;
    return (n >= 64) ? (w >> lo) : ((w >> lo) & ((1ull << n) - 1));
}
quint32 rfidCrc30(const quint8 *data, int len) {        // exact firmware port
    const quint32 MASK = 0x3FFFFFFF, POLY = 0x2030B9C7, TOP = 1u << 29;
    quint32 crc = 0x3FFFFFFF;
    for (int i = 0; i < len; ++i) {
        crc = (crc ^ ((quint32)data[i] << 22)) & MASK;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & TOP) ? (((crc << 1) ^ POLY) & MASK) : ((crc << 1) & MASK);
        }
    }
    return (crc ^ 0x3FFFFFFF) & MASK;
}
// The Anx-D coded-value maps (rfidSection, rfidPlacement, ... rfidLocCorrType)
// and rfidTagType lived here until session 65. RFID decoding moved to the
// schema (kavach.xml <packet name="RFID">, validated by validate_rfid.py), and
// nothing had called them since; the compiler flagged all fifteen as unused.

// MSB-first cursor for Annexure-C radio frames (bit 0 = MSB of byte 0; trailing
// 4-byte CRC excluded). Used by profileOf() to build the SLRP color-profile lanes
// for the track view -- a structural producer, separate from the inspector path.
struct BitCursor {
    const QByteArray &b;
    int pos;     // bit position
    int limit;   // one-past-last decodable bit (frame minus CRC)
    bool over = false;
    BitCursor(const QByteArray &buf, int startBit, int limitBit)
        : b(buf), pos(startBit), limit(limitBit) {}
    quint32 take(int n) {
        quint32 v = 0;
        for (int i = 0; i < n; ++i) {
            if (pos > limit - 1) { over = true; pos++; continue; }
            const int bit = (quint8(b[pos >> 3]) >> (7 - (pos & 7))) & 1;
            v = (v << 1) | quint32(bit);
            pos++;
        }
        return v;
    }
    qint32 takeS(int n) {                 // two's-complement signed
        quint32 v = take(n);
        if (n < 32 && (v & (1u << (n - 1)))) { return (qint32)(v | (~0u << n)); }
        return (qint32)v;
    }
    int bytesLeft() const { return (limit - pos) / 8; }
};

// Shared, lazily-loaded schema decoder (kavach.xml). Every inspector row for a
// migrated packet comes from here; there is no hand-decoder fallback. Loaded once.
QString nmsHealthMeaning(int eid, qint64 v);     // defined below; used as a hook
// The shared instance is no longer const: it can be reloaded at runtime.
//
// Reload is GUI-THREAD ONLY. Every reader — the field inspector, the Loco
// Console, the fault panel, the decode workbench, describe() — runs on the
// GUI thread, and the ingest path deliberately does not decode (schema
// decoding happens on selection, not on arrival). So no lock is needed, and
// adding one would suggest a safety that the readers do not otherwise have.
// If anything ever decodes off-thread, this assumption has to be revisited.
static Schema::Decoder &kavachSchemaImpl()
{
    static Schema::Decoder d = [] {
        Schema::Decoder dec; QString err;
        if (!dec.load(QStringLiteral(":/schema/kavach.xml"), &err)) {
            qWarning("kavach.xml schema load failed: %s", qPrintable(err));
        }
        dec.registerMeaning("nmsHealth", &nmsHealthMeaning);
        dec.registerCrc("jamcrc", &CaptureDecoder::jamcrc);
        // Anx-D RFID CRC-30: mask the last data byte (high 6 bits hold the start
        // of the stored CRC field) then run the firmware CRC-30. Callers pass
        // from=1 len=13 (tag bytes 0..12; the tag begins at frame byte 1).
        dec.registerCrc("rfidcrc30", [](const QByteArray &d, int from, int len) -> quint32 {
            QByteArray s = d.mid(from, len);
            if (!s.isEmpty()) {
                s[s.size() - 1] = char((quint8)s[s.size() - 1] & 0x03);
            }
            return rfidCrc30(reinterpret_cast<const quint8 *>(s.constData()), s.size());
        });
        return dec;
    }();
    return d;
}

// Session 92: the same CRC algorithms for the packet maker's encoder, which
// writes <crc> elements. Registered at load time rather than inside
// kavachSchema(), so a build (or a round-trip check) that runs before
// anything has been decoded still finds them.
namespace {
const bool kEncoderCrcsRegistered = [] {
    Schema::Encoder::registerCrc(QStringLiteral("jamcrc"), &CaptureDecoder::jamcrc);
    Schema::Encoder::registerCrc(QStringLiteral("rfidcrc30"), [](const QByteArray &d, int from, int len) -> quint32 {
        QByteArray s = d.mid(from, len);
        if (!s.isEmpty()) {
            s[s.size() - 1] = char((quint8)s[s.size() - 1] & 0x03);
        }
        return rfidCrc30(reinterpret_cast<const quint8 *>(s.constData()), s.size());
    });
    return true;
}();
}  // namespace

// The single inspector path. Every migrated packet renders its rows from the
// schema (kavach.xml) -- there is NO hand-decoder fallback. If the schema fails
// to load (a packaging error: the :/schema qrc resource is missing or the XML is
// malformed) the user sees one loud diagnostic row instead of a silently stale
// hand decode. decodeRfid()/RfidInfo live on (track view + frame indexing); they
// are structural producers, not an inspector fallback.
// `raw`, when given, collects the decoded NUMERIC value of every field, for
// callers that have to compare values rather than display them — the reject
// rules. Merged rather than assigned: a few types decode twice (an envelope
// and a body), and the second call must not erase the first.
static QVector<FieldRow> schemaRows(const QByteArray &b, const QString &type,
                                    const QHash<int, qint64> *tagLoc = nullptr,
                                    int bodyOffsetBits = -1,
                                    QHash<QString, qint64> *raw = nullptr)
{
    const Schema::Decoder &schema = kavachSchemaImpl();
    if (schema.isLoaded() && schema.handles(b, type)) {
        QHash<QString, qint64> vals;
        QVector<FieldRow> rows =
            schema.decode(b, tagLoc ? *tagLoc : QHash<int, qint64>{}, type,
                          bodyOffsetBits, raw ? &vals : nullptr);
        if (raw) {
            for (auto it = vals.cbegin(); it != vals.cend(); ++it) {
                raw->insert(it.key(), it.value());
            }
        }
        return rows;
    }
    return { { QStringLiteral("schema"),
               QStringLiteral("kavach.xml not loaded for '%1' "
                              "(check the :/schema qrc resource)").arg(type) } };
}


} // anonymous namespace

namespace {  // ---- NMS semantic maps (loco CSVs + Annexure-G value tables) ----

// loco_module_id_4_0.csv : module id (1..43) -> module name
QString nmsModuleName(int id) {
    static const char *m[] = {
        "VCC Mc-1","VCC Mc-2","VCC Mc-3","VCC Mc-4",
        "Input Card 1 Mc-1","Input Card 1 Mc-2","Input Card 1 Mc-3","Input Card 1 Mc-4",
        "Input Card 2 Mc-1","Input Card 2 Mc-2","Input Card 2 Mc-3","Input Card 2 Mc-4",
        "Output Card Mc-1","Output Card Mc-2","Analogue Card Mc-1","Analogue Card Mc-2",
        "Communication Card Mc-1","Communication Card Mc-2","Data logger Card Mc-1","Data logger Card Mc-2",
        "Rfid-Reader-1","Rfid-Reader-2","Radio-1","Radio-2","GPS-1","GPS-2","GSM-1","GSM-2",
        "Speed sensor-1","Speed sensor-2","DMI-1","DMI-2","VCC MC All","VCC Card-1","VCC Card-2",
        "Input Card-1","Input Card-1'","Input Card-2","Input Card-2'","Output Card-1",
        "Analogue Card-1","Data logger Card-1","Comm Card-1" };
    if (id >= 1 && id <= (int)(sizeof(m)/sizeof(m[0]))) return QString::fromLatin1(m[id - 1]);
    return QStringLiteral("?");
}

// loco_fault_inputs.csv : fault-input id -> name (1..35, 100, 1001..1035)
QString nmsFaultInputName(int id) {
    switch (id) {
    case 1:return "Radio-1"; case 2:return "Radio-2"; case 3:return "Radio1 Power"; case 4:return "Radio2 Power";
    case 5:return "GPS-1"; case 6:return "GPS-2"; case 7:return "GPS-1 view"; case 8:return "GPS-2 view";
    case 9:return "GSM-1"; case 10:return "GSM-2"; case 11:return "CAN 1 status"; case 12:return "CAN 2 status";
    case 13:return "CAN 3 Status"; case 14:return "I2C_1"; case 15:return "I2C_2"; case 16:return "I2C_3";
    case 17:return "RS485_1"; case 18:return "FSI_1"; case 19:return "FSI_2"; case 20:return "RTC";
    case 21:return "RTC_2"; case 22:return "24V_1 PS"; case 23:return "24V_2 PS"; case 24:return "3.3V PS";
    case 25:return "1.2V PS"; case 26:return "safety error"; case 27:return "Link_1"; case 28:return "Link_2";
    case 29:return "RS485_2"; case 33:return "CPU Fail"; case 100:return "CARD STATUS";
    case 1001:return "DMI 1"; case 1002:return "DMI 2"; case 1003:return "RFID READER 1"; case 1004:return "RFID READER 2";
    case 1005:return "Active DMI"; case 1006:return "Non-Active Cab DMI"; case 1007:return "Speed Sensor1 comm";
    case 1008:return "Speed Sensor2 comm"; case 1009:return "BIU"; case 1010:return "MBT"; case 1011:return "cpc1";
    case 1012:return "cpc2"; case 1013:return "dmi1_sos_button"; case 1014:return "dmi1_cancel_button";
    case 1015:return "dmi1_common_button"; case 1016:return "dmi1_rotary_button"; case 1017:return "dmi2_sos_button";
    case 1018:return "dmi2_cancel_button"; case 1019:return "dmi2_common_button"; case 1020:return "dmi2_rotary_button";
    case 1021:return "pvef"; case 1022:return "horn1_solenoid"; case 1023:return "horn2_solenoid";
    case 1024:return "horn1_cock"; case 1025:return "horn2_cock"; case 1026:return "traction";
    case 1027:return "kavach_isolation"; case 1028:return "biu_isolation"; case 1029:return "sifa_valve";
    case 1030:return "sifa_isolation"; case 1031:return "fsb_relay"; case 1032:return "nb_relay";
    case 1033:return "eb_relay"; case 1034:return "le_relay"; case 1035:return "sifa_relay";
    default: return QStringLiteral("?");
    }
}

QString nmsFormation(qint64 v) {
    static const char *f[] = {"?","Light Engine 120","Light Engine Multi 120","Pass 3-7 Coach 120",
        "Pass 8-13 Coach 120","Pass 14-20 Coach 120","Pass 21-27 Coach 120","Goods 59 BOXN Empty 75",
        "Goods 59 BOXN Half 75","Goods 59 BOXN Full 60","Goods 42 BCN Empty 75","Goods 42 BCN Half 75",
        "Goods 42 BCN Full 60","LE WAP5 170","WAP5-8LHB 170","LE WAP7 140"};
    return (v >= 1 && v <= 15) ? QString::fromLatin1(f[v]) : QStringLiteral("%1?").arg(v);
}

// Annexure-G value semantics for a Loco-Health event id. Empty string = numeric.
QString nmsHealthMeaning(int eid, qint64 v) {
    auto inRange = [&](qint64 lo, qint64 hi){ return v >= lo && v <= hi; };
    switch (eid) {
    case 1: case 2: {
        static const char *h[] = {"?","OK","Diagnostic Link Fail","Radio Fail"};
        return inRange(0,3) ? QString::fromLatin1(h[v]) : QStringLiteral("?"); }
    case 3: case 4: case 9: case 10:  return QStringLiteral("%1 V").arg(v);
    case 5: case 6: case 7: case 8:   return QStringLiteral("%1 \u00B0C").arg(v);
    case 11: case 12:                 return QStringLiteral("%1 (Tx PA current)").arg(v);
    case 13: case 14: case 15: case 16: return QStringLiteral("%1 W").arg(v * 0.1, 0, 'f', 1);
    case 17:                          return QStringLiteral("%1 ms").arg(v);
    case 18: {
        static const char *g[] = {"No Active GPS","GPS-1","GPS-2","Both GPS"};
        return inRange(0,3) ? QString::fromLatin1(g[v]) : QStringLiteral("?"); }
    case 19: case 20: {
        static const char *g[] = {"No Data","V","A"};
        return inRange(0,2) ? QString::fromLatin1(g[v]) : QStringLiteral("?"); }
    case 21: case 22:                 return QStringLiteral("%1 s").arg(v);
    case 27: case 28: {
        static const char *g[] = {"link+PPS fail","link fail / PPS ok","link ok / PPS fail","link ok / PPS ok"};
        return inRange(0,3) ? QString::fromLatin1(g[v]) : QStringLiteral("?"); }
    case 29: case 30:                 return QStringLiteral("RSSI %1").arg(v);
    case 31: return v == 0 ? QStringLiteral("Default key set") : QStringLiteral("KMS key set %1").arg(v);
    case 32: return v == 0 ? QStringLiteral("No keys") : QStringLiteral("%1 key sets left").arg(v);
    case 33: return QStringLiteral("0x%1").arg((quint32)v, 4, 16, QChar('0'));
    case 57: return QStringLiteral("0x%1").arg((quint32)v, 8, 16, QChar('0'));
    case 34: case 35: case 36: case 37:
        return v == 1 ? QStringLiteral("OK") : (v == 0 ? QStringLiteral("NOT OK") : QStringLiteral("?"));
    case 41: return v ? QStringLiteral("Yes") : QStringLiteral("No");
    case 42: return v == 0 ? QStringLiteral("Brake Test failed") : QStringLiteral("MR not available");
    case 43: return nmsFormation(v);
    case 44: {
        static const char *c[] = {"No Cab","Cab1","Cab2","Both Cabs"};
        return inRange(0,3) ? QString::fromLatin1(c[v]) : QStringLiteral("?"); }
    case 45: {
        static const char *br[] = {"Not used","Reverse movement","Unusual stoppage","Overspeed","Rollback",
            "MBT selected","No LP Acknowledge","MA Shortened","Head-on collision","Rear-end collision",
            "Loco Specific SoS","Station General SoS"};
        return inRange(0,11) ? QString::fromLatin1(br[v]) : QStringLiteral("?"); }
    case 48: { const qint64 lo = (v >> 8) & 0xFFFFFF, code = v & 0xFF;
        return QStringLiteral("loco %1, code %2").arg(lo).arg(code); }
    case 49: {
        static const char *s[] = {"?","Manual SoS","Manual SoS end","Unusual stop start","Unusual stop end"};
        return inRange(1,4) ? QString::fromLatin1(s[v]) : QStringLiteral("?"); }
    case 50: case 51: return v == 1 ? QStringLiteral("Isolated") : (v == 2 ? QStringLiteral("Connected") : QStringLiteral("?"));
    case 52: return v == 1 ? QStringLiteral("EB Connected") : (v == 2 ? QStringLiteral("EB Bypassed") : QStringLiteral("?"));
    case 53: {
        static const char *t[] = {"?","KAVACH Entry","KAVACH Exit","ETCS Entry","ETCS Exit"};
        return inRange(1,4) ? QString::fromLatin1(t[v]) : QStringLiteral("?"); }
    case 55: { const qint64 mid = (v >> 4) & 0xFFF, mh = v & 0xF;
        return QStringLiteral("module %1 (%2) = %3").arg(mid).arg(nmsModuleName((int)mid))
                 .arg(mh == 1 ? QStringLiteral("OK") : (mh == 0 ? QStringLiteral("NOT OK") : QStringLiteral("?"))); }
    default: return QString();
    }
}

} // anonymous namespace (NMS maps)

// External accessor for the shared schema instance. The decoder itself
// lives in the anonymous namespace above (internal linkage), which is
// right — nothing outside should be able to construct another. This
// forwards to it so the field inspector decodes with the SAME instance and
// the same registered CRC/meaning hooks, rather than loading a second copy
// that could drift to a different kavach.xml.
const Schema::Decoder &kavachSchema()
{
    return kavachSchemaImpl();
}

bool reloadKavachSchema(const QString &path, QString *err)
{
    Schema::Decoder &dec = kavachSchemaImpl();

    // An empty path means "go back to the copy compiled into the binary".
    // That matters as an escape hatch: an operator who has loaded a broken
    // external schema needs a way back that does not involve finding the
    // original file.
    const QString target = path.isEmpty() ? QStringLiteral(":/schema/kavach.xml")
                                          : path;

    // load() clears its tables before parsing, and deliberately does NOT
    // clear the registered CRC and meaning hooks — those are C++ functions,
    // not schema data, and re-registering them here would be both redundant
    // and a place for the two lists to drift apart.
    QString localErr;
    const bool ok = dec.load(target, &localErr);
    if (!ok) {
        if (err) *err = localErr;
        // The decoder is now EMPTY, not stale: load() cleared before it
        // failed. Say so rather than letting the caller assume the previous
        // schema survived — decoding silently against nothing looks the
        // same as a schema that covers nothing.
        qWarning("schema reload from %s failed: %s",
                 qPrintable(target), qPrintable(localErr));
        return false;
    }
    return true;
}

namespace CaptureDecoder {

quint32 jamcrc(const QByteArray &d, int from, int len)
{
    quint32 crc = 0;
    const int end = from + len;
    for (int i = from; i < end; ++i) {
        quint8 b = reflect8((quint8)d[i]);
        crc ^= (quint32)b << 24;
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 0x80000000u) ? ((crc << 1) ^ 0x04C11DB7u) : (crc << 1);
        }
    }
    return reflect32(crc);
}

CapType typeFromToken(const QString &token)
{
    if (token == "aap")     return CapType::AAP;
    if (token == "arp")     return CapType::ARP;
    // The received form of the same packet. A distinct token because the two
    // are worth telling apart on sight — one is what this loco said, the other
    // is what another loco said — not because they decode differently: the
    // body layout is identical and the header size is read from the frame.
    if (token == "arprecv" || token == "arp_recv" || token == "rarp")
        return CapType::ArpRecv;
    if (token == "slrp")    return CapType::SLRP;
    if (token == "lsrp" ||
        token == "lrsp")    return CapType::LSRP;
    if (token == "nmsflt")  return CapType::NmsFault;
    if (token == "nmshlth") return CapType::NmsHlth;
    if (token == "nmsrssi") return CapType::NmsRssi;
    if (token == "dlt")     return CapType::Dlt;
    if (token == "dmi")     return CapType::Dmi;
    if (token == "biu")     return CapType::Biu;
    if (token == "brk")     return CapType::Brk;
    if (token == "rfid")    return CapType::Rfid;
    if (token == "dip1")    return CapType::Dip1;
    if (token == "dip2")    return CapType::Dip2;
    if (token == "dop1")    return CapType::Dop1;
    if (token == "dop2")    return CapType::Dop2;
    if (token == "ccsys")   return CapType::CcSys;
    if (token == "dlsys")   return CapType::DlSys;
    if (token == "aep")     return CapType::Aep;
    if (token == "linfo")   return CapType::Linfo;
    if (token == "uba")     return CapType::UBA;
    if (token == "lsos")    return CapType::Lsos;
    if (token == "sos")     return CapType::Sos;
    if (token == "sossrc")  return CapType::SosSrc;
    if (token == "sosev")   return CapType::SosEv;
    if (token == "rdir")    return CapType::Rdir;
    if (token == "speed")   return CapType::Speed;
    if (token == "analog_top")    return CapType::AnalogTop;
    if (token == "analog_bottom") return CapType::AnalogBottom;
    // Auth-key sets arrive under a small family of tags: authkeys / authkey,
    // and the per-set variants the LCU emits (auth_keys1, auth_keys2,
    // auth_key2, ...). Match the family by prefix so every set routes to the
    // same decoder rather than falling through to Unknown.
    if (token == "authkeys" || token == "authkey" ||
        token.startsWith("auth_key") || token.startsWith("authkey"))
        return CapType::AuthKeys;
    // RANDOM_NUMBER pair. Match the whole family by prefix — live captures
    // tag it @rand_num, but random_num / randnum / random also appear — so
    // every variant routes to the same decoder instead of Unknown.
    if (token.startsWith("rand"))
        return CapType::Random;
    return CapType::Unknown;
}

const char *typeLabel(CapType t)
{
    switch (t) {
    case CapType::AuthKeys: return "authkeys";
    case CapType::AAP:      return "aap";
    case CapType::ARP:      return "arp";
    case CapType::ArpRecv:  return "arprecv";
    case CapType::SLRP:     return "slrp";
    case CapType::LSRP:     return "lsrp";
    case CapType::NmsFault: return "nms fault";
    case CapType::NmsHlth:  return "nms hlth";
    case CapType::NmsRssi:  return "nms rssi";
    case CapType::Dlt:      return "dlt";
    case CapType::Dmi:      return "dmi";
    case CapType::Biu:      return "biu";
    case CapType::Brk:      return "brk";
    case CapType::Rfid:     return "rfid";
    case CapType::Dip1:     return "dip1";
    case CapType::Dip2:     return "dip2";
    case CapType::Dop1:     return "dop1";
    case CapType::Dop2:     return "dop2";
    case CapType::CcSys:    return "ccsys";
    case CapType::DlSys:    return "dlsys";
    case CapType::Aep:      return "aep";
    case CapType::Linfo:    return "linfo";
    case CapType::Random:   return "random num";
    case CapType::UBA:      return "uba";
    case CapType::Lsos:     return "lsos";
    case CapType::Sos:      return "sos";
    case CapType::SosSrc:   return "sos source";
    case CapType::SosEv:    return "sos event";
    case CapType::Rdir:     return "reader direction";
    case CapType::Speed:    return "speed";
    case CapType::AnalogTop:    return "analog top";
    case CapType::AnalogBottom: return "analog bottom";
    default:                return "unknown";
    }
}

CapDir directionFor(CapType t)
{
    // Provisional — confirm against firmware. Radio frames handed to the LCU
    // are outbound; aap/slrp look like received downlink; NMS goes to the logger.
    switch (t) {
    case CapType::ARP:
    case CapType::LSRP:     return CapDir::Out;
    case CapType::AAP:
    case CapType::SLRP:     return CapDir::In;
    case CapType::Rfid:     return CapDir::In;
    case CapType::NmsFault:
    case CapType::NmsHlth:
    case CapType::NmsRssi:
    case CapType::Dlt:      return CapDir::Out;
    case CapType::Dmi:      return CapDir::Out;   // loco -> DMI display (TX)
    case CapType::Dip1:
    case CapType::Dip2:     return CapDir::In;    // sensed digital inputs
    case CapType::Dop1:
    case CapType::Dop2:     return CapDir::Out;   // driven outputs (readback)
    case CapType::CcSys:
    case CapType::DlSys:    return CapDir::In;    // controller self-status (sensed)
    case CapType::Aep:      return CapDir::In;    // station emergency -> onboard
    case CapType::UBA:      return CapDir::Out;   // curve computed onboard, logged out
    case CapType::Sos:
    case CapType::SosSrc:
    case CapType::SosEv:    return CapDir::Out;   // the loco's own SoS state, logged out
    case CapType::Rdir:     return CapDir::Out;   // the loco's own reader directions
    case CapType::Speed:                          // tachometer pulses, sensed
    case CapType::AnalogTop:
    case CapType::AnalogBottom: return CapDir::In; // analog (pressure) inputs, sensed
    case CapType::AuthKeys: return CapDir::In;    // key sets pushed from LCU (RX)
    case CapType::ArpRecv:  return CapDir::In;    // another loco's ARP, inbound
    // Random deliberately has no direction: the frame carries BOTH the loco's
    // and the station's number, so labelling it RX or TX would misstate half
    // of it. It shows as "--" in the Link overview.
    default:                return CapDir::Unknown;
    }
}

// =============================================================================
//  How many bytes of message header sit in front of an ARP/LSRP packet.
//
//  It is not a constant. The transmitted form carries a 2-byte station id on
//  the end of the header; the form a loco RECEIVES from another loco does not,
//  so the same packet arrives 2 bytes earlier:
//
//      sent  02 07 0d 00 27 00 00 00 00 00  D3 AC ...   header 10, frame 39
//      recv  07 02 0d 00 25 00 00 00        D3 AC ...   header  8, frame 37
//
//  Nothing outside the frame has to be consulted to tell them apart, because
//  the frame states its own shape twice and the two statements have to agree:
//
//      message_len  – LE at bytes 4..5, the WHOLE frame, header included
//      PKT_LENGTH   – bits 4..10 of the packet, the packet's own length
//
//  so header = message_len - PKT_LENGTH. Reading PKT_LENGTH at a candidate
//  header size and checking the sum is therefore a self-check, and a frame
//  that satisfies neither candidate is one we decline to decode rather than
//  decode at a guessed offset. Deciding this from the captype token instead
//  would have made a received packet undecodable until the sender's token
//  was known, and would have gone quietly wrong the day a token was reused.
//
//  Returns the header size in bytes, or -1 if the frame is not self-consistent
//  at either. `pktLen` receives the packet length that agreed.
static int arpHeaderBytes(const QByteArray &b, int *pktLen = nullptr)
{
    const int n = b.size();
    if (n < 12) { return -1; }

    const int msgLen = (int)leU16(b, 4);

    // 10 first, so a frame that could somehow satisfy both keeps the meaning
    // it has had since the format was first decoded.
    for (int hdr : { 10, 8 }) {
        if (hdr + 6 > n) { continue; }
        if (msgLen <= hdr + 4 || msgLen > n) { continue; }
        const int pl = (((quint8)b[hdr] & 0x0F) << 3) | ((quint8)b[hdr + 1] >> 5);
        if (pl >= 5 && hdr + pl == msgLen) {
            if (pktLen) { *pktLen = pl; }
            return hdr;
        }
    }
    return -1;
}

// =============================================================================
//  What the loco will not act on.
//
//  A frame can decode perfectly and still have most of its content thrown away
//  at the far end. fly.c is explicit about it, and none of it is visible in a
//  field table: an LC entry with id 0 and one with id 7 look equally real, and
//  a TSR list is shown the same way whether the loco reads it or not.
//
//  So the reasons are named, from the decoded rows rather than from the bytes
//  — this runs after the schema has already done the work, and re-deriving the
//  same values a second way is how two answers to one question appear.
//
//  Only the reasons that can be judged FROM THE FRAME are here. The loco also
//  drops entries whose absolute location computes to zero or less, and that
//  depends on the reference tag's absolute location, which lives in the loco's
//  tag queue and not in anything on the wire. Guessing at it would produce a
//  confident note that is wrong whenever the tag table differs.
// =============================================================================
static QVector<FieldRow> locoWillIgnore(const QVector<FieldRow> &rows)
{
    QVector<FieldRow> out;

    auto valueOf = [&rows](const QString &name) -> QString {
        for (const FieldRow &r : rows) {
            if (r.field.trimmed().compare(name, Qt::CaseInsensitive) == 0) {
                return r.value.trimmed();
            }
        }
        return QString();
    };
    auto leadingInt = [](const QString &v, bool *ok) {
        int i = 0;
        while (i < v.size() && (v.at(i).isDigit() || (i == 0 && v.at(i) == '-'))) { ++i; }
        return v.left(i).toInt(ok);
    };

    // ---- TSR entries the loco does not read -------------------------------
    //
    // ProcessTSR walks the entry list only when TSR_STATUS is 2. At any other
    // value the entries are on the wire, decoded, displayed — and ignored.
    {
        const QString sv = valueOf(QStringLiteral("TSR_STATUS"));
        if (!sv.isEmpty()) {
            bool ok = false;
            const int status = leadingInt(sv, &ok);
            if (ok && status != 2) {
                out.push_back({ QStringLiteral("loco ignores"),
                                QStringLiteral("TSR entries — TSR_STATUS is %1, "
                                               "and the loco reads them only "
                                               "when it is 2").arg(status) });
            }
        }
    }

    // ---- placeholder slots -------------------------------------------------
    //
    // Counted rather than listed: on a packet with eight LC slots and one gate
    // in them, seven separate notes would bury the one line worth reading.
    {
        int lcUnused = 0;
        int tcUnused = 0;
        for (const FieldRow &r : rows) {
            const QString f = r.field.trimmed();
            const QString v = r.value.trimmed();
            if (f.endsWith(QLatin1String("id"), Qt::CaseInsensitive)
                && v.contains(QLatin1String("unused slot"))) {
                ++lcUnused;
            }
            if (f.endsWith(QLatin1String("tc"), Qt::CaseInsensitive)
                && v.contains(QLatin1String("Not used"))) {
                ++tcUnused;
            }
        }
        if (lcUnused > 0) {
            out.push_back({ QStringLiteral("loco ignores"),
                            QStringLiteral("%1 LC slot(s) with id 0 — "
                                           "placeholders, not gates")
                                .arg(lcUnused) });
        }
        if (tcUnused > 0) {
            out.push_back({ QStringLiteral("loco ignores"),
                            QStringLiteral("%1 track-condition slot(s) of type 0")
                                .arg(tcUnused) });
        }
    }

    // ---- the profile this packet belongs to --------------------------------
    //
    // REF_PROF_ID 0 is not a missing value: it is the station saying it does
    // not know the route ahead, and the loco responds by KEEPING the profile
    // it already holds rather than clearing it. Worth saying, because a packet
    // that changes nothing looks like a packet that did nothing.
    {
        const QString pv = valueOf(QStringLiteral("REF_PROF_ID"));
        bool ok = false;
        if (!pv.isEmpty() && leadingInt(pv, &ok) == 0 && ok) {
            out.push_back({ QStringLiteral("loco keeps"),
                            QStringLiteral("its current profile — REF_PROF_ID 0 "
                                           "means the station does not know the "
                                           "route ahead") });
        }
    }

    return out;
}

static void verifyCrc(CaptureLine &c)
{
    const QByteArray &b = c.bytes;
    const int n = b.size();
    if (n < 8) { return; }

    auto last4be = [&]() { return (beU16(b, n - 4) << 16) | beU16(b, n - 2); };
    auto last4le = [&]() {
        return (quint32)(quint8(b[n - 4])) | ((quint32)(quint8(b[n - 3])) << 8)
             | ((quint32)(quint8(b[n - 2])) << 16) | ((quint32)(quint8(b[n - 1])) << 24);
    };

    switch (c.type) {
    case CapType::AAP:
    case CapType::SLRP:
    case CapType::Aep:        // C.4.5: JAMCRC(init 0) over body[0:n-4], stored big-endian
        c.crcChecked = true;
        c.crcOk = (jamcrc(b, 0, n - 4) == last4be());
        break;
    case CapType::ARP:
    case CapType::ArpRecv:
    case CapType::LSRP: {
        // C.4.3 / C.4.6: JAMCRC over the PACKET only — the message header in
        // front of it is a transport envelope and is not covered — so the
        // span starts at the PKT_TYPE nibble and runs to the CRC.
        //
        // The stored word is big-endian, like every other Annexure-C packet
        // and like the rest of the frame, which is msb-first throughout. It
        // was being read little-endian here, so these two types reported CRC
        // FAIL on every frame. Verified against the captures in replay/:
        // 3253 LSRP and 9207 ARP frames, all PASS this way and none the old
        // way.
        //
        // Both ends of the span come from the packet now, not from the
        // datagram. They used to be byte 10 and "the last four bytes of what
        // arrived", which is two assumptions that a received packet breaks at
        // once: its header is 8 bytes, and its buffer has five bytes of
        // padding after the CRC, so the last four bytes are zeroes. That
        // combination reported FAIL on a frame whose CRC is correct.
        int pktLen = 0;
        const int hdr = arpHeaderBytes(b, &pktLen);
        if (hdr > 0 && hdr + pktLen <= n) {
            const int crcAt = hdr + pktLen - 4;
            c.crcChecked = true;
            c.crcOk = (jamcrc(b, hdr, pktLen - 4)
                       == ((beU16(b, crcAt) << 16) | beU16(b, crcAt + 2)));
        } else if (n >= 14) {
            // The frame does not agree with its own length fields, which means
            // it is damaged. The useful thing to say about a damaged frame is
            // CRC FAIL — that is the verdict an operator acts on — so fall
            // back to the historical span rather than going quiet and leaving
            // a corrupt frame looking unremarkable.
            //
            // Note what is NOT done here: the field breakdown still refuses to
            // decode a body it cannot place. A verdict on the whole frame is
            // safe to give from a guessed span, because a guess that lands on
            // PASS is vanishingly unlikely; a table of field values from a
            // guessed offset is the opposite, since every one of them looks
            // right.
            c.crcChecked = true;
            c.crcOk = (jamcrc(b, 10, n - 14) == last4be());
        }
        break;
    }
    case CapType::NmsFault:
    case CapType::NmsHlth:
    case CapType::NmsRssi:
        if (n >= 14) {
            c.crcChecked = true;
            c.crcOk = (jamcrc(b, 10, n - 14) == last4be());  // rssi will fail (fw bug)
        }
        break;
    case CapType::Rfid:
        if (n >= 17) {                                       // reader_id + 16-byte tag
            quint8 d[13];
            for (int i = 0; i < 13; ++i) { d[i] = byteAt(b, 1 + i); }
            d[12] &= 0x03;                                    // keep Y32,Y33; zero CRC bits
            const quint64 y = rfidLeWord(b, 1 + 8);
            c.crcChecked = true;
            c.crcOk = (rfidCrc30(d, 13) == (quint32)rfidBf(y, 34, 63));
        }
        break;
    case CapType::Dmi: {
        // DMI regular frame, two lengths in the field:
        //   115 B  [AA AA][len][hdr 7][body 99][CRC 4 LE][BB BB]
        //   116 B  [AA AA][len][hdr 7][body 99][num coaches 1][CRC 4 LE][BB BB]
        // JAMCRC runs from wire[3] up to the CRC word in both.
        //
        // Until session 65 only the 115 B form was accepted, and every
        // capture since the emitter added num coaches is 116 B, so DMI CRCs
        // were silently not checked at all (crcChecked stayed false) rather
        // than failing. The schema's <crc at="880"> already had it right.
        const int crcAt = (n == 116) ? 110 : (n == 115) ? 109 : -1;
        if (crcAt > 0 && (quint8)b[0] == 0xAA && (quint8)b[1] == 0xAA
                      && (quint8)b[n - 2] == 0xBB && (quint8)b[n - 1] == 0xBB) {
            const quint32 stored = (quint32)(quint8)b[crcAt]
                                 | ((quint32)(quint8)b[crcAt + 1] << 8)
                                 | ((quint32)(quint8)b[crcAt + 2] << 16)
                                 | ((quint32)(quint8)b[crcAt + 3] << 24);
            c.crcChecked = true;
            c.crcOk = (jamcrc(b, 3, crcAt - 3) == stored);
        }
        break;
    }
    case CapType::CcSys:
        if (n >= 52) {                                       // pkt_crc (JAMCRC over [0:48]) at [48:52] LE
            const quint32 stored = (quint32)(quint8)b[48] | ((quint32)(quint8)b[49] << 8)
                                 | ((quint32)(quint8)b[50] << 16) | ((quint32)(quint8)b[51] << 24);
            c.crcChecked = true;
            c.crcOk = (jamcrc(b, 0, 48) == stored);
        }
        break;
    case CapType::DlSys:
        if (n >= 16) {                                       // crc (JAMCRC over [0:12]) at [12:16] LE
            const quint32 stored = (quint32)(quint8)b[12] | ((quint32)(quint8)b[13] << 8)
                                 | ((quint32)(quint8)b[14] << 16) | ((quint32)(quint8)b[15] << 24);
            c.crcChecked = true;
            c.crcOk = (jamcrc(b, 0, 12) == stored);
        }
        break;
    case CapType::Linfo:
        // @linfo LOCO_INFO body: trailing loco_info_crc (u32, LE) is a real
        // JAMCRC(init 0) over body[0 : n-4] (everything except the CRC word
        // itself). Matches the host sender's crcFast (poly 0x04C11DB7, init 0,
        // reflected) — golden-verified against a 370 B frame (0xC7C020BC).
        if (n >= 4) {
            c.crcChecked = true;
            c.crcOk = (jamcrc(b, 0, n - 4) == last4le());
        }
        break;
    // @speed: STRUCT_SENSOR_SPEED_DATA, 14 B, ends in a u32 CRC stored LE.
    // ASSUMED, as for @ccsys/@dlsys/@linfo: the firmware's JAMCRC (init 0,
    // reflected) over bytes 0..9. No live frame has been seen yet; if every
    // real one reads FAIL, the algorithm or span differs. Only the exact
    // struct size is checked — any other length is not this struct and is
    // left unchecked rather than failed against a guessed span.
    // @analog_top/@analog_bottom carry no CRC and fall through to default.
    case CapType::Speed:
        if (n == 14) {
            c.crcChecked = true;
            c.crcOk = (jamcrc(b, 0, n - 4) == last4le());
        }
        break;
    default:
        c.crcChecked = false;                                // dlt/biu/brk
        break;
    }
}

CaptureLine parseLine(const QString &text)
{
    CaptureLine c;
    const QString s = text.trimmed();
    if (!s.startsWith('@')) { return c; }

    const QStringList tok = s.split(QChar(' '), Qt::SkipEmptyParts);
    if (tok.size() < 3) { return c; }            // need tag + ts + seq at least

    // tag: @<type>_<loco>_<ctrl>   (last two '_' fields are loco, ctrl)
    QString tag = tok.at(0);
    tag.remove(0, 1);                            // drop '@'
    const QStringList parts = tag.split('_');
    if (parts.size() < 3) { return c; }
    bool okLoco = false, okCtrl = false;
    c.ctrlId = parts.at(parts.size() - 1).toInt(&okCtrl);
    c.locoId = parts.at(parts.size() - 2).toInt(&okLoco);
    if (!okLoco || !okCtrl) { return c; }
    QStringList typeParts = parts.mid(0, parts.size() - 2);
    c.typeToken = typeParts.join('_');
    c.type = typeFromToken(c.typeToken);

    c.rtc = QDateTime::fromString(tok.at(1), Qt::ISODate);

    bool okSeq = false;
    c.seq = tok.at(2).toUInt(&okSeq);
    if (!okSeq) { return c; }

    if (tok.size() > 3) {
        const QString hex = QStringList(tok.mid(3)).join(QString());
        c.bytes = QByteArray::fromHex(hex.toLatin1());
    }

    verifyCrc(c);
    c.valid = true;
    return c;
}


QVector<NmsFaultEntry> decodeFault(const QByteArray &b)
{
    QVector<NmsFaultEntry> out;
    const int n = b.size();
    if (n < 33) { return out; }                  // 27 hdr + subsystem + count + crc
    const int o = 27;
    const int fc = (quint8)b[o + 1];
    int p = o + 2;
    for (int i = 0; i < fc && (p + 4) <= (n - 4); ++i) {
        NmsFaultEntry f;
        f.moduleId = (quint8)b[p];
        f.codeType = (quint8)b[p + 1];
        f.faultId  = (int)beU16(b, p + 2);
        out.push_back(f);
        p += 4;
    }
    return out;
}

RfidInfo decodeRfid(const QByteArray &frame)
{
    RfidInfo t;
    if (frame.size() < 17) { return t; }
    t.readerId = (quint8)frame[0];
    const QByteArray tag = frame.mid(1, 16);
    const quint64 x = rfidLeWord(tag, 0), y = rfidLeWord(tag, 8);

    // CRC-30 over the first 13 bytes with the CRC field (Y34-Y39 in byte 12)
    // zeroed. Identical across all four tag types (verified by golden vector).
    quint8 d[13];
    for (int i = 0; i < 13; ++i) { d[i] = byteAt(tag, i); }
    d[12] &= 0x03;
    t.crcStored = (quint32)rfidBf(y, 34, 63);
    t.crcCalc   = rfidCrc30(d, 13);
    t.crcOk     = (t.crcCalc == t.crcStored);

    // --- shared header (X0..X54) ---
    t.type    = (int)rfidBf(x, 0, 3);
    t.version = (int)rfidBf(x, 4, 5);
    t.unique  = (int)rfidBf(x, 6, 15);
    t.absLoc  = (qint64)rfidBf(x, 16, 38);
    t.tinNom  = (int)rfidBf(x, 39, 46);   // TIN-1 for type 12
    t.tinRev  = (int)rfidBf(x, 47, 54);   // TIN-2 for type 12

    switch (t.type) {
    case 9:   // Normal tag
        t.stnNom  = (int)(rfidBf(x, 55, 63) | (rfidBf(y, 0, 6) << 9));
        t.stnRev  = (int)rfidBf(y, 7, 22);
        t.sectNom = (int)rfidBf(y, 23, 24);
        t.sectRev = (int)rfidBf(y, 25, 26);
        t.place   = (int)rfidBf(y, 27, 30);
        t.dup     = (int)rfidBf(y, 31, 31);
        t.commNom = (int)rfidBf(y, 32, 32);
        t.commRev = (int)rfidBf(y, 33, 33);
        break;

    case 10:  // LC gate tag
        t.sectNom     = (int)rfidBf(x, 55, 56);
        t.sectRev     = (int)rfidBf(x, 57, 58);
        t.place       = (int)rfidBf(x, 59, 62);
        t.lcApproach  = (int)(rfidBf(x, 63, 63) | (rfidBf(y, 0, 0) << 1));
        t.applDir     = (int)rfidBf(y, 1, 1);
        t.gateId      = (int)rfidBf(y, 2, 11);
        t.gateAlpha   = (int)rfidBf(y, 12, 14);
        t.gateType    = (int)rfidBf(y, 15, 15);
        t.distGate    = (int)rfidBf(y, 16, 25);
        t.autoWhistle = (int)rfidBf(y, 26, 26);
        t.whistleType = (int)rfidBf(y, 27, 27);
        // Y30-Y28 fill zeros
        t.dup         = (int)rfidBf(y, 31, 31);
        t.commNom     = (int)rfidBf(y, 32, 32);
        t.commRev     = (int)rfidBf(y, 33, 33);
        break;

    case 11:  // Adjacent-line tag
        t.adj[0] = (int)rfidBf(x, 55, 62);
        t.adj[1] = (int)(rfidBf(x, 63, 63) | (rfidBf(y, 0, 6) << 1));
        t.adj[2] = (int)rfidBf(y, 7, 14);
        t.adj[3] = (int)rfidBf(y, 15, 22);
        t.adj[4] = (int)rfidBf(y, 23, 30);
        t.dup    = (int)rfidBf(y, 31, 31);
        // Y33-Y32 fill zeros; no comm flags on this type
        break;

    case 12:  // Adjustment/Junction tag
        t.absLoc2     = (qint64)(rfidBf(x, 55, 63) | (rfidBf(y, 0, 13) << 9));
        t.dirCorr1    = (int)rfidBf(y, 14, 16);
        t.dirCorr2    = (int)rfidBf(y, 17, 19);
        t.locCorrType = (int)rfidBf(y, 20, 20);
        // Y22-Y21 reserved
        t.sectNom     = (int)rfidBf(y, 23, 24);   // Section type-1
        t.sectRev     = (int)rfidBf(y, 25, 26);   // Section type-2
        // Y30-Y27 reserved
        t.dup         = (int)rfidBf(y, 31, 31);   // "Tag Type" 0=Main 1=Dup
        t.commNom     = (int)rfidBf(y, 32, 32);
        t.commRev     = (int)rfidBf(y, 33, 33);
        break;

    default:
        // Unknown type: header + CRC are still meaningful, leave the rest zero.
        break;
    }

    t.valid = true;
    return t;
}

CaptureIndex indexOf(const CaptureLine &c)
{
    CaptureIndex ix;
    const QByteArray &b = c.bytes;
    const int lim = b.size() * 8;
    auto gb = [&](int start, int n) -> int {
        quint32 v = 0;
        for (int i = start; i < start + n; ++i) {
            const int bit = (i < lim) ? ((quint8(b[i >> 3]) >> (7 - (i & 7))) & 1) : 0;
            v = (v << 1) | quint32(bit);
        }
        return (int)v;
    };
    switch (c.type) {
    case CapType::AAP:  if (b.size() >= 8)  { ix.frameNum = gb(11, 17); } break;
    case CapType::Aep:  if (b.size() >= 8)  { ix.frameNum = gb(11, 17); } break;
    case CapType::SLRP: if (b.size() >= 8)  { ix.frameNum = gb(14, 17); } break;
    case CapType::ARP:  if (b.size() >= 24) { ix.frameNum = gb(91, 17);
                                              ix.emergency = gb(176, 3); ix.locoMode = gb(179, 4); } break;
    case CapType::LSRP: if (b.size() >= 24) { ix.frameNum = gb(91, 17);
                                              ix.emergency = gb(196, 3); ix.locoMode = gb(199, 4); } break;
    case CapType::Rfid: { const RfidInfo t = decodeRfid(b); if (t.valid) { ix.rfidUid = t.unique; } } break;
    default: break;
    }
    return ix;
}

static void appendNmsHeader(QVector<FieldRow> &r, const QByteArray &b)
{
    if (b.size() < 27) { return; }
    r.push_back({ "nms.start_of_frame", hexRange(b, 8, 2) });
    r.push_back({ "nms.message_type",   QStringLiteral("0x%1").arg((quint8)b[10], 2, 16, QChar('0')).toUpper() });
    r.push_back({ "nms.message_length", QString::number(beU16(b, 11)) });
    r.push_back({ "nms.message_seq",    QString::number(beU16(b, 13)) });
    r.push_back({ "nms.onboard_kavach_id", QString::number((quint32(quint8(b[15])) << 16)
                                                           | (quint32(quint8(b[16])) << 8)
                                                           |  quint32(quint8(b[17]))) });
    r.push_back({ "nms.system_id",      QString::number(beU16(b, 18)) });
    r.push_back({ "nms.system_version", QString::number((quint8)b[20]) });
    r.push_back({ "nms.date",           QStringLiteral("%1/%2/%3")
                    .arg((quint8)b[21], 2, 10, QChar('0'))
                    .arg((quint8)b[22], 2, 10, QChar('0'))
                    .arg(2000 + (quint8)b[23]) });
    r.push_back({ "nms.time",           QStringLiteral("%1:%2:%3")
                    .arg((quint8)b[24], 2, 10, QChar('0'))
                    .arg((quint8)b[25], 2, 10, QChar('0'))
                    .arg((quint8)b[26], 2, 10, QChar('0')) });
}

// Coded 6-bit speed -> km/h (or -1 for reserved/unknown).
static int speed6kmh(quint32 v)
{
    if (v == 0)  { return 0; }          // dead stop
    if (v <= 50) { return int(v) * 5; }
    if (v == 62) { return 8; }          // night override
    return -1;                          // 51..61 reserved, 63 unknown
}

// Track-condition type (4-bit) -> short name. Mirrors the kavach.xml tcType enum.
QString tcTypeName(int t)
{
    static const char *m[] = {
        "Not used", "Dead Stop", "Radio hole", "Non-stopping",
        "Tunnel stopping", "Powerless", "Sound horn", "Reversing",
        "Fouling Mark", "KAVACH Exit" };
    if (t >= 0 && t < int(sizeof(m) / sizeof(m[0]))) { return QString::fromLatin1(m[t]); }
    return QString::number(t);
}

// Structured SLRP look-ahead profile. Re-walks the same verified bit layout as
// decodeSLRP()/the sub-decoders, but stores numbers for the track view. All
// distances are AS DECODED (relative to the SLRP profile reference).
SlrpProfile carryProfile(const SlrpProfile &held, const SlrpProfile &latest)
{
    if (!latest.valid) { return held; }
    SlrpProfile out = latest;          // MA, ref tag, direction: always the new frame's

    if (!held.valid || latest.carriesLanes()) {
        return out;                    // nothing to carry, or the frame reissued them
    }

    // REF_PROF_ID zero means the station does not know the route ahead. The
    // firmware keeps the profile it has rather than clearing it, so a run of
    // zero-id frames must not blank the lanes.
    if (latest.refProfId != 0 && held.refProfId != latest.refProfId) {
        return out;                    // a different profile was issued: the old one is gone
    }

    out.ssp  = held.ssp;
    out.grad = held.grad;
    out.tsr  = held.tsr;
    out.tags = held.tags;
    out.cond = held.cond;
    out.tsrStatus = held.tsrStatus;
    // The lanes belong to the held profile, and so does the id that identifies
    // them — otherwise a zero-id frame would relabel them as profile 0 and the
    // next real issue would look like a continuation of it.
    out.refProfId = held.refProfId;
    // The lanes are measured from the reference the profile was ISSUED
    // against. Taking the new frame's reference would slide the whole
    // look-ahead every time the loco passed a balise.
    out.refRfid      = held.refRfid;
    out.distPktStart = held.distPktStart;
    return out;
}

SlrpProfile profileOf(const CaptureLine &c)
{
    SlrpProfile p;
    if (c.type != CapType::SLRP) { return p; }
    const QByteArray &b = c.bytes;
    const int n = b.size();
    if (n < 12) { return p; }
    // The tail is EIGHT bytes, not four: CRC and MAC. The firmware walks
    // sub-packets while offset < pkt_length - 8, and this used to mix three
    // different ideas of where the payload ends — a limit of n-4, a loop bound
    // of n-8, and a guard of n-4 — so a sub-packet was allowed to read into
    // the MAC. Payload length comes from PKT_LENGTH as well, not from the
    // datagram size, since a receive buffer may carry padding after the frame.
    BitCursor head(b, 4, n * 8);
    const int pktLength = int(head.take(10));
    const int payloadEnd = (pktLength >= 21 && pktLength <= n) ? pktLength - 8
                                                               : n - 8;
    const int limit = payloadEnd * 8;
    BitCursor cur(b, 0, limit);

    cur.take(4);                          // PKT_TYPE
    cur.take(10);                         // PKT_LENGTH
    cur.take(17);                         // FRAME_NUM
    cur.take(16);                         // SOURCE_STN_ID
    cur.take(3);                          // STN_VERSION
    cur.take(20);                         // DEST_LOCO_ID
    p.refProfId    = int(cur.take(4));    // REF_PROF_ID
    p.refRfid      = int(cur.take(10));   // LAST_REF_RFID
    p.distPktStart = cur.takeS(15);       // DIST_PKT_START (signed m)
    p.pktDir = int(cur.take(2));          // PKT_DIR (1 nominal, 2 reverse)
    cur.take(3);                          // padding
    p.valid = true;

    while (cur.pos / 8 < payloadEnd) {
        const int startByte = cur.pos / 8;
        const quint32 t = cur.take(4);
        const quint32 L = cur.take(7);
        if (L == 0 || startByte + int(L) > payloadEnd) { break; }
        BitCursor sc(b, startByte * 8 + 11, startByte * 8 + int(L) * 8);
        switch (t) {
        case 0: {                          // Movement Authority
            p.haveMA = true;
            sc.take(4);                    // FRAME_OFFSET
            sc.take(4);                    // DEST_LOCO_SOS
            sc.take(2);                    // TRAIN_SECTION_TYPE
            sc.take(17);                   // CUR_SIG_INFO
            sc.take(6);                    // CUR_SIG_ASPECT
            sc.take(6);                    // NEXT_SIG_ASPECT
            sc.take(15);                   // APPR_SIG_DIST
            const quint32 at = sc.take(2);
            p.authType = int(at);
            if (at == 1) { p.authSpeed = speed6kmh(sc.take(6)); }
            p.maWrtSig = int(sc.take(16));
            const quint32 rs = sc.take(1);
            p.reqShorten = (rs != 0);
            if (rs) { p.newMA = int(sc.take(16)); }
            break;
        }
        case 1: {                          // Static Speed Profile
            const quint32 cnt = sc.take(5);
            for (quint32 i = 0; i < cnt && sc.pos < sc.limit; ++i) {
                SlrpProfile::SpeedStep s;
                s.d = int(sc.take(15));
                const quint32 cls = sc.take(1);
                s.classified = (cls != 0);
                if (cls == 0) { const int v = speed6kmh(sc.take(6)); s.a = s.b = s.c = v; }
                else { s.a = speed6kmh(sc.take(6)); s.b = speed6kmh(sc.take(6)); s.c = speed6kmh(sc.take(6)); }
                p.ssp.push_back(s);
            }
            break;
        }
        case 2: {                          // Gradient
            const quint32 cnt = sc.take(5);
            for (quint32 i = 0; i < cnt && sc.pos < sc.limit; ++i) {
                SlrpProfile::GradPt g;
                g.d = int(sc.take(15));
                g.uphill = (sc.take(1) != 0);
                g.value = int(sc.take(5));
                p.grad.push_back(g);
            }
            break;
        }
        case 5: {                          // Tag Linking
            sc.take(4);                    // DIST_DUP_TAG
            const quint32 cnt = sc.take(6);// ROUTE_RFID_CNT
            if (cnt >= 1 && cnt <= 62) {
                for (quint32 i = 0; i < cnt && sc.pos < sc.limit; ++i) {
                    SlrpProfile::TagLink tl;
                    tl.d   = int(sc.take(11));
                    tl.tag = int(sc.take(10));
                    tl.flag = int(sc.take(1));
                    p.tags.push_back(tl);
                }
            }
            break;
        }
        case 6: {                          // Track Condition (type 6, per Anx-C)
            const quint32 cnt = sc.take(4); // TRACKCOND_CNT
            for (quint32 i = 0; i < cnt && sc.pos < sc.limit; ++i) {
                SlrpProfile::TrackCond tc;
                tc.type = int(sc.take(4));  // tcType
                tc.sd   = int(sc.take(15)); // start distance from ref (m)
                tc.len  = int(sc.take(15)); // zone length (m)
                p.cond.push_back(tc);
            }
            break;
        }
        case 7: {                          // TSR
            p.tsrStatus = int(sc.take(2));
            const quint32 cnt = sc.take(5);
            for (quint32 i = 0; i < cnt && sc.pos < sc.limit; ++i) {
                SlrpProfile::TsrZone z;
                z.id  = int(sc.take(8));
                z.d   = int(sc.take(15));
                z.len = int(sc.take(15));
                const quint32 cls = sc.take(1);
                z.classified = (cls != 0);
                if (cls == 0) { const int v = speed6kmh(sc.take(6)); z.a = z.b = z.c = v; }
                else { z.a = speed6kmh(sc.take(6)); z.b = speed6kmh(sc.take(6)); z.c = speed6kmh(sc.take(6)); }
                sc.take(2);                // whistle
                p.tsr.push_back(z);
            }
            break;
        }
        default: break;
        }
        cur.pos = startByte * 8 + int(L) * 8;
    }
    return p;
}

// Fully-named active faults from an NMS fault packet. Reuses decodeFault() plus
// the module / fault-input dictionaries so naming lives in one place.
bool isStartOfMission(const QHash<QString, qint64> &arp)
{
    static const QString keys[] = { QStringLiteral("LOCO_MODE"), QStringLiteral("MOVEMENT_DIR"),
                                    QStringLiteral("LAST_RFID_TAG"), QStringLiteral("ABS_LOCO_LOC") };
    for (const QString &k : keys) if (!arp.contains(k)) return false;
    return arp.value(keys[0]) == 1 && arp.value(keys[1]) == 0 && arp.value(keys[2]) == 0 && arp.value(keys[3]) == 0;
}

QVector<ActiveFaultInfo> faultsOf(const CaptureLine &c)
{
    QVector<ActiveFaultInfo> out;
    if (c.type != CapType::NmsFault) { return out; }
    const QByteArray &b = c.bytes;
    if (b.size() < 29) { return out; }
    const int sub = (quint8)b[27];
    const QString subName = nmsModuleName(sub);
    for (const NmsFaultEntry &f : decodeFault(b)) {
        ActiveFaultInfo a;
        a.subsystem     = sub;
        a.subsystemName = subName;
        a.moduleId      = f.moduleId;
        a.moduleName    = nmsModuleName(f.moduleId);
        a.codeType      = f.codeType;
        a.faultId       = f.faultId;
        a.faultName     = nmsFaultInputName(f.faultId);
        out.push_back(a);
    }
    return out;
}

QVector<FieldRow> describe(const CaptureLine &c, const QHash<int, qint64> *tagLoc,
                           int keySnapshotId, QHash<QString, qint64> *rawValues,
                           const SessionKeyStore *keys)
{
    if (rawValues) { rawValues->clear(); }
    QVector<FieldRow> r;
    const QByteArray &b = c.bytes;
    const int n = b.size();

    // ---- envelope (common to every type) ----
    r.push_back({ "type",      QString::fromLatin1(typeLabel(c.type)) });
    r.push_back({ "loco / ctrl", QStringLiteral("%1 / %2").arg(c.locoId).arg(c.ctrlId) });
    r.push_back({ "rtc",       c.rtc.isValid() ? c.rtc.toString("yyyy-MM-dd HH:mm:ss")
                                               : QStringLiteral("--") });
    r.push_back({ "seq",       QString::number(c.seq) });
    r.push_back({ "frame len", QStringLiteral("%1 B").arg(n) });
    r.push_back({ "CRC",       c.crcChecked ? (c.crcOk ? QStringLiteral("PASS")
                                                       : QStringLiteral("FAIL"))
                                            : QStringLiteral("n/a") });

    switch (c.type) {
    case CapType::AAP:  r += schemaRows(b, QStringLiteral("aap"), nullptr, -1, rawValues);  break;
    // AUTH_KEYS — authentication key sets from the LCU.
    //
    // Decoded here rather than in kavach.xml because the schema's <repeat>
    // cannot compose a date from four separate bytes, nor apply hex
    // rendering to fields inside it. Both are needed: the validity window
    // is the whole point of the packet, and a run of decimal components
    // ("start.yy=26 start.mm=5") is not a date anyone can read.
    //
    // Wire form is KEY_SET_INFO from key_info.txt, preceded by a count:
    //     1 byte   number of sets
    //   per set:
    //     4 bytes  start KEY_TIME  { yy-2000, mm, dd, hh }
    //     4 bytes  end   KEY_TIME
    //    16 bytes  key[0]
    //    16 bytes  key[1]
    //
    // KEY_TIME's year base is confirmed against the console's own epoch
    // print: [26][5][24][0] alongside START 1779580800, which is
    // 2026-05-24 00:00 UTC.
    //
    // BYTE ORDER: on the wire the 4-byte KEY_TIME word is stored high-to-low
    // relative to the {yy,mm,dd,hh} field order, i.e. [hh,dd,mm,yy]. Live LCU
    // captures (@auth_keys*) show a start window of 00 18 05 1A, which is
    // hh=00 dd=0x18(24) mm=05 yy=0x1A(26) = 2026-05-24, matching the epoch
    // print above. Only the multi-byte time word is swapped; the 16-byte key
    // arrays are byte sequences and are NOT reversed. keyTime() therefore
    // reads the word from off+3 down to off+0.
    //
    // KEY MATERIAL IS DELIBERATELY NOT RENDERED. These are live
    // authentication keys; every decoded frame is archived to .dlr and can
    // reach an exported report or a pasted screenshot. What acceptance
    // testing needs is the lifecycle — how many sets remain, when each is
    // valid, which is current — because REMAINING_KEYS_LESS_THAN_5 and
    // SESSION_KEY_MISMATCH are about key validity, not key value. A short
    // fingerprint is emitted instead: enough to tell two sets apart and to
    // confirm a key changed, without disclosing it.
    case CapType::AuthKeys: {
        constexpr int kKeyBytes = 16;
        constexpr int kSetBytes = 4 + 4 + 2 * kKeyBytes;   // 40

        if (n < 1) { r.push_back({ "AUTH_KEYS", "empty" }); break; }
        // Each @auth_keys frame carries ONE key set of exactly 40 bytes with
        // NO count prefix: start(4) end(4) key0(16) key1(16). (Earlier code
        // assumed a leading count byte; real LCU frames have none.)
        const int fits = n / kSetBytes;
        const int sets = fits;

        r.push_back({ "key sets", QString::number(sets) });
        if (n % kSetBytes != 0) {
            r.push_back({ "  note",
                          QStringLiteral("payload is %1 B, not a whole number of "
                                         "40-byte sets").arg(n) });
        }

        auto keyTime = [&](int off) {
            // The 4-byte KEY_TIME word is stored high-to-low on the wire
            // ([hh,dd,mm,yy]), so read it from off+3 down to off+0 to recover
            // {yy,mm,dd,hh}. Year is stored as yy-2000; rendering it bare
            // would produce "0026-05-24" and quietly misdate every key.
            return QStringLiteral("20%1-%2-%3 %4:00")
                       .arg(quint8(b[off + 3]), 2, 10, QChar('0'))   // yy
                       .arg(quint8(b[off + 2]), 2, 10, QChar('0'))   // mm
                       .arg(quint8(b[off + 1]), 2, 10, QChar('0'))   // dd
                       .arg(quint8(b[off + 0]), 2, 10, QChar('0'));  // hh
        };
        auto fingerprint = [&](int off) {
            // First two and last two bytes only. Enough to distinguish
            // sets and spot a rotation; not enough to reconstruct a key.
            const QString fp = QStringLiteral("%1%2…%3%4")
                       .arg(quint8(b[off + 0]), 2, 16, QChar('0'))
                       .arg(quint8(b[off + 1]), 2, 16, QChar('0'))
                       .arg(quint8(b[off + kKeyBytes - 2]), 2, 16, QChar('0'))
                       .arg(quint8(b[off + kKeyBytes - 1]), 2, 16, QChar('0'))
                       .toUpper();
            // Only the hex is upper-cased; the note is prose.
            return fp + QStringLiteral("  (16 bytes, not shown)");
        };

        for (int i = 0; i < sets; ++i) {
            const int base = i * kSetBytes;      // no count byte
            r.push_back({ QStringLiteral("KEY IDX %1").arg(i), QString() });
            r.push_back({ QStringLiteral("  idx%1.valid_from").arg(i),
                          keyTime(base) });
            r.push_back({ QStringLiteral("  idx%1.valid_to").arg(i),
                          keyTime(base + 4) });
            r.push_back({ QStringLiteral("  idx%1.key0").arg(i),
                          fingerprint(base + 8) });
            r.push_back({ QStringLiteral("  idx%1.key1").arg(i),
                          fingerprint(base + 8 + kKeyBytes) });
        }
        break;
    }
    case CapType::Aep:  r += schemaRows(b, QStringLiteral("aep"), nullptr, -1, rawValues);  break;
    case CapType::SLRP: {
        r += schemaRows(b, QStringLiteral("slrp"), tagLoc, -1, rawValues);
        r += locoWillIgnore(r);
        // Live MAC check against the session key derived from the log.
        // Against the key of the loco this frame was captured on — with more
        // than one loco in the log, the active loco is not necessarily this
        // frame's loco. A caller that has let the operator pick one of the
        // captured key sets passes its id instead.
        if (!keys) { break; }          // no live keys handed in: no MAC row (session 96)
        const SessionKeyStore &sks = *keys;
        const bool chosen = (keySnapshotId > 0);
        const auto verdict = chosen
            ? sks.verifyMacWith(QStringLiteral("slrp"), b, keySnapshotId)
            : sks.verifyMac(QStringLiteral("slrp"), b, c.locoId);
        // Name which material the verdict came from: on a log with several key
        // sets, "FAIL" without saying what it was checked against is not a
        // useful thing to have been told.
        const int usedId = chosen ? keySnapshotId
                                  : sks.currentSnapshotId(c.locoId);
        const QString via = usedId > 0 ? QStringLiteral("  [key set #%1]").arg(usedId)
                                       : QString();
        switch (verdict) {
        case SessionKeyStore::Mac::Pass:
            r.push_back({ "MAC (live)", QStringLiteral("PASS \u2713") + via }); break;
        case SessionKeyStore::Mac::Fail:
            r.push_back({ "MAC (live)", QStringLiteral("FAIL \u2717") + via }); break;
        case SessionKeyStore::Mac::NoKey:
            r.push_back({ "MAC (live)", chosen
                              ? QStringLiteral("that key set is gone")
                              : QStringLiteral("no session key yet") }); break;
        case SessionKeyStore::Mac::None:
            break;
        }
        break;
    }
    case CapType::Dmi:  r += schemaRows(b, QStringLiteral("dmi"), nullptr, -1, rawValues);  break;
    case CapType::ARP:
    case CapType::ArpRecv:
    case CapType::LSRP: {
        // The header is 8 or 10 bytes depending on whether the station id is
        // on the end of it (see arpHeaderBytes). Everything below follows from
        // that one number, so it is resolved once.
        int pktLen = 0;
        const int hdr = arpHeaderBytes(b, &pktLen);
        if (hdr > 0) {
            r.push_back({ "msg.src_id",     QString::number((quint8)b[0]) });
            r.push_back({ "msg.dest_id",    QString::number((quint8)b[1]) });
            r.push_back({ "msg.message_id", QString::number(leU16(b, 2)) });
            r.push_back({ "msg.length",     QString::number(leU16(b, 4)) });
            r.push_back({ "msg.seq_num",    QString::number(leU16(b, 6)) });
            if (hdr >= 10) {
                r.push_back({ "msg.stn_id", QString::number(leU16(b, 8)) });
            } else {
                // Said out loud, because its absence is the whole reason the
                // packet starts two bytes earlier, and an operator comparing
                // a sent frame with a received one will otherwise be looking
                // for a field that was never sent.
                r.push_back({ "msg.stn_id",
                              QStringLiteral("not present (received format, "
                                             "8-byte header)") });
            }
        } else if (n >= 10) {
            // Not self-consistent at either header size. Say so rather than
            // decoding at a guessed offset: a wrong offset produces a full
            // set of plausible-looking values and no error at all.
            r.push_back({ "msg.header",
                          QStringLiteral("frame length %1 does not agree with "
                                         "PKT_LENGTH at either header size — "
                                         "body offset unknown")
                              .arg(leU16(b, 4)) });
        }
        // Both ARP forms decode against the same schema packet: the header in
        // front of them differs, the packet does not, and a second copy of 21
        // field definitions would only be a second thing to keep correct.
        const QString token = (c.type == CapType::LSRP) ? QStringLiteral("lsrp")
                                                        : QStringLiteral("arp");
        if (hdr > 0) {
            QHash<QString, qint64> own;
            QHash<QString, qint64> *raw = rawValues ? rawValues : &own;
            r += schemaRows(b, token, nullptr, hdr * 8, raw);
            // Session 168: the state an ARP reports before a mission starts.
            if (c.type != CapType::LSRP && isStartOfMission(*raw)) {
                r.push_back({ QStringLiteral("start of mission"),
                              QStringLiteral("Stand_By, no direction, no RFID tag, no location: "
                                             "the loco reports it is at the start of a mission") });
            }
        }
        break;
    }
    case CapType::NmsHlth: {
        appendNmsHeader(r, b);
        r += schemaRows(b, QStringLiteral("nmshlth"), nullptr, -1, rawValues);
        break;
    }
    case CapType::NmsFault: {
        appendNmsHeader(r, b);
        if (n >= 29) { r += schemaRows(b, QStringLiteral("nmsflt"), nullptr, -1, rawValues); }
        break;
    }
    case CapType::NmsRssi: {
        appendNmsHeader(r, b);
        if (n >= 45) { r += schemaRows(b, QStringLiteral("nmsrssi"), nullptr, -1, rawValues); }
        break;
    }
    case CapType::Rfid:
        // decodeRfid()/RfidInfo remain for the track view, balise markers and
        // ix.rfidUid; the inspector rows come from the schema.
        r += schemaRows(b, QStringLiteral("rfid"), nullptr, -1, rawValues);
        break;
    case CapType::Dip1:  r += schemaRows(b, QStringLiteral("dip1"), nullptr, -1, rawValues);  break;
    case CapType::Dip2:  r += schemaRows(b, QStringLiteral("dip2"), nullptr, -1, rawValues);  break;
    case CapType::Dop1:  r += schemaRows(b, QStringLiteral("dop1"), nullptr, -1, rawValues);  break;
    case CapType::Dop2:  r += schemaRows(b, QStringLiteral("dop2"), nullptr, -1, rawValues);  break;
    case CapType::CcSys: r += schemaRows(b, QStringLiteral("ccsys"), nullptr, -1, rawValues); break;
    case CapType::DlSys: r += schemaRows(b, QStringLiteral("dlsys"), nullptr, -1, rawValues); break;
    case CapType::Linfo: r += schemaRows(b, QStringLiteral("linfo"), nullptr, -1, rawValues); break;
    // @uba Target_Internal. No frame CRC and no message header: the loco
    // memcpy's the struct out as-is, so the schema decodes from byte 0.
    case CapType::UBA:   r += schemaRows(b, QStringLiteral("uba"),   nullptr, -1, rawValues); break;
    // @lsos LOCO_SOS: flat LE struct, no header/CRC (session 98).
    case CapType::Lsos:  r += schemaRows(b, QStringLiteral("lsos"),  nullptr, -1, rawValues); break;
    // @sos / @sossrc / @sosev: flat LE, written byte by byte (session 184).
    case CapType::Sos:    r += schemaRows(b, QStringLiteral("sos"),    nullptr, -1, rawValues); break;
    case CapType::SosSrc: r += schemaRows(b, QStringLiteral("sossrc"), nullptr, -1, rawValues); break;
    case CapType::SosEv:  r += schemaRows(b, QStringLiteral("sosev"),  nullptr, -1, rawValues); break;
    // @rdir READER_INFO (session 194): 5 B LE, no header/CRC.
    case CapType::Rdir:   r += schemaRows(b, QStringLiteral("rdir"),   nullptr, -1, rawValues); break;
    // @speed / @analog_*: flat LE structs, decoded from byte 0 (session 67).
    // @analog_* is six floats with no CRC; the schema shows them %g.
    case CapType::Speed: r += schemaRows(b, QStringLiteral("speed"), nullptr, -1, rawValues); break;
    case CapType::AnalogTop:
        r += schemaRows(b, QStringLiteral("analog_top"), nullptr, -1, rawValues); break;
    case CapType::AnalogBottom:
        r += schemaRows(b, QStringLiteral("analog_bottom"), nullptr, -1, rawValues); break;
    // RANDOM_NUMBER — the session-nonce pair.
    //
    //     typedef struct { uint16_t loco_random_num;
    //                      uint16_t stn_random_num;  } RANDOM_NUMBER;
    //
    // Flat 4-byte struct, no count prefix and no header. Each uint16 is a
    // direct memcpy on the little-endian target, so it is stored LSB-first.
    // Confirmed against live @rand_num captures: 90 41 0B 0B decodes to
    // loco=0x4190, stn=0x0B0B. Read little-endian (leU16); the raw bytes are
    // printed too so the pairing stays checkable by eye.
    case CapType::Random: {
        constexpr int kBytes = 4;
        if (n < kBytes) {
            r.push_back({ "RANDOM_NUM",
                          QStringLiteral("short frame (%1 of %2 B)")
                              .arg(n).arg(kBytes) });
            break;
        }
        auto num = [&](int off) {
            // Decimal for comparison against the console print, hex so a
            // byte-order error is visible at a glance. Only the hex digits
            // are upper-cased; the "0x" prefix stays lower.
            const quint32 v = leU16(b, off);
            const QString hex = QStringLiteral("%1").arg(v, 4, 16, QChar('0')).toUpper();
            return QStringLiteral("%1  (0x%2)").arg(v).arg(hex);
        };
        r.push_back({ "loco_random_num", num(0) });
        r.push_back({ "stn_random_num",  num(2) });
        r.push_back({ "  raw", hexRange(b, 0, kBytes) });
        if (n > kBytes) {
            // Say so rather than ignoring it: trailing bytes mean the struct
            // on the wire is not the 4-byte one assumed here.
            r.push_back({ "  note",
                          QStringLiteral("%1 trailing byte(s) beyond the struct")
                              .arg(n - kBytes) });
        }
        break;
    }
    default:
        if (n > 0) { r.push_back({ "raw", hexRange(b, 0, n) }); }
        break;
    }
    return r;
}

} // namespace CaptureDecoder

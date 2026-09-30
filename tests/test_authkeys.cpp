#include "testutil.h"
#include "capturedecoder.h"
#include "schema/schemadecoder.h"

namespace {
// A KEY_SET_INFO block as key_info.txt defines it:
//   KEY_TIME start; KEY_TIME end; KEY key[2];   KEY = uint8_t[16]
// On the wire each KEY_TIME word is byte-reversed ([hh,dd,mm,yy]); the keys
// are byte arrays and are not reversed.
QByteArray keySet(int sy,int sm,int sd,int sh, int ey,int em,int ed,int eh,
                  quint8 k0Lead, quint8 k0Second,
                  quint8 k1Lead, quint8 k1Second)
{
    QByteArray b;
    // KEY_TIME is a 4-byte word stored high-to-low on the wire ([hh,dd,mm,yy]),
    // matching live @auth_keys captures; the decoder reads it reversed.
    for (int v : {sh,sd,sm,sy, eh,ed,em,ey}) b.append(char(v));
    QByteArray k0(16, '\0'); k0[0]=char(k0Lead); k0[1]=char(k0Second);
    QByteArray k1(16, '\0'); k1[0]=char(k1Lead); k1[1]=char(k1Second);
    b += k0; b += k1;
    return b;
}
QString line(const QByteArray &payload)
{
    return QStringLiteral("@authkeys_1234_1 2026-08-24T10:57:28 42 %1")
               .arg(QString::fromLatin1(payload.toHex()));
}
QString valueOf(const QVector<FieldRow> &rows, const QString &needle)
{
    for (const FieldRow &r : rows)
        if (r.field.contains(needle)) return r.value;
    return QString();
}
}  // namespace

// AUTH_KEYS decoding. Built from the console output in the field photo:
// two sets, validity 2026-05-24 to 2026-09-01 and to 2026-09-11.
TEST_SUITE(authkeys)
{
    const Schema::Decoder &d = kavachSchema();
    CHECK(d.isLoaded(), "schema loaded");
    if (!d.isLoaded()) return;

    // ---- the per-set tag family all routes to AuthKeys -------------------
    // Live captures tag each set separately: @auth_keys1_..., @auth_keys2_...,
    // @auth_key2_.... These must resolve to AuthKeys, not Unknown, or the
    // frame never reaches the decoder, the live console, or the replay window.
    {
        QByteArray p = keySet(26,5,24,0, 26,9,1,0, 0x3B,0x0C, 0x6A,0x2B);
        const QString hex = QString::fromLatin1(p.toHex());
        for (const char *tag : { "@auth_keys1_1_1", "@auth_keys2_1_1",
                                 "@auth_key2_1_1",  "@authkeys_1_1" }) {
            const QString ln = QStringLiteral("%1 2026-08-25T15:56:34 350 %2")
                                   .arg(QLatin1String(tag), hex);
            const CaptureLine c = CaptureDecoder::parseLine(ln);
            CHECK(c.valid, "auth-key family line parses");
            CHECK(c.type == CapType::AuthKeys, "tag routes to AuthKeys");
            const QVector<FieldRow> rows = CaptureDecoder::describe(c);
            CHECK(valueOf(rows, "valid_from").contains("2026-05-24 00:00"),
                  "and decodes the corrected window");
        }
    }

    // ---- the capture line is recognised ----------------------------------
    {
        QByteArray p = keySet(26,5,24,0, 26,9,1,0,  0x3B,0x0C, 0x6A,0x2B);
        p += keySet(26,5,24,0, 26,9,11,0, 0xF5,0x6C, 0x80,0xC5);

        const CaptureLine c = CaptureDecoder::parseLine(line(p));
        CHECK(c.valid, "@authkeys line parses");
        CHECK(c.typeToken.startsWith("authkeys"), "type recognised");
        CHECK(c.locoId == 1234 && c.ctrlId == 1, "loco and ctrl extracted");
        CHECK(c.bytes.size() == 40 + 40, "two 40-byte key sets, no count byte");

        const QVector<FieldRow> rows = CaptureDecoder::describe(c);
        CHECK(!rows.isEmpty(), "decodes to fields");
        CHECK(valueOf(rows, "key sets") == "2", "two sets reported");

        // Validity windows are the point of this packet for testing:
        // REMAINING_KEYS_LESS_THAN_5 and SESSION_KEY_MISMATCH are about key
        // lifecycle, and that lifecycle is these dates.
        QString all;
        for (const FieldRow &r : rows) all += r.field + "=" + r.value + "\n";
        CHECK(all.contains("2026-05-24 00:00"), "start time decoded");
        CHECK(all.contains("2026-09-01 00:00"), "first set's end time");
        CHECK(all.contains("2026-09-11 00:00"), "second set's end time");
    }

    // ---- KEY_TIME is year-2000 -------------------------------------------
    // Confirmed against the console's own epoch print: [26][5][24][0] is
    // 2026-05-24, matching START 1779580800.
    {
        QByteArray p = keySet(27,12,31,23, 28,1,1,0, 0x11,0x22, 0x33,0x44);
        const QVector<FieldRow> rows = CaptureDecoder::describe(
            CaptureDecoder::parseLine(line(p)));
        QString all;
        for (const FieldRow &r : rows) all += r.value + "\n";
        CHECK(all.contains("2027-12-31 23:00"), "yy=27 renders as 2027");
        CHECK(all.contains("2028-01-01 00:00"), "and rolls over correctly");
        CHECK(!all.contains("0027-"), "never a bare two-digit year");
    }

    // ---- key material is NOT rendered ------------------------------------
    // These are live authentication keys, and every decoded frame is
    // archived to .dlr and can reach a report. A fingerprint distinguishes
    // sets without disclosing the key.
    {
        QByteArray p;
        QByteArray full;
        for (int i = 0; i < 8; ++i) full.append(char(0x26 + i));   // start/end
        QByteArray k0(16, '\0'), k1(16, '\0');
        for (int i = 0; i < 16; ++i) { k0[i] = char(0xA0 + i); k1[i] = char(0xB0 + i); }
        p += full + k0 + k1;

        const QVector<FieldRow> rows = CaptureDecoder::describe(
            CaptureDecoder::parseLine(line(p)));
        QString all;
        for (const FieldRow &r : rows) all += r.value + " ";

        // The full 16-byte key must not appear anywhere in the output.
        CHECK(!all.contains("a0a1a2a3", Qt::CaseInsensitive),
              "the full key is never rendered");
        CHECK(!all.contains("b0b1b2b3", Qt::CaseInsensitive),
              "nor the second key");
        // But something distinguishing must, or two sets are
        // indistinguishable and 'the key changed' is unverifiable.
        CHECK(all.contains("A0A1", Qt::CaseInsensitive),
              "a fingerprint IS shown, so sets can be told apart");
    }

    // ---- degenerate input -------------------------------------------------
    {
        // A partial set (not a whole multiple of 40 bytes) is flagged, and only
        // the whole set that fits is decoded.
        QByteArray p = keySet(26,5,24,0, 26,9,1,0, 1,2, 3,4);
        p += QByteArray(10, '\0');                 // 50 bytes: one set + 10 extra
        const QVector<FieldRow> rows = CaptureDecoder::describe(
            CaptureDecoder::parseLine(line(p)));
        CHECK(valueOf(rows, "key sets") == "1", "one whole set decoded");
        CHECK(!valueOf(rows, "note").isEmpty(),
              "and the non-multiple length is stated");
        int idx = 0;
        for (const FieldRow &r : rows) if (r.field.contains("valid_from")) ++idx;
        CHECK(idx == 1, "only the set that actually fits is decoded");
    }
}

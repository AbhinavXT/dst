#include "testutil.h"
#include "capturedecoder.h"

namespace {
// RANDOM_NUMBER as the struct defines it:
//   typedef struct { uint16_t loco_random_num; uint16_t stn_random_num; }
// Flat 4-byte struct, no count prefix and no header. Each u16 is written
// high-to-low, matching the KEY_TIME convention on this dump path.
QByteArray randomFrame(quint16 loco, quint16 stn)
{
    QByteArray b;
    // Each uint16 is stored LSB-first (direct memcpy on the LE target).
    b.append(char(quint8(loco & 0xFF))); b.append(char(quint8(loco >> 8)));
    b.append(char(quint8(stn  & 0xFF))); b.append(char(quint8(stn  >> 8)));
    return b;
}
QString line(const QString &tag, const QByteArray &payload)
{
    return QStringLiteral("%1 2026-08-25T15:56:34 350 %2")
               .arg(tag, QString::fromLatin1(payload.toHex()));
}
QString valueOf(const QVector<FieldRow> &rows, const QString &needle)
{
    for (const FieldRow &r : rows)
        if (r.field.contains(needle)) return r.value;
    return QString();
}
}  // namespace

TEST_SUITE(randomnum)
{
    // ---- the tag family routes to Random ---------------------------------
    // @random_num_<loco>_<ctrl> and any suffixed variant must resolve to
    // Random, not Unknown, or the frame never reaches the decoder, the live
    // console, or the replay window.
    {
        const QByteArray p = randomFrame(0x1234, 0xABCD);
        for (const char *tag : { "@random_num_1_1", "@random_num1_1_1",
                                 "@randomnum_1_1" }) {
            const CaptureLine c =
                CaptureDecoder::parseLine(line(QLatin1String(tag), p));
            CHECK(c.valid, "random_num line parses");
            CHECK(c.type == CapType::Random, "tag routes to Random");
            CHECK(c.locoId == 1 && c.ctrlId == 1, "loco and ctrl extracted");
            CHECK(c.bytes.size() == 4, "flat 4-byte struct, no prefix");
        }
    }

    // ---- both fields decode, and are not swapped -------------------------
    // The two members must not be transposed: loco and stn are distinct
    // halves of the handshake and a swap would be invisible in live data.
    {
        const QVector<FieldRow> rows = CaptureDecoder::describe(
            CaptureDecoder::parseLine(
                line(QStringLiteral("@random_num_1_1"),
                     randomFrame(0x1234, 0xABCD))));
        CHECK(valueOf(rows, "loco_random_num").contains("4660"),
              "loco number decodes (0x1234)");
        CHECK(valueOf(rows, "stn_random_num").contains("43981"),
              "stn number decodes (0xABCD)");
        CHECK(valueOf(rows, "loco_random_num").contains("0x1234"),
              "hex is shown so byte order can be eyeballed");
        CHECK(!valueOf(rows, "loco_random_num").contains("0X"),
              "the 0x prefix is not upper-cased");
        // The raw bytes must be present: a byte-swapped random number still
        // looks random, so the raw view is the only way to catch an order bug.
        CHECK(!valueOf(rows, "raw").isEmpty(), "raw bytes are shown");
    }

    // ---- real captured frame: LSB-first uint16s --------------------------
    // From a live @rand_num capture: payload 90 41 0B 0B. Each uint16 is a
    // direct memcpy on the LE target, so loco = 0x4190, stn = 0x0B0B.
    {
        const QVector<FieldRow> rows = CaptureDecoder::describe(
            CaptureDecoder::parseLine(
                QStringLiteral("@rand_num_1_1 2026-08-26T09:30:19 886 90 41 0B 0B")));
        CHECK(valueOf(rows, "loco_random_num").contains("0x4190"),
              "real frame: loco = 0x4190 (little-endian)");
        CHECK(valueOf(rows, "loco_random_num").contains("16784"), "loco decimal 16784");
        CHECK(valueOf(rows, "stn_random_num").contains("0x0B0B"),
              "real frame: stn = 0x0B0B");
    }

    // ---- full-range values survive ---------------------------------------
    {
        const QVector<FieldRow> rows = CaptureDecoder::describe(
            CaptureDecoder::parseLine(
                line(QStringLiteral("@random_num_1_1"),
                     randomFrame(0x0000, 0xFFFF))));
        CHECK(valueOf(rows, "loco_random_num").contains("0"), "zero decodes");
        CHECK(valueOf(rows, "stn_random_num").contains("65535"),
              "0xFFFF does not overflow or sign-extend");
    }

    // ---- degenerate input -------------------------------------------------
    {
        // A short frame must be reported, not read past.
        QByteArray sh; sh.append(char(0x12)); sh.append(char(0x34));
        const QVector<FieldRow> rows = CaptureDecoder::describe(
            CaptureDecoder::parseLine(line(QStringLiteral("@random_num_1_1"), sh)));
        CHECK(!rows.isEmpty(), "a short frame does not crash");
        CHECK(!valueOf(rows, "RANDOM_NUM").isEmpty(),
              "and the shortfall is stated");
    }
    {
        // Trailing bytes mean the wire struct is not the assumed 4 bytes.
        QByteArray lg = randomFrame(1, 2); lg.append(char(0x99));
        const QVector<FieldRow> rows = CaptureDecoder::describe(
            CaptureDecoder::parseLine(line(QStringLiteral("@random_num_1_1"), lg)));
        CHECK(!valueOf(rows, "note").isEmpty(),
              "trailing bytes are flagged, not silently ignored");
    }
}

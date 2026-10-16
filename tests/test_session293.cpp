#include "testutil.h"

#include "capturedecoder.h"
#include "packetbuilder.h"
#include "schema/schemaencoder.h"

#include <QFile>

// =============================================================================
//  Session 293 — the DOP pins by name. @dop1/@dop2 rows were pin[0]..pin[15];
//  pin i is bit i of STRUCT_LOCO_OUTPUT, so each is now named after it
//  (kavach_ok, kavach_not_ok, sos, ... light_engine_brake, spare_10..15), in
//  the decode, in Packet Maker's fields, and in the IO timeline. @dip1/@dip2
//  already carried their struct's names, bit for bit; checked here too.
//  Real frames: replay/2026-10-08/loco_1_1_08102026_101300.cap.
// =============================================================================

namespace {
QString firstLine(const QString &path, const QString &prefix)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith(prefix)) return line;
    }
    return QString();
}

QString valueOf(const QVector<FieldRow> &rows, const QString &field)
{
    for (const FieldRow &r : rows)
        if (r.field.trimmed() == field) return r.value.trimmed();
    return QStringLiteral("<none>");
}
}  // namespace

TEST_SUITE(session293)
{
    const QString cap = QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_101300.cap");
    const QStringList outputs = {
        QStringLiteral("kavach_ok"), QStringLiteral("kavach_not_ok"), QStringLiteral("sos"), QStringLiteral("buzzer"),
        QStringLiteral("horn"), QStringLiteral("normal_brake"), QStringLiteral("full_service_brake"),
        QStringLiteral("emergency_brake"), QStringLiteral("no_sos_green_led"), QStringLiteral("light_engine_brake"),
        QStringLiteral("spare_10"), QStringLiteral("spare_11"), QStringLiteral("spare_12"), QStringLiteral("spare_13"),
        QStringLiteral("spare_14"), QStringLiteral("spare_15") };

    // ---- the decode: a real @dop1 ----------------------------------------------------
    // "@dop1_1_1 2026-10-08T10:13:00 25626 17 0C 17 17 17 17 0C 17 17 17 0C 0C 0C 0C 0C 17"
    const QString dop = firstLine(cap, QStringLiteral("@dop1_1_1"));
    CHECK(dop.endsWith(QLatin1String("17 0C 17 17 17 17 0C 17 17 17 0C 0C 0C 0C 0C 17")), "fixture: the real @dop1 frame");
    const CaptureLine line = CaptureDecoder::parseLine(dop);
    const QVector<FieldRow> rows = CaptureDecoder::describe(line);
    QStringList names;
    bool noPin = true;
    for (const FieldRow &r : rows) {
        if (r.value.contains(QLatin1String("output="))) names << r.field.trimmed();
        if (r.field.contains(QLatin1String("pin["))) noPin = false;
    }
    CHECK(names == outputs, QByteArray("sixteen outputs, named in STRUCT_LOCO_OUTPUT's order: ")
                                + names.join(QLatin1Char(',')).toUtf8());
    CHECK(noPin, "no row is called pin[i] any more");
    // 0x17 = output, trans 0, fault, b, a = 1 0 1 1 1; 0x0C = 0 1 1 0 0.
    CHECK(valueOf(rows, QStringLiteral("kavach_ok")) == QLatin1String("a=1 b=1 fault=1 trans=0 output=1"),
          "kavach_ok is pin 0's byte, 0x17");
    CHECK(valueOf(rows, QStringLiteral("kavach_not_ok")) == QLatin1String("a=0 b=0 fault=1 trans=1 output=0"),
          "kavach_not_ok is pin 1's, 0x0C");
    CHECK(valueOf(rows, QStringLiteral("full_service_brake")) == QLatin1String("a=0 b=0 fault=1 trans=1 output=0"),
          "full_service_brake is pin 6's, 0x0C");
    CHECK(valueOf(rows, QStringLiteral("spare_15")) == QLatin1String("a=1 b=1 fault=1 trans=0 output=1"),
          "spare_15 is pin 15's, 0x17");

    const QString dop2 = firstLine(cap, QStringLiteral("@dop2_1_1"));
    if (!dop2.isEmpty()) {
        const QVector<FieldRow> rows2 = CaptureDecoder::describe(CaptureDecoder::parseLine(dop2));
        CHECK(valueOf(rows2, QStringLiteral("emergency_brake")) != QLatin1String("<none>"),
              "@dop2 has the same names");
    }

    // ---- the inputs already had their struct's names ---------------------------------
    // "@dip2_1_1 ... 7A 02 A0 1A": bit 24 (eb_relay_fb_no) is 0xA0's... byte 3 = 0x1A.
    const QString dip2 = firstLine(cap, QStringLiteral("@dip2_1_1"));
    const QVector<FieldRow> in2 = CaptureDecoder::describe(CaptureDecoder::parseLine(dip2));
    CHECK(valueOf(in2, QStringLiteral("25 eb_relay_fb_no")) == QLatin1String("0")
              && valueOf(in2, QStringLiteral("26 eb_relay_fb_nc")) == QLatin1String("1"),
          "@dip2 by name: eb_relay_fb_no 0, _nc 1 (byte 3 = 0x1A, bits 24/25)");
    CHECK(valueOf(in2, QStringLiteral("29 sifa_or_veb_relay_fb_nc")) == QLatin1String("1")
              && valueOf(in2, QStringLiteral("30 sifa_or_veb_relay_fb_no")) == QLatin1String("0"),
          "the swapped sifa_or_veb pair in the struct's order (_nc at bit 28)");

    // ---- Packet Maker: the fields are named too ----------------------------------------
    PacketBuilder pb;
    CHECK(pb.ready(), "fixture: the encoder");
    if (pb.ready()) {
        const Schema::PacketInfo info = pb.encoder().packet(QStringLiteral("dop1"));
        QStringList fields;
        for (const Schema::FieldInfo &f : info.header)
            if (!f.name.isEmpty()) fields << f.name;
        CHECK(fields.contains(QStringLiteral("emergency_brake.output")) && fields.contains(QStringLiteral("kavach_ok.fault"))
                  && !fields.join(QLatin1Char(' ')).contains(QLatin1String("pin[")),
              "Packet Maker's dop1 fields: emergency_brake.output, kavach_ok.fault, no pin[i]");
        QHash<QString, qint64> h;
        h[QStringLiteral("emergency_brake.output")] = 1;
        const PacketBuilder::Result r = pb.build(QStringLiteral("dop1"), h, {}, QByteArray());
        CHECK(r.ok && r.frame.size() == 16 && quint8(r.frame[7]) == 0x10,
              "emergency_brake.output = 1 sets pin 7's output bit");
        const QVector<FieldRow> back = CaptureDecoder::describe(
            CaptureDecoder::parseLine(QStringLiteral("@dop1_1_1 2026-10-08T10:13:00 1 ") + QString::fromLatin1(r.frame.toHex(' ').toUpper())));
        CHECK(valueOf(back, QStringLiteral("emergency_brake")).endsWith(QLatin1String("output=1")),
              "and decodes back under the same name");
    }
}

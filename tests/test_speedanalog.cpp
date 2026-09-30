#include "testutil.h"

#include "capturedecoder.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <cmath>
#include <cstring>
#include <limits>

// =============================================================================
//  @speed, @analog_top, @analog_bottom (session 67)
//
//  No real frame of any of the three has been captured yet, so every frame
//  here is BUILT from the C struct the firmware declares:
//
//      STRUCT_SENSOR_SPEED_DATA   u8 reader, u8 dir, u32 tacho1, u32 tacho2,
//                                 u32 crc                               14 B
//      STRUCT_ANALOG_SENSOR_DATA  float channel1..6 (no CRC)           24 B
//
//  packed, little-endian. @speed's CRC is ASSUMED to be the firmware JAMCRC
//  (init 0, reflected) over everything before it, as for @ccsys/@dlsys. What this suite pins is the host side: the three
//  tokens route to the right type, the CRC is taken over the right span at
//  the right offset, the decode reads each field from where the struct puts
//  it, and a frame of the wrong length is not "checked" against a guessed
//  span. Whether the firmware's CRC really is that JAMCRC is for the first
//  live capture to settle.
// =============================================================================

namespace {

void putLe32(QByteArray &b, quint32 v)
{
    for (int i = 0; i < 4; ++i) { b.append(char((v >> (8 * i)) & 0xFF)); }
}

QByteArray withCrc(QByteArray body)
{
    putLe32(body, CaptureDecoder::jamcrc(body, 0, body.size()));
    return body;
}

QString line(const QString &tag, const QByteArray &frame)
{
    QStringList hex;
    for (char ch : frame) {
        hex << QStringLiteral("%1").arg(quint8(ch), 2, 16, QLatin1Char('0')).toUpper();
    }
    return QStringLiteral("@%1_1_1 2026-09-24T10:00:00 42 %2").arg(tag, hex.join(' '));
}

QByteArray speedFrame(quint8 reader, quint8 dir, quint32 t1, quint32 t2)
{
    QByteArray b;
    b.append(char(reader)).append(char(dir));
    putLe32(b, t1);
    putLe32(b, t2);
    return withCrc(b);
}

QByteArray analogFrame(const float (&ch)[6])
{
    QByteArray b;
    for (float f : ch) {
        quint32 raw; std::memcpy(&raw, &f, sizeof(raw));
        putLe32(b, raw);                    // the target is little-endian
    }
    return b;                               // no CRC in this struct
}

QHash<QString, QString> rowsOf(const CaptureLine &c)
{
    QHash<QString, QString> m;
    for (const FieldRow &r : CaptureDecoder::describe(c)) {
        m.insert(r.field.trimmed(), r.value);
    }
    return m;
}

CaptureLine flip(const QByteArray &frame, const QString &tag, int at)
{
    QByteArray f = frame;
    f[at] = char(quint8(f.at(at)) ^ 0xFF);
    return CaptureDecoder::parseLine(line(tag, f));
}

} // namespace

TEST_SUITE(speedanalog)
{
    using CaptureDecoder::parseLine;

    // ---- routing -------------------------------------------------------------
    CHECK(CaptureDecoder::typeFromToken("speed") == CapType::Speed, "speed token");
    CHECK(CaptureDecoder::typeFromToken("analog_top") == CapType::AnalogTop,
          "analog_top token");
    CHECK(CaptureDecoder::typeFromToken("analog_bottom") == CapType::AnalogBottom,
          "analog_bottom token");
    CHECK(CaptureDecoder::directionFor(CapType::Speed) == CapDir::In
          && CaptureDecoder::directionFor(CapType::AnalogTop) == CapDir::In
          && CaptureDecoder::directionFor(CapType::AnalogBottom) == CapDir::In,
          "all three are sensed inputs");

    // ---- @speed ----------------------------------------------------------------
    // Values chosen so every byte of each u32 differs: a byte-order or offset
    // slip shows up as a different number, not the same one.
    const QByteArray spd = speedFrame(2, 1, 0x01020304u, 0xA1B2C3D4u);
    {
        const CaptureLine c = parseLine(line("speed", spd));
        CHECK(c.valid && c.type == CapType::Speed, "@speed line parses");
        CHECK(c.bytes.size() == 14, "and carries the 14-byte struct");
        CHECK(c.locoId == 1 && c.ctrlId == 1, "loco/ctrl from the tag");
        CHECK(c.crcChecked && c.crcOk, "CRC over [0:10], LE word at [10:14], passes");

        const auto m = rowsOf(c);
        CHECK(m.value("speed_sensor_reader") == "2", "speed_sensor_reader at byte 0");
        CHECK(m.value("sensor_dir") == "1", "sensor_dir at byte 1");
        CHECK(m.value("pulse_from_tachometer1") == QString::number(0x01020304u),
              "tachometer 1 is the LE u32 at [2:6]");
        CHECK(m.value("pulse_from_tachometer2") == QString::number(0xA1B2C3D4u),
              "tachometer 2 is the LE u32 at [6:10], full 32-bit range");
        CHECK(m.value("crc").endsWith("PASS"), "decoded CRC row agrees");
    }
    for (int at : { 0, 1, 2, 9, 10, 13 }) {
        const CaptureLine c = flip(spd, "speed", at);
        CHECK(c.crcChecked && !c.crcOk,
              QStringLiteral("@speed: corrupting byte %1 fails the CRC").arg(at)
                  .toUtf8().constData());
    }
    {
        const CaptureLine c = parseLine(line("speed", spd.left(13)));
        CHECK(c.valid && !c.crcChecked,
              "a 13-byte @speed is not checked against a guessed span");
    }

    // ---- @analog_top / @analog_bottom -------------------------------------------
    // Six LE floats, 24 B, no CRC. Values are the ones a float field has to get
    // right: a fraction, a negative, zero, a large value, and the two IEEE
    // specials a failing ADC path can produce. Each is rendered %g — the same
    // on the C++ and Python sides.
    const float ch[6] = { 4.75f, -0.5f, 0.0f, 12345.678f,
                          std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity() };
    const QByteArray an = analogFrame(ch);
    const char *const want[6] = { "4.75", "-0.5", "0", "12345.7", "nan", "inf" };
    for (const char *tag : { "analog_top", "analog_bottom" }) {
        const QString t = QString::fromLatin1(tag);
        const CaptureLine c = parseLine(line(t, an));
        CHECK(c.valid && c.bytes.size() == 24,
              QStringLiteral("@%1 parses, 24 bytes").arg(t).toUtf8().constData());
        CHECK(!c.crcChecked,
              QStringLiteral("@%1 has no CRC, so none is claimed").arg(t).toUtf8().constData());
        const auto m = rowsOf(c);
        for (int i = 0; i < 6; ++i) {
            const QString got = m.value(QStringLiteral("channel%1_data").arg(i + 1));
            CHECK(got == QLatin1String(want[i]),
                  QStringLiteral("@%1 channel%2 = %3 (got '%4'): LE float at [%5:%6]")
                      .arg(t).arg(i + 1).arg(want[i]).arg(got).arg(4 * i).arg(4 * i + 4)
                      .toUtf8().constData());
        }
    }
    CHECK(parseLine(line("analog_top", an)).type == CapType::AnalogTop
          && parseLine(line("analog_bottom", an)).type == CapType::AnalogBottom,
          "top and bottom stay distinct types (same layout, different source)");
}

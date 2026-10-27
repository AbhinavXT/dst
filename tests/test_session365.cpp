#include "testutil.h"

#include "cabio.h"
#include "capturedecoder.h"

#include <QFile>

// =============================================================================
//  Session 365 — Tools > Schema > Cab layout: @dip and @dop/@lout decoded as
//  the SIFA-extended or the old cab's structs.
//
//  Abhinav: "Can we add a toggle for both dip and dop so that dl console can
//  decode both old and sifa ones".
//
//  Real lines: replay/2026-10-08/loco_1_1_08102026_101300.cap.
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

QString valueOf(const QString &line, const QString &field)
{
    for (const FieldRow &r : CaptureDecoder::describe(CaptureDecoder::parseLine(line)))
        if (r.field.trimmed() == field) return r.value.trimmed();
    return QStringLiteral("<none>");
}

}  // namespace

TEST_SUITE(session365)
{
    const bool dipWas = CaptureDecoder::dipOldCab(), dopWas = CaptureDecoder::dopOldCab();
    const QString cap = QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_101300.cap");
    const QString dip = firstLine(cap, QStringLiteral("@dip1_1_1"));
    const QString dop = firstLine(cap, QStringLiteral("@dop1_1_1"));
    CHECK(!dip.isEmpty() && !dop.isEmpty(), "fixture: a real @dip1 and @dop1");

    // ---- @dip ------------------------------------------------------------------------------------
    // Byte 0 of the real @dip1 is 0x95: bits 0 and 2 set, bit 1 clear.
    CaptureDecoder::setDipOldCab(false);
    CHECK(valueOf(dip, QStringLiteral("1 dmi1_sos_nc")) == QLatin1String("1")
          && valueOf(dip, QStringLiteral("1 biu_status")) == QLatin1String("<none>"), "SIFA: SET1's names");
    CaptureDecoder::setDipOldCab(true);
    CHECK(valueOf(dip, QStringLiteral("1 biu_status")) == QLatin1String("1") && valueOf(dip, QStringLiteral("2 mr_cock")) == QLatin1String("0")
          && valueOf(dip, QStringLiteral("3 bp_cock")) == QLatin1String("1"), "old cab: STRUCT_CAB_INPUT bits 0, 1, 2");
    CHECK(valueOf(dip, QStringLiteral("1 dmi1_sos_nc")) == QLatin1String("<none>"), "old cab: no SIFA name");
    CHECK(valueOf(QString(dip).replace(QLatin1String("@dip1"), QLatin1String("@dip2")), QStringLiteral("1 biu_status")) == QLatin1String("1"),
          "old cab: @dip2 is the same struct");
    {
        // Every old-cab input under its own bit: bit b set alone reads 1 under "b+1 name".
        bool all = true;
        for (const CabIo::Input &in : CabIo::oldCabInputs().inputs) {
            const quint32 w = 1u << in.bit;
            QString line = QStringLiteral("@dip1_1_1 2026-10-08T10:13:00 1");
            for (int i = 0; i < 4; ++i) line += QStringLiteral(" %1").arg((w >> (8 * i)) & 0xFF, 2, 16, QLatin1Char('0'));
            if (valueOf(line, QStringLiteral("%1 %2").arg(in.bit + 1).arg(in.name)) != QLatin1String("1")) all = false;
        }
        CHECK(all, "every STRUCT_CAB_INPUT input decodes at cabio's bit");
    }

    // ---- @dop / @lout ----------------------------------------------------------------------------
    CaptureDecoder::setDopOldCab(true);
    const QString pin0 = valueOf(dop, QStringLiteral("kavach_ok"));
    CHECK(pin0.contains(QLatin1String("output=")), "old cab: pin 0 is kavach_ok");
    CaptureDecoder::setDopOldCab(false);
    CHECK(valueOf(dop, QStringLiteral("dmi_sos_red")) == pin0 && valueOf(dop, QStringLiteral("kavach_ok")) == QLatin1String("<none>"),
          "SIFA: pin 0 is dmi_sos_red, the same flags");
    CHECK(valueOf(dop, QStringLiteral("emergency_brake")) != QLatin1String("<none>")
          && valueOf(dop, QStringLiteral("sifa_valve")) != QLatin1String("<none>"), "SIFA: emergency_brake and sifa_valve pins");

    // @lout 0x2000 (bit 13): SIFA emergency_brake; old cab spare_13.
    const QString lout = QStringLiteral("@lout_1_1 2026-10-24T10:00:00 1 00 20");
    CHECK(valueOf(lout, QStringLiteral("emergency_brake")) == QLatin1String("1"), "SIFA: @lout bit 13 is emergency_brake");
    CaptureDecoder::setDopOldCab(true);
    CHECK(valueOf(lout, QStringLiteral("spare_13")) == QLatin1String("1") && valueOf(lout, QStringLiteral("emergency_brake")) == QLatin1String("0"),
          "old cab: @lout bit 13 is spare_13, emergency_brake (bit 7) 0");

    CaptureDecoder::setDipOldCab(dipWas);
    CaptureDecoder::setDopOldCab(dopWas);
}

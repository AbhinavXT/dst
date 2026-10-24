#include "testutil.h"

#include "cabio.h"
#include "capturedecoder.h"
#include "settings.h"

#ifdef DL_HAVE_SERIAL
#include "brakebenchwindow.h"
#include "cabiosimwindow.h"
#endif

#include <QSettings>

// =============================================================================
//  Session 364 — @lout: the 16-bit output word the VCC sends the output cards.
//
//  Abhinav: "add a lout message that will give you the 16 bkt output that vcc
//  sends to output card". Asked and answered: two bytes, low byte first; it
//  is decoded in the log tabs and drives the Cab I/O Simulator as an output
//  data frame does.
//
//  No real @lout line yet: the word is the bench capture's real output data
//  0x03E1 (tests/fixtures/cabio_frames_20261023.txt; every SIFA brake applied).
// =============================================================================

namespace {

using namespace CabIo;

const QString kLine = QStringLiteral("@lout_1_1 2026-10-24T10:00:00 123 E1 03");

void clearSaved()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    for (const QString &group : { QStringLiteral("cabio"), QStringLiteral("brakebench") }) {
        s.beginGroup(group);
        for (const QString &k : s.childKeys())
            if (k.startsWith(QLatin1String("brakeKg_")) || k.startsWith(QLatin1String("analog")) || k == QLatin1String("rampRate")
                || k == QLatin1String("brakePressures") || k.startsWith(QLatin1String("rules_")) || k == QLatin1String("cab")
                || k == QLatin1String("source") || k.startsWith(QLatin1String("apply_")) || k.startsWith(QLatin1String("release_")))
                s.remove(k);
        s.endGroup();
    }
}

}  // namespace

TEST_SUITE(session364)
{
    // ---- the decoder ------------------------------------------------------------------------------
    const CaptureLine c = CaptureDecoder::parseLine(kLine);
    CHECK(c.valid && c.type == CapType::Lout && c.bytes.size() == 2, "@lout parses: two bytes");
    CHECK(CaptureDecoder::directionFor(CapType::Lout) == CapDir::Out
          && QLatin1String(CaptureDecoder::typeLabel(CapType::Lout)) == QLatin1String("lout"), "out, labelled lout");

    QHash<QString, qint64> raw;
    CaptureDecoder::describe(c, nullptr, 0, &raw);
    // 0x03E1: bits 0, 5, 6, 7, 8, 9 set; the brakes (10-13, act on 0) applied.
    CHECK(raw.value(QStringLiteral("dmi_sos_red"), -1) == 1 && raw.value(QStringLiteral("dmi_sos_green"), -1) == 0,
          "byte 0 bit 0: dmi_sos_red 1, bit 1: dmi_sos_green 0");
    CHECK(raw.value(QStringLiteral("horn2"), -1) == 1 && raw.value(QStringLiteral("fouling_mark"), -1) == 1,
          "bit 5 horn2 1, bit 9 (byte 1 bit 1) fouling_mark 1");
    CHECK(raw.value(QStringLiteral("emergency_brake"), -1) == 0 && raw.value(QStringLiteral("normal_brake"), -1) == 0
              && raw.value(QStringLiteral("spare"), -1) == 0,
          "bits 12, 13, 15: 0");
    // The names match the simulator's SIFA layout, bit for bit.
    {
        const CaptureLine one = CaptureDecoder::parseLine(QStringLiteral("@lout_1_1 2026-10-24T10:00:00 1 00 20"));
        QHash<QString, qint64> r1;
        CaptureDecoder::describe(one, nullptr, 0, &r1);
        CHECK(r1.value(QStringLiteral("emergency_brake"), -1) == 1 && r1.value(QStringLiteral("normal_brake"), -1) == 0,
              "0x2000: only emergency_brake (bit 13), as cabio's outputBits");
        bool same = true;
        for (const OutputBit &o : outputBits(Cab::Sifa)) {
            const quint16 w = quint16(1u << o.bit);
            const CaptureLine l = CaptureDecoder::parseLine(
                QStringLiteral("@lout_1_1 2026-10-24T10:00:00 1 %1 %2").arg(w & 0xFF, 2, 16, QLatin1Char('0')).arg(w >> 8, 2, 16, QLatin1Char('0')));
            QHash<QString, qint64> r;
            CaptureDecoder::describe(l, nullptr, 0, &r);
            if (r.value(o.name, -1) != 1) same = false;
        }
        CHECK(same, "every SIFA output's bit decodes under its name");
    }

#ifdef DL_HAVE_SERIAL
    // ---- the simulator ----------------------------------------------------------------------------
    clearSaved();
    {
        CabIoSimWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.setWriteSink([](const QByteArray &) {});
        w.setCab(Cab::Sifa);
        // The pressures live in the Brake Test Bench since session 360.
        BrakeBenchWindow bench(nullptr);
        bench.setAttribute(Qt::WA_DeleteOnClose, false);
        QObject::connect(&w, &CabIoSimWindow::outputWordChanged, &bench, &BrakeBenchWindow::observeOutputWord);
        bench.setSource(BrakeBenchWindow::Source::Relays);
        for (int ch = 0; ch < 3; ++ch) bench.setRates(ch, 1.0, 1.0);
        w.observeLoutLine(QStringLiteral("@dmi_1_1 2026-10-24T10:00:00 1 E1 03"));
        w.observeLoutLine(QStringLiteral("@lout_1_1 2026-10-24T10:00:00 1 E1"));
        CHECK(!w.outputSeen() && w.loutLines() == 0, "not @lout, or one byte: ignored");
        w.observeLoutLine(kLine);
        CHECK(w.outputSeen() && w.outputWord() == 0x03E1 && w.loutLines() == 1, "@lout E1 03: the output word 0x03E1");
        CHECK(bench.brakeLevelNow() == EbApplied, "EB applied: the bench's brake relays follow");
        bench.advance(10.0);
        CHECK(qAbs(bench.plant().value(0) - bench.brakePressure(EbApplied, 0)) < 0.02, "BP lands on the EB value");
        // The EB rule's feedback goes On (10).
        bool ebRule = false;
        for (int i = 0; i < w.rules().size(); ++i)
            if (w.rules().at(i).when == QLatin1String("out:emergency_brake") && w.ruleFired(i) > 0) ebRule = true;
        CHECK(ebRule, "the EB feedback rule fired");
        w.observeLoutLine(QStringLiteral("@lout_1_1 2026-10-24T10:00:01 124 00 3C"));
        CHECK(w.outputWord() == 0x3C00 && bench.brakeLevelNow() == NoBrake && w.outputChanges(13) == 1,
              "released: 0x3C00, no brake, EB changed once");
    }
    clearSaved();
#endif
}

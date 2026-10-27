#include "testutil.h"

#include "cabio.h"
#include "capturedecoder.h"
#include "settings.h"

#ifdef DL_HAVE_SERIAL
#include "brakebenchwindow.h"
#include "cabiosimwindow.h"
#endif

#include <QFile>
#include <QSettings>

// =============================================================================
//  Session 364 — @lout: the 16-bit output word the VCC sends the output cards.
//
//  Abhinav: "add a lout message that will give you the 16 bkt output that vcc
//  sends to output card". Asked and answered: two bytes, low byte first; it
//  is decoded in the log tabs and drives the Cab I/O Simulator as an output
//  data frame does.
//
//  Session 361: the real @lout line is the whole output data frame (CC CC 10
//  01 0D 2D seq E1 03 crc DD DD), not two bytes: real lines from Abhinav's
//  81_1_24102026_164654.log, tests/fixtures/lout_20261024.txt (0x03E1,
//  every SIFA brake applied, then 0x01E1).
// =============================================================================

namespace {

using namespace CabIo;

// The fixture's @lout lines, from their '@'.
QStringList realLout()
{
    QStringList out;
    QFile f(QStringLiteral(DL_SRC_DIR "/tests/fixtures/lout_20261024.txt"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd()) {
        const QString l = QString::fromUtf8(f.readLine()).trimmed();
        const int at = l.indexOf(QLatin1String("@lout_"));
        if (at >= 0) out << l.mid(at);
    }
    return out;
}

// A frame to output card 13 carrying `w`, as the VCC logs it (CRC not checked by the decoder).
QString loutLine(quint16 w)
{
    return QStringLiteral("@lout_1_1 2026-10-24T10:00:00 1 CC CC 10 01 0D 2D 01 00 %1 %2 00 00 00 00 DD DD")
        .arg(w & 0xFF, 2, 16, QLatin1Char('0')).arg(w >> 8, 2, 16, QLatin1Char('0'));
}

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
    // Session 365: @lout follows the outputs' cab layout; these are the SIFA names.
    const bool dopWas = CaptureDecoder::dopOldCab();
    CaptureDecoder::setDopOldCab(false);
    const QStringList real = realLout();
    CHECK(real.size() == 6, "fixture: six real @lout lines");
    if (real.size() != 6) return;
    const CaptureLine c = CaptureDecoder::parseLine(real.at(0));
    CHECK(c.valid && c.type == CapType::Lout && c.bytes.size() == 16, "@lout parses: the 16-byte output data frame");
    {
        Frame f;
        CHECK(parse(c.bytes, &f) && f.isOutputData() && f.h.dest == 13 && f.outputWord() == 0x03E1,
              "it is an output data frame to card 13, word 0x03E1");
    }
    CHECK(CaptureDecoder::directionFor(CapType::Lout) == CapDir::Out
          && QLatin1String(CaptureDecoder::typeLabel(CapType::Lout)) == QLatin1String("lout"), "out, labelled lout");

    QHash<QString, qint64> raw;
    CaptureDecoder::describe(c, nullptr, 0, &raw);
    CHECK(raw.value(QStringLiteral("dest"), -1) == 13 && raw.value(QStringLiteral("msg_id"), -1) == 0x2D
              && raw.value(QStringLiteral("seq"), -1) == 13, "header: dest 13, msg 0x2D, seq 13 (0D 00, LE)");
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
        const CaptureLine one = CaptureDecoder::parseLine(loutLine(0x2000));
        QHash<QString, qint64> r1;
        CaptureDecoder::describe(one, nullptr, 0, &r1);
        CHECK(r1.value(QStringLiteral("emergency_brake"), -1) == 1 && r1.value(QStringLiteral("normal_brake"), -1) == 0,
              "0x2000: only emergency_brake (bit 13), as cabio's outputBits");
        bool same = true;
        for (const OutputBit &o : outputBits(Cab::Sifa)) {
            const quint16 w = quint16(1u << o.bit);
            const CaptureLine l = CaptureDecoder::parseLine(loutLine(w));
            QHash<QString, qint64> r;
            CaptureDecoder::describe(l, nullptr, 0, &r);
            if (r.value(o.name, -1) != 1) same = false;
        }
        CHECK(same, "every SIFA output's bit decodes under its name");
    }
    CaptureDecoder::setDopOldCab(dopWas);

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
        w.observeLoutLine(QStringLiteral("@lout_1_1 2026-10-24T10:00:00 1 E1 03"));
        CHECK(!w.outputSeen() && w.loutLines() == 0, "not @lout, or not an output data frame: ignored");
        w.observeLoutLine(real.at(0));
        CHECK(w.outputSeen() && w.outputWord() == 0x03E1 && w.loutLines() == 1, "real @lout: the output word 0x03E1");
        CHECK(bench.brakeLevelNow() == EbApplied, "EB applied: the bench's brake relays follow");
        bench.advance(10.0);
        CHECK(qAbs(bench.plant().value(0) - bench.brakePressure(EbApplied, 0)) < 0.02, "BP lands on the EB value");
        // The EB rule's feedback goes On (10).
        bool ebRule = false;
        for (int i = 0; i < w.rules().size(); ++i)
            if (w.rules().at(i).when == QLatin1String("out:emergency_brake") && w.ruleFired(i) > 0) ebRule = true;
        CHECK(ebRule, "the EB feedback rule fired");
        for (int i = 1; i < real.size(); ++i) w.observeLoutLine(real.at(i));
        CHECK(w.loutLines() == 6 && w.outputWord() == 0x01E1 && w.outputChanges(9) == 1 && w.outputChanges(13) == 0,
              "the real lines to cards 13 and 14: 0x01E1 last, fouling_mark (bit 9) changed once, EB never");
        w.observeLoutLine(loutLine(0x3C00));
        CHECK(w.outputWord() == 0x3C00 && bench.brakeLevelNow() == NoBrake && w.outputChanges(13) == 1,
              "released: 0x3C00, no brake, EB changed once");
    }
    clearSaved();
#endif
}

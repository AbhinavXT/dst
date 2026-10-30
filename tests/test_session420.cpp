#include "testutil.h"

#include "capturedecoder.h"
#include "locosim.h"
#include "locosimwindow.h"
#include "settings.h"

#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QSettings>
#include <QStringList>

#include <cmath>

// =============================================================================
//  Session 420 — DLConsole reads the BIU frames the VCC copies to it (patch
//  419: @biu_cmd, @biu_beat, @biu_nmt, @biu_beatrx, @biu_press, @biu_codes,
//  @biu_disc), and the Loco Simulator can brake by the VCC's @biu_cmd: a real
//  BIU on CAN, no bench. No real @biu_* line has been captured yet: the lines
//  are schema/fixtures/biu_synthetic.log, the firmware module's own copies
//  (tests/biugen), the BIU's frames spec-derived.
// =============================================================================

namespace {

QStringList fixture420()
{
    QStringList lines;
    QFile f(QStringLiteral("../schema/fixtures/biu_synthetic.log"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return lines;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (!line.isEmpty()) lines << line;
    }
    return lines;
}

// The fixture's first @biu_cmd line whose data bytes start with `bytes`.
QString cmd420(const QStringList &lines, const QString &bytes)
{
    for (const QString &l : lines)
        if (l.startsWith(QLatin1String("@biu_cmd_")) && l.section(QLatin1Char(' '), 3).startsWith(bytes)) return l;
    return QString();
}

// The same line as another loco / VCC copied it (the header words only).
QString from420(const QString &line, int loco, int vcc)
{
    return QStringLiteral("@biu_cmd_%1_%2 %3").arg(loco).arg(vcc).arg(line.section(QLatin1Char(' '), 1));
}

QString row420(const QVector<FieldRow> &rows, const QString &field)
{
    for (const FieldRow &r : rows)
        if (r.field.trimmed() == field) return r.value;
    return QString();
}

void clearSaved420()
{
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("locosim"));
}

}  // namespace

TEST_SUITE(session420)
{
    clearSaved420();
    const QStringList lines = fixture420();
    CHECK(lines.size() == 129, "fixture: schema/fixtures/biu_synthetic.log, 129 @biu_* lines");

    // ---- the seven tokens --------------------------------------------------------------
    {
        struct T { const char *token; CapType type; CapDir dir; };
        const T types[] = {
            { "biu_cmd", CapType::BiuCmd, CapDir::Out },     { "biu_beat", CapType::BiuBeat, CapDir::Out },
            { "biu_nmt", CapType::BiuNmt, CapDir::In },      { "biu_beatrx", CapType::BiuBeatRx, CapDir::In },
            { "biu_press", CapType::BiuPress, CapDir::In },  { "biu_codes", CapType::BiuCodes, CapDir::In },
            { "biu_disc", CapType::BiuDisc, CapDir::In },
        };
        for (const T &t : types) {
            QString line;
            for (const QString &l : lines)
                if (l.startsWith(QStringLiteral("@%1_1_1 ").arg(QLatin1String(t.token)))) { line = l; break; }
            const CaptureLine cap = CaptureDecoder::parseLine(line);
            CHECK(cap.valid && cap.type == t.type && CaptureDecoder::directionFor(t.type) == t.dir
                      && cap.locoId == 1 && cap.ctrlId == 1,
                  qPrintable(QStringLiteral("@%1: its type, direction, loco 1 VCC 1 (%2)").arg(QLatin1String(t.token), line)));
        }
        CHECK(QLatin1String(CaptureDecoder::typeLabel(CapType::BiuCmd)) == QLatin1String("biu command")
                  && QLatin1String(CaptureDecoder::typeLabel(CapType::BiuBeatRx)) == QLatin1String("biu heartbeat (received)"),
              "each named in the tabs");
    }

    // ---- decoded by the schema ---------------------------------------------------------
    {
        // FSB: A9 3.5 (0x46), SA9 2.4 (0x30); health, speed valid, A9 and SA9 valid.
        const QString fsb = cmd420(lines, QStringLiteral("E8 03 12 0C 46 30"));
        const QVector<FieldRow> rows = CaptureDecoder::describe(CaptureDecoder::parseLine(fsb));
        CHECK(row420(rows, QStringLiteral("speed_mps")) == QLatin1String("10.00")
                  && row420(rows, QStringLiteral("health")) == QLatin1String("1")
                  && row420(rows, QStringLiteral("biu_isolate")) == QLatin1String("0")
                  && row420(rows, QStringLiteral("a9_valid")) == QLatin1String("1")
                  && row420(rows, QStringLiteral("a9_bp_target")) == QStringLiteral("3.50 kg/cm² (70)")
                  && row420(rows, QStringLiteral("sa9_bc_target")) == QStringLiteral("2.40 kg/cm² (48)"),
              qPrintable(QStringLiteral("@biu_cmd FSB, bit by bit: %1").arg(fsb)));
        QString beat, nmt;
        for (const QString &l : lines) {
            if (beat.isEmpty() && l.startsWith(QLatin1String("@biu_beat_")) && l.endsWith(QLatin1String(" 05"))) beat = l;
            if (nmt.isEmpty() && l.startsWith(QLatin1String("@biu_nmt_"))) nmt = l;
        }
        CHECK(row420(CaptureDecoder::describe(CaptureDecoder::parseLine(beat)), QStringLiteral("nmt_state"))
                  .startsWith(QLatin1String("5 (operational)")),
              qPrintable(QStringLiteral("@biu_beat 05: operational (%1)").arg(beat)));
        const QVector<FieldRow> n = CaptureDecoder::describe(CaptureDecoder::parseLine(nmt));
        CHECK(row420(n, QStringLiteral("command")).startsWith(QLatin1String("1 (start remote node)"))
                  && row420(n, QStringLiteral("node")).compare(QLatin1String("0x20"), Qt::CaseInsensitive) == 0,
              qPrintable(QStringLiteral("@biu_nmt 01 20: start, node 0x20 (%1)").arg(nmt)));
    }

    // ---- the targets the BIU follows ---------------------------------------------------
    {
        const LocoSim::BrakeRates rates;
        LocoSim::CommandTargets t;
        const auto bytes = [](const QString &hex) { return QByteArray::fromHex(hex.toLatin1()); };
        CHECK(LocoSim::biuCommandTargets(bytes(QStringLiteral("E803120C00300000")), rates, &t)
                  && t.bp == 0.0 && std::fabs(t.bc - 2.4) < 1e-9 && !t.isolated && !t.a9Above && !t.sa9Above,
              "EB: A9 0, SA9 2.4");
        CHECK(LocoSim::biuCommandTargets(bytes(QStringLiteral("E8031A0C00300000")), rates, &t)
                  && t.isolated && t.bp == rates.releasedBpKgcm2 && t.bc == 0.0,
              "the BIU isolated: the targets not followed, BP released");
        CHECK(LocoSim::biuCommandTargets(bytes(QStringLiteral("E803120000300000")), rates, &t)
                  && t.bp == rates.releasedBpKgcm2 && t.bc == 0.0,
              "A9 and SA9 not valid: released BP, BC 0");
        CHECK(LocoSim::biuCommandTargets(bytes(QStringLiteral("E803120CFFC80000")), rates, &t)
                  && t.a9Above && t.sa9Above && std::fabs(t.bp - 12.75) < 1e-9,
              "A9 0xFF and SA9 0xC8 marked valid: followed as the bench's BIU follows them, flagged above 6.0 / 4.0");
        CHECK(!LocoSim::biuCommandTargets(bytes(QStringLiteral("E803120C003000")), rates, &t)
                  && !LocoSim::biuCommandTargets(QByteArray(8, 0), rates, nullptr),
              "7 bytes, or nowhere to put them: false");
    }

    // ---- the Loco Simulator brakes by the VCC's @biu_cmd -------------------------------
    {
        qint64 t = 1000000;
        const QString released = cmd420(lines, QStringLiteral("E8 03 12 0C 64 00"));
        const QString eb = cmd420(lines, QStringLiteral("E8 03 12 0C 00 30"));
        const QString fsb = cmd420(lines, QStringLiteral("E8 03 12 0C 46 30"));
        const QString isolated = cmd420(lines, QStringLiteral("E8 03 1A 0C 00 30"));
        CHECK(!released.isEmpty() && !eb.isEmpty() && !fsb.isEmpty() && !isolated.isEmpty(),
              "the fixture has released, EB, FSB and an isolated EB");
        {
            LocoSimWindow window(nullptr);
            window.setAttribute(Qt::WA_DeleteOnClose, false);
            window.setClock([&t]() { return t; });
            window.setSendSink([](const QByteArray &) {});
            auto *state = window.findChild<QLabel *>(QStringLiteral("locoSimBrakeState"));
            window.setBrakeSource(LocoSim::BrakeSource::VccLog);
            CHECK(window.brakeSource() == LocoSim::BrakeSource::VccLog && state
                      && state->text().startsWith(QLatin1String("no @biu_cmd from the VCC yet")),
                  "by the VCC's @biu_cmd, none yet: says so");
            CHECK(!window.observeBiuCmdLine(QStringLiteral("@biu_beat_1_1 2026-10-30T10:00:01 4200 05"))
                      && window.biuCmdLines() == 0,
                  "a @biu_beat is not a command");

            window.motion().setSpeed(90);
            CHECK(window.observeBiuCmdLine(eb) && window.motion().braking()
                      && std::fabs(window.motion().brake() - 0.9) < 1e-9,
                  "EB from loco 1 VCC 1: 0.9 m/s² at once, the simulator stopped");
            CHECK(state && state->text().startsWith(QStringLiteral("braking EB 0.90 m/s²"))
                      && state->text().contains(QLatin1String("@biu_cmd, loco 1 VCC 1")),
                  qPrintable(QStringLiteral("the Drive tab names the line's source: %1").arg(state ? state->text() : QString())));

            // Another loco's copy while loco 1 VCC 1 is fresh: not taken.
            t += 500;
            CHECK(!window.observeBiuCmdLine(from420(released, 2, 3)) && window.motion().braking()
                      && window.biuCmdIgnored() == 1,
                  "loco 2 VCC 3's release, loco 1 VCC 1 heard 0.5 s ago: not taken, the loco still brakes");
            t += 100;
            window.observeBiuCmdLine(eb);
            CHECK(state && state->text().contains(QLatin1String("1 not taken from another source (last loco 2 VCC 3)")),
                  qPrintable(QStringLiteral("and the Drive tab says so: %1").arg(state ? state->text() : QString())));
            CHECK(!window.observeBiuCmdLine(from420(released, 1, 2)) && window.biuCmdIgnored() == 2,
                  "loco 1's VCC 2 is another source too");

            // Stale, the simulator stopped: the health timer's tick releases it.
            t += LocoSimWindow::kBiuCmdStaleMs + 1;
            window.healthTick();
            CHECK(!window.motion().braking() && state
                      && state->text().startsWith(QLatin1String("no @biu_cmd for 2.0 s: released")),
                  qPrintable(QStringLiteral("no line for over 2 s, stopped: released by the health tick (%1)").arg(state ? state->text() : QString())));
            t += 1500;
            window.healthTick();
            CHECK(state && state->text().startsWith(QLatin1String("no @biu_cmd for 3.5 s")), "the age keeps counting");

            // The followed source silent: another is taken, and its count starts again.
            CHECK(window.observeBiuCmdLine(from420(fsb, 2, 3)) && std::fabs(window.motion().brake() - 0.6) < 1e-9
                      && window.biuCmdIgnored() == 0 && state && state->text().contains(QLatin1String("loco 2 VCC 3")),
                  "loco 1 VCC 1 silent: loco 2 VCC 3's FSB taken (0.6)");
            t += 100;
            CHECK(!window.observeBiuCmdLine(eb), "and now loco 1 VCC 1 is the other source");

            t += 100;
            CHECK(window.observeBiuCmdLine(from420(isolated, 2, 3)) && !window.motion().braking() && state
                      && state->text().contains(QLatin1String("BIU isolated")),
                  qPrintable(QStringLiteral("an EB with the BIU isolated: the loco pilot's brakes, not the VCC's (%1)").arg(state ? state->text() : QString())));

            window.setBrakeFollows(false);
            t += 100;
            window.observeBiuCmdLine(from420(eb, 2, 3));
            CHECK(!window.motion().braking() && state && state->text().startsWith(QLatin1String("off")),
                  "'Brake the loco' off: EB does nothing");
            window.setBrakeFollows(true);

            window.setBrakeSource(LocoSim::BrakeSource::Command);
            CHECK(!window.motion().braking() && state && state->text().contains(QLatin1String("Brake Test Bench")),
                  "by the BIU's command, no bench: the @biu_cmd lines do not brake it");
            t += 100;
            window.observeBiuCmdLine(from420(eb, 2, 3));
            CHECK(!window.motion().braking() && window.biuCmdLines() == 6, "counted, not followed");
            window.setBrakeSource(LocoSim::BrakeSource::VccLog);
            CHECK(window.motion().braking(), "back to the VCC's @biu_cmd: the fresh EB brakes");
        }   // saved on close
        {
            LocoSimWindow again(nullptr);
            again.setAttribute(Qt::WA_DeleteOnClose, false);
            CHECK(again.brakeSource() == LocoSim::BrakeSource::VccLog, "the source is saved (brakeSource=log)");
            QSettings settings(Settings::iniPath(), QSettings::IniFormat);
            CHECK(settings.value(QStringLiteral("locosim/brakeSource")).toString() == QLatin1String("log"), "as 'log'");
        }
    }
    clearSaved420();
}

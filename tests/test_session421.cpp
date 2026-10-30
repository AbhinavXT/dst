#include "testutil.h"

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
//  Session 421 — the Loco Simulator's brake by the VCC's @biu_cmd: the operator
//  picks the tab whose lines brake the loco (Drive ▸ Brake ▸ "from"). Each tab
//  that carries @biu_cmd is offered as it is heard, by its name, loco and VCC;
//  the others' lines are counted, not taken. "the first tab heard" keeps
//  session 420's rule. Lines: schema/fixtures/biu_synthetic.log (synthetic:
//  no real @biu_* line captured yet).
// =============================================================================

namespace {

QString cmd421(const QString &bytes)
{
    QFile f(QStringLiteral("../schema/fixtures/biu_synthetic.log"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    while (!f.atEnd()) {
        const QString l = QString::fromUtf8(f.readLine()).trimmed();
        if (l.startsWith(QLatin1String("@biu_cmd_")) && l.section(QLatin1Char(' '), 3).startsWith(bytes)) return l;
    }
    return QString();
}

QString as421(const QString &line, int loco, int vcc)
{
    return QStringLiteral("@biu_cmd_%1_%2 %3").arg(loco).arg(vcc).arg(line.section(QLatin1Char(' '), 1));
}

QStringList items421(const QComboBox *box)
{
    QStringList out;
    for (int i = 0; box && i < box->count(); ++i) out << box->itemText(i);
    return out;
}

void clearSaved421()
{
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("locosim"));
}

}  // namespace

TEST_SUITE(session421)
{
    clearSaved421();
    const QString eb = cmd421(QStringLiteral("E8 03 12 0C 00 30"));
    const QString released = cmd421(QStringLiteral("E8 03 12 0C 64 00"));
    CHECK(!eb.isEmpty() && !released.isEmpty(), "fixture: an EB and a released @biu_cmd");
    const QString tabA = QStringLiteral("41_1"), tabB = QStringLiteral("42_2");
    qint64 t = 5000000;
    {
        LocoSimWindow window(nullptr);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        window.setClock([&t]() { return t; });
        window.setSendSink([](const QByteArray &) {});
        auto *from = window.findChild<QComboBox *>(QStringLiteral("locoSimBiuTab"));
        auto *state = window.findChild<QLabel *>(QStringLiteral("locoSimBrakeState"));
        window.setBrakeSource(LocoSim::BrakeSource::VccLog);
        CHECK(from && from->count() == 1 && from->currentText() == QLatin1String("from the first tab heard")
                  && window.biuCmdTab().isEmpty(),
              "by default: from the first tab heard, no tab listed yet");
        CHECK(from && !from->isHidden(), "shown with the VCC's @biu_cmd source");
        window.setBrakeSource(LocoSim::BrakeSource::Command);
        CHECK(from && from->isHidden(), "hidden with the other sources");
        window.setBrakeSource(LocoSim::BrakeSource::VccLog);

        // Each tab listed as heard, by name, loco and VCC; the first heard followed.
        window.motion().setSpeed(80);
        CHECK(window.observeBiuCmdLine(eb, tabA, QStringLiteral("Loco 1 VCC-1")) && window.motion().braking(),
              "tab 41_1's EB, first heard: brakes");
        t += 200;
        CHECK(!window.observeBiuCmdLine(as421(released, 2, 2), tabB, tabB) && window.motion().braking(),
              "tab 42_2's release, 41_1 fresh: not taken");
        CHECK((items421(from) == QStringList{ QStringLiteral("from the first tab heard"),
                                              QStringLiteral("Loco 1 VCC-1 (41_1) \u00B7 loco 1 VCC 1"),
                                              QStringLiteral("42_2 \u00B7 loco 2 VCC 2") }),
              qPrintable(QStringLiteral("both tabs offered: %1").arg(items421(from).join(QLatin1String(" | ")))));
        CHECK(state && state->text().contains(QStringLiteral("@biu_cmd, Loco 1 VCC-1 (41_1) \u00B7 loco 1 VCC 1")),
              qPrintable(QStringLiteral("the Drive tab names the tab followed: %1").arg(state ? state->text() : QString())));

        // Pick tab 42_2: 41_1's EB no longer brakes, and its lines are not taken.
        from->setCurrentIndex(from->findData(tabB));
        CHECK(window.biuCmdTab() == tabB && !window.motion().braking() && state
                  && state->text().startsWith(QStringLiteral("no @biu_cmd from 42_2 \u00B7 loco 2 VCC 2 yet")),
              qPrintable(QStringLiteral("42_2 picked: the other tab's EB dropped, says so (%1)").arg(state ? state->text() : QString())));
        t += 100;
        CHECK(!window.observeBiuCmdLine(eb, tabA, QStringLiteral("Loco 1 VCC-1")) && !window.motion().braking(),
              "41_1's EB, fresh, 42_2 picked: not taken");
        t += 100;
        CHECK(window.observeBiuCmdLine(as421(eb, 2, 2), tabB, tabB) && window.motion().braking()
                  && state && state->text().contains(QLatin1String("1 not taken from another source")),
              qPrintable(QStringLiteral("42_2's EB: brakes; 41_1's line counted (%1)").arg(state ? state->text() : QString())));
        // A tab picked is followed even with 41_1 never silent: no hand-over.
        t += LocoSimWindow::kBiuCmdStaleMs + 1;
        CHECK(!window.observeBiuCmdLine(released, tabA, QStringLiteral("Loco 1 VCC-1")),
              "42_2 silent, picked: 41_1 is still not taken");
        window.healthTick();
        CHECK(!window.motion().braking(), "and 42_2's EB, stale, released");

        // Back to the first heard: open again.
        from->setCurrentIndex(0);
        t += 100;
        CHECK(window.observeBiuCmdLine(eb, tabA, QStringLiteral("Loco 1 VCC-1")) && window.motion().braking(),
              "the first tab heard again: 41_1 taken");
        from->setCurrentIndex(from->findData(tabB));
    }   // saved on close
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        CHECK(settings.value(QStringLiteral("locosim/biuCmdTab")).toString() == tabB, "the tab picked is saved");
    }
    {
        LocoSimWindow again(nullptr);
        again.setAttribute(Qt::WA_DeleteOnClose, false);
        again.setClock([&t]() { return t; });
        auto *from = again.findChild<QComboBox *>(QStringLiteral("locoSimBiuTab"));
        CHECK(again.biuCmdTab() == tabB && from && from->currentText() == QLatin1String("42_2 (not heard yet)"),
              "opened again: 42_2 picked, not heard yet");
        CHECK(!again.observeBiuCmdLine(eb, tabA, QStringLiteral("Loco 1 VCC-1")), "41_1 not taken");
        CHECK(again.observeBiuCmdLine(as421(eb, 2, 2), tabB, QStringLiteral("Loco 2 VCC-2"))
                  && from->currentText() == QStringLiteral("Loco 2 VCC-2 (42_2) \u00B7 loco 2 VCC 2"),
              qPrintable(QStringLiteral("42_2 heard: taken, its entry named (%1)").arg(from ? from->currentText() : QString())));
    }
    clearSaved421();
}

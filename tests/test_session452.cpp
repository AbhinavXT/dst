#include "testutil.h"

#include "biuprotocol.h"
#include "biusim.h"
#include "brakebenchwindow.h"
#include "settings.h"
#include "statusline.h"
#include "theme.h"

#include <QFile>
#include <QMap>
#include <QSettings>

// =============================================================================
//  Session 452 — the Brake Test Bench on the log port (51234): the BIU runs
//  from the VCC's @biu_cmd / @biu_beat copies, whichever tab they arrive in,
//  and answers the address they came from. Lines: the synthetic fixture
//  (schema/fixtures/biu_synthetic.log: no real @biu_* capture exists yet).
// =============================================================================

namespace {

using namespace Biu;

struct Sent452 {
    QMap<QString, QVector<Frame>> by;
    void add(const QByteArray &d, const QString &to) { Frame f; if (fromDatagram(d, &f)) by[to] << f; }
    int count(const QString &to, quint16 cob) const
    {
        int n = 0;
        for (const Frame &f : by.value(to)) if (f.cob == cob) ++n;
        return n;
    }
};

QStringList fixtureLines(const QString &prefix)
{
    QStringList out;
    QFile f(QStringLiteral("../schema/fixtures/biu_synthetic.log"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    for (const QString &line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n')))
        if (line.startsWith(prefix)) out << line.trimmed();
    return out;
}

}  // namespace

TEST_SUITE(session452)
{
    {
        QSettings s(Settings::iniPath(), QSettings::IniFormat);
        s.remove(QStringLiteral("brakebench"));
    }
    ThemeUtil::apply(Theme::Light);

    const QStringList cmds = fixtureLines(QStringLiteral("@biu_cmd_"));
    const QStringList beats = fixtureLines(QStringLiteral("@biu_beat_"));
    const QStringList nmts = fixtureLines(QStringLiteral("@biu_nmt_"));
    const QStringList press = fixtureLines(QStringLiteral("@biu_press_"));
    CHECK(!cmds.isEmpty() && !beats.isEmpty() && !nmts.isEmpty() && !press.isEmpty(), "the fixture's @biu_* lines");
    if (cmds.isEmpty() || beats.isEmpty() || nmts.isEmpty() || press.isEmpty()) return;

    BrakeBenchWindow w(nullptr);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    qint64 t = 1000;
    w.setClock([&t]() { return t; });
    Sent452 sent;
    w.biu()->setSendToSink([&sent](const QByteArray &d, const QString &to) { sent.add(d, to); });
    const QString vcc = QStringLiteral("192.168.25.16");

    // On its own port the lines are not the BIU's input.
    CHECK(!w.onLogPort() && w.startBiu(), "on 51235: started");
    CHECK(!w.observeBiuLine(cmds.first(), vcc) && w.biu()->commandCount() == 0, "a @biu_cmd line is not taken there");
    w.stopBiu();

    w.setListenPort(quint16(Settings::udpPort()));
    CHECK(w.onLogPort(), "the log port");
    CHECK(!w.observeBiuLine(cmds.first(), vcc), "stopped: not taken");
    CHECK(w.startBiu() && w.biuRunning(), "Start BIU on the log port: running, not refused");
    StatusLine *status = w.findChild<StatusLine *>();
    CHECK(status && status->message().contains(QStringLiteral("@biu_cmd")), "the status line says it takes the @biu_* lines");

    // KAVACH's heartbeat (pre-operational 7F in the fixture's second line), then commands.
    CHECK(w.observeBiuLine(beats.at(1), vcc), "@biu_beat taken");
    CHECK(w.biu()->kavachState() == 0x7F, "KAVACH heard pre-operational, from the heartbeat line");
    CHECK(w.observeBiuLine(cmds.first(), vcc), "@biu_cmd taken");
    CHECK(w.biu()->commandCount() == 1, "one command");
    const Command c = w.biu()->lastCommand();
    CHECK(c.speedRaw == 1000, "the command's bytes: speed 10.00 m/s (E8 03)");
    CHECK(!w.observeBiuLine(nmts.first(), vcc) && !w.observeBiuLine(press.first(), vcc),
          "the frames the VCC received (@biu_nmt, @biu_press) are not taken");
    CHECK(w.logLinesTaken() == 2, "two lines taken");

    // The BIU answers the VCC the lines came from, on 50001.
    t += 600; w.advance(0.6);
    const QString to = vcc + QStringLiteral(":50001");
    CHECK(sent.count(to, kCobPressures) >= 1 && sent.count(to, kCobCodes) >= 1 && sent.count(to, kCobDiscretes) >= 1,
          "the three status frames go to the sender");
    CHECK(sent.count(to, kCobNmt) >= 1, "and NMT start, KAVACH being pre-operational");

    // Every tab: a second VCC's line is taken too, and answered.
    const QString line2 = QString(cmds.first()).replace(QStringLiteral("@biu_cmd_1_1"), QStringLiteral("@biu_cmd_81_1"));
    CHECK(w.observeBiuLine(line2, QStringLiteral("192.168.25.81")) && w.biu()->commandCount() == 2, "a line from 81_1 taken");
    t += 300; w.advance(0.3);
    CHECK(sent.count(QStringLiteral("192.168.25.81:50001"), kCobPressures) >= 1, "answered at its address");

    w.stopBiu();
    CHECK(!w.observeBiuLine(cmds.first(), vcc), "stopped again: not taken");
    w.setListenPort(51235);
    {
        QSettings s(Settings::iniPath(), QSettings::IniFormat);
        s.remove(QStringLiteral("brakebench"));
    }
}

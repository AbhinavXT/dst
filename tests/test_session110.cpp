#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "querylineedit.h"
#include "serialconsolewindow.h"
#include "serialmanager.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QElapsedTimer>

#ifdef Q_OS_UNIX
#  include <termios.h>
#  include <unistd.h>
#  if defined(Q_OS_MACOS)
#    include <util.h>
#  else
#    include <pty.h>
#  endif
#endif

// =============================================================================
//  Session 110 — Show only / Highlight in the serial terminal, in the
//  console's own query language.
//
//  Real lines: @dop1 from replay/loco_2_1_29062026_134128.cap, @dop2 from
//  replay/loco_1_1_26062026_162418.cap.
// =============================================================================

namespace {

const QByteArray kDop1 =
    "@dop1_2_1 2026-06-29T13:41:29 14034 0C 17 17 17 17 0C 0C 0C 0C 17 17 17 17 17 17 17";
const QByteArray kDop2 =
    "@dop2_1_1 2026-06-26T16:24:18 989 0C 17 17 17 17 0C 0C 0C 0C 0C 17 17 17 17 17 17";

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

}  // namespace

TEST_SUITE(session110)
{
#ifdef Q_OS_UNIX
    int master = -1, slave = -1;
    char name[256] = {};
    if (::openpty(&master, &slave, name, nullptr, nullptr) != 0) { CHECK(false, "openpty"); return; }
    termios t{};
    tcgetattr(master, &t);
    cfmakeraw(&t);
    tcsetattr(master, TCSANOW, &t);
    ::close(slave);
    auto print = [master](const QByteArray &line) {
        const QByteArray b = line + "\r\n";
        (void)::write(master, b.constData(), size_t(b.size()));
    };

    MessageDispatcher disp;
    SerialManager mgr(&disp);
    SerialConsoleWindow w(&mgr);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.findChild<QCheckBox *>(QStringLiteral("serialTimestamps"))->setChecked(false);
    w.findChild<QCheckBox *>(QStringLiteral("serialHexView"))->setChecked(false);
    SerialConfig c;
    c.portName = QString::fromLocal8Bit(name);
    w.setConfigToUi(c);
    CHECK(w.openPort(), "opens");

    print(kDop1); print(kDop2); print("RAD LINK FAIL 3"); print(kDop2);
    settle(250);
    CHECK(w.receivedText().count(QLatin1String("@dop2")) == 2 && w.receivedText().contains(QLatin1String("@dop1")),
          "unfiltered: everything");

    // ---- Show only ------------------------------------------------------------------
    w.setViewFilter(QStringLiteral("@dop2"));
    QString v = w.receivedText();
    CHECK(v.count(QLatin1String("@dop2")) == 2 && !v.contains(QLatin1String("@dop1")) && !v.contains(QLatin1String("RAD")),
          "\"show only @dop2\" re-filters what was already received");
    CHECK(v.contains(QStringLiteral("\u2500\u2500 opened")), "markers stay: what happened to the port is never filtered away");
    print(kDop1); print(kDop2);
    settle(200);
    CHECK(w.receivedText().count(QLatin1String("@dop2")) == 3 && !w.receivedText().contains(QLatin1String("@dop1")),
          "and applies to what arrives after");

    w.setViewFilter(QStringLiteral("msg:/LINK\\s+FAIL/ OR field:LOCO_ID=2"));
    v = w.receivedText();
    CHECK(v.contains(QLatin1String("RAD LINK FAIL")), "a regex, as in the main window");
    CHECK(w.viewQueryError().isEmpty(), "the query parsed");

    w.setViewFilter(QStringLiteral("NOT @dop"));
    v = w.receivedText();
    CHECK(v.contains(QLatin1String("RAD LINK FAIL")) && !v.contains(QLatin1String("@dop")), "NOT works");

    // Text, not hex: the Hex box is remembered in the shared ini.
    w.findChild<QCheckBox *>(QStringLiteral("serialSendHex"))->setChecked(false);
    CHECK(w.sendText(QStringLiteral("STATUS?")), "send");
    w.setViewFilter(QStringLiteral("dir:out"));
    v = w.receivedText();
    CHECK(v.contains(QLatin1String("TX> STATUS?")) && !v.contains(QLatin1String("@dop")),
          "dir:out shows what was sent, as in the console");

    w.setViewFilter(QStringLiteral("(@dop2"));
    CHECK(!w.viewQueryError().isEmpty(), "a broken query says so");
    CHECK(w.receivedText().contains(QLatin1String("@dop1")) && w.receivedText().contains(QLatin1String("@dop2")),
          "…and filters nothing (rather than hiding everything)");
    w.setViewFilter(QString());

    // ---- Highlight ----------------------------------------------------------------------
    w.setViewHighlight(QStringLiteral("@dop1"));
    CHECK(w.highlightedLines() == 2, "both @dop1 lines highlighted");
    CHECK(w.receivedText().count(QStringLiteral("\u25B6 @dop1")) == 2, "with a \u25B6 as well as the tint");
    w.setViewFilter(QStringLiteral("@dop"));
    CHECK(w.highlightedLines() == 2 && w.receivedText().count(QLatin1String("@dop")) == 5,
          "Show only and Highlight together");
    w.setViewHighlight(QString());
    CHECK(w.highlightedLines() == 0, "cleared");

    // ---- the console is not filtered ---------------------------------------------------
    disp.drainNow();
    LogModel *m = disp.modelForKey(SerialManager::tabKeyFor(c.portName));
    CHECK(m && m->count() == 6, "the console tab got every line, whatever the terminal shows");

    // ---- hex view: the boxes do not apply -----------------------------------------------
    w.findChild<QCheckBox *>(QStringLiteral("serialHexView"))->setChecked(true);
    CHECK(!w.findChild<QueryLineEdit *>(QStringLiteral("serialFilter"))->isEnabled(),
          "in hex view Show only is off (hex rows are not lines)");
    w.closePort();
    ::close(master);
#endif
}

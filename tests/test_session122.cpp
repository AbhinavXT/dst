#include "testutil.h"
#include "layoutaudit.h"

#include "messagedispatcher.h"
#include "serialconsolewindow.h"
#include "serialmanager.h"
#include "uistyle.h"

#include <QLabel>
#include <QPushButton>
#include <QToolButton>

// =============================================================================
//  Session 122 — UI revamp, tool windows 1: the serial terminal.
//
//  The connection as one panel with Open as the one primary action, the
//  state as a chip (by tone, not by hand-picked colour), counters mono,
//  macros as chips — and nothing left out of a layout.
// =============================================================================

TEST_SUITE(session122)
{
    MessageDispatcher disp;
    SerialManager mgr(&disp);
    SerialConsoleWindow w(&mgr);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1100, 720);
    w.show();

    const QStringList orphans = LayoutAudit::orphans(&w);
    CHECK(orphans.isEmpty(), QByteArray("no visible widget outside every layout: ") + orphans.join(", ").toUtf8());

    QWidget *conn = w.findChild<QWidget *>(QStringLiteral("serialConnection"));
    QPushButton *open = w.findChild<QPushButton *>(QStringLiteral("serialOpen"));
    CHECK(conn && conn->property("dlRole").toString() == QLatin1String("panel") && open && conn->isAncestorOf(open),
          "the connection is one panel, Open inside it");
    CHECK(open && open->property("dlRole").toString() == QLatin1String("primary"), "Open is the primary action");

    QLabel *state = w.findChild<QLabel *>(QStringLiteral("serialState"));
    CHECK(state && state->property("dlRole").toString() == QLatin1String("chip")
              && state->styleSheet().isEmpty(),
          "the state is a chip coloured by its tone, with no colour of its own");
    CHECK(UiStyle::toneOf(state) == UiStyle::Tone::Neutral && state->text() == QLatin1String("Closed"),
          "closed: neutral");
    w.setConfigToUi([] { SerialConfig c; c.portName = QStringLiteral("/dev/no-such-port-122"); return c; }());
    w.openPort();
    CHECK(UiStyle::toneOf(state) == UiStyle::Tone::Fail, "a port that will not open: the fail tone");

    w.setMacros({ [] { SerialMacro m; m.label = QStringLiteral("Status"); m.text = QStringLiteral("STATUS?"); return m; }(),
                  [] { SerialMacro m; m.label = QStringLiteral("Reset"); m.text = QStringLiteral("RESET"); m.confirm = true; return m; }() });
    const QList<QToolButton *> macros = w.findChildren<QToolButton *>(QStringLiteral("serialMacro"));
    CHECK(macros.size() == 2 && macros[0]->property("dlRole").toString() == QLatin1String("chip")
              && UiStyle::toneOf(macros[1]) == UiStyle::Tone::Warn,
          "macros are chips; one that asks first is in the warn tone");
    CHECK(LayoutAudit::orphans(&w).isEmpty(), "still nothing outside a layout after the macro row is rebuilt");
    w.setMacros({});
}

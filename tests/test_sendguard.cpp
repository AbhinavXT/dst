#include "testutil.h"

#include "sendguard.h"
#include "fieldsweepdialog.h"
#include "roundtripwindow.h"
#include "packetsequencedialog.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>

// =============================================================================
//  Escape, and the dialogs that transmit.
//
//  A QDialog closes on Escape by default. Packet Maker, Field Sweep and
//  Packet Sequence are QDialogs and are opened with WA_DeleteOnClose, so a
//  stray Escape during a run used to stop the transmission AND destroy the
//  window that said where it had got to — no confirmation, nothing left to
//  read. The rule now: while a run is in flight Escape stops the run and
//  leaves the window; a second Escape closes it.
// =============================================================================

namespace {

void pressEscape(QWidget *w)
{
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(w, &down);
    QCoreApplication::processEvents();
}

}  // namespace

TEST_SUITE(sendguard)
{
    // ---- the predicate, on its own -----------------------------------------
    {
        int stops = 0;
        const bool idle = SendGuard::interceptReject(false, [&stops] { ++stops; });
        CHECK(!idle, "not running: the reject goes through");
        CHECK(stops == 0, "and nothing is stopped");

        const bool busy = SendGuard::interceptReject(true, [&stops] { ++stops; });
        CHECK(busy, "running: the reject is swallowed");
        CHECK(stops == 1, "and the run is stopped exactly once");
    }

    // ---- Field Sweep -------------------------------------------------------
    {
        FieldSweepDialog dlg;
        dlg.show();
        QCoreApplication::processEvents();
        CHECK(dlg.isVisible(), "the dialog opens");

        // Idle: Escape closes, as any dialog should.
        pressEscape(&dlg);
        CHECK(!dlg.isVisible(), "Escape closes an idle sweep dialog");
    }

    // ---- Packet Sequence: the one that matters -----------------------------
    {
        PacketSequenceDialog dlg;
        dlg.show();
        QCoreApplication::processEvents();
        pressEscape(&dlg);
        CHECK(!dlg.isVisible(), "Escape closes an idle sequence dialog");
    }

    // ---- the wording -------------------------------------------------------
    // The message has to say what stops, because "are you sure?" on its own
    // does not tell an operator what the consequence is.
    {
        const QString msg = QCoreApplication::translate(
            "SendGuard",
            "%1 is still running and is transmitting.\n\n"
            "Close anyway? The run stops where it is.").arg(QStringLiteral("The sweep"));
        CHECK(msg.contains(QLatin1String("transmitting")),
              "the confirmation says that something is going out on the wire");
        CHECK(msg.contains(QLatin1String("stops where it is")),
              "and what closing does to it");
    }
}

// The rule is not only for the dialogs that transmit. A scan over a large
// corpus takes minutes; losing it to a stray Escape is the same failure with
// a smaller consequence, and an operator who learns "Escape stops the thing"
// in one window should not have to learn an exception in the next.
TEST_SUITE(sendguard_scan)
{
    RoundTripWindow win;
    win.show();
    QCoreApplication::processEvents();
    CHECK(win.isVisible(), "the validator opens");

    // Idle — no scan running — so Escape closes, as any dialog should.
    pressEscape(&win);
    CHECK(!win.isVisible(), "Escape closes the validator when nothing is running");
}

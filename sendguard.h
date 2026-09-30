#ifndef SENDGUARD_H
#define SENDGUARD_H
// =====================================================================
//  sendguard.h -- Escape and the close box, for the dialogs that
//  transmit to live equipment.
//
//  THE PROBLEM
//    Packet Maker, Field Sweep and Packet Sequence are QDialogs, and a
//    QDialog closes on Escape by default: QDialog::reject() hides it,
//    WA_DeleteOnClose destroys it, and the UdpSender dies with it. So a
//    stray Escape during a run stopped the transmission and threw away
//    the window that said where it had got to — no confirmation, no
//    record. For a sweep that is annoying; for a packet sequence it
//    means the equipment saw a partial sequence and the operator has
//    nothing left on screen to say which step it stopped at.
//
//  THE RULE
//    While a run is in flight, Escape stops the run and LEAVES THE
//    WINDOW OPEN. One keypress does one thing, and the operator can
//    still read the result. A second Escape then closes it, because by
//    then nothing is running. Closing by the window's close box asks
//    first, since that is a deliberate act with a consequence the
//    operator may not have in mind.
//
//  This is a header of two small functions rather than a base class:
//    the three dialogs have nothing else in common, and giving them a
//    shared parent to share four lines would tie them together for no
//    other reason.
// =====================================================================
#include <QCoreApplication>
#include <QMessageBox>
#include <QString>
#include <QWidget>

namespace SendGuard {

// Escape (or any other reject) while `running`. Returns true when the
// caller should swallow it — the run has been stopped and the window
// stays. `stop` is the dialog's own stop path, so the UI ends up in the
// same state as pressing its Stop button.
template <typename StopFn>
bool interceptReject(bool running, StopFn stop)
{
    if (!running) { return false; }
    stop();
    return true;
}

// The close box while a run is in flight. True means go ahead and close.
inline bool confirmClose(QWidget *parent, const QString &what)
{
    const QString title = parent ? parent->windowTitle()
                                 : QCoreApplication::applicationName();
    return QMessageBox::question(
               parent, title,
               QCoreApplication::translate(
                   "SendGuard",
                   "%1 is still running and is transmitting.\n\n"
                   "Close anyway? The run stops where it is.").arg(what),
               QMessageBox::Yes | QMessageBox::No,
               QMessageBox::No) == QMessageBox::Yes;
}

}  // namespace SendGuard

#endif  // SENDGUARD_H

#include "windowgeometry.h"

#include "settings.h"

#include <QApplication>
#include <QByteArray>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QWidget>

namespace {

QString settingKey(const QString &key)
{
    return QStringLiteral("windows/%1_geometry").arg(key);
}

// Is any part of `frame` on a screen that currently exists?
//
// Intersection rather than containment: a window straddling two monitors,
// or hanging slightly off the bottom edge, is perfectly usable and
// insisting it fit entirely within one screen would reject arrangements the
// user deliberately chose.
bool isOnAnyScreen(const QRect &frame)
{
    const auto screens = QApplication::screens();
    for (const QScreen *s : screens) {
        // availableGeometry, not geometry: the taskbar area is not somewhere
        // a title bar can be grabbed, so a window restored entirely behind
        // it would be as unreachable as one off-screen.
        if (s->availableGeometry().intersects(frame)) {
            // Require a usable strip of the window to be visible, not a
            // single pixel — a window with two pixels showing is
            // technically on-screen and practically lost.
            const QRect visible = s->availableGeometry().intersected(frame);
            if (visible.width() >= 120 && visible.height() >= 60) return true;
        }
    }
    return false;
}

}  // namespace

bool WindowGeometry::restore(QWidget *w, const QString &key)
{
    if (!w || key.isEmpty()) return false;

    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    const QByteArray blob = s.value(settingKey(key)).toByteArray();
    if (blob.isEmpty()) return false;

    // Apply, then check where it landed. restoreGeometry understands
    // multi-monitor layouts and DPI in ways that reimplementing the
    // decode would not, so it is easier to let it act and then verify
    // than to parse the blob ourselves.
    const QRect before = w->frameGeometry();
    if (!w->restoreGeometry(blob)) return false;

    if (!isOnAnyScreen(w->frameGeometry())) {
        // The monitor it was on is gone. Put it back where it was and let
        // the window manager place it, rather than opening something the
        // user cannot see or reach.
        w->setGeometry(before);
        return false;
    }
    return true;
}

void WindowGeometry::save(const QWidget *w, const QString &key)
{
    if (!w || key.isEmpty()) return;

    // Don't record a minimised or maximised window's transient geometry as
    // if it were a chosen size; saveGeometry stores the window STATE too,
    // so a maximised window restores maximised, which is correct — but a
    // minimised one would restore minimised, which is not what anyone
    // means by "reopen it".
    if (w->isMinimized()) return;

    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.setValue(settingKey(key), w->saveGeometry());
}

void WindowGeometry::makeResizableWindow(QWidget *w)
{
    if (!w) { return; }

    // The window TYPE has to be replaced, not OR-ed into.
    //
    // Qt::Dialog is Qt::Window | 0x2, so `flags | Qt::Window` leaves a
    // QDialog still typed as a dialog — and a dialog is a transient window,
    // which most window managers refuse to maximise however many hints it
    // carries. That is exactly what was seen: maximise worked in the Decode
    // Workbench and Frame Diff (QMainWindow, already plain windows) and did
    // nothing in the Packet Maker (QDialog). Masking the type bits out first
    // is the difference between the button being there and the button doing
    // something.
    Qt::WindowFlags f = w->windowFlags();
    f &= ~Qt::WindowType_Mask;
    f |= Qt::Window
       | Qt::WindowMinMaxButtonsHint
       | Qt::WindowCloseButtonHint
       | Qt::WindowSystemMenuHint;
    w->setWindowFlags(f);
}

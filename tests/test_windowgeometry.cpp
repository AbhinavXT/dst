#include "testutil.h"
#include "windowgeometry.h"
#include "settings.h"

#include <QApplication>
#include <QScreen>
#include <QSettings>
#include <QWidget>

// Window geometry persistence. The critical behaviour is the off-screen
// guard: restoring a window onto a monitor that no longer exists makes it
// invisible and unreachable, with nothing to indicate it opened at all.
// That is strictly worse than forgetting the position.
TEST_SUITE(windowgeometry)
{
    const QRect avail = QApplication::primaryScreen()->availableGeometry();

    // ---- round trip -------------------------------------------------------
    {
        QWidget w;
        w.setGeometry(avail.x() + 40, avail.y() + 40, 500, 300);
        WindowGeometry::save(&w, "testWin");

        QWidget w2;
        w2.resize(100, 100);
        CHECK(WindowGeometry::restore(&w2, "testWin"),
              "a saved on-screen geometry is restored");
        CHECK(w2.size() == QSize(500, 300), "size restored");
    }

    // ---- no stored value leaves the default alone ------------------------
    {
        QWidget w;
        w.resize(860, 400);
        CHECK(!WindowGeometry::restore(&w, "neverSavedKey"),
              "unknown key restores nothing");
        CHECK(w.size() == QSize(860, 400),
              "and the caller's default size survives untouched");
    }

    // ---- empty key / null widget are refused, not crashed on -------------
    {
        QWidget w;
        CHECK(!WindowGeometry::restore(&w, ""),   "empty key restores nothing");
        CHECK(!WindowGeometry::restore(nullptr, "x"), "null widget is safe");
        WindowGeometry::save(nullptr, "x");       // must not crash
        WindowGeometry::save(&w, "");
        CHECK(true, "null/empty saves are no-ops");
    }

    // ---- off-screen geometry ---------------------------------------------
    // Forge a geometry far outside any real screen — what a saved position
    // from an unplugged second monitor looks like.
    //
    // NOTE: Qt 5.15's own restoreGeometry() already clamps such a window
    // back onto an existing screen, so our guard is usually redundant. It
    // is kept as defence in depth: the clamping is Qt-version and platform
    // specific, and the consequence of it not happening — a window that
    // opens invisibly with no indication it opened at all — is bad enough
    // to be worth a second check that costs one rectangle intersection.
    //
    // So the invariant asserted here is the one that actually matters, and
    // it holds whichever layer enforces it: after restore, the window is
    // reachable.
    {
        QWidget probe;
        probe.setGeometry(50000, 50000, 400, 300);
        QByteArray offscreen = probe.saveGeometry();
        QSettings s(Settings::iniPath(), QSettings::IniFormat);
        s.setValue("windows/ghostWin_geometry", offscreen);
        s.sync();

        QWidget w;
        w.setGeometry(avail.x() + 10, avail.y() + 10, 640, 480);
        WindowGeometry::restore(&w, "ghostWin");

        // Whatever happened, the window must not be stranded off-screen.
        bool reachable = false;
        for (const QScreen *sc : QApplication::screens()) {
            const QRect vis = sc->availableGeometry().intersected(w.frameGeometry());
            if (vis.width() >= 120 && vis.height() >= 60) reachable = true;
        }
        CHECK(reachable,
              "after restoring an off-screen blob the window is still reachable");
    }

    // ---- garbage blob is refused -----------------------------------------
    {
        QSettings s(Settings::iniPath(), QSettings::IniFormat);
        s.setValue("windows/junkWin_geometry", QByteArray("not a geometry"));
        s.sync();
        QWidget w;
        w.resize(300, 200);
        CHECK(!WindowGeometry::restore(&w, "junkWin"), "garbage blob refused");
        CHECK(w.size() == QSize(300, 200), "default size survives");
    }

    // ---- a minimised window's geometry is not recorded --------------------
    // Restoring "minimised" is not what anyone means by reopening a window.
    {
        QWidget w;
        w.setGeometry(avail.x() + 20, avail.y() + 20, 700, 400);
        WindowGeometry::save(&w, "minWin");
        const QByteArray good =
            QSettings(Settings::iniPath(), QSettings::IniFormat)
                .value("windows/minWin_geometry").toByteArray();

        w.showMinimized();
        if (w.isMinimized()) {
            WindowGeometry::save(&w, "minWin");
            const QByteArray after =
                QSettings(Settings::iniPath(), QSettings::IniFormat)
                    .value("windows/minWin_geometry").toByteArray();
            CHECK(after == good, "minimised state does not overwrite the saved size");
        } else {
            CHECK(true, "platform did not minimise; guard untested here");
        }
    }

    // ---- keys are independent --------------------------------------------
    // Both sizes fit the offscreen screen (800 x 600 on Qt 5, 800 x 800 on
    // Qt 6): Qt 6's restoreGeometry shrinks a window to its screen, so a
    // 900-wide winB came back 798 wide there (session 127).
    {
        QWidget a, b;
        a.setGeometry(avail.x() + 5, avail.y() + 5, 400, 300);
        b.setGeometry(avail.x() + 5, avail.y() + 5, 600, 450);
        WindowGeometry::save(&a, "winA");
        WindowGeometry::save(&b, "winB");
        QWidget ra, rb;
        WindowGeometry::restore(&ra, "winA");
        WindowGeometry::restore(&rb, "winB");
        CHECK(ra.size() == QSize(400, 300), "winA restored its own size");
        CHECK(rb.size() == QSize(600, 450), "winB restored its own size");
        CHECK(ra.size() != rb.size(), "the two keys do not collide");
    }

    // clean up
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    for (const char *k : { "testWin", "ghostWin", "junkWin", "minWin",
                           "winA", "winB" }) {
        s.remove(QStringLiteral("windows/%1_geometry").arg(k));
    }
}

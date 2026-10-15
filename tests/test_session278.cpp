#include "testutil.h"

#include "dmipanel.h"
#include "dmitimetravel.h"
#include "messagedispatcher.h"
#include "settings.h"

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

// =============================================================================
//  Session 278 — the live DMI window showed nothing. Abhinav: "live dmi not
//  showing anything" (DMI window, live UDP). Two ways it stayed blank:
//  - Follow cursor is remembered, and "Show on DMI" turns it on: the live
//    menu item then opened a window that ignored live frames;
//  - a payload starting with CR/LF reaches the entry as leading spaces, and
//    the live hook wanted the text to start with "@dmi_" exactly.
//  Real frames: replay/loco_1_1_29062026_134509.cap.
// =============================================================================

namespace {
QStringList dmiLines(int n)
{
    QStringList out;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_29062026_134509.cap"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd() && out.size() < n) {
        const QString l = QString::fromLatin1(f.readLine()).trimmed();
        if (l.startsWith(QLatin1String("@dmi_"))) out << l;
    }
    return out;
}

void feed(MessageDispatcher &disp, const QString &payload)
{
    disp.ingestLocal(1, 1, payload.toLatin1(), QDateTime::currentMSecsSinceEpoch(), QString());
    disp.drainNow();
    QApplication::processEvents();
}

bool live(const DmiWindow &w) { return w.statusText().contains(QStringLiteral("last @dmi")); }
}  // namespace

TEST_SUITE(session278)
{
    const QStringList lines = dmiLines(3);
    CHECK(lines.size() == 3, "fixture: three real @dmi frames");
    if (lines.size() < 3) return;

    const QByteArray savedPkg = qgetenv("DLCONSOLE_PACKAGE");
    QTemporaryDir where;
    const QString exe = where.filePath(QStringLiteral("DLConsole.exe"));
    { QFile f(exe); CHECK(f.open(QIODevice::WriteOnly), "a stand-in for the .exe"); }
    qputenv("DLCONSOLE_PACKAGE", exe.toLocal8Bit());

    // ---- a payload that starts with a line break ------------------------------------------
    {
        MessageDispatcher disp;
        DmiWindow *w = DmiWindow::openLive(nullptr, &disp);
        w->setAttribute(Qt::WA_DeleteOnClose, false);
        feed(disp, QStringLiteral("\r\n") + lines.at(0));
        CHECK(live(*w), QByteArray("a frame whose payload starts with CR/LF is drawn (") + w->statusText().toUtf8() + ")");
        CHECK(w->selectedSource() == QLatin1String("1_1"), "on loco 1_1");
        feed(disp, lines.at(1));
        CHECK(live(*w), "and a plain one, as before");
        delete w;
    }

    // ---- "Follow cursor" remembered from "Show on DMI" -------------------------------------
    {
        MessageDispatcher disp;
        DmiWindow *following = DmiWindow::showFollowing(nullptr, &disp);
        following->setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(following->followCursor(), "Show on DMI follows the cursor");
        {
            QSettings s(Settings::iniPath(), QSettings::IniFormat);
            CHECK(s.value(QStringLiteral("dmi/followCursor")).toBool(), "fixture: and that is remembered");
        }
        delete following;

        DmiWindow *w = DmiWindow::openLive(nullptr, &disp);
        w->setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(!w->followCursor(), "the live menu item opens live all the same");
        feed(disp, lines.at(2));
        CHECK(live(*w), QByteArray("and draws the live frame (") + w->statusText().toUtf8() + ")");
        delete w;
    }

    if (savedPkg.isNull()) qunsetenv("DLCONSOLE_PACKAGE");
    else qputenv("DLCONSOLE_PACKAGE", savedPkg);
}

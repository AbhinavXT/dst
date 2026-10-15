#include "testutil.h"

#include "flasherwindow.h"
#include "settings.h"

#include <QAbstractButton>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

// =============================================================================
//  Session 280 — Abhinav: "remove engineer and operator from flash window".
//  He chose: always Engineer. The toggle is gone, and a flasher/mode of
//  "operator" saved by an older build no longer puts the flasher in
//  Operator mode with no way back.
// =============================================================================

TEST_SUITE(session280)
{
    const QByteArray savedPkg = qgetenv("DLCONSOLE_PACKAGE");
    QTemporaryDir where, data;
    const QString exe = where.filePath(QStringLiteral("DLConsole.exe"));
    { QFile f(exe); CHECK(f.open(QIODevice::WriteOnly), "a stand-in for the .exe"); }
    qputenv("DLCONSOLE_PACKAGE", exe.toLocal8Bit());
    {
        QSettings s(Settings::iniPath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("flasher/mode"), QStringLiteral("operator"));   // an older build's choice
    }

    {
        FlasherWindow w(nullptr, data.path());
        w.show();
        CHECK(w.mode() == Flasher::Mode::Engineer, "Engineer, whatever an older build saved");
        QStringList found;
        for (QAbstractButton *b : w.findChildren<QAbstractButton *>()) {
            const QString t = b->text().remove(QLatin1Char('&'));
            if (t == QLatin1String("Engineer") || t == QLatin1String("Operator")) found << t;
        }
        CHECK(found.isEmpty(), QByteArray("no Engineer / Operator button (") + found.join(QStringLiteral(", ")).toUtf8() + ")");
    }

    if (savedPkg.isNull()) qunsetenv("DLCONSOLE_PACKAGE");
    else qputenv("DLCONSOLE_PACKAGE", savedPkg);
}

// Throwaway: render the main window to PNG in every theme so the look can be
// looked at. Not part of the suite.
//
// Session 118: with traffic. Real capture lines from replay/ are fed in as
// three sources, so the shot shows the window as it is used, not empty.
// Output: $SHOT_DIR (default /tmp)/shot_<theme>.png; SHOT_THEMES=dark,light
// limits which themes are drawn.
#include <QApplication>
#include <QFile>
#include <QPixmap>
#include <QTableView>
#include <QTimer>
#include "mainwindow.h"
#include "messagedispatcher.h"
#include "theme.h"
#include "uistyle.h"
#include "settings.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString dir = qEnvironmentVariableIsEmpty("SHOT_DIR") ? QStringLiteral("/tmp")
                                                                : qEnvironmentVariable("SHOT_DIR");
    const QStringList only = qEnvironmentVariable("SHOT_THEMES").split(QLatin1Char(','), Qt::SkipEmptyParts);

    QStringList lines;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    if (f.open(QIODevice::ReadOnly)) {
        while (!f.atEnd() && lines.size() < 400) {
            const QString l = QString::fromUtf8(f.readLine()).trimmed();
            if (l.startsWith(QLatin1Char('@'))) lines << l;
        }
    }

    for (Theme t : ThemeUtil::all()) {
        if (!only.isEmpty() && !only.contains(ThemeUtil::toString(t))) continue;
        Settings::setTheme(ThemeUtil::toString(t));
        ThemeUtil::apply(t);
        UiStyle::apply();
        MainWindow w;
        w.resize(1440, 900);
        w.show();
        if (auto *disp = w.findChild<MessageDispatcher *>()) {
            qint64 ms = 1782558147000LL;
            for (int i = 0; i < lines.size(); ++i) {
                const quint8 src = (i % 5 == 0) ? 81 : 21;
                const quint16 kv = (i % 7 == 0) ? 2 : 1;
                disp->ingestLocal(src, kv, lines.at(i).toUtf8(), ms + i * 137, QString());
            }
            disp->drainNow();
        }
        for (int i = 0; i < 60; ++i) { QCoreApplication::processEvents(); }
        if (auto *view = w.findChild<QTableView *>()) view->selectRow(3);
        for (int i = 0; i < 20; ++i) { QCoreApplication::processEvents(); }
        w.grab().save(QStringLiteral("%1/shot_%2.png").arg(dir, ThemeUtil::toString(t)));
    }
    return 0;
}

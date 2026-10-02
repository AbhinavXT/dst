// Throwaway: render the main window to PNG in every theme so the look can be
// looked at. Not part of the suite.
//
// Session 118: with traffic. Real capture lines from replay/ are fed in as
// three sources, so the shot shows the window as it is used, not empty.
// Output: $SHOT_DIR (default /tmp)/shot_<theme>.png; SHOT_THEMES=dark,light
// limits which themes are drawn.
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QPixmap>
#include <QTableView>
#include <QTimer>
#include "mainwindow.h"
#include "messagedispatcher.h"
#ifdef DL_HAVE_SERIAL
#include "serialconsolewindow.h"
#include "serialmanager.h"
#endif
#include <QPlainTextEdit>
#include "searchwindow.h"
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

    // SHOT_WINDOW=serial: a tool window instead of the main one.
    const QString which = qEnvironmentVariable("SHOT_WINDOW");
    if (!which.isEmpty()) {
        for (Theme t : ThemeUtil::all()) {
            if (!only.isEmpty() && !only.contains(ThemeUtil::toString(t))) continue;
            Settings::setTheme(ThemeUtil::toString(t));
            ThemeUtil::apply(t);
            UiStyle::apply();
            MessageDispatcher disp;
            QWidget *win = nullptr;
#ifdef DL_HAVE_SERIAL
            SerialManager mgr(&disp);
            if (which == QLatin1String("serial")) {
                auto *w = new SerialConsoleWindow(&mgr);
                for (const QString &l : lines.mid(0, 40)) w->findChild<QPlainTextEdit *>(QStringLiteral("serialView"))->appendPlainText(l);
                win = w;
            }
#endif
            if (which == QLatin1String("search")) {
                qint64 ms = 1782558147000LL;
                for (int i = 0; i < lines.size(); ++i)
                    disp.ingestLocal(i % 3 ? 21 : 81, 1, lines.at(i).toUtf8(), ms + i * 137, QString());
                disp.drainNow();
                auto *w = new SearchWindow(&disp, nullptr);
                w->setQueryText(QStringLiteral("@lsrp OR @dmi"), true);
                win = w;
            }
            if (!win) return 2;
            win->resize(1100, 720);
            win->show();
            QElapsedTimer settle;
            settle.start();
            while (settle.elapsed() < 800) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            win->grab().save(QStringLiteral("%1/shot_%2_%3.png").arg(dir, which, ThemeUtil::toString(t)));
            delete win;
        }
        return 0;
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
        // Let the window run a few status ticks (rates, health, the header)
        // before the picture: what a user sees after a moment, not at t=0.
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 4000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);   // past the lanes' 3 s debounce
        // The visible log table's row 8 (an @lsrp line in this capture): the
        // inspector follows the current row.
        for (QTableView *view : w.findChildren<QTableView *>()) {
            if (!view->isVisible() || !view->model() || view->model()->rowCount() < 10) continue;
            for (int r = 0; r < view->model()->rowCount(); ++r) {
                if (view->model()->index(r, 5).data().toString().startsWith(QLatin1String("@lsrp"))) {
                    view->setCurrentIndex(view->model()->index(r, 0));
                    view->selectRow(r);
                    break;
                }
            }
            break;
        }
        for (int i = 0; i < 20; ++i) { QCoreApplication::processEvents(); }
        w.grab().save(QStringLiteral("%1/shot_%2.png").arg(dir, ThemeUtil::toString(t)));
    }
    return 0;
}

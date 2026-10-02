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
#include <QScrollBar>
#include <QTimer>
#include "mainwindow.h"
#include "messagedispatcher.h"
#ifdef DL_HAVE_SERIAL
#include "serialconsolewindow.h"
#include "serialmanager.h"
#endif
#include <QPlainTextEdit>
#include "searchwindow.h"
#include "lococonsolewindow.h"
#include "dmipanel.h"
#include <QGroupBox>
#include <QTabWidget>
#include <QSplitter>
#include <cstdio>
#include "packetmakerdialog.h"
#include "comparewindow.h"
#include "mergedwindow.h"
#include "trackdiagramwindow.h"
#include "twolocowindow.h"
#include "incidentreportwindow.h"
#include "incidentreportdialog.h"
#include "flasherwindow.h"
#include "flasherqueuepage.h"
#include "flashercore.h"
#include "layoutaudit.h"
#include "lococonfigwindow.h"
#include "replaywindow.h"
#include "decodeworkbench.h"
#include "framediffwindow.h"
#include "subpacketwindow.h"
#include "archivesearchwindow.h"
#include "colorrules.h"
#include "sessionfile.h"
#include "querylineedit.h"
#include <QPushButton>
#include "packetbuilder.h"
#include <QDir>
#include <QTemporaryDir>
#include <QDateTime>
#include "namemap.h"
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
            if (which == QLatin1String("loco") || which == QLatin1String("dmi")) {
                // Window first: these pick up locos from traffic as it arrives.
                if (which == QLatin1String("loco")) win = new LocoConsoleWindow(&disp, nullptr);
                else win = new DmiWindow(&disp, nullptr);
                win->resize(1100, 720);
                win->show();
                qint64 ms = 1782558147000LL;
                for (int i = 0; i < lines.size(); ++i) {
                    disp.ingestLocal(21, 1, lines.at(i).toUtf8(), ms + i * 137, QString());
                    if (i % 50 == 49) { disp.drainNow(); QCoreApplication::processEvents(); }
                }
                disp.drainNow();
            }
            if (which == QLatin1String("packet")) win = new PacketMakerDialog(nullptr);
            if (which == QLatin1String("compare")) {
                // Two locos' worth of real traffic, so both panes have a tab.
                qint64 ms = 1782558147000LL;
                for (int i = 0; i < lines.size(); ++i)
                    disp.ingestLocal(i % 2 ? 21 : 81, 1, lines.at(i).toUtf8(), ms + i * 137, QString());
                disp.drainNow();
                win = new CompareWindow(&disp, nullptr);
            }
            NameMap names;
            if (which == QLatin1String("merged")) {
                // Three sources, interleaved: what the window exists for.
                qint64 ms = 1782558147000LL;
                for (int i = 0; i < lines.size(); ++i)
                    disp.ingestLocal(i % 3 == 0 ? 81 : (i % 3 == 1 ? 21 : 22), 1, lines.at(i).toUtf8(), ms + i * 137, QString());
                disp.drainNow();
                auto *w = new MergedWindow(&disp, &names, t, 5000, nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("track")) {
                // A run with RFID tags, signals and location: the whole of
                // one real capture, at its own timestamps.
                // SHOT_CAP=29062026_134509 picks another replay/loco_1_1_ capture.
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("26062026_162418")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_%1.cap").arg(cap));
                if (run.open(QIODevice::ReadOnly)) {
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
                        disp.ingestLocal(21, 1, l, ms, QString());
                    }
                }
                disp.drainNow();
                const QString key = disp.knownKeys().value(0);
                auto *w = new TrackDiagramWindow(disp.modelForKey(key), key, QStringLiteral("L1_V1"));
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("twoloco")) {
                // Two real locos recorded at the same time (SHOT_CAP picks
                // the date_time; 27062026_151052 has a rear-end episode).
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("27062026_151052")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                for (int loco = 1; loco <= 2; ++loco) {
                    QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_%1_1_%2.cap").arg(loco).arg(cap));
                    if (!run.open(QIODevice::ReadOnly)) continue;
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
                        disp.ingestLocal(loco == 1 ? 21 : 22, 1, l, ms, QString());
                    }
                }
                disp.drainNow();
                auto *w = new TwoLocoWindow(&disp);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("incident") || which == QLatin1String("incidentdlg")) {
                // One real run, around its rear-end moment (15:13:10).
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("27062026_151052")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_%1.cap").arg(cap));
                qint64 lo = 0, hi = 0;
                if (run.open(QIODevice::ReadOnly)) {
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
                        if (!lo) lo = ms;
                        hi = ms;
                        disp.ingestLocal(21, 1, l, ms, QString());
                    }
                }
                disp.drainNow();
                const qint64 at = qEnvironmentVariableIsEmpty("SHOT_AT")
                    ? QDateTime::fromString(QStringLiteral("2026-06-27T15:13:10"), Qt::ISODate).toMSecsSinceEpoch()
                    : QDateTime::fromString(qEnvironmentVariable("SHOT_AT"), Qt::ISODate).toMSecsSinceEpoch();
                if (which == QLatin1String("incidentdlg")) {
                    win = new IncidentReportDialog(at, lo, hi);
                } else {
                    auto *w = new IncidentReportWindow(disp.modelForKey(QStringLiteral("21_1")), QStringLiteral("21_1"),
                                                       QStringLiteral("L1_V1"), at, IncidentReport::Options());
                    w->setAttribute(Qt::WA_DeleteOnClose, false);
                    win = w;
                }
            }
            QTemporaryDir flashDir;
            if (which.startsWith(QLatin1String("flasher"))) {
                // Three images (arbitrary bytes: the updater does not parse
                // them) through a profile, as test_flasher does; the VCC
                // address is one nothing answers on, so a run sits on the
                // power-cycle prompt and then gives up.
                auto writeImg = [&](const QString &name, int size) {
                    QFile out(QDir(flashDir.path()).filePath(name));
                    out.open(QIODevice::WriteOnly);
                    QByteArray b(size, '\0');
                    for (int i = 0; i < size; ++i) b[i] = char((i * 31 + 7) & 0xFF);
                    out.write(b);
                    return out.fileName();
                };
                Flasher::ProfileStore store(QDir(flashDir.path()).filePath(QStringLiteral("flasher_profiles.json")));
                Flasher::FlashProfile profile;
                profile.name = QStringLiteral("Bench 2");
                profile.vccIp = QStringLiteral("127.0.0.1");
                profile.port = 9;   // discard: nothing answers
                profile.defaultImages.insert(Flasher::CardInput, writeImg(QStringLiteral("KAVACH_Input_Card_v5.appimage"), 40 * 1450));
                profile.defaultImages.insert(Flasher::CardOutput, writeImg(QStringLiteral("KAVACH_Output_Card_v5.appimage"), 25 * 1450));
                profile.defaultImages.insert(Flasher::CardVcc, writeImg(QStringLiteral("LKAVACH_v1.2.9.appimage"), 30 * 1450));
                profile.updaterWaitSeconds = Flasher::kMinUpdaterWaitSeconds;
                store.upsert(profile, store.names().first());
                store.setActiveName(profile.name);
                store.save();
                auto *w = new FlasherWindow(nullptr, flashDir.path());
                w->queuePage()->selectCard(Flasher::CardInput);
                if (which != QLatin1String("flasher")) {
                    w->resize(1100, 720);
                    w->show();
                    w->startBatch(false);
                    QElapsedTimer t;
                    t.start();
                    const qint64 until = which == QLatin1String("flasherrun") ? 2500 : 60000;
                    while (t.elapsed() < until && !(which == QLatin1String("flashersum")
                                                    && w->currentPage() == FlasherWindow::SummaryPageIndex))
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                }
                win = w;
            }
            if (which == QLatin1String("lococonfig")) {
                auto *w = new LocoConfigWindow(nullptr, flashDir.path());
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("replay")) {
                // Both locos of one real run (SHOT_CAP picks the run).
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("27062026_151052")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                auto *w = new ReplayWindow(QStringList{
                    QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_%1.cap").arg(cap),
                    QStringLiteral(DL_SRC_DIR "/replay/loco_2_1_%1.cap").arg(cap) });
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("workbench")) {
                auto *w = new DecodeWorkbench(nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                // A real @lsrp line from the capture.
                for (const QString &l : lines)
                    if (l.startsWith(QLatin1String("@lsrp"))) { w->loadBuffer(l); break; }
                win = w;
            }
            if (which == QLatin1String("framediff")) {
                auto *w = new FrameDiffWindow(nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                int side = 0;
                for (const QString &l : lines)
                    if (l.startsWith(QLatin1String("@lsrp")) && side < 3) w->setSide(side++, l);
                win = w;
            }
            static PacketBuilder subBuilder;
            static QVector<Schema::SubEntry> subs;
            if (which == QLatin1String("subpacket")) {
                // SLRP's sub-packet SHOT_SUB (default 0), as Packet Maker opens it.
                subs.clear();
                Schema::SubEntry e;
                e.type = qEnvironmentVariableIsEmpty("SHOT_SUB") ? 0 : qEnvironmentVariableIntValue("SHOT_SUB");
                subs.push_back(e);
                auto *w = new SubPacketWindow(nullptr);
                w->setTarget(&subBuilder.encoder(), QStringLiteral("slrp"), &subs, 0);
                win = w;
            }
            static ColorRules archiveRules;
            if (which == QLatin1String("archive")) {
                // Real capture lines as the records of a .dlr dated today,
                // under a temporary disk-log root; then a search for @lsrp.
                const QString day = QDir(flashDir.path()).filePath(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
                QDir().mkpath(day);
                QFile dlr(QDir(day).filePath(QStringLiteral("21_1.dlr")));
                if (dlr.open(QIODevice::WriteOnly)) {
                    const qint64 start = QDateTime(QDate::currentDate(), QTime(14, 2, 26)).toMSecsSinceEpoch();
                    SessionFile::FileHeader h;
                    h.createdMs = start; h.sourceId = 21; h.kvchId = 1;
                    dlr.write(SessionFile::encodeHeader(h));
                    for (int i = 0; i < lines.size(); ++i) {
                        const QByteArray body = lines.at(i).toUtf8();
                        QByteArray wire;
                        wire.append(char(21)); wire.append(char(101)); wire.append(char(7));
                        wire.append(char(body.size() & 0xFF)); wire.append(char(body.size() >> 8));
                        wire.append(char(1)); wire.append(char(0));
                        wire.append(body);
                        dlr.write(SessionFile::encodeRecord(start + i * 137, wire));
                    }
                    dlr.close();
                }
                const QString savedRoot = Settings::diskLogRoot();
                Settings::setDiskLogRoot(flashDir.path());
                auto *w = new ArchiveSearchWindow(&archiveRules, &names, nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                w->resize(1100, 720);
                w->show();
                if (auto *q = w->findChild<QueryLineEdit *>()) q->setText(QStringLiteral("@lsrp"));
                for (QPushButton *b : w->findChildren<QPushButton *>())
                    if (b->text() == QLatin1String("Search")) b->click();
                QElapsedTimer t;
                t.start();
                while (t.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                Settings::setDiskLogRoot(savedRoot);
                win = w;
            }
            if (!win) return 2;
            win->resize(1100, 720);
            win->show();
            QElapsedTimer settle;
            settle.start();
            while (settle.elapsed() < 800) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            win->grab().save(QStringLiteral("%1/shot_%2_%3.png").arg(dir, which, ThemeUtil::toString(t)));
            // SHOT_DUMP=1: every visible group box, tab widget and splitter
            // with its geometry and minimum hint, to see where height goes.
            if (qEnvironmentVariableIsSet("SHOT_DUMP")) {
                printf("window %dx%d  minHint %dx%d\n", win->width(), win->height(),
                       win->minimumSizeHint().width(), win->minimumSizeHint().height());
                for (const QString &o : LayoutAudit::orphans(win)) printf("  loose: %s\n", qPrintable(o));
                for (QTableView *tv : win->findChildren<QTableView *>()) {
                    if (!tv->isVisible()) continue;
                    QString cols;
                    for (int c = 0; c < tv->model()->columnCount(); ++c)
                        cols += QString::number(tv->isColumnHidden(c) ? 0 : tv->columnWidth(c)) + QLatin1Char(' ');
                    printf("  table %s %dx%d viewport %dx%d rows %d rowH %d hbar %d cols %s\n", qPrintable(tv->objectName()),
                           tv->width(), tv->height(), tv->viewport()->width(), tv->viewport()->height(),
                           tv->model()->rowCount(), tv->rowHeight(0), tv->horizontalScrollBar()->isVisible(), qPrintable(cols));
                }
                if (qEnvironmentVariableIsSet("SHOT_DEEP")) {
                    // Every visible widget whose minimum is at least SHOT_DEEP px wide or tall.
                    const int big = qEnvironmentVariableIntValue("SHOT_DEEP");
                    for (QWidget *c : win->findChildren<QWidget *>()) {
                        if (!c->isVisible()) continue;
                        const QSize m = c->minimumSizeHint().expandedTo(c->minimumSize());
                        if (m.width() < big && m.height() < big) continue;
                        printf("  %-28s %-24s min %4dx%-4d\n", c->metaObject()->className(),
                               qPrintable(c->objectName().left(24)), m.width(), m.height());
                    }
                }
                for (QWidget *c : win->findChildren<QWidget *>()) {
                    if (!c->isVisible()) continue;
                    const char *k = c->metaObject()->className();
                    if (!qobject_cast<QGroupBox *>(c) && !qobject_cast<QTabWidget *>(c)
                        && !qobject_cast<QSplitter *>(c)) continue;
                    const QRect g(c->mapTo(win, QPoint(0, 0)), c->size());
                    const QString title = qobject_cast<QGroupBox *>(c) ? qobject_cast<QGroupBox *>(c)->title() : QString();
                    printf("  %-12s y=%4d h=%4d minH=%4d  %s\n", k, g.y(), g.height(),
                           c->minimumSizeHint().height(), qPrintable(title.left(30)));
                }
            }
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

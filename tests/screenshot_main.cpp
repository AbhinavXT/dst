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
#include <QHeaderView>
#include <QTableWidget>
#include <QScrollBar>
#include <QTimer>
#include <QMainWindow>
#include <QLayout>
#include "mainwindow.h"
#include "shortcutsdialog.h"
#include <memory>
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
#include <QAbstractButton>
#include "findbar.h"
#include "tablefindbar.h"
#include "testrun.h"

// SHOT_CLIP=1: every visible button drawn narrower or shorter than its own
// size hint (padding + border + label/icon): its label or glyph is clipped.
static void clipAudit(QWidget *root, const QString &where)
{
    if (!qEnvironmentVariableIsSet("SHOT_CLIP")) return;
    struct Flush { ~Flush() { fflush(stdout); } } flush;
    for (QAbstractButton *b : root->findChildren<QAbstractButton *>()) {
        if (!b->isVisible() || b->inherits("QCheckBox") || b->inherits("QRadioButton")) continue;
        if (b->text().isEmpty() && b->icon().isNull() && !b->inherits("QToolButton")) continue;
        const QSize h = b->sizeHint();
        if (b->width() + 1 < h.width() || b->height() + 1 < h.height())
            printf("  CLIP %s: %s %s '%s' tip='%s' is %dx%d, needs %dx%d\n", qPrintable(where), b->metaObject()->className(),
                   qPrintable(b->objectName()), qPrintable(b->text()), qPrintable(b->toolTip().left(40)),
                   b->width(), b->height(), h.width(), h.height());
    }
}
#include <QTabWidget>
#include <QSplitter>
#include <cstdio>
#include "packetmakerdialog.h"
#include "comparewindow.h"
#include "mergedwindow.h"
#include "trackdiagramwindow.h"
#include "stationlayoutwindow.h"
#include "testcatalogwindow.h"
#include "packetmatrixdialog.h"
#include <QTemporaryDir>
#include "twolocowindow.h"
#include "soswindow.h"
#include "readerdirwindow.h"
#include "tagbuilderwindow.h"
#include "taglibrary.h"
#include <QTabWidget>
#include "incidentreportwindow.h"
#include "incidentreportdialog.h"
#include "flasherwindow.h"
#include "fielddocs.h"
#include <QToolTip>
#include "flasherboarddialog.h"
#include "flasherfakeboard.h"
#include "laneband.h"
#include "fieldcatalog.h"
#include <QMenu>
#include "flasherqueuepage.h"
#include "flashercore.h"
#include "layoutaudit.h"
#include "lococonfigmodel.h"
#include "lococonfigwindow.h"
#include "replaywindow.h"
#include "decodeworkbench.h"
#include "framediffwindow.h"
#include "subpacketwindow.h"
#include "archivesearchwindow.h"
#include "faultpanelwindow.h"
#include "nmslogwindow.h"
#include <QCheckBox>
#include "roundtripwindow.h"
#include "runreportwindow.h"
#include "sessionwindow.h"
#include "fieldsweepdialog.h"
#include "fieldplot.h"
#include "speeddistance.h"
#include "brakingpanel.h"
#include "settingsdialog.h"
#include "exportdialog.h"
#include "sessionkeydialog.h"
#include "gototimestampdialog.h"
#include "exporter.h"
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include "logmodel.h"
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
#include "testassertions.h"
#include "testcaseboard.h"
#include "testcasepanel.h"
#include "linfofielddialog.h"
#include "schemaeditorwindow.h"
#include "schemaelementdialog.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (qEnvironmentVariableIsSet("SHOT_CLIP")) Settings::setFindDetached(false);   // the docked find bar's glyph buttons
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
            // SHOT_FROM=dark: the window is built in that theme and switched
            // to this one once shown (what a theme change leaves behind).
            const QString from = qEnvironmentVariable("SHOT_FROM");
            const Theme built = from.isEmpty() ? t : ThemeUtil::fromString(from);
            Settings::setTheme(ThemeUtil::toString(built));
            ThemeUtil::apply(built);
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
                // SHOT_TESTS=1 (session 306): the test cases' strip, after a
                // run of the real 105200 capture, section 1.
                if (qEnvironmentVariableIsSet("SHOT_TESTS")) {
                    FieldCatalog::instance().load(QStringLiteral(DL_SRC_DIR "/fieldmap.json"));
                    static TestAssertionEngine en;
                    static TestCaseBoard board(&en);
                    en.load(QStringLiteral(DL_SRC_DIR "/testcases.json"));
                    QFile cap(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
                    if (cap.open(QIODevice::ReadOnly)) {
                        while (!cap.atEnd()) {
                            const QString l = QString::fromLatin1(cap.readLine().trimmed());
                            if (!l.startsWith(QLatin1Char('@'))) continue;
                            QDateTime t = QDateTime::fromString(l.section(QLatin1Char(' '), 1, 1), Qt::ISODate);
                            t.setTimeSpec(Qt::UTC);
                            auto e = LogEntryPtr::create();
                            e->text = l;
                            e->epochMs = t.toMSecsSinceEpoch();
                            e->cachedTabKey = QStringLiteral("1_1");
                            en.observe(e);
                        }
                    }
                    board.setSection(QStringLiteral("1"));
                    board.poll();
                    if (auto *lc = qobject_cast<LocoConsoleWindow *>(win))
                        lc->setTestStrip(new TestCaseStrip(&board, lc->centralWidget()));
                    else if (auto *dw = qobject_cast<DmiWindow *>(win))
                        dw->setTestStrip(new TestCaseStrip(&board, dw));
                    if (qEnvironmentVariableIsSet("SHOT_PANEL")) {
                        auto *panel = new TestCasePanel(&board);
                        panel->resize(1100, 420);
                        panel->table()->setCurrentCell(4, 0);
                        win = panel;
                    }
                }
                // SHOT_BIG=1: the Loco Console's Big numbers panel shown.
                if (qEnvironmentVariableIsSet("SHOT_BIG"))
                    for (QPushButton *b : win->findChildren<QPushButton *>())
                        if (b->text() == QLatin1String("Big numbers") && !b->isChecked()) b->click();
            }
            if (which == QLatin1String("packet")) win = new PacketMakerDialog(nullptr);
            // Session 290: Help > Keyboard Shortcuts, from a real main window's menus.
            std::unique_ptr<MainWindow> shortcutsOwner;
            if (which == QLatin1String("shortcuts")) {
                shortcutsOwner.reset(new MainWindow);
                win = new ShortcutsDialog(harvestShortcuts(shortcutsOwner->menuBar(), shortcutsOwner.get()), nullptr);
            }
            // Session 294: the lanes with Link hidden and a TRAIN_SPEED lane,
            // and the Lanes menu and its LSRP submenu saved beside them.
            if (which == QLatin1String("lanes")) {
                QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap"));
                run.open(QIODevice::ReadOnly);
                while (!run.atEnd()) {
                    const QByteArray l = run.readLine().trimmed();
                    const QList<QByteArray> tok = l.split(' ');
                    if (!l.startsWith('@') || tok.size() < 3) continue;
                    disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(),
                                     QString());
                }
                disp.drainNow();
                FieldCatalog::instance().load(QStringLiteral(DL_SRC_DIR "/fieldmap.json"));   // the app loads it at start
                auto *host = new QWidget;
                auto *lay = new QVBoxLayout(host);
                auto *band = new LaneBand;
                lay->addWidget(band);
                lay->addStretch(1);
                host->resize(1100, 230);
                host->show();
                band->setHiddenLanes({ QStringLiteral("Link") });
                band->setCustomLanes({ CustomLane{ QStringLiteral("lsrp"), QStringLiteral("TRAIN_SPEED") } });
                band->setModel(disp.modelForKey(QStringLiteral("21_1")), QStringLiteral("21_1"), QStringLiteral("21_1"));
                QCoreApplication::processEvents();
                QMenu *menu = band->buildLaneMenu(host);
                menu->popup(QPoint(0, 0));
                QCoreApplication::processEvents();
                menu->grab().save(QStringLiteral("%1/shot_lanemenu_%2.png").arg(dir, ThemeUtil::toString(t)));
                if (QMenu *sub = menu->findChild<QMenu *>(QStringLiteral("laneMenu_lsrp"))) {
                    sub->popup(QPoint(0, 0));
                    QCoreApplication::processEvents();
                    sub->grab().save(QStringLiteral("%1/shot_lanemenu_lsrp_%2.png").arg(dir, ThemeUtil::toString(t)));
                    sub->hide();
                }
                if (auto *search = menu->findChild<QLineEdit *>(QStringLiteral("laneFieldSearch"))) {
                    search->setText(QStringLiteral("speed"));
                    QCoreApplication::processEvents();
                    menu->adjustSize();
                    menu->grab().save(QStringLiteral("%1/shot_lanemenu_search_%2.png").arg(dir, ThemeUtil::toString(t)));
                }
                menu->hide();
                win = host;
            }
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
                if (!qEnvironmentVariableIsEmpty("SHOT_LAYOUT")) w->loadLayout(qEnvironmentVariable("SHOT_LAYOUT"));   // session 209
                win = w;
            }
            if (which == QLatin1String("nmshlth") || which == QLatin1String("nmsflt")
                || which == QLatin1String("nmsrssi")) {
                // Session 247: one real capture through the dispatcher, at its
                // own timestamps; the window opened halfway, so the second half
                // arrives live. Prints the table's rows against the file's count.
                const NmsLog::Kind kind = which == QLatin1String("nmshlth") ? NmsLog::Kind::Health
                                          : which == QLatin1String("nmsflt") ? NmsLog::Kind::Fault
                                                                             : NmsLog::Kind::Rssi;
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("loco_1_1_26062026_162418")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                QList<QByteArray> run;
                QFile in(QStringLiteral(DL_SRC_DIR "/replay/%1.cap").arg(cap));
                if (in.open(QIODevice::ReadOnly))
                    while (!in.atEnd()) {
                        const QByteArray l = in.readLine().trimmed();
                        if (l.startsWith('@') && l.split(' ').size() >= 3) run << l;
                    }
                const QByteArray prefix = '@' + which.toLatin1() + '_';
                int inFile = 0;
                for (const QByteArray &l : run) if (l.startsWith(prefix)) ++inFile;
                auto feed = [&](int from, int to) {
                    for (int i = from; i < to; ++i) {
                        const QByteArray &l = run.at(i);
                        const qint64 ms = QDateTime::fromString(QString::fromLatin1(l.split(' ').at(1)), Qt::ISODate).toMSecsSinceEpoch();
                        disp.ingestLocal(21, 1, l, ms, QString());
                    }
                    disp.drainNow();
                    QCoreApplication::processEvents();
                };
                feed(0, run.size() / 2);
                const QString key = disp.knownKeys().value(0);
                auto *w = new NmsLogWindow(kind, disp.modelForKey(key), key, QStringLiteral("L1_V1"));
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                const int before = w->table().rows.size();
                feed(run.size() / 2, run.size());
                printf("%s: %s  rows %d at open, %d after the rest arrived; %d in the file; %d columns\n",
                       qPrintable(which), qPrintable(cap), before, w->table().rows.size(), inFile,
                       w->table().columnCount());
                // SHOT_CHANGES=1: Changes only ticked (session 251).
                if (qEnvironmentVariableIsSet("SHOT_CHANGES")) {
                    w->findChild<QCheckBox *>(QStringLiteral("nmsChangesOnly"))->setChecked(true);
                    printf("  changes only: %d of %d shown\n", w->view()->model()->rowCount(), w->table().rows.size());
                }
                // SHOT_HIDESAME=1: Hide unchanging (session 254).
                if (qEnvironmentVariableIsSet("SHOT_HIDESAME")) {
                    w->setHideUnchanging(true);
                    printf("  hidden: %d of %d columns: %s\n", int(w->hiddenColumns().size()), w->table().columnCount(),
                           qPrintable(w->hiddenColumns().join(QStringLiteral(", "))));
                }
                // SHOT_FAULTS=1: the fault summary dialog instead (session 255).
                if (qEnvironmentVariableIsSet("SHOT_FAULTS")) {
                    QDialog *fd = w->faultSummary();
                    fd->resize(1250, 500);
                    fd->show();
                    QElapsedTimer t0;
                    t0.start();
                    while (t0.elapsed() < 500) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    fd->grab().save(QStringLiteral("%1/shot_nmsfaults.png").arg(dir));
                }
                // SHOT_FIND=words (session 256), then Next once.
                if (!qEnvironmentVariableIsEmpty("SHOT_FIND")) {
                    w->resize(1100, 720);
                    w->show();
                    w->setFindText(qEnvironmentVariable("SHOT_FIND"));
                    w->findNext();
                    printf("  find: %d matching\n", w->findMatches());
                }
                // SHOT_FREEZE=n, SHOT_PIN=r,r (1-based packet numbers): session 253.
                if (!qEnvironmentVariableIsEmpty("SHOT_FREEZE")) w->freezeColumns(qEnvironmentVariableIntValue("SHOT_FREEZE"));
                for (const QString &r : qEnvironmentVariable("SHOT_PIN").split(QLatin1Char(','), Qt::SkipEmptyParts))
                    w->setPinned(r.toInt() - 1, true);
                // SHOT_SCROLLTO=<column>: the view at that column's first marked cell (session 250).
                if (!qEnvironmentVariableIsEmpty("SHOT_SCROLLTO")) {
                    w->findChild<QCheckBox *>(QStringLiteral("nmsFollow"))->setChecked(false);
                    const QString col = qEnvironmentVariable("SHOT_SCROLLTO");
                    const int c = w->table().index.value(col, -1);
                    for (int r = 0; c >= 0 && r < w->table().rows.size(); ++r)
                        if (NmsLog::markOf(col, w->table().cell(r, c)) != NmsLog::Mark::None
                            && w->filter()->mapFromSource(w->filter()->sourceModel()->index(r, c)).isValid()) {
                            w->resize(1100, 720);
                            w->show();
                            w->view()->scrollTo(w->filter()->mapFromSource(w->filter()->sourceModel()->index(r, c)),
                                                QAbstractItemView::PositionAtCenter);
                            printf("  scrolled to row %d, %s\n", r + 1, qPrintable(w->table().cell(r, c)));
                            if (auto *fz = w->findChild<QTableView *>(QStringLiteral("frozenColumns")))
                                printf("  table vbar %d/%d, frozen vbar %d/%d visible %d\n", w->view()->verticalScrollBar()->value(),
                                       w->view()->verticalScrollBar()->maximum(), fz->verticalScrollBar()->value(),
                                       fz->verticalScrollBar()->maximum(), fz->isVisible());
                            break;
                        }
                }
                win = w;
            }
            if (which == QLatin1String("stationlayout")) {
                // SHOT_FILE picks a station file; default: the synthetic fixture.
                auto *w = new StationLayoutWindow;
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                w->openFile(qEnvironmentVariableIsEmpty("SHOT_FILE")
                                ? QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_layout_synthetic.xlsx")
                                : qEnvironmentVariable("SHOT_FILE"));
                if (!qEnvironmentVariableIsEmpty("SHOT_EXPORT")) w->exportXlsx(qEnvironmentVariable("SHOT_EXPORT"));
                if (!qEnvironmentVariableIsEmpty("SHOT_DRAWING")) w->exportDrawing(qEnvironmentVariable("SHOT_DRAWING"));   // session 212
                // Session 233: SHOT_MEASURE=910,981 measures between two tags; SHOT_POINT=3 selects a point.
                if (!qEnvironmentVariableIsEmpty("SHOT_MEASURE") || !qEnvironmentVariableIsEmpty("SHOT_POINT")) {
                    w->resize(1200, 760);
                    w->show();
                    w->canvas()->grab();
                    if (!qEnvironmentVariableIsEmpty("SHOT_POINT")) w->selectSheetRow(2, qEnvironmentVariable("SHOT_POINT").toInt());
                    const QStringList m = qEnvironmentVariable("SHOT_MEASURE").split(QLatin1Char(','), Qt::SkipEmptyParts);
                    if (m.size() == 2) {
                        w->measureButton()->setChecked(true);
                        for (const QString &t : m) w->canvas()->measureAt(w->canvas()->tagRect(t).center());
                    }
                }
                win = w;
            }
            if (which == QLatin1String("testcatalog")) {
                // Session 231. SHOT_SELECT: the clause shown (default 22.1, a matrix);
                // SHOT_SEARCH: what the search box holds.
                auto *w = new TestCatalogWindow;
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                w->loadBuiltin();
                if (!qEnvironmentVariableIsEmpty("SHOT_SEARCH")) w->setSearch(qEnvironmentVariable("SHOT_SEARCH"));
                // SHOT_RUN=1 (session 263): a test run open, a few results set.
                if (qEnvironmentVariableIsSet("SHOT_RUN")) {
                    TestRun::Run r;
                    r.title = QStringLiteral("FRS 4 bench run");
                    r.loco = QStringLiteral("21");
                    r.firmware = QStringLiteral("LKAVACH v1.2.10");
                    r.testers = QStringLiteral("Abhinav");
                    static QTemporaryDir runDir;
                    w->newRun(r, runDir.filePath(QStringLiteral("shot.testrun.json")));
                    w->setTester(QStringLiteral("Abhinav"));
                    const QVector<QPair<const char *, TestRun::Verdict>> set{
                        { "4.1", TestRun::Verdict::Pass }, { "4.2", TestRun::Verdict::Pass }, { "4.3", TestRun::Verdict::Fail },
                        { "4.4", TestRun::Verdict::NotApplicable }, { "4.5", TestRun::Verdict::Pass } };
                    for (const auto &e : set) { w->select(QLatin1String(e.first)); w->setVerdict(e.second); }
                    w->select(QStringLiteral("4.3"));
                    w->setRemarks(QStringLiteral("FSB not applied at the MA end; EB came 1.2 s later."));
                    w->setEvidence(QStringLiteral("loco_1_1_12102026_1354.cap 13:55:36"));
                }
                w->select(qEnvironmentVariableIsEmpty("SHOT_SELECT") ? QStringLiteral("22.1") : qEnvironmentVariable("SHOT_SELECT"));
                win = w;
            }
            if (which == QLatin1String("packetmatrix")) {
                // Session 232. SHOT_SELECT: the case (default 31.23); the base preset is
                // the bufferload suite's real SLRP reference frame 6.
                static QTemporaryDir tmp;
                PacketBuilder pb;
                const QByteArray frame = QByteArray::fromHex(QByteArray(
                    "91 B9 AA E6 04 1E 80 00 08 40 6F 8E 68 01 80 00 00 00 00 01 7C AA 08 20 "
                    "00 11 63 0D 0C 58 10 B1 00 F4 84 00 22 05 09 BA 20 14 00 00 34 C6 08 5A "
                    "40 F4 93 00 54 E8 60 00 00 87 BC 04 99 E0 1A 3E 80 80 F1 02 84 38 0C 11 "
                    "30 38 22 41 08 7A 04 83 23 8A 29 0E 08 41 38 40 95 0A 60 61 64 E0 00 03 "
                    "43 38 68 60 42 C0 3E A4 18 46 67 50 A7 6E").replace(' ', ""));
                const Schema::ParsedPacket pp = pb.encoder().parseBody(QStringLiteral("slrp"), frame);
                PacketPreset base;
                base.captype = QStringLiteral("slrp");
                base.header = pp.header;
                base.subs = pp.subs;
                const QString basePath = tmp.filePath(QStringLiteral("slrp_base.packet.json"));
                base.save(basePath);
                const QString id = qEnvironmentVariableIsEmpty("SHOT_SELECT") ? QStringLiteral("31.23") : qEnvironmentVariable("SHOT_SELECT");
                const TestCatalog::Catalog &b = TestCatalog::builtin();
                QVector<TestCatalog::Case> ops;
                for (const QString &k : TestCatalog::childrenOf(b, id)) ops << b.cases.at(TestCatalog::indexOf(b, k));
                auto *d = new PacketMatrixDialog(b.cases.at(TestCatalog::indexOf(b, id)), ops);
                d->setAttribute(Qt::WA_DeleteOnClose, false);
                d->setBasePreset(basePath);
                d->setFolder(tmp.path());
                win = d;
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
            if (which == QLatin1String("taglibrary")) {
                // The built-in tag scenario library (tags_sim's KAV_CONFIG).
                auto *w = new TagLibraryDialog;
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                if (!qEnvironmentVariableIsEmpty("SHOT_FILTER")) w->setFilter(qEnvironmentVariable("SHOT_FILTER"));
                win = w;
            }
            if (which == QLatin1String("tagbuilder")) {
                // REAL tags: tags_sim's Hafizpet DN_MAIN route, signals included.
                auto *w = new TagBuilderWindow;
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                w->loadFile(QStringLiteral(DL_SRC_DIR "/tests/fixtures/tags_sim/DN_MAIN.tagroute.xml"));
                w->setDirection(RfidTag::DirNominal);
                w->selectRow(3);
                if (qEnvironmentVariable("SHOT_TAB") == QLatin1String("checks")) w->tabs()->setCurrentIndex(2);
                if (qEnvironmentVariable("SHOT_TAB") == QLatin1String("simchecks")) {
                    // The simulator's "LC gate_16 manned auto on": its rows' own findings.
                    w->loadFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"), 7);
                    w->tabs()->setCurrentIndex(2);
                }
                if (qEnvironmentVariable("SHOT_TAB") == QLatin1String("sim")) {
                    // The simulator's own first route (built in), previewed at 80 km/h.
                    w->loadFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"), 0);
                    SimPreview::Options o;
                    o.speedKmh = 80;
                    w->previewSimulator(o);
                }
                win = w;
            }
            if (which == QLatin1String("rdir")) {
                // SYNTHETIC @rdir (schema/fixtures, tests/rdirgen): FRS 18.1-18.7.
                // SHOT_TEST=18.x picks the case (its run is found by the minute gaps).
                QFile run(QStringLiteral(DL_SRC_DIR "/schema/fixtures/rdir_synthetic.log"));
                if (run.open(QIODevice::ReadOnly)) {
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        disp.ingestLocal(1, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
                    }
                }
                disp.drainNow();
                auto *w = new ReaderDirWindow(&disp);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                const QString id = qEnvironmentVariableIsEmpty("SHOT_TEST") ? QStringLiteral("18.4") : qEnvironmentVariable("SHOT_TEST");
                QVector<int> starts;
                const auto &reads = w->log().reads;
                for (int i = 0; i < reads.size(); ++i)
                    if (i == 0 || reads.at(i).ms - reads.at(i - 1).ms > 30000) starts << i;
                int k = 0;
                for (int i = 0; i < ReaderDir::frs18().size(); ++i) if (ReaderDir::frs18().at(i).id == id) k = i;
                if (k < starts.size()) {
                    w->setFromRead(starts.at(k));
                    w->setToRead(k + 1 < starts.size() ? starts.at(k + 1) - 1 : -1);
                }
                w->setTestCase(id);
                win = w;
            }
            if (which == QLatin1String("sos") || which == QLatin1String("twolocosos")) {
                // SYNTHETIC @sos (schema/fixtures, tests/sosgen): no LKAVACH
                // build logs SoS yet. SHOT_AT=HH:mm:ss picks the moment.
                for (int loco = 1; loco <= 2; ++loco) {
                    // SHOT_MINIMAL=1: README 03's minimal layout (session 192).
                    QFile run(QStringLiteral(DL_SRC_DIR "/schema/fixtures/%1_loco%2.log")
                                  .arg(qEnvironmentVariableIsSet("SHOT_MINIMAL") ? QStringLiteral("sos_minimal")
                                                                                 : QStringLiteral("sos_synthetic"))
                                  .arg(loco));
                    if (!run.open(QIODevice::ReadOnly)) continue;
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
                        disp.ingestLocal(quint8(loco), 1, l, ms, QString());
                    }
                }
                disp.drainNow();
                if (which == QLatin1String("twolocosos")) {
                    auto *w = new TwoLocoWindow(&disp);
                    w->setAttribute(Qt::WA_DeleteOnClose, false);
                    w->setSources(QStringLiteral("1_1"), QStringLiteral("2_1"));
                    win = w;
                } else {
                    auto *w = new SosWindow(&disp);
                    w->setAttribute(Qt::WA_DeleteOnClose, false);
                    const QString at = qEnvironmentVariableIsEmpty("SHOT_AT") ? QStringLiteral("10:01:40") : qEnvironmentVariable("SHOT_AT");
                    w->showMoment(QDateTime::fromString(QStringLiteral("2026-10-09T") + at, Qt::ISODate).toMSecsSinceEpoch());
                    // SHOT_TWOLOGS=1: the Two logs tab (session 187).
                    if (qEnvironmentVariableIsSet("SHOT_TWOLOGS")) w->lowerTabs()->setCurrentIndex(1);
                    win = w;
                }
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
                // Session 291: eight VCCs at once (flashervccs: flashing,
                // flashervccsq: the queue). Ports 9-16: nothing answers.
                if (which.startsWith(QLatin1String("flashervccs"))) {
                    QStringList eight;
                    for (int port = 9; port < 17; ++port) eight << QStringLiteral("127.0.0.1:%1").arg(port);
                    profile.vccIp = eight.join(QStringLiteral(", "));
                }
                profile.defaultImages.insert(Flasher::CardInput, writeImg(QStringLiteral("KAVACH_Input_Card_v5.appimage"), 40 * 1450));
                profile.defaultImages.insert(Flasher::CardOutput, writeImg(QStringLiteral("KAVACH_Output_Card_v5.appimage"), 25 * 1450));
                profile.defaultImages.insert(Flasher::CardVcc, writeImg(QStringLiteral("LKAVACH_v1.2.9.appimage"), 30 * 1450));
                profile.updaterWaitSeconds = Flasher::kMinUpdaterWaitSeconds;
                // Session 296: flasher26 is the queue with the 2.6 updater on.
                if (which == QLatin1String("flasher26")) profile.updater = kflash::Updater::V26;
                // Session 300: flasherloco, loco 1's four VCCs with 2.6 on.
                if (which == QLatin1String("flasherloco")) {
                    profile.updater = kflash::Updater::V26;
                    profile.vccIp = QStringLiteral("192.168.25.16, 192.168.25.102, 192.168.25.103, 192.168.25.104");
                }
                // Session 299: flashercards, a 2.6 Input run whose two
                // controllers report back, on its Summary.
                static FlasherTest::FakeBoard cardsBoard;
                if (which == QLatin1String("flashercards")) {
                    cardsBoard.v26 = true;
                    cardsBoard.cardReports = { qMakePair(5, 1), qMakePair(6, 3) };
                    profile.updater = kflash::Updater::V26;
                    profile.port = cardsBoard.port();
                    profile.tuning.card_wait_total_ms = 3000;
                    profile.tuning.card_wait_quiet_ms = 600;
                    QTimer::singleShot(300, []() { cardsBoard.powerOn(); });
                }
                store.upsert(profile, store.names().first());
                store.setActiveName(profile.name);
                store.save();
                auto *w = new FlasherWindow(nullptr, flashDir.path());
                w->queuePage()->selectCard(which.startsWith(QLatin1String("flashervccs")) || which == QLatin1String("flasherloco")
                                               ? Flasher::CardVcc
                                                                                         : Flasher::CardInput);
                if (which != QLatin1String("flasher") && which != QLatin1String("flashervccsq")
                    && which != QLatin1String("flasher26") && which != QLatin1String("flasherloco")) {
                    w->resize(1100, 720);
                    w->show();
                    w->startBatch(false);
                    QElapsedTimer t;
                    t.start();
                    const qint64 until = which == QLatin1String("flasherrun") || which == QLatin1String("flashervccs")
                                             ? 2500 : 60000;
                    while (t.elapsed() < until && !((which == QLatin1String("flashersum") || which == QLatin1String("flashercards"))
                                                    && w->currentPage() == FlasherWindow::SummaryPageIndex))
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                }
                win = w;
            }
            if (which == QLatin1String("lococonfig")) {
                auto *w = new LocoConfigWindow(nullptr, flashDir.path());
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                // Session 279: one field off its default, to see it stand out.
                if (LocoFieldModel *m = w->fieldModel())
                    m->setData(m->index(m->rowForKey(QStringLiteral("loco_unit_id")), LocoFieldModel::ColumnValue),
                               QStringLiteral("30421"), Qt::EditRole);
                win = w;
            }
            if (which == QLatin1String("schemaadd")) {
                // Session 309: Schema Editor ▸ Add, after SLRP's PKT_DIR.
                static SchemaEdit::Session session;
                session.open(QStringLiteral(":/schema/kavach.xml"), nullptr);
                int anchor = -1;
                for (int i : session.doc().all(QStringLiteral("field"))) {
                    if (session.doc().node(i).value(QStringLiteral("name")) == QLatin1String("PKT_DIR")) { anchor = i; break; }
                }
                auto *d = new SchemaElementDialog(session, anchor, nullptr);
                d->nameEdit()->setText(QStringLiteral("WEATHER_CODE"));
                d->bitsSpin()->setValue(4);
                d->unitEdit()->setText(QStringLiteral("code"));
                win = d;
            }
            if (which == QLatin1String("schemaeditor") || which == QLatin1String("schemaeditorro")) {
                // Session 308: Schema Editor. "schemaeditor" edits a temporary
                // copy (one staged edit shown); "...ro" browses the built-in one.
                static QString copy;
                if (which == QLatin1String("schemaeditor")) {
                    copy = QDir::temp().filePath(QStringLiteral("shot_kavach.xml"));
                    QFile::remove(copy);
                    QFile::copy(QStringLiteral(":/schema/kavach.xml"), copy);
                    QFile::setPermissions(copy, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                }
                auto *w = new SchemaEditorWindow(which == QLatin1String("schemaeditor") ? copy
                                                                                        : QStringLiteral(":/schema/kavach.xml"));
                const QString pkt = qEnvironmentVariableIsEmpty("SHOT_PACKET") ? QStringLiteral("SLRP")
                                                                                : qEnvironmentVariable("SHOT_PACKET");
                if (!w->selectContainer(QStringLiteral("packet"), pkt)) w->selectContainer(QStringLiteral("struct"), pkt);
                const SchemaDoc::Document &d = w->session().doc();
                for (int i = 0; i < d.count(); ++i) {
                    if (d.container(i) == w->selectedNode() && d.node(i).value(QStringLiteral("name"))
                                                                   == qEnvironmentVariable("SHOT_FIELD", QStringLiteral("DIST_PKT_START"))) {
                        if (which == QLatin1String("schemaeditor")) w->editAttribute(i, QStringLiteral("unit"), QStringLiteral("metre"));
                        w->refreshNow();
                        w->selectNode(i);
                        break;
                    }
                }
                win = w;
            }
            if (which == QLatin1String("linfoadd") || which == QLatin1String("linforemove")) {
                // Session 307: Manage ▸ Fields, on the built-in LINFO.
                static LocoInfo::Layout linfo;
                linfo.load(QStringLiteral(":/schema/kavach.xml"), nullptr);
                if (which == QLatin1String("linfoadd")) {
                    auto *d = new LinfoAddFieldDialog(linfo, { QStringLiteral("Brake pressures") },
                                                      QStringLiteral("C:/DLConsole/kavach.xml"),
                                                      QStringLiteral("brake_pressure_used"), nullptr);
                    d->nameEdit()->setText(QStringLiteral("max_brake_cylinder_pressure"));
                    d->typeBox()->setCurrentIndex(1);
                    d->noteEdit()->setText(QStringLiteral("kPa"));
                    d->defaultEdit()->setText(QStringLiteral("380"));
                    win = d;
                } else {
                    win = new LinfoRemoveFieldDialog(linfo, QStringLiteral("C:/DLConsole/kavach.xml"),
                                                     QStringLiteral("loco_doubtover"), {}, nullptr);
                }
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
            if (which == QLatin1String("fault")) {
                // A run that raises and clears NMS faults (SHOT_CAP picks it).
                auto *w = new FaultPanelWindow(&disp, nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                w->resize(1100, 720);
                w->show();
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("26062026_162418")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_%1.cap").arg(cap));
                if (run.open(QIODevice::ReadOnly)) {
                    int n = 0;
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
                        if (++n % 500 == 0) { disp.drainNow(); QCoreApplication::processEvents(); }
                    }
                }
                disp.drainNow();
                win = w;
            }
            if (which == QLatin1String("roundtrip")) {
                // A real run over the capture's lines as the live log.
                auto *w = new RoundTripWindow(nullptr);
                QVector<LogEntryPtr> entries;
                for (const QString &l : lines) {
                    auto e = LogEntryPtr::create();
                    e->text = l;
                    entries.push_back(e);
                }
                w->setLiveEntries(entries);
                w->resize(1100, 720);
                w->show();
                QMetaObject::invokeMethod(w, "onRun");
                QElapsedTimer t;
                t.start();
                auto *tbl = w->findChild<QTableWidget *>();
                while (t.elapsed() < 8000 && !(tbl && tbl->rowCount() > 0))
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                if (tbl && tbl->rowCount() > 0) tbl->selectRow(0);
                win = w;
            }
            if (which == QLatin1String("runreport")) {
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("26062026_162418")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_%1.cap").arg(cap));
                if (run.open(QIODevice::ReadOnly)) {
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
                    }
                }
                disp.drainNow();
                auto *w = new RunReportWindow(disp.modelForKey(QStringLiteral("21_1")), QStringLiteral("21_1"), QStringLiteral("L1_V1"));
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("session")) {
                // Two loco tabs of real traffic, written to .dlr the way
                // Save does and read back: what an operator opens.
                qint64 ms = 1782558147000LL;
                for (int i = 0; i < lines.size(); ++i)
                    disp.ingestLocal(i % 2 ? 21 : 81, 1, lines.at(i).toUtf8(), ms + i * 137, QString());
                disp.drainNow();
                static QTemporaryDir tmp;
                QStringList paths;
                for (const QString &key : {QStringLiteral("21_1"), QStringLiteral("81_1")}) {
                    LogModel *m = disp.modelForKey(key);
                    if (!m) continue;
                    const QString path = tmp.path() + QLatin1Char('/') + key + QStringLiteral(".dlr");
                    QFile f(path);
                    if (!f.open(QIODevice::WriteOnly)) continue;
                    SessionFile::FileHeader h;
                    h.createdMs = ms;
                    h.sourceId = quint8(key.section(QLatin1Char('_'), 0, 0).toInt());
                    h.kvchId = 1;
                    f.write(SessionFile::encodeHeader(h));
                    for (int r = 0; r < m->rowCount(); ++r) {
                        const LogEntryPtr e = m->entryAt(r);
                        if (e && !e->rawBytes.isEmpty()) f.write(SessionFile::encodeRecord(e->epochMs, e->rawBytes));
                    }
                    paths << path;
                }
                static ColorRules rules;
                auto *w = new SessionWindow(&rules, &names, t, nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                w->loadFiles(paths);
                win = w;
            }
            if (which == QLatin1String("fieldsweep")) {
                // Seeded from a real lsrp frame; a short sweep to the local
                // discard port, so the results table has rows.
                auto *w = new FieldSweepDialog(nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                for (const QString &l : lines)
                    if (l.startsWith(QLatin1String("@lsrp"))) { w->seedFromBuffer(l); break; }
                for (QSpinBox *sb : w->findChildren<QSpinBox *>()) {
                    if (sb->suffix().isEmpty()) sb->setValue(9);
                    else sb->setValue(20);
                }
                w->resize(1100, 720);
                w->show();
                for (QPushButton *b : w->findChildren<QPushButton *>())
                    if (b->text() == QLatin1String("Start sweep")) b->click();
                QElapsedTimer t;
                t.start();
                while (t.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                win = w;
            }
            // The small dialogs, at their own size (SHOT_SIZE=WxH to force one).
            if (which == QLatin1String("settings")) win = new SettingsDialog(nullptr);
            if (which == QLatin1String("export"))
                win = new ExportDialog(QStringLiteral("/Users/operator/Documents/DLConsole/loco_1_1_export.csv"),
                                       Exporter::CSV, Exporter::ColAll, nullptr);
            if (which == QLatin1String("sessionkey")) win = new SessionKeyDialog(nullptr);
            if (which == QLatin1String("gototime")) {
                const qint64 t0 = QDateTime::fromString(QStringLiteral("2026-06-27T14:02:26"), Qt::ISODate).toMSecsSinceEpoch();
                win = new GotoTimestampDialog(t0 + 60000, t0, t0 + 1800000, nullptr);
            }
            if (which == QLatin1String("braking")) {
                // SYNTHETIC @uba (schema/fixtures): no real capture in replay/
                // carries braking curves. Layout only, not firmware evidence.
                QFile uba(QStringLiteral(DL_SRC_DIR "/schema/fixtures/uba_synthetic.log"));
                if (uba.open(QIODevice::ReadOnly)) {
                    while (!uba.atEnd()) {
                        const QByteArray l = uba.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
                    }
                }
                disp.drainNow();
                auto *w = new BrakingPanel(&disp, nullptr);
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("speeddist")) {
                // A whole real run: speed over absolute location.
                const QString cap = qEnvironmentVariableIsEmpty("SHOT_CAP") ? QStringLiteral("26062026_162418")
                                                                            : qEnvironmentVariable("SHOT_CAP");
                QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_%1.cap").arg(cap));
                if (run.open(QIODevice::ReadOnly)) {
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
                    }
                }
                disp.drainNow();
                auto *w = new SpeedDistanceWindow(disp.modelForKey(QStringLiteral("21_1")), QStringLiteral("21_1"));
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                win = w;
            }
            if (which == QLatin1String("fieldplot")) {
                // A whole real run, speed and location on one time axis.
                QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_26062026_162418.cap"));
                if (run.open(QIODevice::ReadOnly)) {
                    while (!run.atEnd()) {
                        const QByteArray l = run.readLine().trimmed();
                        if (!l.startsWith('@')) continue;
                        const QList<QByteArray> tok = l.split(' ');
                        if (tok.size() < 3) continue;
                        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
                    }
                }
                disp.drainNow();
                auto *w = new FieldPlotWindow(disp.modelForKey(QStringLiteral("21_1")), QStringLiteral("21_1"));
                w->setAttribute(Qt::WA_DeleteOnClose, false);
                w->plotField(qEnvironmentVariableIsEmpty("SHOT_FIELD") ? QStringLiteral("TRAIN_SPEED") : qEnvironmentVariable("SHOT_FIELD"));
                if (qEnvironmentVariableIsSet("SHOT_FIELD2")) w->addField(qEnvironmentVariable("SHOT_FIELD2"));
                win = w;
            }
            if (which == QLatin1String("fielddoctip")) {
                // Session 302: a field description as its tooltip draws it (a
                // word-wrapped rich-text label in the tooltip palette).
                // SHOT_FIELD = "packet:name[:tagType]".
                const QStringList spec = (qEnvironmentVariableIsEmpty("SHOT_FIELD") ? QStringLiteral("slrp:AUTHORIZED_SPEED")
                                                                                   : qEnvironmentVariable("SHOT_FIELD")).split(QLatin1Char(':'));
                auto *tip = new QLabel(FieldDocs::instance().tooltip(spec.value(0), spec.value(1), spec.value(2).toInt()));
                tip->setWordWrap(true);
                tip->setAutoFillBackground(true);
                tip->setPalette(QToolTip::palette());
                tip->setFont(QToolTip::font());
                tip->setMargin(6);
                tip->setAttribute(Qt::WA_DeleteOnClose, false);
                win = tip;
            }
            if (which == QLatin1String("flasherboard")) {
                // Session 297: Ask the board, answered by a fake 2.6 VCC.
                static FlasherTest::FakeBoard board;
                board.v26 = true;
                board.activeCrc = 0x6A46F27Eu;
                board.activeSize = 43500;
                board.activeFwVersion = 0x010209u;
                board.powerOn();
                auto *d = new FlasherBoardDialog(QStringLiteral("127.0.0.1"), board.port(), 10,
                                                 { qMakePair(QStringLiteral("VCC image LKAVACH_v1.2.9.appimage"), 0x6A46F27Eu) });
                d->setAttribute(Qt::WA_DeleteOnClose, false);
                d->ask();
                QElapsedTimer t;
                t.start();
                while (!d->hasAnswer() && t.elapsed() < 5000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                win = d;
            }
            if (!win) return 2;
            const bool small = which == QLatin1String("settings") || which == QLatin1String("export")
                               || which == QLatin1String("flasherboard") || which == QLatin1String("fielddoctip")
                               || which == QLatin1String("sessionkey") || which == QLatin1String("gototime");
            if (which == QLatin1String("session")) win->resize(1200, 700);   // its own default
            else if (small) {
                const QStringList wh = qEnvironmentVariable("SHOT_SIZE").split(QLatin1Char('x'));
                if (wh.size() == 2) win->resize(wh.at(0).toInt(), wh.at(1).toInt());
                else win->adjustSize();
            } else win->resize(1100, 720);
            win->show();
            QElapsedTimer settle;
            settle.start();
            while (settle.elapsed() < 800) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            for (FindBar *fb : win->findChildren<FindBar *>()) fb->activate();
            if (qEnvironmentVariableIsSet("SHOT_CLIP")) for (TableFindBar *tb : win->findChildren<TableFindBar *>()) tb->show();
            QCoreApplication::processEvents();
            clipAudit(win, which);
            if (built != t) {
                Settings::setTheme(ThemeUtil::toString(t));
                ThemeUtil::apply(t);
                UiStyle::apply();
                settle.restart();
                while (settle.elapsed() < 500) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
            if (auto *fz = win->findChild<QTableView *>(QStringLiteral("frozenColumns")))   // session 253
                if (auto *tv = qobject_cast<QTableView *>(fz->parentWidget()))
                    printf("  settled: table vbar %d, frozen vbar %d, frozen top row %d\n", tv->verticalScrollBar()->value(),
                           fz->verticalScrollBar()->value(), fz->rowAt(0));
            if (qEnvironmentVariableIsSet("SHOT_WIDE"))
                for (QWidget *c : win->findChildren<QWidget *>())
                    if ((c->isVisible() || qEnvironmentVariableIsSet("SHOT_HIDDEN")) && c->minimumSizeHint().width() > qEnvironmentVariable("SHOT_WIDE").toInt())
                        printf("  wide: %s %s min=%d w=%d '%s'\n", c->metaObject()->className(), qPrintable(c->objectName()),
                               c->minimumSizeHint().width(), c->width(), qPrintable(c->property("text").toString().left(30)));
            if (qEnvironmentVariableIsSet("SHOT_ROWS"))
                if (auto *mw = qobject_cast<QMainWindow *>(win))
                    if (QLayout *cl = mw->centralWidget() ? mw->centralWidget()->layout() : nullptr)
                        for (int i = 0; i < cl->count(); ++i)
                        {
                            printf("  row %d: min %d x %d\n", i, cl->itemAt(i)->minimumSize().width(), cl->itemAt(i)->minimumSize().height());
                            if (QLayout *sub = cl->itemAt(i)->layout())
                                for (int j = 0; j < sub->count(); ++j) {
                                    QWidget *sw = sub->itemAt(j)->widget();
                                    printf("     %d: %s '%s' min %d\n", j, sw ? sw->metaObject()->className() : "spacer",
                                           sw ? qPrintable(sw->property("text").toString().left(20)) : "", sub->itemAt(j)->minimumSize().width());
                                }
                        }
            if (qEnvironmentVariableIsSet("SHOT_TALL"))
                for (QWidget *c : win->findChildren<QWidget *>())
                    if (c->isVisible() && c->minimumSizeHint().height() > qEnvironmentVariable("SHOT_TALL").toInt())
                        printf("  tall: %s %s minH=%d h=%d '%s'\n", c->metaObject()->className(), qPrintable(c->objectName()),
                               c->minimumSizeHint().height(), c->height(), qPrintable(c->property("text").toString().left(30)));
            if (which == QLatin1String("session")) {
                // A row selected, so the side panel has a frame to show.
                for (QTableView *tv : win->findChildren<QTableView *>())
                    if (tv->isVisible() && tv->model() && tv->model()->rowCount() > 100) { tv->selectRow(5); break; }
                for (QWidget *fw : win->findChildren<QWidget *>())
                    if (QString::fromLatin1(fw->metaObject()->className()) == QLatin1String("FilterBar") && fw->isVisible() && qEnvironmentVariableIsSet("SHOT_DUMP")) {
                        printf("filterbar w=%d minHint=%d hint=%d\n", fw->width(), fw->minimumSizeHint().width(), fw->sizeHint().width());
                        for (QWidget *c : fw->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
                            if (c->isVisible()) printf("   %s '%s' w=%d min=%d hint=%d\n", c->metaObject()->className(), qPrintable(c->property("text").toString()), c->width(), c->minimumSizeHint().width(), c->sizeHint().width());
                    }
                settle.restart();
                while (settle.elapsed() < 300) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                if (qEnvironmentVariableIsSet("SHOT_DUMP"))
                    for (QWidget *c : win->findChildren<QWidget *>())
                        if (c->isVisible() && c->minimumSizeHint().width() > 150 && c->minimumSizeHint().width() < 400)
                            printf("  wide: %s %s min=%d w=%d\n", c->metaObject()->className(), qPrintable(c->objectName()), c->minimumSizeHint().width(), c->width());
            }
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
        // SHOT_EMPTY=1 (session 289): no traffic, so the start page shows.
        auto *disp = qEnvironmentVariableIsSet("SHOT_EMPTY") ? nullptr : w.findChild<MessageDispatcher *>();
        if (disp) {
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
                if (view->model()->index(r, LogModel::ColMessage).data().toString().startsWith(QLatin1String("@lsrp"))) {
                    view->setCurrentIndex(view->model()->index(r, 0));
                    view->selectRow(r);
                    break;
                }
            }
            break;
        }
        for (int i = 0; i < 20; ++i) { QCoreApplication::processEvents(); }
        for (FindBar *fb : w.findChildren<FindBar *>()) if (fb->parentWidget() && fb->parentWidget()->isVisible()) fb->activate();
        for (int i = 0; i < 20; ++i) { QCoreApplication::processEvents(); }
        clipAudit(&w, QStringLiteral("main"));
        w.grab().save(QStringLiteral("%1/shot_%2.png").arg(dir, ThemeUtil::toString(t)));
        if (qEnvironmentVariableIsSet("SHOT_DUMP")) {
            printf("main %dx%d minHint %dx%d\n", w.width(), w.height(), w.minimumSizeHint().width(), w.minimumSizeHint().height());
            for (QTableView *tv : w.findChildren<QTableView *>()) {
                if (!tv->isVisible() || tv->model()->columnCount() < 5) continue;
                QString cols;
                for (int c = 0; c < tv->model()->columnCount(); ++c)
                    cols += QString::number(tv->isColumnHidden(c) ? 0 : tv->columnWidth(c)) + QLatin1Char(' ');
                printf("  table %dx%d viewport %d hbar %d cols %s\n", tv->width(), tv->height(), tv->viewport()->width(),
                       tv->horizontalScrollBar()->isVisible(), qPrintable(cols));
                QHeaderView *hh = tv->horizontalHeader();
                printf("  header minSection %d stretchLast %d lastMode %d defSection %d hintLast %d visualLast %d\n",
                       hh->minimumSectionSize(), hh->stretchLastSection(), int(hh->sectionResizeMode(hh->logicalIndex(hh->count() - 1))),
                       hh->defaultSectionSize(), hh->sectionSizeHint(hh->count() - 1), hh->logicalIndex(hh->count() - 1));
            }
        }
    }
    return 0;
}

#include "dmipanel.h"
#include "dmitimetravel.h"
#include "sessionwindow.h"
#include "uistyle.h"
#include "bookmarks.h"
#include "reviewpackdialog.h"
#include "gototimestampdialog.h"
#include "markerscrollbar.h"
#include "timelineribbon.h"
#include "findbar.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include "colorrules.h"
#include "filterbar.h"
#include "logmodel.h"
#include "logtableview.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "capturedecoder.h"
#include "fieldinspector.h"
#include "rawbytespanel.h"
#include "sessionreader.h"
#include "sessionsummary.h"
#include "settings.h"
#include "decodeworkbench.h"
#include "framediffwindow.h"
#include "fieldplot.h"
#include "packetmakerdialog.h"
#include "dmiclip.h"
#include "dmiclipdialog.h"
#include "incidentreport.h"
#include "incidentreportdialog.h"
#include "incidentreportwindow.h"
#include "radiochartdialog.h"
#include "runcomparewindow.h"
#include <QTableWidget>
#include "configfiles.h"
#include "testassertions.h"
#include "testcaseboard.h"
#include "testcasepanel.h"
#include <QFileDialog>
#include <QSaveFile>
#include <QProgressDialog>
#include <algorithm>
#include "faulttimelinewindow.h"
#include "iotimelinewindow.h"
#include "odometrywindow.h"
#include "radiohealthwindow.h"
#include "runreportwindow.h"
#include "speeddistance.h"
#include "trackdiagramwindow.h"
#include "testcatalogwindow.h"
#include <QDesktopServices>
#include <QPushButton>
#include <QUrl>
#include <QCheckBox>
#include <QComboBox>
#include <QTimer>
#include <QToolBar>

#include <QCloseEvent>
#include <QApplication>
#include <QDockWidget>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QHeaderView>
#include <QLabel>
#include <QSortFilterProxyModel>
#include <QDateTime>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QTreeWidget>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <QVector>

namespace {
// "1 file" / "3 files" (the status read "400 records from 2 file(s)").
QString countOf(qint64 n, const char *one, const char *many)
{
    return QStringLiteral("%1 %2").arg(n).arg(QCoreApplication::translate("SessionWindow", n == 1 ? one : many));
}
}  // namespace

SessionWindow::SessionWindow(const ColorRules *rules,
                             const NameMap    *names,
                             Theme             theme,
                             QWidget          *parent)
    : QMainWindow(parent)
    , m_rules(rules)
    , m_names(names)
    , m_theme(theme)
{
    // Qt::Window so it gets its own taskbar entry and can sit alongside the
    // live console rather than on top of it — the usual reason to open one
    // of these is to compare an archive against what is happening now.
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Recorded session"));
    WindowGeometry::makeResizableWindow(this);
    // 1200, not 1100: the filter bar (846 px) and the side panel (290) need
    // 1146 on the Mac, and more with Linux's fonts; a larger minimum wins
    // over this anyway.
    resize(1200, 700);
    // Default above; a remembered size/position wins over it.
    WindowGeometry::restore(this, QStringLiteral("sessionWindow"));

    // The tools, before anything else so the menu bar exists whether or not
    // any file turns out to be readable.
    buildToolsMenu();

    m_tabs = new QTabWidget(this);
    UiStyle::useTabTooltips(m_tabs);
    m_tabs->setTabsClosable(false);
    m_tabs->setMovable(true);
    setCentralWidget(m_tabs);

    m_rawPanel = new RawBytesPanel(this);
    m_rawPanel->setNameMap(m_names);
    auto *dock = new QDockWidget(tr("Raw bytes"), this);
    dock->setObjectName(QStringLiteral("sessionRawDock"));
    dock->setWidget(m_rawPanel);
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // Schema decode of the selected row, byte-linked to the hex dump —
    // the same pair MainWindow shows, tabbed the same way.
    //
    // The archive viewer had the bytes and no way to decode them, which
    // undercut the reason for keeping bytes rather than text: re-running a
    // corrected decoder over an old capture meant exporting the frame and
    // pasting it into the workbench. Sharing the widget rather than growing
    // a second decode view also means the archive and the live console can
    // never disagree about what a frame says, which they would within a
    // release if each had its own.
    m_fieldPanel = new FieldInspector(this);
    m_fieldPanel->setDecoder(&kavachSchema());
    auto *fieldDock = new QDockWidget(tr("Decoded fields"), this);
    fieldDock->setObjectName(QStringLiteral("sessionFieldDock"));
    fieldDock->setWidget(m_fieldPanel);
    fieldDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, fieldDock);
    tabifyDockWidget(dock, fieldDock);
    // Decoded fields in front, as in the live window: what the frame says
    // first, its bytes one tab away.
    fieldDock->raise();

    // Session 349: what the recording holds, per source, and where each
    // went quiet. A quiet span is clicked to land on its last row.
    auto *summaryPage = new QWidget(this);
    auto *summaryLayout = new QVBoxLayout(summaryPage);
    summaryLayout->setContentsMargins(4, 4, 4, 4);
    auto *gapRow = new QHBoxLayout;
    gapRow->addWidget(new QLabel(tr("Quiet spans over"), summaryPage));
    m_gapOver = new QSpinBox(summaryPage);
    m_gapOver->setRange(1, 3600);
    m_gapOver->setValue(5);
    m_gapOver->setSuffix(tr(" s"));
    gapRow->addWidget(m_gapOver);
    gapRow->addStretch(1);
    summaryLayout->addLayout(gapRow);
    m_summary = new QTreeWidget(summaryPage);
    m_summary->setColumnCount(2);
    m_summary->setHeaderLabels({ tr("Source"), tr("Value") });
    m_summary->setRootIsDecorated(true);
    summaryLayout->addWidget(m_summary, 1);
    auto *summaryDock = new QDockWidget(tr("Summary"), this);
    summaryDock->setObjectName(QStringLiteral("sessionSummaryDock"));
    summaryDock->setWidget(summaryPage);
    summaryDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, summaryDock);
    tabifyDockWidget(fieldDock, summaryDock);
    fieldDock->raise();
    connect(m_gapOver, QOverload<int>::of(&QSpinBox::valueChanged), this, &SessionWindow::rebuildSummary);
    connect(m_summary, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *it) {
        const QString key = it->data(0, Qt::UserRole).toString();
        const qint64 ms = it->data(0, Qt::UserRole + 1).toLongLong();
        if (!key.isEmpty() && ms > 0) { jumpToTime(key, ms); }
    });

    connect(m_fieldPanel, &FieldInspector::byteRangeSelected,
            this, [this](int from, int to) {
                if (m_rawPanel) { m_rawPanel->highlightBytes(from, to); }
            });

    m_lblSummary = new QLabel(tr("No session loaded"), this);
    statusBar()->addWidget(m_lblSummary);

    buildPlaybackBar();   // session 371
    buildTestCasesUi();   // session 373
}

// ---------------------------------------------------------------------------
//  Session 373 — the test cases over a recording
//
//  Live, the engine sees every entry of every source as it arrives and marks
//  each case OBSERVED / NOT ANSWERED (sessions 305, 306). A recording from
//  the site is the same traffic: the same engine runs over all its tabs,
//  merged in time order, so the reviewer sees which clauses the run
//  exercised without the site having had the cases loaded.
// ---------------------------------------------------------------------------

void SessionWindow::buildTestCasesUi()
{
    m_tests = new TestAssertionEngine(this);
    m_testBoard = new TestCaseBoard(m_tests, this);
    m_testPanel = new TestCasePanel(m_testBoard, this);
    connect(m_testPanel, &TestCasePanel::jumpRequested, this,
            [this](const QString &tabKey, qint64 ms) { jumpToTime(tabKey, ms); });
    m_testDock = new QDockWidget(tr("Test cases"), this);
    m_testDock->setObjectName(QStringLiteral("sessionTestCasesDock"));
    m_testDock->setWidget(m_testPanel);
    m_testDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, m_testDock);
    m_testDock->hide();   // shown, and tabbed with the panels, once there are cases

    auto *menu = menuBar()->addMenu(tr("Test &cases"));
    QAction *a = menu->addAction(tr("Run the &built-in test cases over this recording"));
    a->setObjectName(QStringLiteral("sessionTestCasesBuiltin"));
    connect(a, &QAction::triggered, this, [this]() {
        QFile f(builtinConfigPath(ConfigKind::TestCases));
        if (f.open(QIODevice::ReadOnly)) setTestCases(f.readAll());
    });
    a = menu->addAction(tr("Run test cases from a &file…"));
    a->setObjectName(QStringLiteral("sessionTestCasesFile"));
    connect(a, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Test cases"), QString(),
                                                          tr("Test cases (*.json);;All files (*)"));
        QFile f(path);
        if (!path.isEmpty() && f.open(QIODevice::ReadOnly)) setTestCases(f.readAll());
    });
    menu->addSeparator();
    a = menu->addAction(tr("&Show results"));
    connect(a, &QAction::triggered, this, [this]() { m_testDock->show(); m_testDock->raise(); });
    a = menu->addAction(tr("&Save observation report…"));
    a->setObjectName(QStringLiteral("sessionTestCasesReport"));
    connect(a, &QAction::triggered, this, [this]() {
        if (m_tests->assertions().isEmpty()) {
            statusBar()->showMessage(tr("No test cases have been run over this recording."), 4000);
            return;
        }
        const QString path = QFileDialog::getSaveFileName(this, tr("Save observation report"),
                                                          QDir::home().filePath(QStringLiteral("observations_recording.html")),
                                                          tr("HTML (*.html)"));
        if (path.isEmpty()) return;
        QSaveFile f(path);
        if (f.open(QIODevice::WriteOnly) && f.write(testReportHtml().toUtf8()) >= 0 && f.commit())
            statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 4000);
        else
            statusBar()->showMessage(tr("Not saved: %1").arg(f.errorString()), 6000);
    });
}

void SessionWindow::setTestCases(const QByteArray &json)
{
    QString err;
    if (!m_tests->loadFromJson(json, &err)) {
        statusBar()->showMessage(tr("The test cases did not load: %1").arg(err), 6000);
        return;
    }
    runTestCases();
}

void SessionWindow::runTestCases()
{
    m_tests->resetObservations();
    if (m_tests->assertions().isEmpty()) {
        m_testBoard->poll();
        m_testDock->hide();
        return;
    }
    // Every tab, merged by time: the engine sees the recording as it saw
    // the traffic live, whichever source each frame came from.
    QVector<LogEntryPtr> all;
    for (LogModel *m : qAsConst(m_models))
        for (int r = 0; r < m->count(); ++r)
            if (LogEntryPtr e = m->entryAt(r)) all.append(e);
    std::stable_sort(all.begin(), all.end(),
                     [](const LogEntryPtr &a, const LogEntryPtr &b) { return a->epochMs < b->epochMs; });
    // The engine decodes a frame for each case that asks about it: about a
    // millisecond a row with the 101 built-in cases. An hour of recording is
    // a minute or more, so it runs with a progress dialog and can be
    // stopped; the results then stand up to where it stopped, and say so.
    QProgressDialog progress(tr("Running %1 test case(s) over %2 rows…").arg(m_tests->assertions().size()).arg(all.size()),
                             tr("Stop"), 0, all.size(), this);
    progress.setWindowTitle(tr("Test cases"));
    progress.setMinimumDuration(1500);
    progress.setWindowModality(Qt::WindowModal);
    int done = 0;
    for (const LogEntryPtr &e : qAsConst(all)) {
        m_tests->observe(e);
        if (++done % 250 == 0) {
            progress.setValue(done);
            if (progress.wasCanceled()) break;
        }
    }
    progress.setValue(all.size());
    const bool stopped = done < all.size();
    // A precondition near the end: resolved by the last moment run, as the
    // live timer resolves it by the clock.
    if (done > 0) m_tests->expireArmed(all.at(done - 1)->epochMs);
    m_testBoard->poll();
    // Tabbed with the decoded fields the first time it has something to
    // show: a hidden dock still takes a tab in that bar.
    if (!m_testDock->property("tabbed").toBool()) {
        for (QDockWidget *d : findChildren<QDockWidget *>())
            if (d->objectName() == QLatin1String("sessionFieldDock")) tabifyDockWidget(d, m_testDock);
        m_testDock->setProperty("tabbed", true);
    }
    m_testDock->show();
    m_testDock->raise();
    if (stopped)
        statusBar()->showMessage(tr("Test cases stopped at %1 (%2 of %3 rows): %4")
                                     .arg(QDateTime::fromMSecsSinceEpoch(all.at(done - 1)->epochMs).toString(QStringLiteral("HH:mm:ss")))
                                     .arg(done).arg(all.size()).arg(m_testBoard->summary()));
    else
        statusBar()->showMessage(tr("Test cases over the recording: %1").arg(m_testBoard->summary()), 8000);
}

QString SessionWindow::testReportHtml() const
{
    qint64 last = 0;
    for (LogModel *m : m_models)
        if (m->count()) last = qMax(last, m->entryAt(m->count() - 1)->epochMs);
    return m_tests->buildReportHtml(tr("KAVACH test observations — recording %1").arg(m_loadedNames.join(QStringLiteral(", "))),
                                    last);
}

// ---------------------------------------------------------------------------
//  Session 371 — Play
//
//  A recording is read with its DMI beside it. The GIF clip (session 262)
//  shows a minute or two; a test of half an hour is watched, paused,
//  rewound. Play moves the cursor through the tab in time: the DMI window
//  and the Live Loco Console already follow the cursor (session 84/169), so
//  they play too, from every loaded tab, not only the one on show.
// ---------------------------------------------------------------------------

namespace {
constexpr int    kPlayTickMs = 100;
constexpr qint64 kQuietMs    = 3000;   // a step longer than this is skipped, as the clip shortens it
}  // namespace

void SessionWindow::buildPlaybackBar()
{
    m_playBar = addToolBar(tr("Playback"));
    m_playBar->setObjectName(QStringLiteral("playbackBar"));
    m_playBar->setMovable(false);

    m_actPlay = m_playBar->addAction(tr("▶ Play"));
    m_actPlay->setObjectName(QStringLiteral("playbackPlay"));
    m_actPlay->setShortcut(QKeySequence(QStringLiteral("F5")));
    m_actPlay->setToolTip(tr("Play the recording from the selected row: the cursor moves in time, "
                             "and the DMI follows it (F5 plays and pauses)"));
    connect(m_actPlay, &QAction::triggered, this, [this]() { m_playing ? stopPlayback() : startPlayback(); });

    m_speed = new QComboBox(m_playBar);
    m_speed->setObjectName(QStringLiteral("playbackSpeed"));
    for (int x : { 1, 2, 4, 8, 16 }) m_speed->addItem(tr("%1×").arg(x), x);
    m_speed->setToolTip(tr("How much faster than it happened"));
    m_playBar->addWidget(m_speed);

    m_skipQuiet = new QCheckBox(tr("Skip quiet spans"), m_playBar);
    m_skipQuiet->setObjectName(QStringLiteral("playbackSkipQuiet"));
    m_skipQuiet->setChecked(true);
    m_skipQuiet->setToolTip(tr("When nothing was recorded for more than 3 s, go straight to the next row"));
    m_playBar->addWidget(m_skipQuiet);

    // Session 375: from one change on the DMI to the next.
    QAction *prevChange = m_playBar->addAction(tr("◀ DMI change"));
    prevChange->setObjectName(QStringLiteral("playbackPrevDmiChange"));
    prevChange->setShortcut(QKeySequence(QStringLiteral("Shift+F8")));
    prevChange->setToolTip(tr("The previous change on the DMI (Shift+F8)"));
    connect(prevChange, &QAction::triggered, this, [this]() { stepDmiChange(-1); });
    QAction *nextChange = m_playBar->addAction(tr("DMI change ▶"));
    nextChange->setObjectName(QStringLiteral("playbackNextDmiChange"));
    nextChange->setShortcut(QKeySequence(QStringLiteral("F8")));
    nextChange->setToolTip(tr("The next change on the DMI: mode, brake, target, signal, messages, tag (F8)"));
    connect(nextChange, &QAction::triggered, this, [this]() { stepDmiChange(+1); });

    QAction *dmi = m_playBar->addAction(tr("DMI"));
    dmi->setObjectName(QStringLiteral("playbackDmi"));
    dmi->setToolTip(tr("Open the DMI window, following the cursor"));
    connect(dmi, &QAction::triggered, this, [this]() {
        const QString key = currentTabKey();
        if (m_tabUi.contains(key) && m_tabUi[key].view) offerDmiAt(key, m_tabUi[key].view->currentIndex());
        DmiWindow::showFollowing(this, nullptr);
    });

    m_playClock = new QLabel(m_playBar);
    m_playClock->setObjectName(QStringLiteral("playbackClock"));
    m_playBar->addSeparator();
    m_playBar->addWidget(m_playClock);

    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(kPlayTickMs);
    connect(m_playTimer, &QTimer::timeout, this, [this]() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 wall = now - m_playWallMs;
        m_playWallMs = now;
        advancePlayback(wall);
    });
    updatePlaybackUi();
}

void SessionWindow::setPlaybackSpeed(int times)
{
    const int i = m_speed->findData(times);
    if (i >= 0) m_speed->setCurrentIndex(i);
    updatePlaybackUi();
}

void SessionWindow::setSkipQuiet(bool on) { m_skipQuiet->setChecked(on); }

void SessionWindow::updatePlaybackUi()
{
    m_actPlay->setText(m_playing ? tr("❚❚ Pause") : tr("▶ Play"));
    m_playClock->setText(m_clock > 0 ? QStringLiteral("%1 · %2")
                                           .arg(QDateTime::fromMSecsSinceEpoch(m_clock).toString(QStringLiteral("HH:mm:ss")),
                                                m_speed->currentText())
                                     : QString());
}

void SessionWindow::startPlayback()
{
    const QString key = currentTabKey();
    const TabUi ui = m_tabUi.value(key);
    auto *proxy = ui.view ? qobject_cast<QSortFilterProxyModel *>(ui.view->model()) : nullptr;
    if (!proxy || proxy->rowCount() == 0) {
        statusBar()->showMessage(tr("Nothing to play in this tab."), 3000);
        return;
    }
    int row = ui.view->currentIndex().isValid() ? ui.view->currentIndex().row() : 0;
    if (row >= proxy->rowCount() - 1) row = 0;   // at the end: from the start again
    ui.view->setCurrentIndex(proxy->index(row, qMax(0, ui.view->currentIndex().column())));
    m_playTab = key;
    m_playRow = row;
    m_clock = ui.model->entryAt(proxy->mapToSource(proxy->index(row, 0)).row())->epochMs;
    m_playing = true;
    m_playWallMs = QDateTime::currentMSecsSinceEpoch();
    m_playTimer->start();
    updatePlaybackUi();
}

void SessionWindow::stopPlayback()
{
    m_playing = false;
    if (m_playTimer) m_playTimer->stop();
    updatePlaybackUi();
}

void SessionWindow::advancePlayback(qint64 wallMs)
{
    if (!m_playing) return;
    const QString key = currentTabKey();
    const TabUi ui = m_tabUi.value(key);
    auto *proxy = ui.view ? qobject_cast<QSortFilterProxyModel *>(ui.view->model()) : nullptr;
    if (!proxy || proxy->rowCount() == 0) { stopPlayback(); return; }
    auto timeAt = [&](int r) { return ui.model->entryAt(proxy->mapToSource(proxy->index(r, 0)).row())->epochMs; };
    const int rows = proxy->rowCount();

    int r = ui.view->currentIndex().isValid() ? ui.view->currentIndex().row() : -1;
    if (key != m_playTab) {
        // Another tab brought forward: play it from the same moment.
        m_playTab = key;
        r = -1;
        while (r + 1 < rows && timeAt(r + 1) <= m_clock) ++r;
    } else if (r != m_playRow && r >= 0) {
        m_clock = timeAt(r);   // the operator moved the cursor: play from there
    }
    m_clock += wallMs * qMax(1, m_speed->currentData().toInt());

    while (r + 1 < rows && timeAt(r + 1) <= m_clock) ++r;
    if (r + 1 < rows && m_skipQuiet->isChecked() && r >= 0) {
        const qint64 quiet = timeAt(r + 1) - timeAt(r);
        if (quiet > kQuietMs && m_clock - timeAt(r) >= kQuietMs) {
            m_clock = timeAt(r + 1);
            while (r + 1 < rows && timeAt(r + 1) <= m_clock) ++r;
            statusBar()->showMessage(tr("Nothing recorded for %1 s: skipped to %2")
                                         .arg((quiet + 500) / 1000)
                                         .arg(QDateTime::fromMSecsSinceEpoch(m_clock).toString(QStringLiteral("HH:mm:ss"))),
                                     4000);
        }
    }
    if (r >= 0 && r != ui.view->currentIndex().row()) {
        ui.view->setCurrentIndex(proxy->index(r, qMax(0, ui.view->currentIndex().column())));
        ui.view->scrollTo(ui.view->currentIndex(), QAbstractItemView::EnsureVisible);
    }
    m_playRow = r;
    if (r == rows - 1) {
        stopPlayback();
        statusBar()->showMessage(tr("End of the recording (%1)").arg(QDateTime::fromMSecsSinceEpoch(timeAt(r))
                                                                        .toString(QStringLiteral("HH:mm:ss"))),
                                 5000);
        return;
    }
    updatePlaybackUi();
}

SessionWindow::~SessionWindow() = default;

void SessionWindow::setReviewPack(const ReviewPack::Opened &pack)
{
    const QJsonObject &m = pack.manifest;
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(6, 6, 6, 6);

    const QString what = m.value(QStringLiteral("what_to_look_at")).toString();
    auto *whatLabel = new QLabel(page);
    whatLabel->setObjectName(QStringLiteral("reviewPackWhat"));
    whatLabel->setWordWrap(true);
    whatLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (pack.isReply()) {
        // Session 384: the reviewer's answer to a pack this site sent.
        const QJsonObject r = m.value(QStringLiteral("in_reply_to")).toObject();
        whatLabel->setText(tr("<b>A reply</b> from %1 to the pack of %2. You asked: <i>%3</i><br><b>Answer:</b> %4")
                               .arg(m.value(QStringLiteral("tester")).toString().toHtmlEscaped(),
                                    r.value(QStringLiteral("created")).toString().toHtmlEscaped(),
                                    r.value(QStringLiteral("what_to_look_at")).toString().toHtmlEscaped(),
                                    what.isEmpty() ? tr("(none given)") : what.toHtmlEscaped()));
    } else {
        whatLabel->setText(what.isEmpty() ? tr("The site gave no note on what to look at.")
                                          : tr("<b>What to look at:</b> %1").arg(what.toHtmlEscaped()));
    }
    lay->addWidget(whatLabel);

    // Who, where, when, with what: the README's lines.
    QStringList rows;
    auto row = [&rows](const QString &label, const QString &value) {
        if (!value.isEmpty()) rows << QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(label, value.toHtmlEscaped());
    };
    row(tr("Site"), m.value(QStringLiteral("site")).toString());
    row(tr("Tester"), m.value(QStringLiteral("tester")).toString());
    row(tr("Loco"), m.value(QStringLiteral("loco")).toString());
    row(tr("Made"), m.value(QStringLiteral("created")).toString());
    const QJsonObject span = m.value(QStringLiteral("span")).toObject();
    row(tr("Span"), span.value(QStringLiteral("from")).toString().isEmpty()
                        ? tr("everything in the tabs sent")
                        : span.value(QStringLiteral("from")).toString() + QStringLiteral(" → ")
                              + span.value(QStringLiteral("to")).toString());
    const QJsonObject prog = m.value(QStringLiteral("program")).toObject();
    row(tr("Program"), QStringLiteral("%1, Qt %2, %3")
                           .arg(QStringLiteral("%1 %2").arg(prog.value(QStringLiteral("name")).toString(),
                                                            prog.value(QStringLiteral("version")).toString()).trimmed(),
                                prog.value(QStringLiteral("qt")).toString(), prog.value(QStringLiteral("os")).toString()));
    // The schema: the same bytes as the one decoding here, or not. A
    // different one is said, not swapped in: which schema decodes is the
    // reader's choice (File ▸ Schema).
    if (!pack.schemaPath.isEmpty()) {
        QFile theirs(pack.schemaPath), ours(Settings::schemaPath().isEmpty() ? QStringLiteral(":/schema/kavach.xml")
                                                                             : Settings::schemaPath());
        const bool same = theirs.open(QIODevice::ReadOnly) && ours.open(QIODevice::ReadOnly) && theirs.readAll() == ours.readAll();
        row(tr("kavach.xml"), same ? tr("the same as the one decoding here")
                                   : tr("DIFFERENT from the one decoding here: %1").arg(QDir::toNativeSeparators(pack.schemaPath)));
    }
    auto *details = new QLabel(QStringLiteral("<table cellspacing=\"4\">%1</table>").arg(rows.join(QString())), page);
    details->setObjectName(QStringLiteral("reviewPackDetails"));
    details->setWordWrap(true);
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(details);
    if (!pack.damaged.isEmpty()) {
        auto *bad = new QLabel(tr("Damaged in the pack, not opened: %1").arg(pack.damaged.join(QStringLiteral(", "))), page);
        bad->setWordWrap(true);
        bad->setStyleSheet(UiColor::warningStyle());
        lay->addWidget(bad);
    }

    if (!pack.attachments.isEmpty()) {
        // The site's attachments, opened by whatever opens them on this machine.
        QStringList links;
        for (const QString &p : pack.attachments)
            links << QStringLiteral("<a href=\"%1\">%2</a>").arg(QUrl::fromLocalFile(p).toString(),
                                                                QFileInfo(p).fileName().toHtmlEscaped());
        auto *att = new QLabel(tr("<b>Attached:</b> %1").arg(links.join(QStringLiteral(", "))), page);
        att->setObjectName(QStringLiteral("reviewPackAttachments"));
        att->setWordWrap(true);
        att->setOpenExternalLinks(true);
        att->setTextInteractionFlags(Qt::TextBrowserInteraction);
        lay->addWidget(att);
    }

    auto *buttons = new QHBoxLayout;
    if (!pack.testRunPath.isEmpty()) {
        auto *run = new QPushButton(tr("Open its test run…"), page);
        run->setObjectName(QStringLiteral("reviewPackRun"));
        run->setToolTip(QDir::toNativeSeparators(pack.testRunPath));
        connect(run, &QPushButton::clicked, this, [this, path = pack.testRunPath]() {
            auto *w = new TestCatalogWindow(this);
            w->reopenLast();
            w->openRun(path);
            w->show();
            w->raise();
        });
        buttons->addWidget(run);
    }
    auto *reply = new QPushButton(pack.isReply() ? tr("Answer the reply…") : tr("Reply to the site…"), page);
    reply->setObjectName(QStringLiteral("reviewPackReply"));
    reply->setToolTip(tr("A pack back: these captures with your notes, your answer and any files you attach."));
    connect(reply, &QPushButton::clicked, this, [this]() {
        if (ReviewPackDialog *d = makeReplyDialog()) {
            d->setAttribute(Qt::WA_DeleteOnClose);
            d->exec();
        }
    });
    buttons->addWidget(reply);
    auto *folder = new QPushButton(tr("Show the files"), page);
    folder->setToolTip(QDir::toNativeSeparators(pack.dir));
    connect(folder, &QPushButton::clicked, this, [dir = pack.dir]() { QDesktopServices::openUrl(QUrl::fromLocalFile(dir)); });
    buttons->addWidget(folder);
    buttons->addStretch(1);
    lay->addLayout(buttons);
    lay->addStretch(1);

    auto *dock = new QDockWidget(tr("Review pack"), this);
    dock->setObjectName(QStringLiteral("sessionReviewPackDock"));
    dock->setWidget(page);
    dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    for (QDockWidget *d : findChildren<QDockWidget *>())
        if (d->objectName() == QLatin1String("sessionFieldDock")) tabifyDockWidget(d, dock);
    dock->raise();   // in front: the site's own words first
    m_packTitle = m.value(QStringLiteral("site")).toString();
    m_pack = pack;
}

void SessionWindow::rememberMade(const QString &path)
{
    if (!m_pack.manifest.isEmpty() && !m_made.contains(path)) m_made << path;
}

ReviewPackDialog *SessionWindow::makeReplyDialog()
{
    if (m_pack.manifest.isEmpty()) return nullptr;
    QStringList keys = m_models.keys();
    keys.sort();
    QVector<ReviewPack::Capture> captures;
    for (const QString &key : keys)
        captures.append({ key, key, m_models.value(key)->snapshot(),
                          m_bookmarks ? m_bookmarks->all() : QVector<Bookmark>() });
    auto *d = new ReviewPackDialog(captures, ReviewPack::replyBase(m_pack), this);
    d->setReply();
    // Session 385: what was made while reviewing goes back with the answer;
    // the reviewer can still Remove any of it.
    for (const QString &p : qAsConst(m_made))
        if (QFileInfo::exists(p)) d->addAttachment(p);
    d->setWho(m_pack.manifest.value(QStringLiteral("site")).toString(), QString(),
              m_pack.manifest.value(QStringLiteral("loco")).toString());
    return d;
}

LogModel *SessionWindow::modelForKey(const QString &tabKey)
{
    auto it = m_models.constFind(tabKey);
    if (it != m_models.constEnd()) return it.value();

    // Capacity 0 would mean "keep nothing"; an archive viewer should hold
    // whatever the file contains, so the cap is generous. A .dlr big enough
    // to exceed this is better handled by loading a narrower time range,
    // which is a future refinement.
    auto *m = new LogModel(this, 5'000'000);
    m->setNameMap(m_names);
    m->setTheme(m_theme);
    m_models.insert(tabKey, m);
    return m;
}

void SessionWindow::buildTab(const QString &tabKey, LogModel *model)
{
    if (m_tabUi.contains(tabKey)) return;

    auto *filterBar = new FilterBar(model);
    filterBar->setNameMap(m_names);

    auto *view = new QTableView;
    view->setModel(filterBar->proxyModel());
    // The live log's table setup (selection, density, the Time column's
    // painter, widths measured to the font, the operator's stored widths
    // and hidden columns), as Compare and Merged use it. The pixel widths
    // this window had (Time 100, Source 60) clipped "16:32:27.1..." and
    // "ime (local".
    LogTableView::configure(view);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(view->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this, &SessionWindow::onSelectionChanged);
    // Session 84: the DMI window, following the cursor, shows this moment of
    // the recording. Every tab of this session is searched, none of the live
    // console's: a recording is its own timeline.
    connect(view->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this, tabKey](const QModelIndex &cur, const QModelIndex &) { offerDmiAt(tabKey, cur); });

    view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view, &QWidget::customContextMenuRequested,
            this, &SessionWindow::onRowContextMenu);

    // The same find bar the live window has, with the same shape preference:
    // Ctrl+F opens it as a floating window or as a bar in the tab, whichever
    // the operator used last.
    auto *findBar = new FindBar(view);
    findBar->setContextLabel(m_names ? m_names->lookupByKey(tabKey) : tabKey);

    // Session 348: the live window's density strip (by time) and severity
    // marks on the scrollbar (by row). Click the strip to land on the
    // nearest row; drag it to filter that span.
    auto *ribbon = new TimelineRibbon;
    ribbon->setModel(model);
    auto *marks = new MarkerScrollBar;
    marks->setModels(filterBar->proxyModel(), model);
    view->setVerticalScrollBar(marks);
    connect(ribbon, &TimelineRibbon::timeClicked, this,
            [this, tabKey](qint64 ms) { jumpToTime(tabKey, ms); });
    connect(ribbon, &TimelineRibbon::rangeSelected, filterBar, [filterBar](qint64 from, qint64 to) {
        filterBar->setQuery(GotoTimestamp::rangeQuery(from, to));
    });

    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(ribbon);
    layout->addWidget(filterBar);
    layout->addWidget(findBar);
    layout->addWidget(view, 1);

    auto *container = new QWidget;
    container->setLayout(layout);

    const QString friendly = m_names ? m_names->lookupByKey(tabKey) : tabKey;
    m_tabs->addTab(container, friendly);

    TabUi ui;
    ui.view      = view;
    ui.filterBar = filterBar;
    ui.model     = model;
    ui.findBar   = findBar;
    ui.ribbon    = ribbon;
    ui.marks     = marks;
    m_tabUi.insert(tabKey, ui);
}

qint64 SessionWindow::loadFiles(const QStringList &paths)
{
    m_warnings.clear();
    qint64 grandTotal = 0;
    qint64 skipped    = 0;

    m_merge = NotesSidecar::Merge();
    QApplication::setOverrideCursor(Qt::WaitCursor);

    for (const QString &path : paths) {
        SessionReader reader;
        if (!reader.open(path)) {
            m_warnings << tr("%1: %2")
                              .arg(QFileInfo(path).fileName(), reader.errorString());
            continue;
        }

        const QString key   = reader.tabKey();
        LogModel     *model = modelForKey(key);

        // Accumulate into a batch and insert in chunks. A single
        // appendEntries() for a million rows would build one enormous
        // begin/endInsertRows span; chunking keeps each model notification
        // a sane size while still being far cheaper than per-row inserts.
        constexpr int kChunk = 4096;
        QVector<LogEntryPtr> batch;
        batch.reserve(kChunk);

        m_loadedNames << QFileInfo(path).fileName();
        LoadedFile loaded;
        loaded.path = path;
        loaded.tabKey = key;
        while (reader.next()) {
            LogEntryPtr e = MessageDispatcher::buildEntry(
                reader.wire(), reader.arrivalMs(), m_rules);
            if (!e) { ++skipped; continue; }
            if (!loaded.firstMs || e->epochMs < loaded.firstMs) loaded.firstMs = e->epochMs;
            loaded.lastMs = qMax(loaded.lastMs, e->epochMs);

            batch.append(e);
            if (batch.size() >= kChunk) {
                model->appendEntries(batch);
                batch.clear();
                // Keep the UI answering while a long file loads. This is a
                // read-only window with no live feed, so re-entrancy here
                // is safe — there is no ingest path that could mutate these
                // models underneath us.
                QApplication::processEvents();
            }
        }
        if (!batch.isEmpty()) model->appendEntries(batch);

        grandTotal += reader.recordsRead();
        m_files.append(loaded);

        // Session 369: the notes the site made, from the file beside it.
        // Into the shared store (the reader's own notes are never
        // overwritten), before the marks are painted below.
        QString notesErr;
        const QVector<Bookmark> sideMarks = NotesSidecar::read(path, &notesErr);
        if (!notesErr.isEmpty()) m_warnings << notesErr;
        if (!sideMarks.isEmpty()) {
            if (m_bookmarks) {
                const NotesSidecar::Merge m = NotesSidecar::mergeInto(m_bookmarks, sideMarks);
                m_merge.added += m.added;
                m_merge.filled += m.filled;
                m_merge.differ += m.differ;
            } else {
                m_warnings << tr("%1: %2 note(s) beside it not shown (no bookmark store)")
                                  .arg(QFileInfo(path).fileName()).arg(sideMarks.size());
            }
        }

        // A truncated tail is expected often enough that it is reported as
        // information, not failure: it is what a file looks like when the
        // app was killed or the archive was copied while still being
        // written. Every record before the tear is present and valid.
        switch (reader.status()) {
        case SessionReader::TruncatedTail:
            m_warnings << tr("%1: ends mid-record (app was interrupted while "
                             "writing); %2 complete records recovered")
                              .arg(QFileInfo(path).fileName())
                              .arg(reader.recordsRead());
            break;
        case SessionReader::Corrupt:
            m_warnings << tr("%1: stopped early — %2; %3 records recovered")
                              .arg(QFileInfo(path).fileName(),
                                   reader.errorString())
                              .arg(reader.recordsRead());
            break;
        default:
            break;
        }

        if (!m_tabUi.contains(key)) buildTab(key, model);
        const TabUi &ui = m_tabUi[key];
        if (ui.ribbon) { ui.ribbon->refreshLater(); }
        if (ui.marks)  { ui.marks->refreshLater(); }
    }

    // Re-apply stored bookmarks to what was just loaded. The flag lives on
    // LogEntry for cheap painting but the truth lives in the store, and these
    // entries were built a moment ago from a file — without this, a row
    // bookmarked in a previous sitting comes back unmarked and F2 walks past
    // it.
    if (m_bookmarks) {
        for (auto it = m_models.constBegin(); it != m_models.constEnd(); ++it) {
            m_bookmarks->applyToModel(it.value(), it.key());
        }
    }

    QApplication::restoreOverrideCursor();
    rebuildSummary();

    QString summary = tr("%1 from %2")
                          .arg(countOf(grandTotal, "record", "records"),
                               countOf(paths.size(), "file", "files"));
    if (skipped > 0) {
        summary += tr("  ·  %1 skipped").arg(countOf(skipped, "undersized record", "undersized records"));
    }
    if (!m_warnings.isEmpty()) {
        summary += tr("  ·  %1").arg(countOf(m_warnings.size(), "warning", "warnings"));
        m_lblSummary->setToolTip(m_warnings.join('\n'));
        m_lblSummary->setStyleSheet(UiColor::warningStyle());
    } else {
        m_lblSummary->setToolTip(QString());
        m_lblSummary->setStyleSheet(QString());
    }
    m_lblSummary->setText(summary);

    if (!m_packTitle.isEmpty()) {
        setWindowTitle(tr("Review pack — %1").arg(m_packTitle));
    } else if (paths.size() == 1) {
        setWindowTitle(tr("Recorded session — %1")
                           .arg(QFileInfo(paths.first()).fileName()));
    } else {
        setWindowTitle(tr("Recorded session — %1 files").arg(paths.size()));
    }

    // Session 373: the test cases, over everything now loaded.
    if (!m_tests->assertions().isEmpty()) runTestCases();

    // Session 369: say what came in from the notes files, and what did not.
    const int fromFiles = m_merge.added + m_merge.filled;
    if (fromFiles || m_merge.differ) {
        QString notes = tr("From the notes beside the recording: %1").arg(countOf(fromFiles, "mark", "marks"));
        if (m_merge.differ)
            notes += tr("; %1 your own, kept").arg(countOf(m_merge.differ, "note differs from", "notes differ from"));
        statusBar()->showMessage(notes, 15000);
    }

    return grandTotal;
}

void SessionWindow::writeSidecars(const QString &tabKey)
{
    if (!m_bookmarks) return;
    for (const LoadedFile &f : qAsConst(m_files)) {
        if (f.tabKey != tabKey) continue;
        QString err;
        if (!NotesSidecar::write(f.path, NotesSidecar::marksIn(m_bookmarks->all(), tabKey, f.firstMs, f.lastMs), &err))
            statusBar()->showMessage(tr("The notes were kept, but not written beside %1: %2")
                                         .arg(QFileInfo(f.path).fileName(), err), 8000);
    }
}

// ---------------------------------------------------------------------------
//  Tools
//
//  The live window's Inspect and Transmit tools, on archived rows. Nothing
//  here is new code: the same DecodeWorkbench, PacketMakerDialog and
//  FrameDiffWindow, handed the same entryBufferText() the live log hands
//  them, so a frame read out of a recording decodes exactly as it did when
//  it arrived.
//
//  Not offered here, deliberately:
//    * Field Sweep and Packet Sequence — those transmit repeatedly against
//      live equipment. Starting one from a window whose whole premise is
//      "this is yesterday" invites doing it by accident.
//    * The round-trip validator — it already reads .dlr files directly, so
//      an entry point here would be a second route to the same thing.
// ---------------------------------------------------------------------------

void SessionWindow::buildToolsMenu()
{
    // Find lives in its own menu rather than under Tools: it is not a tool
    // you open, it is how you move around the thing already on screen, and
    // that is where an operator looks for it.
    auto *edit = menuBar()->addMenu(tr("&Edit"));
    QAction *find = edit->addAction(tr("&Find…"));
    find->setShortcut(QKeySequence::Find);          // Ctrl+F
    connect(find, &QAction::triggered, this, &SessionWindow::onFind);

    QAction *goTo = edit->addAction(tr("&Go to timestamp…"));
    goTo->setShortcut(QKeySequence("Ctrl+G"));
    connect(goTo, &QAction::triggered, this, &SessionWindow::onGotoTimestamp);

    // Session 348: the live window's Back/Forward, over this window's jumps.
    m_actBack = edit->addAction(tr("Bac&k"));
    m_actBack->setShortcut(QKeySequence("Alt+Left"));
    m_actBack->setToolTip(tr("Back to where you were before the last jump "
                             "(Go to timestamp, next problem, a bookmark, the timeline)."));
    m_actBack->setEnabled(false);
    connect(m_actBack, &QAction::triggered, this, [this]() { navigate(true); });
    m_actForward = edit->addAction(tr("For&ward"));
    m_actForward->setShortcut(QKeySequence("Alt+Right"));
    m_actForward->setEnabled(false);
    connect(m_actForward, &QAction::triggered, this, [this]() { navigate(false); });

    edit->addSeparator();

    // The same keys as the live window. An operator who learns F4 watching
    // traffic should not have to learn something else to read it back.
    QAction *nextProb = edit->addAction(tr("Next &problem"));
    nextProb->setShortcut(QKeySequence("F4"));
    connect(nextProb, &QAction::triggered, this, &SessionWindow::onNextProblem);

    QAction *prevProb = edit->addAction(tr("Previous pro&blem"));
    prevProb->setShortcut(QKeySequence("Shift+F4"));
    connect(prevProb, &QAction::triggered, this, &SessionWindow::onPrevProblem);

    auto *bm = edit->addMenu(tr("&Bookmarks"));
    QAction *tog = bm->addAction(tr("Toggle &bookmark"));
    tog->setShortcut(QKeySequence("Ctrl+B"));
    connect(tog, &QAction::triggered, this, &SessionWindow::onToggleBookmark);

    QAction *nextBm = bm->addAction(tr("Next bookmark"));
    nextBm->setShortcut(QKeySequence("F2"));
    connect(nextBm, &QAction::triggered, this, &SessionWindow::onNextBookmark);

    QAction *prevBm = bm->addAction(tr("Previous bookmark"));
    prevBm->setShortcut(QKeySequence("Shift+F2"));
    connect(prevBm, &QAction::triggered, this, &SessionWindow::onPrevBookmark);

    auto *tools = menuBar()->addMenu(tr("&Tools"));

    QAction *a = tools->addAction(tr("&Decode Workbench…"));
    a->setShortcut(QKeySequence("Ctrl+Shift+D"));
    connect(a, &QAction::triggered, this, &SessionWindow::onToolWorkbench);

    a = tools->addAction(tr("Selected row → Decode Workbenc&h"));
    a->setShortcut(QKeySequence("Ctrl+Shift+B"));
    connect(a, &QAction::triggered, this, &SessionWindow::onToolRowToWorkbench);

    a = tools->addAction(tr("Frame &Diff…"));
    connect(a, &QAction::triggered, this, &SessionWindow::onToolFrameDiff);

    a = tools->addAction(tr("&Plot field over time…"));
    a->setShortcut(QKeySequence("Ctrl+Shift+P"));
    connect(a, &QAction::triggered, this, &SessionWindow::onToolPlotField);

    // Session 372: what Tools ▸ Monitor ▸ Running and Reports open live,
    // over the tab on show. (Braking Curves and the Two-loco view read the
    // live dispatcher, not a tab: not here.)
    tools->addSeparator();
    const struct { const char *text; const char *name; ReviewTool tool; } reviewTools[] = {
        { QT_TR_NOOP("&Run summary report…"),       "sessionRunReport",     ReviewTool::RunReport },
        { QT_TR_NOOP("&Mission report…"),           "sessionMissionReport", ReviewTool::MissionReport },
        { QT_TR_NOOP("Speed vs &distance…"),        "sessionSpeedDistance", ReviewTool::SpeedDistance },
        { QT_TR_NOOP("Radio and GPS &health…"),     "sessionRadioHealth",   ReviewTool::RadioHealth },
        { QT_TR_NOOP("&Odometry at the tags…"),     "sessionOdometry",      ReviewTool::Odometry },
        { QT_TR_NOOP("&Fault timeline…"),           "sessionFaultTimeline", ReviewTool::FaultTimeline },
        { QT_TR_NOOP("&Cab inputs and outputs…"),   "sessionCabIo",         ReviewTool::CabIo },
        { QT_TR_NOOP("Trac&k diagram…"),            "sessionTrackDiagram",  ReviewTool::TrackDiagram },
    };
    for (const auto &t : reviewTools) {
        a = tools->addAction(tr(t.text));
        a->setObjectName(QLatin1String(t.name));
        const ReviewTool which = t.tool;
        connect(a, &QAction::triggered, this, [this, which]() { openReviewTool(which); });
    }
    tools->addSeparator();

    // Session 375: the moments the DMI changed.
    a = tools->addAction(tr("What changed on the D&MI…"));
    a->setObjectName(QStringLiteral("sessionDmiChanges"));
    a->setToolTip(tr("Each change on a loco's DMI (mode, brake, target, signal, messages, tag), frame to frame"));
    connect(a, &QAction::triggered, this, &SessionWindow::showDmiChanges);

    // Session 374: this run beside another one.
    a = tools->addAction(tr("Compare with another r&un…"));
    a->setObjectName(QStringLiteral("sessionCompareRuns"));
    a->setToolTip(tr("This recording and another run of the same scenario, played in step, the DMIs side by side"));
    connect(a, &QAction::triggered, this, [this]() { compareWithAnotherRun(); });

    // Session 368: the incident pack, over a recording sent from the site.
    a = tools->addAction(tr("&Incident report…"));
    a->setObjectName(QStringLiteral("sessionIncidentReport"));
    a->setToolTip(tr("The incident pack around the selected row (or the tab's last), "
                     "with this recording's notes"));
    connect(a, &QAction::triggered, this, &SessionWindow::onIncidentReport);

    tools->addSeparator();

    // Under a separator and named for what it does. This is the one entry
    // here that leads somewhere that can transmit, and a recording is the
    // last place an operator expects to find a send button.
    a = tools->addAction(tr("Selected row → Packet Ma&ker…"));
    connect(a, &QAction::triggered, this, &SessionWindow::onToolRowToPacketMaker);
}

QString SessionWindow::currentTabKey() const
{
    const int idx = m_tabs ? m_tabs->currentIndex() : -1;
    if (idx < 0) { return QString(); }
    QWidget *page = m_tabs->widget(idx);
    for (auto it = m_tabUi.constBegin(); it != m_tabUi.constEnd(); ++it) {
        if (it->view && it->view->window() == window()
            && it->view->parentWidget() == page) {
            return it.key();
        }
    }
    // The view is nested one level inside the page's layout container.
    for (auto it = m_tabUi.constBegin(); it != m_tabUi.constEnd(); ++it) {
        if (it->view && page && page->isAncestorOf(it->view)) { return it.key(); }
    }
    return QString();
}

LogEntryPtr SessionWindow::entryFor(const TabUi &t, const QModelIndex &proxyIndex) const
{
    if (!t.model || !proxyIndex.isValid()) { return LogEntryPtr(); }
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(t.view->model());
    const QModelIndex src = proxy ? proxy->mapToSource(proxyIndex) : proxyIndex;
    if (!src.isValid()) { return LogEntryPtr(); }
    return t.model->entryAt(src.row());
}

LogEntryPtr SessionWindow::currentEntry() const
{
    const QString key = currentTabKey();
    if (key.isEmpty() || !m_tabUi.contains(key)) { return LogEntryPtr(); }
    const TabUi &t = m_tabUi[key];
    if (!t.view) { return LogEntryPtr(); }
    return entryFor(t, t.view->currentIndex());
}

QVector<LogEntryPtr> SessionWindow::selectedEntries() const
{
    QVector<LogEntryPtr> out;
    const QString key = currentTabKey();
    if (key.isEmpty() || !m_tabUi.contains(key)) { return out; }
    const TabUi &t = m_tabUi[key];
    if (!t.view || !t.view->selectionModel()) { return out; }

    // Column 0 only, or a row selection yields one index per column.
    const QModelIndexList rows = t.view->selectionModel()->selectedRows();
    for (const QModelIndex &idx : rows) {
        if (const LogEntryPtr e = entryFor(t, idx)) { out.push_back(e); }
    }
    return out;
}

void SessionWindow::jumpToProxyRow(int row)
{
    const QString key = currentTabKey();
    if (row < 0 || key.isEmpty() || !m_tabUi.contains(key)) { return; }
    QTableView *view = m_tabUi[key].view;
    if (!view || !view->model()) { return; }
    if (!m_navReplaying) { m_nav.record(currentNavPoint()); }
    m_actBack->setEnabled(m_nav.canBack());
    m_actForward->setEnabled(m_nav.canForward());
    const QModelIndex landing = view->model()->index(row, LogModel::ColTime);
    view->setCurrentIndex(landing);
    view->scrollTo(landing, QAbstractItemView::PositionAtCenter);
    view->setFocus();
}

void SessionWindow::rebuildSummary()
{
    if (!m_summary) { return; }
    // "12.3 s", "4:05", "1:02:03".
    const auto span = [](qint64 ms) {
        if (ms < 60000) { return QStringLiteral("%1 s").arg(ms / 1000.0, 0, 'f', 1); }
        const qint64 s = ms / 1000;
        const QString ms_ = QStringLiteral("%1:%2").arg(s / 60 % 60, s >= 3600 ? 2 : 1, 10, QLatin1Char('0'))
                                                   .arg(s % 60, 2, 10, QLatin1Char('0'));
        return s >= 3600 ? QStringLiteral("%1:%2").arg(s / 3600).arg(ms_) : ms_;
    };
    const auto clock = [](qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss.zzz")); };
    const auto row = [](QTreeWidgetItem *parent, const QString &a, const QString &b) {
        return new QTreeWidgetItem(parent, { a, b });
    };

    m_summary->clear();
    for (int i = 0; i < m_tabs->count(); ++i) {   // in tab order
        QString key;
        for (auto it = m_tabUi.constBegin(); it != m_tabUi.constEnd(); ++it)
            if (it->view && m_tabs->widget(i)->isAncestorOf(it->view)) { key = it.key(); }
        if (key.isEmpty()) { continue; }
        const SessionSummary::Source s = SessionSummary::summarize(m_tabUi[key].model, m_gapOver->value() * 1000LL);
        auto *top = new QTreeWidgetItem(m_summary, { m_tabs->tabText(i), countOf(s.records, "record", "records") });
        if (s.firstMs > 0) {
            const qint64 len = s.lastMs - s.firstMs;
            row(top, tr("Start"), clock(s.firstMs));
            row(top, tr("End"), clock(s.lastMs));
            row(top, tr("Length"), span(len));
            row(top, tr("Rate"), len > 0 ? tr("%1 /s").arg(s.records * 1000.0 / len, 0, 'f', 2) : QStringLiteral("—"));
        }
        row(top, tr("Errors"), QString::number(s.errors));
        row(top, tr("Warnings"), QString::number(s.warnings));
        if (s.longest.ms() > 0) {
            QTreeWidgetItem *l = row(top, tr("Longest gap"), tr("%1 after %2").arg(span(s.longest.ms()), clock(s.longest.fromMs)));
            l->setData(0, Qt::UserRole, key);
            l->setData(0, Qt::UserRole + 1, s.longest.fromMs);
        }
        QTreeWidgetItem *quiet = row(top, tr("Quiet spans"), QString::number(s.gaps.size()));
        if (!s.gaps.isEmpty()) { quiet->setForeground(1, UiColor::warning()); }
        for (const SessionSummary::Gap &g : s.gaps) {
            QTreeWidgetItem *it = row(quiet, clock(g.fromMs), tr("quiet %1").arg(span(g.ms())));
            it->setToolTip(0, tr("Double-click: the last row before it went quiet."));
            it->setData(0, Qt::UserRole, key);
            it->setData(0, Qt::UserRole + 1, g.fromMs);
        }
        top->setExpanded(true);
    }
    m_summary->resizeColumnToContents(0);
}

NavPoint SessionWindow::currentNavPoint() const
{
    const LogEntryPtr e = currentEntry();
    return e ? NavPoint{ currentTabKey(), e->epochMs } : NavPoint{};
}

void SessionWindow::jumpToTime(const QString &tabKey, qint64 epochMs)
{
    const TabUi t = m_tabUi.value(tabKey);
    if (!t.view || !t.model) { return; }
    const int row = GotoTimestamp::resolveRow(t.view->model(), t.model, epochMs,
                                              int(GotoTimestampDialog::Mode::Nearest));
    if (row < 0) { return; }
    // Recorded here, before the tab switch, so Back returns to the tab it left.
    const bool replaying = m_navReplaying;
    if (!replaying) { m_nav.record(currentNavPoint()); }
    m_navReplaying = true;
    m_tabs->setCurrentWidget(t.view->parentWidget());
    jumpToProxyRow(row);
    m_navReplaying = replaying;
}

void SessionWindow::navigate(bool back)
{
    const NavPoint to = back ? m_nav.back(currentNavPoint()) : m_nav.forward(currentNavPoint());
    if (!to.isValid()) {
        statusBar()->showMessage(back ? tr("Nothing to go back to.")
                                      : tr("Nothing to go forward to."), 3000);
        return;
    }
    m_navReplaying = true;
    jumpToTime(to.tabKey, to.epochMs);
    m_navReplaying = false;
}

void SessionWindow::stepProblem(int dir)
{
    const QString key = currentTabKey();
    if (key.isEmpty() || !m_tabUi.contains(key)) { return; }
    const TabUi &t = m_tabUi[key];
    if (!t.view || !t.model || !t.view->model()) { return; }

    const int rows = t.view->model()->rowCount();
    const int from = t.view->currentIndex().isValid() ? t.view->currentIndex().row()
                                                      : (dir > 0 ? -1 : rows);
    // The same walk the live window uses, and the same marks the minimap
    // draws — one code path, so the two cannot disagree about what counts.
    const int row = nextMarkedRow(t.view->model(), t.model, from, dir);
    if (row < 0) {
        statusBar()->showMessage(dir > 0
            ? tr("No further errors or warnings below this row.")
            : tr("No errors or warnings above this row."), 3000);
        return;
    }
    jumpToProxyRow(row);
}

void SessionWindow::stepBookmark(int dir)
{
    const QString key = currentTabKey();
    if (key.isEmpty() || !m_tabUi.contains(key)) { return; }
    const TabUi &t = m_tabUi[key];
    if (!t.view || !t.model || !t.view->model()) { return; }

    const int rows = t.view->model()->rowCount();
    const int from = t.view->currentIndex().isValid() ? t.view->currentIndex().row()
                                                      : (dir > 0 ? -1 : rows);
    const int row = nextBookmarkedRow(t.view->model(), t.model, from, dir);
    if (row < 0) {
        statusBar()->showMessage(dir > 0 ? tr("No further bookmarks in this tab.")
                                         : tr("No earlier bookmarks in this tab."), 3000);
        return;
    }
    jumpToProxyRow(row);
}

void SessionWindow::onNextProblem() { stepProblem(+1); }
void SessionWindow::onPrevProblem() { stepProblem(-1); }
void SessionWindow::onNextBookmark() { stepBookmark(+1); }
void SessionWindow::onPrevBookmark() { stepBookmark(-1); }

void SessionWindow::onToggleBookmark()
{
    const QString key = currentTabKey();
    if (key.isEmpty() || !m_tabUi.contains(key)) { return; }
    const TabUi &t = m_tabUi[key];
    if (!t.view || !t.model) { return; }

    const QModelIndex cur = t.view->currentIndex();
    if (!cur.isValid()) {
        statusBar()->showMessage(tr("Select a row first, then Ctrl+B."), 3000);
        return;
    }
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(t.view->model());
    const QModelIndex src = proxy ? proxy->mapToSource(cur) : cur;
    LogEntryPtr e = t.model->entryAt(src.row());
    if (!e) { return; }

    // The paint flag is set either way; the store only remembers it across
    // openings, and a window without one still marks rows for this sitting.
    const bool added = m_bookmarks ? m_bookmarks->toggle(key, e->epochMs, e->text)
                                   : !e->bookmarked;
    e->bookmarked = added;

    const QModelIndex tl = t.model->index(src.row(), LogModel::ColTime);
    emit t.model->dataChanged(tl, tl, { Qt::DisplayRole });
    if (t.marks)  { t.marks->refreshLater(); }
    if (t.ribbon) { t.ribbon->refreshLater(); }
    statusBar()->showMessage(added ? tr("Bookmarked.") : tr("Bookmark removed."), 2000);
    writeSidecars(key);   // session 369
}

void SessionWindow::onGotoTimestamp()
{
    const QString key = currentTabKey();
    if (key.isEmpty() || !m_tabUi.contains(key)) { return; }
    const TabUi &t = m_tabUi[key];
    if (!t.view || !t.model) { return; }

    auto *proxy = qobject_cast<QSortFilterProxyModel *>(t.view->model());
    if (!proxy || proxy->rowCount() <= 0) {
        statusBar()->showMessage(tr("No records in this tab to go to."), 3000);
        return;
    }

    const GotoTimestamp::Span span = GotoTimestamp::spanOf(proxy, t.model);
    if (!span.ok) { return; }

    qint64 seedMs = span.minMs;
    const int topRow = t.view->rowAt(0);
    if (topRow >= 0) {
        const qint64 topMs = GotoTimestamp::epochAtProxyRow(proxy, t.model, topRow);
        if (topMs >= 0) { seedMs = topMs; }
    }

    GotoTimestampDialog dlg(seedMs, span.minMs, span.maxMs, this);
    if (dlg.exec() != QDialog::Accepted) { return; }

    const int row = GotoTimestamp::resolveRow(proxy, t.model, dlg.targetMs(),
                                              int(dlg.mode()));
    if (row < 0) { return; }
    jumpToProxyRow(row);

    const qint64 landedMs = GotoTimestamp::epochAtProxyRow(proxy, t.model, row);
    statusBar()->showMessage(tr("Jumped to %1  (row %2 of %3)")
                                 .arg(QDateTime::fromMSecsSinceEpoch(landedMs)
                                          .toString(QStringLiteral("HH:mm:ss.zzz")))
                                 .arg(row + 1).arg(proxy->rowCount()), 4000);
}

void SessionWindow::onFind()
{
    const QString key = currentTabKey();
    if (key.isEmpty() || !m_tabUi.contains(key)) { return; }
    if (FindBar *f = m_tabUi[key].findBar) { f->activate(); }
}

void SessionWindow::onToolWorkbench()
{
    if (!m_workbench) {
        m_workbench = new DecodeWorkbench(this, m_sessionKeys);
        m_workbench->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_workbench->show();
    m_workbench->raise();
    m_workbench->activateWindow();
}

void SessionWindow::onToolRowToWorkbench()
{
    const QString buf = entryBufferText(currentEntry());
    if (buf.isEmpty()) {
        statusBar()->showMessage(tr("That row carries no bytes to decode."), 4000);
        return;
    }
    onToolWorkbench();
    m_workbench->loadBuffer(buf);
}

void SessionWindow::onToolRowToPacketMaker()
{
    const QString buf = entryBufferText(currentEntry());
    if (buf.isEmpty()) {
        statusBar()->showMessage(tr("That row carries no bytes to load."), 4000);
        return;
    }
    // A maker that is mid-send is doing something the operator started and is
    // watching; overwriting its fields would change what goes on the wire with
    // no warning. That one gets a window of its own.
    if (m_packetMaker && m_packetMaker->isSending()) { m_packetMaker = nullptr; }
    if (!m_packetMaker) {
        m_packetMaker = new PacketMakerDialog(this, m_frameWatch, m_sessionKeys);
        m_packetMaker->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_packetMaker->loadBuffer(buf);
    m_packetMaker->show();
    m_packetMaker->raise();
    m_packetMaker->activateWindow();
}

void SessionWindow::onToolFrameDiff()
{
    if (!m_frameDiff) {
        m_frameDiff = new FrameDiffWindow(this);
        m_frameDiff->setAttribute(Qt::WA_DeleteOnClose);
    }
    // Two selected rows fill both sides; anything else opens the window empty
    // and lets the operator paste, which is what it is for.
    const QVector<LogEntryPtr> sel = selectedEntries();
    if (sel.size() >= 2 && sel.size() <= FrameDiffWindow::maxFrames()) {
        m_frameDiff->setEntries(sel);
    } else if (!sel.isEmpty()) {
        statusBar()->showMessage(
            tr("Select between two and %1 rows to compare them; opening empty.")
                .arg(FrameDiffWindow::maxFrames()), 4000);
    }
    m_frameDiff->show();
    m_frameDiff->raise();
    m_frameDiff->activateWindow();
}

void SessionWindow::onToolPlotField()
{
    const QString key = currentTabKey();
    LogModel *model = key.isEmpty() ? nullptr : m_models.value(key, nullptr);
    if (!model || model->count() == 0) {
        statusBar()->showMessage(tr("Select a tab with records first."), 4000);
        return;
    }
    // One window per invocation: comparing two fields side by side is the
    // usual follow-up, and that needs two open at once.
    auto *plot = new FieldPlotWindow(model, key, this);
    plot->setAttribute(Qt::WA_DeleteOnClose);
    plot->show();
    plot->raise();
}

void SessionWindow::offerDmiAt(const QString &tabKey, const QModelIndex &proxyIndex)
{
    const auto it = m_tabUi.constFind(tabKey);
    if (!proxyIndex.isValid() || it == m_tabUi.constEnd() || !it->model || !it->filterBar) return;
    const QModelIndex src = it->filterBar->proxyModel()->mapToSource(proxyIndex);
    if (!src.isValid()) return;
    LogModel *model = it->model;
    const QString name = m_names ? m_names->lookupByKey(tabKey) : tabKey;
    DmiTimeTravel::instance()->offer(
        this, dmiTabResolver(m_models.values().toVector(), model, model->entryAt(src.row()), src.row(),
                             tr("recording, tab %1").arg(name.isEmpty() ? tabKey : name)));
}

void SessionWindow::onRowContextMenu(const QPoint &pos)
{
    auto *view = qobject_cast<QTableView *>(sender());
    if (!view) { return; }

    const QModelIndex under = view->indexAt(pos);
    if (under.isValid() && !view->selectionModel()->isSelected(under)) {
        view->setCurrentIndex(under);
    }

    const QVector<LogEntryPtr> sel = selectedEntries();
    const bool haveBytes = !entryBufferText(currentEntry()).isEmpty();

    QMenu menu(this);
    QAction *wb = menu.addAction(tr("Open in Decode Workbench"));
    wb->setEnabled(haveBytes);
    connect(wb, &QAction::triggered, this, &SessionWindow::onToolRowToWorkbench);

    QAction *diff = menu.addAction(
        sel.size() > 2 ? tr("Compare these %1 frames").arg(sel.size())
                       : tr("Compare these two frames"));
    diff->setEnabled(sel.size() >= 2 && sel.size() <= FrameDiffWindow::maxFrames());
    connect(diff, &QAction::triggered, this, &SessionWindow::onToolFrameDiff);

    menu.addSeparator();
    QAction *pm = menu.addAction(tr("Open in Packet Maker"));
    pm->setEnabled(haveBytes);
    connect(pm, &QAction::triggered, this, &SessionWindow::onToolRowToPacketMaker);

    // Session 158: an @dmi row, on the panel as it stood then (this
    // recording's timeline: no live dispatcher feeds that window).
    const LogEntryPtr cur = currentEntry();
    if (cur && !dmiKeyOfText(cur->text).isEmpty()) {
        menu.addSeparator();
        QAction *dmi = menu.addAction(tr("Show on DMI"));
        dmi->setObjectName(QStringLiteral("rowShowOnDmi"));
        connect(dmi, &QAction::triggered, this, [this, view]() {
            for (auto it = m_tabUi.constBegin(); it != m_tabUi.constEnd(); ++it) {
                if (it->view == view) offerDmiAt(it.key(), view->currentIndex());
            }
            DmiWindow::showFollowing(this, nullptr);
        });
    }

    // Session 368: the live window's review tools, on this recording.
    QString key;
    for (auto it = m_tabUi.constBegin(); it != m_tabUi.constEnd(); ++it)
        if (it->view == view) key = it.key();
    if (cur && !key.isEmpty()) {
        menu.addSeparator();
        QAction *note = menu.addAction(noteOf(key, cur).isEmpty() && !cur->bookmarked
                                           ? tr("Add &note…") : tr("Edit &note…"));
        note->setObjectName(QStringLiteral("rowNote"));
        connect(note, &QAction::triggered, this, [this, key, cur]() {
            bool ok = false;
            const QString text = QInputDialog::getText(
                this, tr("Note on this row"),
                tr("Note (kept with the bookmark, and in the incident pack):"),
                QLineEdit::Normal, noteOf(key, cur), &ok);
            if (ok) setRowNote(key, cur, text);
        });
        QAction *clip = menu.addAction(tr("DMI c&lip around here…"));
        clip->setObjectName(QStringLiteral("rowDmiClip"));
        clip->setToolTip(tr("The DMI as the loco pilot saw it, ±30 s around this row, as an animated GIF"));
        connect(clip, &QAction::triggered, this, [this, key, cur]() {
            QString why;
            DmiClipDialog *dlg = makeClipDialog(key, cur, &why);
            if (!dlg) { statusBar()->showMessage(why, 5000); return; }
            dlg->exec();
            delete dlg;
        });
        QAction *radio = menu.addAction(tr("Radio ch&art around here…"));
        radio->setObjectName(QStringLiteral("rowRadioChart"));
        radio->setToolTip(tr("SLRP, LSRP and ARP ±30 s around this row as a sequence diagram (PNG or SVG)"));
        connect(radio, &QAction::triggered, this, [this, key, cur]() {
            RadioChartDialog *dlg = makeRadioChart(key, cur->epochMs);
            if (!dlg) return;
            dlg->exec();
            delete dlg;
        });
        QAction *pack = menu.addAction(tr("&Incident report around here…"));
        pack->setObjectName(QStringLiteral("rowIncidentReport"));
        connect(pack, &QAction::triggered, this, [this, key, cur]() {
            const LogModel *m = m_models.value(key);
            IncidentReportDialog dlg(cur->epochMs, m->entryAt(0)->epochMs,
                                     m->entryAt(m->count() - 1)->epochMs, this);
            if (dlg.exec() != QDialog::Accepted) return;
            openIncidentReport(key, dlg.atMs(), dlg.beforeMs(), dlg.afterMs());
        });
    }

    menu.exec(view->viewport()->mapToGlobal(pos));
}

QString SessionWindow::tabName(const QString &tabKey) const
{
    const QString name = m_names ? m_names->lookupByKey(tabKey) : QString();
    return name.isEmpty() ? tabKey : name;
}

DmiClipDialog *SessionWindow::makeClipDialog(const QString &tabKey, const LogEntryPtr &entry, QString *why)
{
    if (!entry) { if (why) *why = tr("Select a row first."); return nullptr; }
    // The row's tab when it holds @dmi, else the first that does: the clip
    // is of the moment, wherever its DMI frames were recorded.
    QString key = tabKey;
    LogModel *model = m_models.value(tabKey);
    if (!model || DmiClip::locosIn(model).isEmpty()) {
        model = nullptr;
        for (int i = 0; i < m_tabs->count() && !model; ++i) {
            for (auto it = m_tabUi.constBegin(); it != m_tabUi.constEnd(); ++it) {
                if (it->view && it->view->parentWidget() == m_tabs->widget(i)
                    && !DmiClip::locosIn(it->model).isEmpty()) {
                    model = it->model;
                    key = it.key();
                    break;
                }
            }
        }
    }
    if (!model) {
        if (why) *why = tr("This recording holds no @dmi frames: no DMI to clip.");
        return nullptr;
    }
    auto *d = new DmiClipDialog(model, tabName(key), entry->epochMs, dmiKeyOfText(entry->text), this);
    connect(d, &DmiClipDialog::exported, this, &SessionWindow::rememberMade);
    return d;
}

RadioChartDialog *SessionWindow::makeRadioChart(const QString &tabKey, qint64 atMs)
{
    LogModel *model = m_models.value(tabKey);
    if (!model) return nullptr;
    auto *d = new RadioChartDialog(model, tabName(tabKey), atMs, this);
    connect(d, &RadioChartDialog::exported, this, &SessionWindow::rememberMade);
    return d;
}

IncidentReportWindow *SessionWindow::openIncidentReport(const QString &tabKey, qint64 atMs,
                                                        qint64 beforeMs, qint64 afterMs)
{
    LogModel *model = m_models.value(tabKey);
    if (!model || model->count() == 0) return nullptr;
    IncidentReport::Options options;
    options.beforeMs = beforeMs;
    options.afterMs = afterMs;
    auto *w = new IncidentReportWindow(model, tabKey, tabName(tabKey), atMs, options, this);
    w->setBookmarks(m_bookmarks);   // the notes made here, and live, on this tab
    w->show();
    w->raise();
    return w;
}

QWidget *SessionWindow::openReviewTool(ReviewTool tool)
{
    const QString key = currentTabKey();
    LogModel *model = m_models.value(key);
    if (!model || model->count() == 0) {
        statusBar()->showMessage(tr("No records in this tab."), 3000);
        return nullptr;
    }
    const QString name = tabName(key);
    // Parented here: they read this window's rows, so they go when it goes.
    QWidget *w = nullptr;
    auto jump = [this](const QString &tabKey, qint64 ms) { jumpToTime(tabKey, ms); };
    switch (tool) {
    case ReviewTool::RunReport:
        w = new RunReportWindow(model, key, name, this);
        break;
    case ReviewTool::MissionReport:
        w = new RunReportWindow(model, key, name, this, RunReportWindow::Kind::Missions);
        break;
    case ReviewTool::SpeedDistance: {
        auto *sd = new SpeedDistanceWindow(model, key, this);
        connect(sd, &SpeedDistanceWindow::jumpRequested, this, jump);
        w = sd;
        break;
    }
    case ReviewTool::RadioHealth: {
        auto *rh = new RadioHealthWindow(model, key, name, this);
        connect(rh, &RadioHealthWindow::jumpRequested, this, jump);
        w = rh;
        break;
    }
    case ReviewTool::Odometry: {
        auto *od = new OdometryWindow(model, key, name, this);
        connect(od, &OdometryWindow::jumpRequested, this, jump);
        w = od;
        break;
    }
    case ReviewTool::FaultTimeline: {
        auto *ft = new FaultTimelineWindow(model, key, name, this);
        connect(ft, &FaultTimelineWindow::jumpRequested, this, jump);
        w = ft;
        break;
    }
    case ReviewTool::CabIo: {
        auto *io = new IoTimelineWindow(model, key, name, this);
        connect(io, &IoTimelineWindow::jumpRequested, this, jump);
        w = io;
        break;
    }
    case ReviewTool::TrackDiagram:
        w = new TrackDiagramWindow(model, key, name, this);
        break;
    }
    if (w) {
        w->setWindowTitle(tr("%1 — recording").arg(w->windowTitle()));
        w->show();
        w->raise();
    }
    return w;
}

// ---------------------------------------------------------------------------
//  Session 375 — what changed on the DMI
// ---------------------------------------------------------------------------

void SessionWindow::showDmiChanges()
{
    if (!m_dmiDock) {
        auto *page = new QWidget(this);
        auto *lay = new QVBoxLayout(page);
        lay->setContentsMargins(4, 4, 4, 4);
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Loco"), page));
        m_dmiLoco = new QComboBox(page);
        m_dmiLoco->setObjectName(QStringLiteral("dmiChangesLoco"));
        row->addWidget(m_dmiLoco, 1);
        lay->addLayout(row);
        m_dmiMoving = new QCheckBox(tr("Speed, distances and location too"), page);
        m_dmiMoving->setObjectName(QStringLiteral("dmiChangesContinuous"));
        m_dmiMoving->setToolTip(tr("They change every frame while the train moves"));
        lay->addWidget(m_dmiMoving);
        m_dmiTable = new QTableWidget(0, 2, page);
        m_dmiTable->setObjectName(QStringLiteral("dmiChangesTable"));
        m_dmiTable->setHorizontalHeaderLabels({ tr("Time"), tr("What changed") });
        m_dmiTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_dmiTable->horizontalHeader()->setStretchLastSection(true);
        m_dmiTable->verticalHeader()->hide();
        // A change is a sentence: rows grow to show it whole (tens to a
        // few hundred rows, so the cost is nothing).
        m_dmiTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_dmiTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_dmiTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_dmiTable->setWordWrap(true);
        lay->addWidget(m_dmiTable, 1);
        m_dmiCount = new QLabel(page);
        m_dmiCount->setStyleSheet(UiColor::mutedStyle());
        lay->addWidget(m_dmiCount);
        // The tab on show lands on the moment; the DMI window follows it.
        connect(m_dmiTable, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
            if (r >= 0 && r < m_dmiChanges.size()) jumpToTime(currentTabKey(), m_dmiChanges.at(r).ms);
        });
        connect(m_dmiLoco, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SessionWindow::refreshDmiChanges);
        connect(m_dmiMoving, &QCheckBox::toggled, this, &SessionWindow::refreshDmiChanges);

        m_dmiDock = new QDockWidget(tr("DMI changes"), this);
        m_dmiDock->setObjectName(QStringLiteral("sessionDmiChangesDock"));
        m_dmiDock->setWidget(page);
        m_dmiDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
        addDockWidget(Qt::RightDockWidgetArea, m_dmiDock);
        for (QDockWidget *d : findChildren<QDockWidget *>())
            if (d->objectName() == QLatin1String("sessionFieldDock")) tabifyDockWidget(d, m_dmiDock);
    }
    // Every loco with @dmi in any tab: a recording split by source keeps
    // the DMI in another tab than the one on show.
    QStringList keys;
    for (LogModel *m : qAsConst(m_models))
        for (const QString &k : DmiChanges::locos(m))
            if (!keys.contains(k)) keys << k;
    keys.sort();
    const QString was = m_dmiLoco->currentData().toString();
    {
        const QSignalBlocker block(m_dmiLoco);
        m_dmiLoco->clear();
        for (const QString &k : keys) m_dmiLoco->addItem(tr("Loco %1").arg(k), k);
        const int i = m_dmiLoco->findData(was);
        if (i >= 0) m_dmiLoco->setCurrentIndex(i);
    }
    refreshDmiChanges();
    m_dmiDock->show();
    m_dmiDock->raise();
}

void SessionWindow::refreshDmiChanges()
{
    m_dmiChanges.clear();
    const QString loco = m_dmiLoco ? m_dmiLoco->currentData().toString() : QString();
    for (LogModel *m : qAsConst(m_models)) {
        if (loco.isEmpty() || !DmiChanges::locos(m).contains(loco)) continue;
        m_dmiChanges = DmiChanges::list(m, loco, m_dmiMoving && m_dmiMoving->isChecked());
        break;
    }
    if (!m_dmiTable) return;
    m_dmiTable->setRowCount(m_dmiChanges.size());
    for (int i = 0; i < m_dmiChanges.size(); ++i) {
        const DmiChanges::Change &c = m_dmiChanges.at(i);
        m_dmiTable->setItem(i, 0, new QTableWidgetItem(QDateTime::fromMSecsSinceEpoch(c.ms).toString(QStringLiteral("HH:mm:ss.zzz"))));
        auto *what = new QTableWidgetItem(c.text());
        what->setToolTip(c.text());
        m_dmiTable->setItem(i, 1, what);
    }
    m_dmiCount->setText(loco.isEmpty() ? tr("No @dmi frames in this recording.")
                                       : tr("%1 change(s) on loco %2's DMI").arg(m_dmiChanges.size()).arg(loco));
}

bool SessionWindow::stepDmiChange(int dir)
{
    if (m_dmiChanges.isEmpty()) {
        // The list is made on first use, for the first loco with @dmi.
        if (!m_dmiDock) showDmiChanges();
        if (m_dmiChanges.isEmpty()) {
            statusBar()->showMessage(tr("No changes on the DMI in this recording."), 3000);
            return false;
        }
    }
    const QString key = currentTabKey();
    const LogEntryPtr cur = currentEntry();
    const qint64 at = cur ? cur->epochMs : 0;
    const DmiChanges::Change *c = dir > 0 ? DmiChanges::next(m_dmiChanges, at) : DmiChanges::previous(m_dmiChanges, at);
    if (!c) {
        statusBar()->showMessage(dir > 0 ? tr("No further change on the DMI.") : tr("No earlier change on the DMI."), 3000);
        return false;
    }
    jumpToTime(key, c->ms);
    statusBar()->showMessage(tr("DMI %1: %2").arg(QDateTime::fromMSecsSinceEpoch(c->ms).toString(QStringLiteral("HH:mm:ss")),
                                                  c->text()),
                             6000);
    return true;
}

RunCompareWindow *SessionWindow::compareWithAnotherRun()
{
    QStringList paths;
    for (const LoadedFile &f : qAsConst(m_files)) paths << f.path;
    if (paths.isEmpty()) {
        statusBar()->showMessage(tr("No recording loaded."), 3000);
        return nullptr;
    }
    auto *w = new RunCompareWindow(m_rules, this);
    QString err;
    if (!w->loadRun(RunCompareWindow::A, paths, &err)) {
        delete w;
        statusBar()->showMessage(err, 6000);
        return nullptr;
    }
    w->show();
    w->raise();
    return w;
}

void SessionWindow::onIncidentReport()
{
    const QString key = currentTabKey();
    LogModel *model = m_models.value(key);
    if (!model || model->count() == 0) {
        statusBar()->showMessage(tr("No records in this tab to report on."), 3000);
        return;
    }
    const LogEntryPtr cur = currentEntry();
    const qint64 first = model->entryAt(0)->epochMs;
    const qint64 last = model->entryAt(model->count() - 1)->epochMs;
    IncidentReportDialog dlg(cur ? cur->epochMs : last, first, last, this);
    if (dlg.exec() != QDialog::Accepted) return;
    openIncidentReport(key, dlg.atMs(), dlg.beforeMs(), dlg.afterMs());
}

QString SessionWindow::noteOf(const QString &tabKey, const LogEntryPtr &entry) const
{
    if (!m_bookmarks || !entry) return QString();
    for (const Bookmark &b : m_bookmarks->all())
        if (b.tabKey == tabKey && b.epochMs == entry->epochMs) return b.note;
    return QString();
}

bool SessionWindow::setRowNote(const QString &tabKey, const LogEntryPtr &entry, const QString &note)
{
    // Without a store a note has nowhere to live: say so rather than drop it.
    if (!entry || !m_bookmarks) {
        statusBar()->showMessage(tr("Notes are not kept in this window."), 3000);
        return false;
    }
    if (!m_bookmarks->has(tabKey, entry->epochMs)) m_bookmarks->toggle(tabKey, entry->epochMs, entry->text);
    entry->bookmarked = true;
    for (int i = 0; i < m_bookmarks->count(); ++i)
        if (m_bookmarks->all().at(i).tabKey == tabKey && m_bookmarks->all().at(i).epochMs == entry->epochMs)
            m_bookmarks->setNote(i, note);
    LogModel *model = m_models.value(tabKey);
    for (int r = 0; model && r < model->count(); ++r) {
        if (model->entryAt(r) == entry) { refreshMarks(tabKey, r); break; }
    }
    statusBar()->showMessage(note.isEmpty() ? tr("Bookmarked, no note.") : tr("Note kept."), 2000);
    writeSidecars(tabKey);   // session 369: beside the recording too
    return true;
}

void SessionWindow::refreshMarks(const QString &tabKey, int sourceRow)
{
    auto it = m_tabUi.constFind(tabKey);
    if (it == m_tabUi.constEnd() || !it->model) return;
    const QModelIndex tl = it->model->index(sourceRow, LogModel::ColTime);
    emit it->model->dataChanged(tl, tl, { Qt::DisplayRole });
    if (it->marks)  { it->marks->refreshLater(); }
    if (it->ribbon) { it->ribbon->refreshLater(); }
}

void SessionWindow::onSelectionChanged()
{
    const int idx = m_tabs->currentIndex();
    if (idx < 0) { m_rawPanel->clear(); m_fieldPanel->clear(); return; }

    for (auto it = m_tabUi.constBegin(); it != m_tabUi.constEnd(); ++it) {
        const TabUi &t = it.value();
        // The view whose cursor moved, not the one with focus: Go to
        // timestamp, Next problem and the find bar move the cursor while
        // focus is elsewhere, and the panels kept the previous frame.
        if (!t.view || t.view->selectionModel() != sender()) continue;

        const QModelIndex cur = t.view->currentIndex();
        if (!cur.isValid()) { m_rawPanel->clear(); m_fieldPanel->clear(); return; }

        // The view shows the proxy; the model holds source rows.
        auto *proxy = qobject_cast<QSortFilterProxyModel*>(t.view->model());
        const QModelIndex src = proxy ? proxy->mapToSource(cur) : cur;
        const LogEntryPtr entry = t.model->entryAt(src.row());
        m_rawPanel->showEntry(entry);
        m_fieldPanel->showEntry(entry);
        return;
    }
}

void SessionWindow::setTheme(Theme t)
{
    m_theme = t;
    for (LogModel *m : m_models) m->setTheme(t);
}

void SessionWindow::closeEvent(QCloseEvent *event)
{
    // These windows are WA_DeleteOnClose, so this is the last
    // point at which the geometry still exists to be read.
    WindowGeometry::save(this, QStringLiteral("sessionWindow"));
    QMainWindow::closeEvent(event);
}

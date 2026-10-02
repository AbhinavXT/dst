#include "dmitimetravel.h"
#include "sessionwindow.h"
#include "uistyle.h"
#include "bookmarks.h"
#include "gototimestampdialog.h"
#include "markerscrollbar.h"
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
#include "settings.h"
#include "decodeworkbench.h"
#include "framediffwindow.h"
#include "fieldplot.h"
#include "packetmakerdialog.h"

#include <QCloseEvent>
#include <QApplication>
#include <QDockWidget>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QSortFilterProxyModel>
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

    connect(m_fieldPanel, &FieldInspector::byteRangeSelected,
            this, [this](int from, int to) {
                if (m_rawPanel) { m_rawPanel->highlightBytes(from, to); }
            });

    m_lblSummary = new QLabel(tr("No session loaded"), this);
    statusBar()->addWidget(m_lblSummary);
}

SessionWindow::~SessionWindow() = default;

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
            [this, tabKey, model, filterBar](const QModelIndex &cur, const QModelIndex &) {
                if (!cur.isValid()) return;
                const QModelIndex src = filterBar->proxyModel()->mapToSource(cur);
                if (!src.isValid()) return;
                const QString name = m_names ? m_names->lookupByKey(tabKey) : tabKey;
                DmiTimeTravel::instance()->offer(
                    this, dmiTabResolver(m_models.values().toVector(), model, model->entryAt(src.row()), src.row(),
                                         tr("recording, tab %1").arg(name.isEmpty() ? tabKey : name)));
            });

    view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view, &QWidget::customContextMenuRequested,
            this, &SessionWindow::onRowContextMenu);

    // The same find bar the live window has, with the same shape preference:
    // Ctrl+F opens it as a floating window or as a bar in the tab, whichever
    // the operator used last.
    auto *findBar = new FindBar(view);
    findBar->setContextLabel(m_names ? m_names->lookupByKey(tabKey) : tabKey);

    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
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
    m_tabUi.insert(tabKey, ui);
}

qint64 SessionWindow::loadFiles(const QStringList &paths)
{
    m_warnings.clear();
    qint64 grandTotal = 0;
    qint64 skipped    = 0;

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

        while (reader.next()) {
            LogEntryPtr e = MessageDispatcher::buildEntry(
                reader.wire(), reader.arrivalMs(), m_rules);
            if (!e) { ++skipped; continue; }

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

    if (paths.size() == 1) {
        setWindowTitle(tr("Recorded session — %1")
                           .arg(QFileInfo(paths.first()).fileName()));
    } else {
        setWindowTitle(tr("Recorded session — %1 files").arg(paths.size()));
    }

    return grandTotal;
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
    const QModelIndex landing = view->model()->index(row, LogModel::ColTime);
    view->setCurrentIndex(landing);
    view->scrollTo(landing, QAbstractItemView::PositionAtCenter);
    view->setFocus();
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
    statusBar()->showMessage(added ? tr("Bookmarked.") : tr("Bookmark removed."), 2000);
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

    menu.exec(view->viewport()->mapToGlobal(pos));
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

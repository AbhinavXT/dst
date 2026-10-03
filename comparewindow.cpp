#include "comparewindow.h"
#include <QMenu>
#include <QMenuBar>
#include "framediffwindow.h"
#include "decodeworkbench.h"
#include "markerscrollbar.h"
#include "gototimestampdialog.h"
#include "findbar.h"
#include "uicolors.h"
#include "uistyle.h"
#include "windowgeometry.h"

#include "logmodel.h"
#include "bookmarks.h"
#include "capturedecoder.h"
#include "colorrules.h"
#include "emptystate.h"
#include "fieldinspector.h"
#include "logtableview.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "rawbytespanel.h"
#include "settings.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <algorithm>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QMessageBox>
#include <QSplitter>
#include <QTabWidget>
#include <QStringList>
#include <QTableView>
#include <QVBoxLayout>

// Source and Name hold one value per pane: the pane shows one source, named
// in its picker above. In two panes at 1100 px they took ~200 px and left
// Message ~35 px, scrolling sideways (session 154, Abhinav's decision).
// Every other column follows View > Columns, as in the log tabs.
static void hidePaneConstantColumns(QTableView *view)
{
    if (!view) return;
    view->setColumnHidden(LogModel::ColSource, true);
    view->setColumnHidden(LogModel::ColFriendly, true);
}

CompareWindow::CompareWindow(MessageDispatcher *dispatcher,
                             const NameMap     *names,
                             QWidget           *parent)
    : QDialog(parent)
    , m_dispatcher(dispatcher)
    , m_names(names)
{
    setWindowTitle(tr("Compare tabs"));
    // Sets the window type as well as the min/max hints — see
    // makeResizableWindow(); OR-ing Qt::Window in afterwards would put the
    // dialog type back and take the maximise button with it.
    WindowGeometry::makeResizableWindow(this);
    setModal(false);   // the operator keeps using the main window
    resize(1300, 720);
    // Default above; a remembered size/position wins over it.
    WindowGeometry::restore(this, QStringLiteral("compareWindow"));

    auto *root = new QVBoxLayout(this);

    // After the layout exists: setMenuBar() needs one to attach to.
    buildMenus();

    // ---- Top toolbar -------------------------------------------------
    {
        auto *row = new QHBoxLayout;

        m_addBtn = new QPushButton(tr("+ Add pane"));
        m_addBtn->setToolTip(tr("Append a new comparison pane on the right "
                                 "(or bottom, in vertical layout)."));
        connect(m_addBtn, &QPushButton::clicked,
                this,     &CompareWindow::onAddPane);
        row->addWidget(m_addBtn);

        m_removeBtn = new QPushButton(tr("- Remove pane"));
        m_removeBtn->setToolTip(tr("Remove the last pane. Disabled when only "
                                    "one pane remains (keeping zero panes "
                                    "leaves an awkward empty window)."));
        connect(m_removeBtn, &QPushButton::clicked,
                this,        &CompareWindow::onRemovePane);
        row->addWidget(m_removeBtn);

        row->addSpacing(20);

        m_timeLockCb = new QCheckBox(tr("Time-lock scrolling"));
        m_timeLockCb->setChecked(true);
        m_timeLockCb->setToolTip(tr(
            "When checked, scrolling any pane jumps every other pane to "
            "its nearest message in time. Useful for cross-source "
            "correlation across N controllers."));
        row->addWidget(m_timeLockCb);

        row->addSpacing(20);

        m_verticalCb = new QCheckBox(tr("Vertical layout"));
        m_verticalCb->setToolTip(tr(
            "Stack panes top-to-bottom instead of side-by-side. Useful "
            "for tall monitors and 4+ panes, or when messages are long "
            "and benefit from full width."));
        connect(m_verticalCb, &QCheckBox::toggled,
                this,         &CompareWindow::onLayoutToggled);
        row->addWidget(m_verticalCb);

        row->addStretch();
        root->addLayout(row);
    }

    // ---- Splitter (panes go here) ------------------------------------
    // Panes above, the selected frame's bytes and fields below, in one
    // vertical splitter (session 129): the details had a fixed 280-px cap
    // yet a 252-px floor, and the panes got what was left (264 px at 720).
    auto *vsplit = new QSplitter(Qt::Vertical);
    vsplit->setChildrenCollapsible(false);
    m_splitter = new QSplitter(Qt::Horizontal);
    vsplit->addWidget(m_splitter);
    root->addWidget(vsplit, 1);

    // ---- Raw bytes and decoded fields under the splitter -------------
    //
    // Both, side by side, because this window exists to compare two frames
    // and the useful comparison is usually of DECODED values — "LSRP says
    // Stand By here and Staff Responsible there" — not of hex. The bytes
    // alone made an operator carry the row back to a log tab to read it.
    {
        auto *bottom = new QSplitter(Qt::Horizontal);

        // Section labels, not boxes: the raw-bytes panel draws its own
        // Header and Raw bytes boxes, and a box round them was a box of
        // boxes (session 129).
        auto section = [](const QString &title, QWidget *body) {
            auto *w = new QWidget;
            auto *l = new QVBoxLayout(w);
            l->setContentsMargins(0, UiStyle::space(2), 0, 0);
            auto *label = new QLabel(title);
            UiStyle::makeSectionLabel(label);
            l->addWidget(label);
            l->addWidget(body, 1);
            return w;
        };
        m_rawPanel = new RawBytesPanel;
        m_rawPanel->setNameMap(m_names);
        bottom->addWidget(section(tr("Raw bytes of the selected row"), m_rawPanel));

        m_fieldPanel = new FieldInspector;
        m_fieldPanel->setDecoder(&kavachSchema());
        bottom->addWidget(section(tr("Decoded fields of the selected row"), m_fieldPanel));

        // The inspector's byte highlight writes into the hex dump beside it,
        // exactly as in the main window.
        connect(m_fieldPanel, &FieldInspector::byteRangeSelected,
                this, [this](int from, int to) {
                    if (m_rawPanel) { m_rawPanel->highlightBytes(from, to); }
                });
        // Pinning and plotting are forwarded whole: the pin belongs to the
        // pane's source, which this window knows and the inspector does not.
        connect(m_fieldPanel, &FieldInspector::pinFieldRequested,
                this, [this](const QString &field) {
                    const int i = activePane();
                    emit pinFieldRequested(
                        field,
                        i >= 0 ? m_panes.at(i).currentKey : QString(),
                        m_fieldPanel->currentCaptype());
                });
        connect(m_fieldPanel, &FieldInspector::locateFieldRequested,
                this, &CompareWindow::locateFieldRequested);
        connect(m_fieldPanel, &FieldInspector::plotFieldRequested,
                this, [this](const QString &field) {
                    const int i = activePane();
                    emit plotFieldRequested(
                        field, i >= 0 ? m_panes.at(i).currentKey : QString());
                });

        vsplit->addWidget(bottom);
        vsplit->setStretchFactor(0, 3);
        vsplit->setStretchFactor(1, 2);
        vsplit->setSizes({ 600, 400 });
    }

    // ---- Initial state: 2 panes (matches old default) ----------------
    appendPane();
    appendPane();

    // Try to default the two panes to different sources if we have ≥ 2
    // known. Otherwise both pick the only one (or both empty).
    QStringList keys = m_dispatcher->knownKeys();
    keys.sort();
    if (keys.size() >= 2) {
        m_panes[0].sourceBox->setCurrentIndex(
            m_panes[0].sourceBox->findData(keys.at(0)));
        m_panes[1].sourceBox->setCurrentIndex(
            m_panes[1].sourceBox->findData(keys.at(1)));
    } else if (keys.size() == 1) {
        m_panes[0].sourceBox->setCurrentIndex(0);
        m_panes[1].sourceBox->setCurrentIndex(0);
    }

    // Force rebind for any pane whose combo's currentIndex wasn't
    // CHANGED above (e.g. setCurrentIndex(0) on a combo already at 0
    // doesn't fire currentIndexChanged). Without this, that pane's
    // view stays unbound until the user touches the combo.
    for (int i = 0; i < m_panes.size(); ++i) {
        const int ci = m_panes[i].sourceBox->currentIndex();
        if (ci >= 0 && m_panes[i].currentKey.isEmpty()) {
            onPaneSourceChanged(i, ci);
        }
    }

    // Listen for new sources appearing in the main app.
    connect(m_dispatcher, &MessageDispatcher::tabRequested,
            this,         &CompareWindow::onDispatcherTabRequested);

    // Update Remove button enabled state.
    m_removeBtn->setEnabled(m_panes.size() > 1);
}

// -----------------------------------------------------------------------
//  Pane lifecycle
// -----------------------------------------------------------------------

void CompareWindow::appendPane()
{
    Pane p;
    const int idx = m_panes.size();   // pane's index in the vector

    // Container: vertical column with combo + label + view.
    p.container = new QWidget;
    auto *col = new QVBoxLayout(p.container);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);

    p.sourceBox = new QComboBox;
    p.sourceBox->setMinimumWidth(160);
    col->addWidget(p.sourceBox);

    p.timeLabel = new QLabel(tr("Top: —"));
    p.timeLabel->setStyleSheet(UiColor::mutedStyle());
    col->addWidget(p.timeLabel);

    p.view = new QTableView;
    // The same setup a log tab gets, from the same place: row height, word
    // wrap, column widths, hidden columns and multi-row selection. These
    // panes used to hard-code 44 px and a fixed set of columns, so setting
    // rows to Comfortable or hiding the Source column changed the log tabs
    // and left the compare panes looking like a different program.
    LogTableView::configure(p.view);
    hidePaneConstantColumns(p.view);

    // An unbound pane and a bound one with nothing in it look identical
    // without this, and so does a source that has simply gone quiet.
    EmptyState::attach(p.view, [this, idx]() -> QString {
        if (idx < 0 || idx >= m_panes.size() || m_panes.at(idx).currentKey.isEmpty()) {
            return tr("Pick a source for this pane.");
        }
        return tr("Waiting for traffic from %1.")
            .arg(m_dispatcher->friendlyNameFor(m_panes.at(idx).currentKey));
    });
    // A find bar per pane, as the log tabs have. It follows the shape
    // preference, so Ctrl+F gives a floating window or an in-pane bar,
    // whichever was used last.
    p.findBar = new FindBar(p.view);
    col->addWidget(p.findBar);

    col->addWidget(p.view, 1);

    m_splitter->addWidget(p.container);
    m_panes.append(p);

    // Now wire signals. Capture idx by value so the lambdas refer to the
    // correct pane regardless of subsequent vector growth. Note: idx is
    // STABLE because we only ever append, and the only removal path
    // (removeLastPane) trims from the end.
    connect(p.sourceBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, idx](int comboIndex) {
                onPaneSourceChanged(idx, comboIndex);
            });

    // A right-click menu, as the log tabs have. Without one, the only way
    // to get a row out of this window was to find it again in its own tab —
    // which is the thing the window exists to avoid.
    m_panes[idx].view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_panes[idx].view, &QWidget::customContextMenuRequested,
            this, [this, idx](const QPoint &pos) {
                onPaneContextMenu(idx, pos);
            });

    // Populate this new pane's combo with current known keys. Other
    // panes already have theirs; we don't disturb them.
    refreshAllSourcePickers();

    m_removeBtn->setEnabled(m_panes.size() > 1);
}

void CompareWindow::removeLastPane()
{
    if (m_panes.size() <= 1) return;

    Pane p = m_panes.takeLast();
    // QSplitter doesn't take ownership in the documented sense — it
    // does parent the widget though, so deleting the container will
    // disconnect everything Qt-style. setParent(nullptr) first to
    // remove from the splitter cleanly, then deleteLater for safety.
    if (p.container) {
        p.container->setParent(nullptr);
        p.container->deleteLater();
    }

    m_removeBtn->setEnabled(m_panes.size() > 1);
}

void CompareWindow::buildMenus()
{
    // A menu bar in a QDialog: the tools need somewhere to live and
    // somewhere to show their shortcuts, and this window is a work surface
    // rather than a prompt. The keys match the log tabs and the session
    // viewer exactly — the same fingers, wherever the operator is reading.
    auto *bar = new QMenuBar(this);

    auto *edit = bar->addMenu(tr("&Edit"));
    QAction *a = edit->addAction(tr("&Find…"));
    a->setShortcut(QKeySequence::Find);
    connect(a, &QAction::triggered, this, &CompareWindow::onFind);

    a = edit->addAction(tr("&Go to timestamp…"));
    a->setShortcut(QKeySequence("Ctrl+G"));
    connect(a, &QAction::triggered, this, &CompareWindow::onGotoTimestamp);

    edit->addSeparator();
    a = edit->addAction(tr("&Copy message"));
    a->setShortcut(QKeySequence::Copy);
    connect(a, &QAction::triggered, this, &CompareWindow::onCopyMessage);

    // Ctrl+Alt+C rather than Ctrl+Shift+C, matching the log tabs: the same
    // fingers wherever the operator is reading, and Ctrl+Shift+C is spoken
    // for in the main window.
    a = edit->addAction(tr("Copy &row (tab-separated)"));
    a->setShortcut(QKeySequence("Ctrl+Alt+C"));
    connect(a, &QAction::triggered, this, &CompareWindow::onCopyRow);

    edit->addSeparator();
    m_bookmarkAction = edit->addAction(tr("Add &bookmark"));
    m_bookmarkAction->setShortcut(QKeySequence("Ctrl+B"));
    connect(m_bookmarkAction, &QAction::triggered,
            this, &CompareWindow::onToggleBookmark);

    a = edit->addAction(tr("Next bookmark"));
    a->setShortcut(QKeySequence("F2"));
    connect(a, &QAction::triggered, this, &CompareWindow::onNextBookmark);

    a = edit->addAction(tr("Previous bookmark"));
    a->setShortcut(QKeySequence("Shift+F2"));
    connect(a, &QAction::triggered, this, &CompareWindow::onPrevBookmark);

    edit->addSeparator();
    a = edit->addAction(tr("Next &problem"));
    a->setShortcut(QKeySequence("F4"));
    connect(a, &QAction::triggered, this, &CompareWindow::onNextProblem);

    a = edit->addAction(tr("Previous pro&blem"));
    a->setShortcut(QKeySequence("Shift+F4"));
    connect(a, &QAction::triggered, this, &CompareWindow::onPrevProblem);

    auto *tools = bar->addMenu(tr("&Tools"));
    a = tools->addAction(tr("Selected row → Decode Workbenc&h"));
    a->setShortcut(QKeySequence("Ctrl+Shift+B"));
    connect(a, &QAction::triggered, this, &CompareWindow::onRowToWorkbench);

    // The one tool that is specific to this window: the panes are already
    // showing two sources, so "compare what is selected in each" is the
    // question the window was opened to ask.
    a = tools->addAction(tr("&Diff the selected rows across panes"));
    connect(a, &QAction::triggered, this, &CompareWindow::onDiffAcrossPanes);

    layout()->setMenuBar(bar);
}

int CompareWindow::activePane() const
{
    if (m_activePane >= 0 && m_activePane < m_panes.size()) { return m_activePane; }
    return m_panes.isEmpty() ? -1 : 0;
}

void CompareWindow::onFind()
{
    const int i = activePane();
    if (i < 0 || !m_panes[i].findBar) { return; }
    m_panes[i].findBar->activate();
}

void CompareWindow::onGotoTimestamp()
{
    const int i = activePane();
    if (i < 0 || !m_panes[i].view) { return; }
    auto *model = qobject_cast<LogModel *>(m_panes[i].view->model());
    if (!model || model->count() == 0) { return; }

    // The panes bind their LogModel directly — no filter proxy — which the
    // shared resolver handles: it takes whatever model the view holds.
    const GotoTimestamp::Span span = GotoTimestamp::spanOf(model, model);
    if (!span.ok) { return; }

    qint64 seedMs = span.minMs;
    const int topRow = m_panes[i].view->rowAt(0);
    if (topRow >= 0) {
        const qint64 ms = GotoTimestamp::epochAtProxyRow(model, model, topRow);
        if (ms >= 0) { seedMs = ms; }
    }

    GotoTimestampDialog dlg(seedMs, span.minMs, span.maxMs, this);
    if (dlg.exec() != QDialog::Accepted) { return; }

    const int row = GotoTimestamp::resolveRow(model, model, dlg.targetMs(),
                                              int(dlg.mode()));
    if (row < 0) { return; }
    const QModelIndex landing = model->index(row, LogModel::ColTime);
    m_panes[i].view->setCurrentIndex(landing);
    m_panes[i].view->scrollTo(landing, QAbstractItemView::PositionAtCenter);
    // Time-lock, if it is on, carries the other panes along — which is the
    // reason to want "go to a time" in this window at all.
}

void CompareWindow::stepProblem(int dir)
{
    const int i = activePane();
    if (i < 0 || !m_panes[i].view) { return; }
    auto *model = qobject_cast<LogModel *>(m_panes[i].view->model());
    if (!model) { return; }

    const QModelIndex cur = m_panes[i].view->currentIndex();
    const int from = cur.isValid() ? cur.row() : (dir > 0 ? -1 : model->rowCount());
    const int row  = nextMarkedRow(model, model, from, dir);
    if (row < 0) { return; }

    const QModelIndex landing = model->index(row, LogModel::ColTime);
    m_panes[i].view->setCurrentIndex(landing);
    m_panes[i].view->scrollTo(landing, QAbstractItemView::PositionAtCenter);
}

void CompareWindow::onNextProblem() { stepProblem(+1); }
void CompareWindow::onPrevProblem() { stepProblem(-1); }

void CompareWindow::onRowToWorkbench()
{
    const int i = activePane();
    if (i < 0 || !m_panes[i].view) { return; }
    auto *model = qobject_cast<LogModel *>(m_panes[i].view->model());
    const QModelIndex cur = m_panes[i].view->currentIndex();
    if (!model || !cur.isValid()) { return; }

    const QString buf = entryBufferText(model->entryAt(cur.row()));
    if (buf.isEmpty()) { return; }

    if (!m_workbench) {
        m_workbench = new DecodeWorkbench(this, m_sessionKeys);
        m_workbench->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_workbench->loadBuffer(buf);
    m_workbench->show();
    m_workbench->raise();
    m_workbench->activateWindow();
}

QTableView *CompareWindow::paneView(int paneIndex) const
{
    if (paneIndex < 0 || paneIndex >= m_panes.size()) { return nullptr; }
    return m_panes.at(paneIndex).view;
}

LogEntryPtr CompareWindow::entryAt(int paneIndex, const QModelIndex &index) const
{
    if (paneIndex < 0 || paneIndex >= m_panes.size()) { return {}; }
    const Pane &p = m_panes.at(paneIndex);
    if (!p.view || !index.isValid()) { return {}; }
    auto *model = qobject_cast<LogModel *>(p.view->model());
    return model ? model->entryAt(index.row()) : LogEntryPtr();
}

LogEntryPtr CompareWindow::currentEntry(int paneIndex) const
{
    if (paneIndex < 0 || paneIndex >= m_panes.size()) { return {}; }
    const Pane &p = m_panes.at(paneIndex);
    return p.view ? entryAt(paneIndex, p.view->currentIndex()) : LogEntryPtr();
}

QVector<LogEntryPtr> CompareWindow::selectedEntries(int paneIndex) const
{
    QVector<LogEntryPtr> out;
    if (paneIndex < 0 || paneIndex >= m_panes.size()) { return out; }
    const Pane &p = m_panes.at(paneIndex);
    if (!p.view || !p.view->selectionModel()) { return out; }

    QModelIndexList rows = p.view->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(),
              [](const QModelIndex &a, const QModelIndex &b) {
                  return a.row() < b.row();
              });
    out.reserve(rows.size());
    for (const QModelIndex &idx : rows) {
        if (const LogEntryPtr e = entryAt(paneIndex, idx)) { out.append(e); }
    }
    // Falling back to the current row matters: a pane can have a current
    // index with nothing formally selected, and copying nothing because of
    // that would look like the key did not work.
    if (out.isEmpty()) {
        if (const LogEntryPtr e = currentEntry(paneIndex)) { out.append(e); }
    }
    return out;
}

void CompareWindow::setLocoIdentity(LocoIdentity *identity)
{
    m_identity = identity;
    // Forwarded here rather than at construction: the owner supplies this
    // after the window is built, so a constructor-time hand-off would always
    // pass null and the panel would silently learn nothing.
    if (m_fieldPanel) { m_fieldPanel->setLocoIdentity(identity); }
}

void CompareWindow::setBookmarks(BookmarkStore *bookmarks)
{
    m_bookmarks = bookmarks;
}

void CompareWindow::onToggleBookmark()
{
    if (!m_bookmarks) { return; }
    const int pane = activePane();
    if (pane < 0) { return; }
    const LogEntryPtr e = currentEntry(pane);
    if (!e) { return; }

    const QString key = m_panes.at(pane).currentKey;
    if (key.isEmpty()) { return; }

    const bool added = m_bookmarks->toggle(key, e->epochMs, e->text);
    e->bookmarked = added;

    // Repaint just that row's time cell rather than the whole model, which
    // is what draws the mark.
    if (auto *model = qobject_cast<LogModel *>(m_panes.at(pane).view->model())) {
        const QModelIndex tl =
            model->index(m_panes.at(pane).view->currentIndex().row(),
                         LogModel::ColTime);
        emit model->dataChanged(tl, tl, { Qt::DisplayRole });
    }
}

void CompareWindow::onNextBookmark() { stepBookmark(+1); }
void CompareWindow::onPrevBookmark() { stepBookmark(-1); }

void CompareWindow::stepBookmark(int dir)
{
    const int pane = activePane();
    if (pane < 0) { return; }
    QTableView *view = m_panes.at(pane).view;
    if (!view) { return; }
    auto *model = qobject_cast<LogModel *>(view->model());
    if (!model) { return; }

    const int rows = model->rowCount();
    const int from = view->currentIndex().isValid()
                         ? view->currentIndex().row()
                         : (dir > 0 ? -1 : rows);

    // The same helper the log tabs and the session window use, so the three
    // cannot drift apart. It takes a (proxy, source) pair; these panes bind
    // their model directly, so it is passed as both.
    const int r = nextBookmarkedRow(model, model, from, dir);
    if (r < 0) { return; }

    const QModelIndex landing = model->index(r, LogModel::ColTime);
    view->setCurrentIndex(landing);
    view->scrollTo(landing, QAbstractItemView::PositionAtCenter);
    view->setFocus();
}

void CompareWindow::onCopyMessage()
{
    const QVector<LogEntryPtr> rows = selectedEntries(activePane());
    if (rows.isEmpty()) { return; }
    QApplication::clipboard()->setText(formatMessagesForClipboard(rows));
}

void CompareWindow::onCopyRow()
{
    const QVector<LogEntryPtr> rows = selectedEntries(activePane());
    if (rows.isEmpty()) { return; }
    QApplication::clipboard()->setText(
        formatEntriesForClipboard(rows, false, Settings::showUtc()));
}

QMenu *CompareWindow::buildRowMenu(int paneIndex, const QModelIndex &index,
                                   QWidget *parent)
{
    const LogEntryPtr entry = entryAt(paneIndex, index);
    if (!entry) { return nullptr; }

    auto *menu = new QMenu(parent);

    // Everything selected, in view order — not just the row under the
    // cursor. Right-clicking inside a selection and getting one row would
    // silently discard the span the operator just made.
    QVector<LogEntryPtr> rows = selectedEntries(paneIndex);
    if (rows.isEmpty()) { rows.append(entry); }
    const int n = rows.size();

    // The message first and on Ctrl+C, for the same reason as in the log
    // tabs: a capture line is the input format of half this program, and
    // four columns in front of it have to be stripped off by hand.
    QAction *a = menu->addAction(n > 1 ? tr("&Copy %1 messages").arg(n)
                                       : tr("&Copy message"));
    a->setShortcut(QKeySequence::Copy);
    connect(a, &QAction::triggered, this, [rows] {
        QApplication::clipboard()->setText(formatMessagesForClipboard(rows));
    });

    a = menu->addAction(n > 1 ? tr("Copy %1 &rows (tab-separated)").arg(n)
                              : tr("Copy &row (tab-separated)"));
    a->setShortcut(QKeySequence("Ctrl+Alt+C"));
    connect(a, &QAction::triggered, this, [rows] {
        QApplication::clipboard()->setText(
            formatEntriesForClipboard(rows, false, Settings::showUtc()));
    });

    a = menu->addAction(tr("Copy rows &with header"));
    connect(a, &QAction::triggered, this, [rows] {
        QApplication::clipboard()->setText(
            formatEntriesForClipboard(rows, true, Settings::showUtc()));
    });

    const QString buf = entryBufferText(entry);
    a = menu->addAction(tr("Copy &bytes"));
    a->setEnabled(!buf.isEmpty());
    if (buf.isEmpty()) {
        a->setToolTip(tr("This row carries no bytes — nothing to copy."));
    }
    connect(a, &QAction::triggered, this, [buf] {
        QApplication::clipboard()->setText(buf);
    });

    menu->addSeparator();

    // Bookmarks are the shared store, so a frame marked here shows its mark
    // in the log tab too.
    a = menu->addAction(entry->bookmarked ? tr("Remove &bookmark")
                                          : tr("Add &bookmark"));
    a->setShortcut(QKeySequence("Ctrl+B"));
    a->setEnabled(m_bookmarks != nullptr);
    connect(a, &QAction::triggered, this, &CompareWindow::onToggleBookmark);

    // The affordance that made the shipped colour_rules.json bug findable:
    // decorative rules were pinning severity to info, so real errors
    // rendered as info and nothing on screen said which rule did it.
    a = menu->addAction(tr("&Why this colour?"));
    a->setEnabled(m_rules != nullptr);
    connect(a, &QAction::triggered, this, [this, entry] {
        if (!m_rules) { return; }
        QMessageBox box(this);
        box.setWindowTitle(tr("Why this colour?"));
        box.setTextFormat(Qt::PlainText);
        box.setText(m_rules->explainClassification(entry->text));
        box.exec();
    });

    menu->addSeparator();

    a = menu->addAction(tr("Load into &Packet Maker"));
    a->setEnabled(!buf.isEmpty());
    if (buf.isEmpty()) {
        a->setToolTip(tr("This row carries no bytes to load."));
    }
    connect(a, &QAction::triggered, this,
            [this, buf] { emit packetMakerRequested(buf); });

    a = menu->addAction(tr("Open in Decode Workbenc&h"));
    a->setEnabled(!buf.isEmpty());
    if (buf.isEmpty()) {
        a->setToolTip(tr("This row carries no bytes — nothing to decode."));
    }
    // Through the pane rather than the entry: the workbench acts on the
    // ACTIVE pane, and right-clicking in a pane is how an operator says
    // which one that is.
    connect(a, &QAction::triggered, this, [this, paneIndex, index] {
        m_activePane = paneIndex;
        if (m_panes[paneIndex].view) {
            m_panes[paneIndex].view->setCurrentIndex(index);
        }
        onRowToWorkbench();
    });

    // The one verb that belongs to this window rather than to a row: the
    // panes are already showing two sources, so "compare what is selected
    // in each" is the question the window was opened to ask.
    int withSelection = 0;
    for (const Pane &p : m_panes) {
        if (p.view && p.view->currentIndex().isValid()) { ++withSelection; }
    }
    a = menu->addAction(tr("&Diff the selected rows across panes"));
    a->setEnabled(withSelection >= 2);
    if (withSelection < 2) {
        a->setToolTip(tr("Select a row in a second pane to compare them."));
    }
    connect(a, &QAction::triggered, this, &CompareWindow::onDiffAcrossPanes);

    return menu;
}

void CompareWindow::onPaneContextMenu(int paneIndex, const QPoint &pos)
{
    if (paneIndex < 0 || paneIndex >= m_panes.size()) { return; }
    QTableView *view = m_panes[paneIndex].view;
    if (!view) { return; }

    const QModelIndex idx = view->indexAt(pos);
    if (!idx.isValid()) { return; }

    // Right-clicking a row selects it. Otherwise every action in the menu
    // would act on whatever was selected before, which is not the row the
    // operator is pointing at.
    view->setCurrentIndex(idx);
    m_activePane = paneIndex;

    QMenu *menu = buildRowMenu(paneIndex, idx, this);
    if (!menu) { return; }
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->exec(view->viewport()->mapToGlobal(pos));
}

void CompareWindow::onDiffAcrossPanes()
{
    // One row from each pane that has one, in pane order. Frame Diff takes
    // up to six, which is also the practical limit on panes here.
    QVector<LogEntryPtr> picked;
    for (const Pane &p : m_panes) {
        if (!p.view) { continue; }
        auto *model = qobject_cast<LogModel *>(p.view->model());
        const QModelIndex cur = p.view->currentIndex();
        if (!model || !cur.isValid()) { continue; }
        if (const LogEntryPtr e = model->entryAt(cur.row())) { picked.push_back(e); }
    }
    if (picked.size() < 2) { return; }

    if (!m_frameDiff) {
        m_frameDiff = new FrameDiffWindow(this);
        m_frameDiff->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_frameDiff->setEntries(picked);
    m_frameDiff->show();
    m_frameDiff->raise();
    m_frameDiff->activateWindow();
}

void CompareWindow::onAddPane()
{
    appendPane();
    // If there are still unused known sources, default the new pane to
    // the next one. Otherwise leave it on whatever the combo defaulted
    // to (usually item 0).
    QStringList used;
    for (int i = 0; i < m_panes.size() - 1; ++i) {
        used.append(m_panes[i].currentKey);
    }
    QStringList all = m_dispatcher->knownKeys();
    all.sort();
    for (const QString &k : all) {
        if (!used.contains(k)) {
            const int comboIdx = m_panes.last().sourceBox->findData(k);
            if (comboIdx >= 0) {
                m_panes.last().sourceBox->setCurrentIndex(comboIdx);
            }
            break;
        }
    }
    // If nothing changed (the picked source was already at index 0),
    // currentIndexChanged didn't fire and the pane is unbound. Force
    // a rebind. Same trick as the constructor uses.
    const int newIdx = m_panes.size() - 1;
    if (m_panes[newIdx].currentKey.isEmpty()) {
        const int ci = m_panes[newIdx].sourceBox->currentIndex();
        if (ci >= 0) onPaneSourceChanged(newIdx, ci);
    }
}

void CompareWindow::onRemovePane()
{
    removeLastPane();
}

// -----------------------------------------------------------------------
//  Source picker management
// -----------------------------------------------------------------------

void CompareWindow::refreshAllSourcePickers()
{
    QStringList keys = m_dispatcher->knownKeys();
    keys.sort();

    for (Pane &p : m_panes) {
        const QString keep = p.currentKey;
        // blockSignals while rebuilding to avoid spurious
        // currentIndexChanged signals causing rebinds.
        p.sourceBox->blockSignals(true);
        p.sourceBox->clear();
        for (const QString &k : keys) {
            const QString friendly = m_dispatcher->friendlyNameFor(k);
            const QString display = (friendly == k)
                ? k
                : QString("%1   (%2)").arg(friendly, k);
            p.sourceBox->addItem(display, k);
        }
        if (!keep.isEmpty()) {
            const int idx = p.sourceBox->findData(keep);
            if (idx >= 0) p.sourceBox->setCurrentIndex(idx);
        }
        p.sourceBox->blockSignals(false);
    }
}

void CompareWindow::onDispatcherTabRequested(QString /*tabKey*/,
                                              QString /*friendlyName*/)
{
    // A new source appeared. Refresh every pane's combo.
    refreshAllSourcePickers();
}

void CompareWindow::onPaneSourceChanged(int paneIndex, int comboIndex)
{
    if (paneIndex < 0 || paneIndex >= m_panes.size()) return;
    Pane &p = m_panes[paneIndex];
    if (comboIndex < 0) {
        p.currentKey.clear();
        p.view->setModel(nullptr);
        EmptyState::refresh(p.view);
        return;
    }
    p.currentKey = p.sourceBox->itemData(comboIndex).toString();
    rebindPane(paneIndex);
}

void CompareWindow::rebindPane(int paneIndex)
{
    if (paneIndex < 0 || paneIndex >= m_panes.size()) return;
    Pane &p = m_panes[paneIndex];
    if (p.currentKey.isEmpty()) return;

    LogModel *m = m_dispatcher->modelForKey(p.currentKey);
    if (!m) return;

    p.view->setModel(m);
    EmptyState::refresh(p.view);   // setModel() emits no row signal to hide it

    // Re-connect scroll-bar and selection signals. setModel() invalidates
    // the previous selectionModel(), so connections to it are gone — we
    // wire fresh ones, with paneIndex captured by value. The old ones are
    // dropped first so a rebind does not stack a second scroll handler.
    disconnect(p.scrollConn);
    disconnect(p.selectionConn);
    p.scrollConn = connect(p.view->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this, paneIndex](int) { onPaneScrolled(paneIndex); });

    p.selectionConn = connect(p.view->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this, [this, paneIndex](const QModelIndex &cur,
                                    const QModelIndex &prev) {
                onPaneSelectionChanged(paneIndex, cur, prev);
            });

    // The find bar was built against a view with no model at all, and this is
    // the second and every subsequent model it will be shown. Nothing in Qt
    // announces that, so it is said here — otherwise Find keeps listening to
    // the source the operator just switched away from.
    if (p.findBar) { p.findBar->refreshModelBinding(); }

    // Widths and hidden columns are re-applied here as well as at
    // construction: setModel resets the header, so a pane rebound to
    // another source would otherwise come back with Qt's defaults.
    LogTableView::applyDefaultColumnWidths(p.view);
    LogTableView::applyColumnWidths(p.view);
    LogTableView::applyColumnVisibility(p.view);
    hidePaneConstantColumns(p.view);

    updateAllTimestampReadouts();
}

// -----------------------------------------------------------------------
//  Time-lock + selection handlers
// -----------------------------------------------------------------------

void CompareWindow::onPaneScrolled(int paneIndex)
{
    updateAllTimestampReadouts();
    if (!m_timeLockCb->isChecked()) return;
    if (m_syncing) return;
    if (paneIndex < 0 || paneIndex >= m_panes.size()) return;

    // Read the topmost visible row in the source pane.
    Pane &src = m_panes[paneIndex];
    auto *srcModel = qobject_cast<LogModel*>(src.view->model());
    if (!srcModel) return;

    const int topRow = src.view->rowAt(0);
    if (topRow < 0) return;
    LogEntryPtr e = srcModel->entryAt(topRow);
    if (!e) return;
    const qint64 targetMs = e->epochMs;

    // Drive every OTHER pane to its nearest-time row. Single guard
    // covers the whole batch — none of the recipient scroll handlers
    // re-propagate.
    m_syncing = true;
    for (int i = 0; i < m_panes.size(); ++i) {
        if (i == paneIndex) continue;
        Pane &dst = m_panes[i];
        auto *dstModel = qobject_cast<LogModel*>(dst.view->model());
        if (!dstModel) continue;
        const int dstRow = findNearestRowByTime(dstModel, targetMs);
        if (dstRow < 0) continue;
        dst.view->scrollTo(dstModel->index(dstRow, 0),
                            QAbstractItemView::PositionAtTop);
    }
    m_syncing = false;
}

void CompareWindow::onPaneSelectionChanged(int paneIndex,
                                            const QModelIndex &cur,
                                            const QModelIndex & /*prev*/)
{
    if (!cur.isValid()) return;
    if (paneIndex < 0 || paneIndex >= m_panes.size()) return;
    // Remember which pane is being worked in: every tool acts on one pane,
    // and guessing wrong sends the operator somewhere they were not looking.
    m_activePane = paneIndex;
    auto *model = qobject_cast<LogModel*>(m_panes[paneIndex].view->model());
    if (!model) return;
    LogEntryPtr e = model->entryAt(cur.row());
    if (!e) { return; }
    m_rawPanel->showEntry(e);
    // Decoded AFTER the hex dump exists — the inspector's byte highlight
    // writes into it.
    if (m_fieldPanel) { m_fieldPanel->showEntry(e); }
    if (m_bookmarkAction) {
        m_bookmarkAction->setText(e->bookmarked ? tr("Remove &bookmark")
                                                : tr("Add &bookmark"));
    }
}

// -----------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------

int CompareWindow::findNearestRowByTime(LogModel *model, qint64 epochMs) const
{
    if (!model) return -1;
    const int n = model->count();
    if (n == 0) return -1;

    int lo = 0, hi = n - 1;
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        LogEntryPtr e = model->entryAt(mid);
        if (!e) { lo = mid + 1; continue; }
        if (e->epochMs < epochMs) lo = mid + 1;
        else                       hi = mid;
    }
    if (lo > 0) {
        LogEntryPtr a = model->entryAt(lo - 1);
        LogEntryPtr b = model->entryAt(lo);
        if (a && b) {
            const qint64 distA = qAbs(a->epochMs - epochMs);
            const qint64 distB = qAbs(b->epochMs - epochMs);
            if (distA <= distB) return lo - 1;
        }
    }
    return lo;
}

void CompareWindow::updateAllTimestampReadouts()
{
    for (Pane &p : m_panes) {
        if (!p.view->model()) {
            p.timeLabel->setText(tr("Top: —"));
            continue;
        }
        auto *model = qobject_cast<LogModel*>(p.view->model());
        if (!model) continue;
        const int top = p.view->rowAt(0);
        if (top < 0) {
            p.timeLabel->setText(tr("Top: —"));
            continue;
        }
        LogEntryPtr e = model->entryAt(top);
        if (!e) continue;
        const QString t = QDateTime::fromMSecsSinceEpoch(e->epochMs)
                              .toString("HH:mm:ss.zzz");
        p.timeLabel->setText(tr("Top: %1").arg(t));
    }
}

void CompareWindow::onLayoutToggled(bool vertical)
{
    applyLayoutOrientation(vertical);
}

void CompareWindow::applyLayoutOrientation(bool vertical)
{
    if (!m_splitter) return;
    m_splitter->setOrientation(vertical ? Qt::Vertical : Qt::Horizontal);
}

void CompareWindow::closeEvent(QCloseEvent *event)
{
    // These windows are WA_DeleteOnClose, so this is the last
    // point at which the geometry still exists to be read.
    WindowGeometry::save(this, QStringLiteral("compareWindow"));
    QDialog::closeEvent(event);
}

void CompareWindow::setSessionKeys(SessionKeyStore *keys)
{
    m_sessionKeys = keys;
    if (m_fieldPanel) m_fieldPanel->setSessionKeys(keys);
}

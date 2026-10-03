#ifndef COMPAREWINDOW_H
#define COMPAREWINDOW_H

// =============================================================================
//  CompareWindow
//  -----------------------------------------------------------------------------
//  N-pane side-by-side viewer for source tabs. Two panes by default
//  (matches the original two-pane behavior). Add/Remove buttons grow or
//  shrink the row of panes at runtime.
//
//  Why N panes?
//    Operators sometimes need to look at 3 controllers at once (e.g. a
//    stationary unit + two locos in formation). The two-pane layout
//    forced them to flip between compare windows. Generalizing means one
//    window, one time-lock, one raw-bytes view.
//
//  Layout (horizontal):
//      ┌─ Compare tabs ─────────────────────────────────────────────────┐
//      │  [+ Add pane] [- Remove pane]   ☑ Time-lock   ◯ ☐ Layout: H/V  │
//      ├──────────────────┬──────────────────┬─────────────────────────┤
//      │ [Pane 1 src ▾]   │ [Pane 2 src ▾]   │ [Pane 3 src ▾]          │
//      │ Top: 14:23:01.5  │ Top: 14:23:01.4  │ Top: 14:23:01.6         │
//      │ ┌──────────────┐ │ ┌──────────────┐ │ ┌─────────────────────┐ │
//      │ │ rows         │ │ │ rows         │ │ │ rows                │ │
//      │ └──────────────┘ │ └──────────────┘ │ └─────────────────────┘ │
//      ├──────────────────┴──────────────────┴─────────────────────────┤
//      │  Raw bytes for last-selected row (any pane)                    │
//      └────────────────────────────────────────────────────────────────┘
//
//  Time-lock with N panes:
//    When any pane is scrolled, every OTHER pane jumps to its nearest
//    message in time. A single re-entry guard (m_syncing) covers the
//    whole batch — the propagating pane sets it, drives the others,
//    clears it. Other panes' scroll handlers see m_syncing == true and
//    bail.
//
//  Layout toggle:
//    Horizontal (default): panes side-by-side in a horizontal QSplitter.
//    Good for wide monitors and 2-3 panes.
//    Vertical: panes stacked top-to-bottom. Good for tall monitors and
//    4+ panes, or when each pane has long messages and benefits from
//    full width.
//
//  Multiple compare windows:
//    Still supported — open as many CompareWindow instances as you like.
//    Each is independent.
// =============================================================================

#include <QDialog>
#include <QPointer>
#include <QString>
#include <QVector>

#include "logentry.h"

class BookmarkStore;
class ColorRules;
class LocoIdentity;
class FieldInspector;
class LogModel;
class MessageDispatcher;
class NameMap;
class RawBytesPanel;
class FindBar;
class DecodeWorkbench;
class FrameDiffWindow;
class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QMenu;
class QModelIndex;
class QPushButton;
class QSplitter;
class QTableView;
class QVBoxLayout;
class QWidget;

class CompareWindow : public QDialog
{
    Q_OBJECT

public:
    // Session 96: the live session keys (MainWindow's): its field panel's
    // MAC row and its Decode Workbench check against them.
    void setSessionKeys(class SessionKeyStore *keys);
    CompareWindow(MessageDispatcher *dispatcher,
                  const NameMap     *names,
                  QWidget           *parent = nullptr);

    // Borrowed, both optional. Supplied by MainWindow, which owns them.
    //
    // Passed in rather than duplicated: a bookmark set in a compare pane and
    // a bookmark set in a log tab are the same bookmark on the same frame,
    // and two stores would mean a mark that exists in one window and not the
    // other. Without them the actions that need them stay disabled rather
    // than silently doing nothing.
    void setBookmarks(BookmarkStore *bookmarks);
    void setColorRules(const ColorRules *rules) { m_rules = rules; }

    // The same learned identity the log tabs use. Borrowed for the same
    // reason bookmarks are: a loco identified in one window is the same loco
    // in the other, and two trackers would disagree.
    void setLocoIdentity(LocoIdentity *identity);

    // The right-click menu for one row of one pane.
    //
    // Public, and returning the menu rather than showing it, so the suite
    // can check what it offers and trigger it. A menu built and executed
    // inside the handler that receives the click cannot be tested without
    // synthesising the click, and a menu nobody can test is a menu whose
    // actions quietly stop working.
    //
    // Ownership passes to the caller. Returns nullptr when the pane or the
    // row is not something a menu can act on.
    QMenu *buildRowMenu(int paneIndex, const QModelIndex &index,
                        QWidget *parent);
    // How many panes, and the table in one of them. buildRowMenu already
    // addresses panes by index; without these there is no way to reach the
    // view that index refers to, and widget-tree order is not pane order.
    int paneCount() const { return m_panes.size(); }
    QTableView *paneView(int paneIndex) const;

signals:
    // Raised rather than acted on, for the same reason FieldInspector
    // raises them: this window knows which field and which frame, and
    // nothing about where the pin board, the plot or the Packet Maker
    // live. Opening windows is MainWindow's job everywhere else here.
    // sourceKey and captype travel with the name so the pin is narrowed
    // exactly as one made in a log tab: the operator pointed at a value in
    // ONE packet of ONE source, and an unnarrowed pin would then show
    // whichever loco spoke last.
    void pinFieldRequested(const QString &fieldName, const QString &sourceKey,
                           const QString &captype);
    void plotFieldRequested(const QString &fieldName, const QString &sourceKey);
    void packetMakerRequested(const QString &buffer);
    void locateFieldRequested(const QString &fieldName);

protected:
    // Remember size/position for the next opening.
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onAddPane();
    void onRemovePane();
    void onLayoutToggled(bool vertical);
    void onPaneSourceChanged(int paneIndex, int comboIndex);
    void onPaneScrolled(int paneIndex);
    void onPaneSelectionChanged(int paneIndex,
                                 const QModelIndex &cur,
                                 const QModelIndex &prev);
    void onDispatcherTabRequested(QString tabKey, QString friendlyName);

    // The same reading tools the log tabs and the recorded-session window
    // have. A compare window is where a difference between two sources is
    // actually studied, and it had no way to search, no way to jump to a
    // time, and no way to open a row — so the work moved back to a single
    // tab, which is the thing the window exists to avoid.
    void onFind();
    void onGotoTimestamp();
    void onNextProblem();
    void onPrevProblem();
    void onRowToWorkbench();
    void onPaneContextMenu(int paneIndex, const QPoint &pos);

    // The copies. The message alone is Ctrl+C, because that is what gets
    // pasted into a decoder or a mail; the full tab-separated row keeps
    // Ctrl+Shift+C, because that is what a report wants.
    void onCopyMessage();
    void onCopyRow();

    // Bookmarks, on the same keys as the log tabs. The store is shared, so
    // a frame marked here is marked there.
    void onToggleBookmark();
    void onNextBookmark();
    void onPrevBookmark();
    void onDiffAcrossPanes();
    void updateAllTimestampReadouts();

private:
    // Per-pane bundle. Replaces the old left*/right* member pairs.
    // Owned by Qt parent-child chain via the splitter container.
    struct Pane {
        QWidget    *container = nullptr;   // outer widget added to splitter
        QComboBox  *sourceBox = nullptr;
        QLabel     *timeLabel = nullptr;
        QTableView *view      = nullptr;
        FindBar    *findBar   = nullptr;   // per pane, like the log tabs
        QString     currentKey;            // tab key currently shown
        // Wired by rebindPane and dropped before it wires again. Not
        // Qt::UniqueConnection: that needs a member-function slot, and Qt 6
        // aborts on it with a lambda (Qt 5 silently stacked duplicates).
        QMetaObject::Connection scrollConn;
        QMetaObject::Connection selectionConn;
    };

    QVector<Pane> m_panes;

    // Build a fresh pane (combo + label + view inside a container) and
    // append it to m_splitter. Wires up signals to slots, indexed by
    // the pane's position in m_panes at the time of creation.
    void appendPane();
    void buildMenus();

    // The pane the operator is working in: whichever last had a selection,
    // falling back to the first. Every tool here acts on one pane, and
    // guessing wrong sends the operator somewhere they were not looking.
    int  activePane() const;
    void stepProblem(int dir);
    void stepBookmark(int dir);

    // The entry under a row of a pane, or null. The panes bind their
    // LogModel directly — no filter proxy — so this is a straight lookup,
    // but it is the same three lines in six places otherwise.
    LogEntryPtr entryAt(int paneIndex, const QModelIndex &index) const;
    LogEntryPtr currentEntry(int paneIndex) const;

    // Everything selected in a pane, in view order rather than click order.
    // A report wants time order, and "as displayed" is what the operator
    // just made with a shift-click.
    QVector<LogEntryPtr> selectedEntries(int paneIndex) const;

    int m_activePane = 0;
    QPointer<DecodeWorkbench> m_workbench;
    QPointer<FrameDiffWindow> m_frameDiff;

    // Remove the rightmost / bottommost pane. No-op if only 1 left
    // (we don't allow zero panes — leaves an awkward empty window).
    void removeLastPane();

    // Re-populate every pane's source picker with the current set of
    // known keys. Preserves each pane's selected key. Called on
    // construction and whenever the dispatcher reports a new tab.
    void refreshAllSourcePickers();

    // Re-attach the pane at index `paneIndex` to the model for its
    // currentKey. Called after the user picks a new source from the
    // combo box, and after appendPane() if the pane has a default key.
    void rebindPane(int paneIndex);

    // Binary search on epochMs. Same algorithm as the previous
    // two-pane implementation — proven correct in earlier tests.
    int findNearestRowByTime(LogModel *model, qint64 epochMs) const;

    // Switch the splitter orientation between Qt::Horizontal and
    // Qt::Vertical without disturbing pane state.
    void applyLayoutOrientation(bool vertical);

    MessageDispatcher *m_dispatcher = nullptr;
    const NameMap     *m_names      = nullptr;

    // Toolbar widgets
    QPushButton *m_addBtn       = nullptr;
    QPushButton *m_removeBtn    = nullptr;
    QCheckBox   *m_timeLockCb   = nullptr;
    QCheckBox   *m_verticalCb   = nullptr;

    QSplitter   *m_splitter     = nullptr;
    RawBytesPanel  *m_rawPanel   = nullptr;
    FieldInspector *m_fieldPanel = nullptr;
    BookmarkStore  *m_bookmarks  = nullptr;
    const ColorRules *m_rules    = nullptr;
    LocoIdentity     *m_identity = nullptr;
    QAction        *m_bookmarkAction = nullptr;

    // Re-entry guard for the bidirectional (now N-directional) time-lock.
    // True while a scroll on one pane is propagating to all the others.
    bool m_syncing = false;

    // Set by appendPane to feed paneIndex into lambda-wrapped slot calls.
    // Computed at lambda capture time so it stays correct even if the
    // pane vector grows.
    class SessionKeyStore *m_sessionKeys = nullptr;   // not owned
};

#endif // COMPAREWINDOW_H

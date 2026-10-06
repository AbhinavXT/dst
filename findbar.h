#ifndef FINDBAR_H
#define FINDBAR_H

// =============================================================================
//  FindBar
//  -----------------------------------------------------------------------------
//  Per-tab find (Ctrl+F), distinct from FilterBar.
//
//  Filter vs. find:
//    – FilterBar HIDES non-matching rows.  Useful when you want to ignore
//      noise for an extended period.
//    – FindBar KEEPS all rows visible and JUMPS to matches.  Useful when
//      you want to navigate to a specific moment without losing context.
//
//  TWO SHAPES, ONE WIDGET
//    Find opens as a WINDOW by default and can be docked into the tab as a
//    one-line strip. Both are this same widget: detaching reparents it
//    rather than building a second find UI, so there is one set of state,
//    one scan and one cursor to keep straight.
//
//    The window is the real UI — the input gets a row of its own, the mode
//    and the options get room to be spelled out, and the advanced section
//    holds the things that change how the search behaves rather than what
//    it looks for. The strip is the compact shape: the same controls with
//    short labels, no advanced section, and the ⧉ button to open the window
//    when more room is wanted.
//
//  SEARCH MODES
//    Text      plain substring, honouring Case and Whole word.
//    Extended  the same, after \n \r \t \0 \xHH \uHHHH \\ are turned into
//              the characters they name.
//    Regex     a QRegularExpression written by the operator.
//    Hex       BYTES of the datagram, not text: "0a 1b" matches the raw
//              frame, whatever the message column happens to render.
//    Query     the boolean language, identical to the filter bar's.
//
//    These are a MODE rather than five checkboxes because they answer the
//    same question five ways. As checkboxes, "regex + query" had to be
//    resolved by disabling boxes at scan time, and the operator's only clue
//    about which one won was a control greying out.
//
//  Search target:
//    The bar searches the PROXY model that the table is actually showing,
//    not the source LogModel. That way an active filter is honored — if
//    the user has filtered to errors and then Ctrl+F's "STN_ID", they
//    navigate among visible matching rows, not among hidden ones.
//
//  Live tabs (session 157):
//    A search covers the rows that were there when it ran. Traffic arriving
//    afterwards is not searched and starts no scan: matches stay where they
//    were and Next / Previous walk them. Typing or changing an option runs
//    the search again, over whatever has arrived by then. Before this, every
//    batch restarted the 200 ms debounce, so under steady traffic the search
//    the operator typed waited for a gap in the packets.
//
//  Per-tab:
//    Each tab gets its own FindBar instance. Find state (last query, last
//    match) is local to the tab. This matches what users expect from
//    multi-document editors.
// =============================================================================

#include <QPointer>
#include <QVector>
#include <QWidget>

#include "logentry.h"
#include "logquery.h"

class QAbstractItemModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QueryLineEdit;
class QPushButton;
class QTableView;
class QToolButton;

class FindBar : public QWidget
{
    Q_OBJECT

public:
    // The five ways of asking. The numbers are persisted (Settings::findMode)
    // so they must not be reordered; new modes append.
    enum class Mode {
        Text     = 0,
        Extended = 1,
        Regex    = 2,
        Hex      = 3,
        Query    = 4
    };

    // 'view' is the table view this bar searches. We need the view (not
    // just its model) so we can drive the selection / scrollTo. We do
    // NOT take ownership; the view outlives us.
    explicit FindBar(QTableView *view, QWidget *parent = nullptr);

    // True while the bar is parked on a match, i.e. the user has asked to
    // look at one specific row. Whoever owns the view's follow-the-tail
    // behaviour (MainWindow's scroll lock) must not scroll to the bottom
    // while this is true, otherwise the view ping-pongs between the match
    // and the log end once per arriving batch.
    bool holdsView() const;

    // Told by the owner whether the tab is currently following new
    // messages. Purely cosmetic: it decides whether the bar explains that
    // following is paused, since a paused follow with no explanation looks
    // like scroll lock has broken.
    void setScrollLockActive(bool on);

    // Floating vs. docked. Detaching REPARENTS this same widget into a
    // window rather than building a second find UI — one set of state, one
    // scan, one cursor. A separate dialog with its own copy of the logic
    // would be two things to keep in step, and they would not stay in step.
    bool isDetached() const { return m_host != nullptr; }

    // What this bar searches, for the floating window's title. With a bar
    // per tab, a window titled only "Find" can end up searching a tab that
    // is not even on screen.
    void setContextLabel(const QString &label);

    // Current mode, and the search text. Setting either re-scans.
    Mode mode() const;
    void setMode(Mode m);
    QString searchText() const;
    void setSearchText(const QString &text);

    // How many rows the last scan matched, and where the cursor sits in
    // that set (-1 for nowhere). For tests and for anyone who wants to
    // report on a search without reading a label back.
    int matchCount() const { return m_matches.size(); }
    // The matching rows themselves, in view order. matchCount alone cannot
    // say WHICH rows, which is what the narrowing path has to be checked
    // against: it must find exactly what a full scan finds.
    QVector<int> matchRows() const { return m_matches; }
    int matchCursor() const { return m_cursor; }

    // ---- pure text rules, exposed for testing -----------------------------
    //
    // The escape rules are the whole of Extended mode, and they are easier
    // to get wrong than they look (a trailing backslash, \x with one digit,
    // an escape nobody defined). Kept as a function over strings so they can
    // be tested without a window.
    struct Unescaped {
        QString text;
        QString error;
        int     errorOffset = -1;
        bool    ok          = false;
    };
    static Unescaped unescapeExtended(const QString &pattern);

    // The same search as a LogQuery, for "All tabs" (session 82): the search
    // window speaks only the query language. Text becomes a quoted phrase,
    // Extended its unescaped text, Regex /…/, Hex hex:"…", Query itself.
    // Empty with `error` set when it cannot be said (an Extended escape
    // error, a regex containing '/').
    static QString toLogQuery(Mode m, const QString &text, QString *error = nullptr);

    // One line saying what a mode accepts, shown under the mode selector.
    static QString modeHelp(Mode m);

signals:
    // Emitted when holdsView() flips. The owner re-reads holdsView() and,
    // on release, snaps back to the tail if it is still following.
    void holdChanged(bool held);

    // "All tabs": search every source for what this bar is searching for,
    // as a LogQuery (session 82).
    void searchAllTabs(const QString &query);

public slots:
    // Show + focus the input. Called by the parent tab on Ctrl+F.
    void activate();

    // Told by the owner that the view has been given a different model.
    //
    // Qt emits nothing when QAbstractItemView::setModel() is called, so a bar
    // cannot notice on its own, and the compare window is the one place that
    // swaps a model under a view that is already on screen. One call rather
    // than a timer polling the view forever.
    void refreshModelBinding();

    void setDetached(bool on);
    void toggleDetached() { setDetached(!isDetached()); }
    // Hide; restore focus to the table view.
    void close();

private slots:
    void findNext();
    void findPrev();
    void onTextChanged();
    void rebuildMatches();      // re-scan for matches after filter changed
    void updateCountLabel();

private:
    // Build the two layouts. buildWidgets() runs once; applyShape() moves the
    // same widgets between the strip and the window form and is called on
    // every dock/undock.
    void buildWidgets();
    void applyShape();
    void setAdvancedOpen(bool on);

    // Which controls make sense in the current mode, plus the help line.
    // One place, called after every mode change, rather than the scan
    // deciding what to grey out as a side effect of running.
    void syncModeControls();

    void scanMatches();         // walk the proxy and collect matching rows

    // What the last scan looked for, so the next one can tell whether it is
    // a narrowing of the same search — see the narrowing block in
    // scanMatches. m_scanDirty says the rows moved and the answer is no.
    QString m_scanText;
    Mode    m_scanMode          = Mode::Text;
    bool    m_scanCase          = false;
    int     m_scanColumn        = -1;
    int     m_lastScanRowsTotal = -1;
    bool    m_lastScanWasCapped = false;
    bool    m_scanDirty         = true;

    // Session 157: a search covers the rows present when it ran. Rows that
    // arrive afterwards are not searched until the search is run again
    // (typing, or changing an option); rows inserted or removed underneath
    // only move the matches' row numbers, without a rescan.
    void onRowsInserted(const QModelIndex &parent, int first, int last);
    void onRowsRemoved(const QModelIndex &parent, int first, int last);

    // Re-point the model-change connections at whatever the view is showing
    // now. See the comment on the definition: a compare pane swaps its model
    // under the bar, so wiring once in the constructor is not enough.
    void ensureModelWiring();

    // Push the current match set into the LogModel so every hit is tinted,
    // or clear it. Called after every scan and when the bar closes — a
    // highlight left behind after the bar is gone is worse than none, since
    // nothing on screen then explains why those rows are coloured.
    void publishHits();
    void jumpToCurrentMatch();  // setCurrentIndex + scrollTo

    // Re-scan because the MODEL changed (new traffic, eviction, filter),
    // not because the user asked for anything. Keeps the cursor on the
    // same LogEntry it was on and never moves the view — a live tab would
    // otherwise yank the operator back to match #1 every 200 ms.
    void rescanKeepingPosition();

    // Recompute holdsView(), refresh the hint, and emit holdChanged on a
    // transition. Called after anything that can change the cursor or the
    // bar's visibility.
    void refreshHold();
    void updateHoldHint();

    // The LogEntry behind a proxy row, or null if the view is not sitting
    // on a LogModel (nothing else uses this bar today, but the cast is the
    // only honest way to ask).
    LogEntryPtr entryAtProxyRow(int proxyRow) const;

    // Rows this scan will walk: the operator's limit, 0 meaning everything.
    int scanLimitRows() const;

    QTableView *m_view = nullptr;

    // The model the connections below currently point at, and those
    // connections, so they can be moved when the view changes model.
    QPointer<QAbstractItemModel>     m_wiredModel;
    QVector<QMetaObject::Connection> m_modelConns;

    QueryLineEdit *m_edit    = nullptr;
    QPushButton   *m_allTabsBtn = nullptr;   // session 82
    QLabel     *m_findLabel  = nullptr;
    QComboBox  *m_modeBox    = nullptr;
    QLabel     *m_modeLabel  = nullptr;
    QLabel     *m_inLabel    = nullptr;
    QComboBox  *m_columnBox  = nullptr;
    QCheckBox  *m_caseSens   = nullptr;
    QCheckBox  *m_wholeWord  = nullptr;
    QCheckBox  *m_markAll    = nullptr;
    QCheckBox  *m_wrapCb     = nullptr;
    QComboBox  *m_limitBox   = nullptr;
    QLabel     *m_limitLabel = nullptr;
    QLabel     *m_modeHelp   = nullptr;
    QToolButton *m_advBtn    = nullptr;
    QWidget    *m_advPanel   = nullptr;
    QPushButton *m_detachBtn = nullptr;
    QString      m_contextLabel;
    bool         m_closing = false;   // inside close(): a re-dock is not a choice
    bool         m_building = true;   // suppress rescans while wiring up

    // Where to put this widget back when it re-docks.
    class QDialog   *m_host       = nullptr;
    class QBoxLayout *m_dockLayout = nullptr;
    int              m_dockIndex  = -1;

    // Parsed once per rescan rather than per row: the scan loop below runs
    // over the whole model, and parsing inside it would make each keystroke
    // O(rows x query length).
    LogQuery    m_query;
    bool        m_queryOk   = true;
    QPushButton *m_prevBtn   = nullptr;
    QPushButton *m_nextBtn   = nullptr;
    QPushButton *m_closeBtn  = nullptr;
    QLabel     *m_count      = nullptr;
    QLabel     *m_wrapHint   = nullptr;
    QLabel     *m_holdHint   = nullptr;   // "auto-scroll paused" notice

    // Last value reported through holdChanged(), so the signal fires on
    // transitions only.
    bool        m_hold             = false;
    // Does the owning tab currently follow new messages? Display only.
    bool        m_scrollLockActive = false;
    // Set by the actions the USER takes (typing, toggling a checkbox) and
    // cleared when the debounce fires. It is what tells the timeout apart
    // from a model-driven rebuild: a user rescan jumps to the first match,
    // a model-driven one stays put.
    bool        m_userRescan       = false;

    // Indices in PROXY index space — we want navigation order to match
    // what the user sees (post-filter, post-sort).
    QVector<int> m_matches;
    int          m_cursor = -1;     // index into m_matches, or -1 = none

    // Debounce timer for the text input. Without this, scanning fires
    // on every keystroke; on a 75K-row model the GUI thread stalls long
    // enough between keystrokes that typing feels stuttery. With a
    // 200ms debounce, scan happens once after the user stops typing.
    QTimer *m_debounce = nullptr;

    // Default scan-row cap. To keep the GUI responsive on extremely large
    // models, scanning bails after this many rows and the count label
    // shows "X+ matches in first N rows" so the operator knows the
    // scan was capped. 200k is comfortably above any reasonable
    // single-tab capacity; below that we always scan everything. The
    // advanced section can raise it, lower it, or lift it entirely.
    static constexpr int kScanRowCap = 200000;

    // Was the most recent scan capped? Used by updateCountLabel.
    bool m_lastScanCapped = false;

    // How far the last scan actually walked, so the count label can say
    // "first N rows" with the number that was really used rather than the
    // compiled-in default.
    int  m_lastScanRows = 0;
};

#endif // FINDBAR_H

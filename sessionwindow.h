#ifndef SESSIONWINDOW_H
#define SESSIONWINDOW_H

// =============================================================================
//  SessionWindow
//  -----------------------------------------------------------------------------
//  Read-only viewer for one or more recorded .dlr session files.
//
//  Deliberately a SEPARATE top-level window with its own LogModels rather
//  than loading into the live tabs. Two reasons, and the second is the
//  important one:
//
//    – A live console is something an operator is watching right now.
//      Silently interleaving four hours of yesterday's traffic into it
//      would be indistinguishable from a flood of new messages.
//
//    – More seriously, it would corrupt the disk archive. MainWindow feeds
//      every entry that reaches the live models into LogWriter, so loading
//      an archive into those models would re-record it into today's .log
//      and .dlr — the same traffic appearing twice, on the wrong date,
//      indistinguishable from the real thing. An archive viewer must be a
//      strict consumer.
//
//  Everything else is reused as-is: the same LogModel, the same FilterBar
//  (so filtering, regex, and the severity/direction chips all work), the
//  same RawBytesPanel and FieldInspector, and — critically — the same
//  MessageDispatcher::buildEntry(), so a loaded frame is decoded and
//  classified exactly the way the live path would decode it.
//
//  Re-classification on load is intentional. Severity and colours are NOT
//  stored in the archive; they are re-derived from whatever
//  color_rules.json is in force now. Load a session after fixing a rule and
//  you see the corrected classification, which is the point of keeping the
//  bytes rather than the rendering.
// =============================================================================

#include <QHash>
#include <QPointer>
#include <QMainWindow>
#include <QStringList>
#include <QVector>

#include "logentry.h"
#include "navhistory.h"
#include "notessidecar.h"
#include "reviewpack.h"
#include "theme.h"

class ColorRules;
class FilterBar;
class FindBar;
class BookmarkStore;
class LogModel;
class NameMap;
class RawBytesPanel;
class FieldInspector;
class QLabel;
class QTabWidget;
class QTableView;
class DecodeWorkbench;
class FrameDiffWindow;
class PacketMakerDialog;

class FrameNumberWatch;

class SessionWindow : public QMainWindow
{
    Q_OBJECT

public:
    // `rules` and `names` are borrowed from MainWindow and must outlive this
    // window; it is parented to MainWindow so that holds.
    SessionWindow(const ColorRules *rules,
                  const NameMap    *names,
                  Theme             theme,
                  QWidget          *parent = nullptr);
    ~SessionWindow() override;

    // Session 95: the live frame-number watch its Packet Maker seeds from
    // (MainWindow's), as before when that watch was process-wide.
    void setFrameWatch(class FrameNumberWatch *watch) { m_frameWatch = watch; }
    // ...and the live session keys its Decode Workbench and Packet Maker
    // offer. Its own field panel shows no live MAC row: a recorded frame
    // checked against today's live keys says nothing true.
    void setSessionKeys(class SessionKeyStore *keys) { m_sessionKeys = keys; }

    // Load every given .dlr. Returns the number of records loaded across all
    // files; per-file problems are collected and reported in the status
    // area rather than aborting the whole load, because a partially
    // readable archive is still evidence.
    // Share the live window's bookmark store, so a mark made here is the
    // same mark. Call before loadFiles().
    void setBookmarkStore(BookmarkStore *store) { m_bookmarks = store; }

    qint64 loadFiles(const QStringList &paths);

    void setTheme(Theme t);

    // Session 368: the live window's review tools, on a recording. A file
    // sent from the test site is read here, not in the live tabs, so the
    // DMI clip, radio chart, notes and incident report are here too. Each
    // returns what it opened (null, with `why`, when there is nothing to
    // show), so a test can drive it without a modal loop.
    //
    // The DMI clip of the row's tab, or of the first tab holding @dmi when
    // the row's tab has none (a recording can be split by source).
    class DmiClipDialog *makeClipDialog(const QString &tabKey, const LogEntryPtr &entry,
                                        QString *why = nullptr);
    class RadioChartDialog *makeRadioChart(const QString &tabKey, qint64 atMs);
    // The incident pack over tab `tabKey` around `atMs`, with this
    // window's bookmarks as its Operator notes. Shown.
    class IncidentReportWindow *openIncidentReport(const QString &tabKey, qint64 atMs,
                                                   qint64 beforeMs, qint64 afterMs);
    // Bookmark the row if it is not, and set its note ("" clears the note,
    // the bookmark stays), as Add note… in the live window.
    bool setRowNote(const QString &tabKey, const LogEntryPtr &entry, const QString &note);
    QString noteOf(const QString &tabKey, const LogEntryPtr &entry) const;

    // Session 369: what the last loadFiles() took from the notes files
    // beside the recordings (x.notes.json next to x.dlr).
    NotesSidecar::Merge lastMerge() const { return m_merge; }

    // Session 370: this window shows a review pack. A "Review pack" panel,
    // in front: what to look at, who sent it from where, the schema it was
    // decoded with there (same as here, or not), its test run.
    void setReviewPack(const ReviewPack::Opened &pack);

protected:
    // Remember size/position for the next opening.
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onSelectionChanged();
    void onRowContextMenu(const QPoint &pos);

    // The same tools the live window offers, on the same rows.
    //
    // An archive is where most of the careful work happens: the live window
    // is watched, the recording is READ. Having to re-open a capture in the
    // live console to decode one frame — which is what the operator was
    // doing — is the wrong way round, and it is also the dangerous way
    // round, because the live models are wired to the disk writer.
    void onToolWorkbench();
    void onToolRowToWorkbench();
    void onToolRowToPacketMaker();
    void onToolFrameDiff();
    void onToolPlotField();

    // Ctrl+F, on the tab being looked at. A recording is READ more than a
    // live log is — it is where the careful work happens — and it had every
    // filtering tool except the one for "where does this word appear".
    void onFind();
    void onGotoTimestamp();
    void onNextProblem();
    void onPrevProblem();
    void onToggleBookmark();
    void onNextBookmark();
    void onPrevBookmark();
    void onIncidentReport();          // session 368: Tools ▸ Incident report…

private:
    LogModel *modelForKey(const QString &tabKey);
    void      buildTab(const QString &tabKey, LogModel *model);

    struct TabUi {
        QTableView *view      = nullptr;
        FilterBar  *filterBar = nullptr;
        LogModel   *model     = nullptr;
        FindBar    *findBar   = nullptr;
        class TimelineRibbon  *ribbon = nullptr;   // session 348
        class MarkerScrollBar *marks  = nullptr;
    };

    void buildToolsMenu();
    // Session 84: tell the DMI broker the cursor is at this row of tab `tabKey`.
    void offerDmiAt(const QString &tabKey, const QModelIndex &proxyIndex);
    void jumpToProxyRow(int row);     // select, centre, focus
    // Session 348: the nearest row to `epochMs` in tab `tabKey`.
    void jumpToTime(const QString &tabKey, qint64 epochMs);
    NavPoint currentNavPoint() const;
    void navigate(bool back);         // Alt+Left / Alt+Right
    // Session 349: the Summary dock, per source, with its quiet spans.
    void rebuildSummary();
    void stepProblem(int dir);
    void stepBookmark(int dir);
    QString tabName(const QString &tabKey) const;
    // Session 369: rewrite the notes file beside each recording of `tabKey`.
    void writeSidecars(const QString &tabKey);
    void refreshMarks(const QString &tabKey, int sourceRow);

    // The tab the operator is looking at, and what is selected in it.
    QString      currentTabKey() const;
    LogEntryPtr  currentEntry() const;
    QVector<LogEntryPtr> selectedEntries() const;
    LogEntryPtr  entryFor(const TabUi &t, const QModelIndex &proxyIndex) const;

    const ColorRules *m_rules = nullptr;
    const NameMap    *m_names = nullptr;
    Theme             m_theme = Theme::Light;

    QTabWidget    *m_tabs     = nullptr;
    RawBytesPanel *m_rawPanel   = nullptr;
    FieldInspector *m_fieldPanel = nullptr;
    QLabel        *m_lblSummary = nullptr;
    class QTreeWidget *m_summary  = nullptr;   // session 349
    class QSpinBox    *m_gapOver  = nullptr;   // quiet spans longer than this, s

    QHash<QString, LogModel*> m_models;
    QHash<QString, TabUi>     m_tabUi;

    QStringList m_warnings;

    // Session 369: each file loaded, the tab it fed and the span it held, so
    // a note made here goes into the notes file beside the right one.
    struct LoadedFile { QString path, tabKey; qint64 firstMs = 0, lastMs = 0; };
    QVector<LoadedFile> m_files;
    NotesSidecar::Merge m_merge;
    QString m_packTitle;   // session 370: the pack's site, for the title

    // Session 348: Back/Forward over this window's jumps, as the live window.
    NavHistory m_nav;
    bool       m_navReplaying = false;   // Back/Forward's own jump is not recorded
    class QAction *m_actBack    = nullptr;
    class QAction *m_actForward = nullptr;

    // Borrowed, not owned. Bookmarks set while reviewing a recording go into
    // the same store the live window uses, keyed by (tabKey, epochMs) — the
    // same row bookmarked live is the same row bookmarked here, which is the
    // behaviour anyone would assume. Null is handled: the window still works,
    // it just cannot remember marks between openings.
    BookmarkStore *m_bookmarks = nullptr;

    // Reused between invocations, like the live window's, so a second
    // "open this row" lands in the window already on screen.
    QPointer<DecodeWorkbench>   m_workbench;
    QPointer<FrameDiffWindow>   m_frameDiff;
    QPointer<PacketMakerDialog> m_packetMaker;
    class FrameNumberWatch *m_frameWatch = nullptr;   // not owned
    class SessionKeyStore *m_sessionKeys = nullptr;   // not owned
};

#endif // SESSIONWINDOW_H

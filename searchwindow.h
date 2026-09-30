#ifndef SEARCHWINDOW_H
#define SEARCHWINDOW_H

// =============================================================================
//  SearchWindow
//  -----------------------------------------------------------------------------
//  Runs one LogQuery across EVERY source at once and lists the hits in a
//  single ranked-by-time table.
//
//  WHY
//    Filter and Find are both per-tab. The question "where else did this
//    appear?" — which is most of incident work, because a fault in one
//    subsystem shows up as a symptom in another — currently means opening
//    each tab in turn and re-typing the same filter. With a dozen sources
//    that is slow enough that people stop asking the question.
//
//  RESULTS ARE A SNAPSHOT
//    The search runs once against the models as they stand and does not
//    live-update. That is deliberate: results are ordered by time and
//    double-clicking one jumps to it in its live tab, so a list that
//    reshuffled underneath the pointer while traffic arrived would be
//    unusable. Re-run explicitly to pick up newer messages; the header says
//    when the snapshot was taken.
//
//  The result table is a plain QTableWidget rather than another LogModel:
//  rows come from many models and carry their source with them, so there is
//  no single underlying model to proxy.
// =============================================================================

#include <QDialog>
#include <QVector>

#include "logentry.h"

class MessageDispatcher;
class NameMap;
class QCheckBox;
class QLabel;
class StatusLine;
class QueryLineEdit;
class QPushButton;
class QTableWidget;

class SearchWindow : public QDialog
{
    Q_OBJECT

public:
    SearchWindow(MessageDispatcher *dispatcher,
                 const NameMap     *names,
                 QWidget           *parent = nullptr);

    // Pre-fill and immediately run — used when opening from a tab that
    // already has a filter typed into it.
    void setQueryText(const QString &text, bool runNow = true);
    // Session 82: grouped by tab (set before setQueryText to run grouped).
    void setGroupByTab(bool on);
    int resultRowCount() const;          // rows shown, headings included
    int hitCount() const;                // hits only

signals:
    // Emitted on double-click. MainWindow switches to that tab and selects
    // the row; the search window itself knows nothing about tab UI.
    void jumpRequested(const QString &tabKey, qint64 epochMs);

protected:
    // Remember size/position for the next opening.
    void closeEvent(QCloseEvent *event) override;

private slots:
    void runSearch();
    void onCellActivated(int row, int column);

private:
    // What the last search was for, so an empty result table can say
    // "no matches for X" rather than leaving the operator to guess
    // whether the search ran at all.
    QString m_lastQuery;

    void setStatus(const QString &text, bool isError);

    MessageDispatcher *m_dispatcher = nullptr;
    const NameMap     *m_names      = nullptr;

    QueryLineEdit *m_edit    = nullptr;
    QCheckBox    *m_capOnly  = nullptr;
    QCheckBox    *m_group    = nullptr;   // session 82
    QPushButton  *m_runBtn   = nullptr;
    StatusLine   *m_status   = nullptr;
    QLabel       *m_explain  = nullptr;
    QTableWidget *m_results  = nullptr;

    // Parallel to the result rows: which tab each hit came from and when.
    struct Hit { QString tabKey; qint64 epochMs; };
    QVector<Hit> m_hits;

    // Hard cap on collected results. A query like `dir:in` against a dozen
    // full 200k-row tabs matches millions of rows; building that many
    // table items would hang the GUI for minutes and help nobody. We stop
    // at the cap and say so.
    static constexpr int kMaxResults = 5000;
};

#endif // SEARCHWINDOW_H

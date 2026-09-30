#ifndef MERGEDWINDOW_H
#define MERGEDWINDOW_H

// =============================================================================
//  MergedWindow
//  -----------------------------------------------------------------------------
//  Every source interleaved into one time-ordered stream.
//
//  WHY
//    The per-tab layout answers "what did this source say?". Incident work
//    asks the other question — "what happened, in order?" — and that one
//    spans sources by definition: a loco fault shows up as a VCC message,
//    then a RAD exchange, then an alarm, each in a different tab. Reading
//    causality out of three tabs by comparing timestamps by eye is slow and
//    error-prone. CompareWindow's synchronised panes help, but they still
//    make you do the interleaving in your head.
//
//  IMPLEMENTATION
//    This is just another LogModel, fed from
//    MessageDispatcher::allEntriesAppended. That is the whole trick, and it
//    is why this window is small: a LogModel already has the columns, the
//    severity colouring, the theme handling and the ring buffer, and
//    FilterBar already works against one — so the merged view gets the
//    filter chips and the full query language without a line of new code.
//
//    The alternative, a proxy that merges N models live, would have to cope
//    with each source evicting independently and would need a custom
//    mapping layer for every one of those features.
//
//    The Source and Name columns already come from each entry, so rows
//    identify themselves without any extra plumbing.
//
//  ENTRIES ARE SHARED, NOT COPIED
//    LogEntryPtr is a shared pointer, so a row here is the same object as
//    the row in its own tab. Bookmarking in one place shows up in the
//    other, which is the behaviour you want and comes for free.
// =============================================================================

#include <QMainWindow>

#include "logentry.h"
#include "theme.h"

class FilterBar;
class LogModel;
class MessageDispatcher;
class NameMap;
class QCheckBox;
class QLabel;
class QTableView;

class MergedWindow : public QMainWindow
{
    Q_OBJECT

public:
    MergedWindow(MessageDispatcher *dispatcher,
                 const NameMap     *names,
                 Theme              theme,
                 int                capacity,
                 QWidget           *parent = nullptr);

    void setTheme(Theme t);

    // Seed with everything the live tabs already hold, so opening the
    // window mid-session shows history rather than starting blank.
    void primeFromExisting();

signals:
    void jumpRequested(const QString &tabKey, qint64 epochMs);

protected:
    // Remember size/position for the next opening.
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onAllEntries(QVector<LogEntryPtr> entries);
    void onRowActivated();

private:
    MessageDispatcher *m_dispatcher = nullptr;
    const NameMap     *m_names      = nullptr;

    LogModel   *m_model     = nullptr;
    FilterBar  *m_filterBar = nullptr;
    QTableView *m_view      = nullptr;
    QCheckBox  *m_follow    = nullptr;
    QLabel     *m_count     = nullptr;
};

#endif // MERGEDWINDOW_H

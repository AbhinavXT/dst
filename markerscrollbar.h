#ifndef MARKERSCROLLBAR_H
#define MARKERSCROLLBAR_H

// =============================================================================
//  MarkerScrollBar
//  -----------------------------------------------------------------------------
//  A vertical scrollbar that paints marks for errors, warnings and
//  bookmarks at their positions in the list — the IDE minimap idea.
//
//  WHY
//    The timeline ribbon answers "when", in time. This answers "where", in
//    the list you are actually scrolling. They are not the same axis: a
//    quiet hour occupies most of the ribbon and almost none of the
//    scrollbar. With 200,000 rows, three errors are invisible until you
//    happen to scroll past them; here they are three red lines you can aim
//    at.
//
//  WHY SUBCLASS RATHER THAN OVERLAY
//    An overlaid widget has to track the scrollbar's geometry, stacking
//    order, and style-dependent margins, and it breaks differently on every
//    platform style. Painting inside the scrollbar's own paintEvent gets
//    the geometry for free and cannot drift out of alignment.
//
//  COST
//    Marks are binned by row into one bucket per pixel of track height, and
//    the scan is throttled and only runs when marked dirty. It reads the
//    PROXY, so marks reflect what is actually in the list after filtering —
//    a mark you cannot scroll to would be worse than no mark.
// =============================================================================

#include <QScrollBar>
#include <QVector>

class LogModel;
class QAbstractItemModel;

struct RowMarks {
    struct Mark { bool error = false; bool warn = false; bool bookmark = false; };
    QVector<Mark> buckets;
    int rows = 0;
    bool isEmpty() const { return rows == 0; }
};

// Bin the proxy's rows into `bucketCount` slots. `proxy` may be the model
// itself when unfiltered. Safe with nulls.
RowMarks binRowMarks(const QAbstractItemModel *proxy,
                     const LogModel *source,
                     int bucketCount);

// The next row worth stopping at, searching from `from` in direction `dir`
// (+1 forward, -1 back). Returns a PROXY row, or -1 when there is none.
//
// The minimap has always shown where the errors and warnings are; there was
// no way to move between them, so finding the three bad frames in a
// two-hour capture meant dragging the scrollbar and squinting at the marks.
// This is the same question binRowMarks answers for drawing, asked one row
// at a time — same mapping, same severity test, so what the operator lands
// on is always a row the minimap has a mark for.
//
// Deliberately does NOT wrap. Wrapping past the end of a log without saying
// so puts the operator somewhere they did not ask to be, and in a file this
// long they may not notice they have gone back to the top.
int nextMarkedRow(const QAbstractItemModel *proxy,
                  const LogModel *source,
                  int from, int dir, bool errorsOnly = false);

// The same walk, for bookmarks. Stepping through what is VISIBLE rather than
// through the bookmark store matters: a bookmark hidden by the current filter
// would otherwise scroll the view to a row the operator cannot see, which
// reads as the shortcut being broken.
int nextBookmarkedRow(const QAbstractItemModel *proxy,
                      const LogModel *source,
                      int from, int dir);

class MarkerScrollBar : public QScrollBar
{
    Q_OBJECT

public:
    explicit MarkerScrollBar(QWidget *parent = nullptr);

    void setModels(QAbstractItemModel *proxy, LogModel *source);
    void refreshLater();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void rebuild();

    QAbstractItemModel *m_proxy  = nullptr;
    LogModel           *m_source = nullptr;
    RowMarks            m_marks;
    class QTimer       *m_timer  = nullptr;
    bool                m_dirty  = true;
};

#endif // MARKERSCROLLBAR_H

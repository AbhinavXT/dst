#ifndef TIMELINERIBBON_H
#define TIMELINERIBBON_H

// =============================================================================
//  TimelineRibbon
//  -----------------------------------------------------------------------------
//  A thin strip above a log table showing message density over time, with
//  errors and warnings called out.
//
//  WHY
//    A 200,000-row table gives no sense of shape. "When did it go wrong?"
//    currently means scrubbing the scrollbar and watching timestamps go by.
//    Density over time answers it at a glance: a burst, a gap, a cluster of
//    red. The data is already there and already sorted — epochMs is
//    monotonic within a model — so this is close to free.
//
//  ENCODING, AND WHY IT IS NOT JUST A HISTOGRAM
//    Bar height is the total message count in that time bucket, log-scaled.
//    But severity is NOT encoded as a proportion of the bar, because that
//    is exactly the encoding that hides what you are looking for: one error
//    among five hundred routine messages would be a sub-pixel sliver and
//    might as well not be drawn.
//
//    Instead any bucket containing a warning or an error gets a
//    full-height tick in that colour, drawn over the density bar. Rare
//    severe events stay visible at any zoom and any traffic rate, which is
//    the entire job. Density and severity are different questions and get
//    different marks.
//
//  INTERACTION
//    Click seeks. Drag selects a time range and emits it — MainWindow turns
//    that into an `after:… before:…` query, which is how you get a
//    time-window filter without inventing separate UI for it.
//
//  COST
//    Binning walks the model, so it is throttled behind a timer and only
//    runs when marked dirty. Bucket count is capped at the widget's pixel
//    width: finer than that cannot be drawn and only costs time.
// =============================================================================

#include <QVector>
#include <QWidget>

class LogModel;

// ---- binning (separated from painting so it can be tested on its own) ----

struct TimelineBins {
    struct Bin {
        int total     = 0;
        int warn      = 0;
        int error     = 0;
        int bookmarks = 0;
    };

    QVector<Bin> bins;
    qint64 minMs    = 0;
    qint64 maxMs    = 0;
    int    maxTotal = 0;     // largest `total` across bins, for scaling
    int    entries  = 0;

    bool isEmpty() const { return entries == 0; }

    // Bucket index for a timestamp, or -1 if outside the span. Public so
    // the viewport overlay and the hover tooltip agree with the bars
    // exactly rather than each re-deriving the mapping.
    int binForTime(qint64 ms) const;
    // Midpoint time of a bucket — what a click on it means.
    qint64 timeForBin(int index) const;
};

// Walk a model and bin it. Safe with a null model or zero bins.
TimelineBins binLogModel(const LogModel *model, int binCount);

// ---- widget --------------------------------------------------------------

class TimelineRibbon : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineRibbon(QWidget *parent = nullptr);

    void setModel(LogModel *model);

    // Mark stale. Cheap: the rebuild happens on the next timer tick, so a
    // thousand-message burst causes one rebuild, not a thousand.
    void refreshLater();

    // Highlight the slice currently visible in the table.
    void setViewportRange(qint64 fromMs, qint64 toMs);

signals:
    void timeClicked(qint64 epochMs);
    void rangeSelected(qint64 fromMs, qint64 toMs);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    QSize sizeHint() const override;

private:
    void rebuild();
    qint64 timeAtX(int x) const;

    LogModel     *m_model = nullptr;
    TimelineBins  m_bins;
    class QTimer *m_timer = nullptr;
    bool          m_dirty = true;

    qint64 m_viewFrom = 0;
    qint64 m_viewTo   = 0;

    // Drag state. m_dragStartX < 0 means "not dragging".
    int  m_dragStartX = -1;
    int  m_dragCurX   = -1;
    int  m_hoverX     = -1;

    static constexpr int kHeight       = 46;
    static constexpr int kMinDragPx    = 4;   // below this a drag is a click
};

#endif // TIMELINERIBBON_H

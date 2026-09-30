#include "markerscrollbar.h"
#include "uicolors.h"

#include "logmodel.h"

#include <QAbstractItemModel>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>

int nextMarkedRow(const QAbstractItemModel *proxy, const LogModel *source,
                  int from, int dir, bool errorsOnly)
{
    if (!source || dir == 0) { return -1; }

    const QAbstractItemModel *rowSource = proxy ? proxy : source;
    const int rows = rowSource->rowCount();
    if (rows <= 0) { return -1; }

    const auto *sfp = qobject_cast<const QSortFilterProxyModel *>(proxy);

    for (int r = from + (dir > 0 ? 1 : -1); r >= 0 && r < rows; r += (dir > 0 ? 1 : -1)) {
        int sourceRow = r;
        if (sfp) {
            const QModelIndex src = sfp->mapToSource(sfp->index(r, 0));
            if (!src.isValid()) { continue; }
            sourceRow = src.row();
        }
        const LogEntryPtr e = source->entryAt(sourceRow);
        if (!e) { continue; }
        if (e->severity == Severity::Error) { return r; }
        if (!errorsOnly && e->severity == Severity::Warn) { return r; }
    }
    return -1;
}

RowMarks binRowMarks(const QAbstractItemModel *proxy,
                     const LogModel *source,
                     int bucketCount)
{
    RowMarks out;
    if (!source || bucketCount <= 0) return out;

    const QAbstractItemModel *rowSource = proxy ? proxy : source;
    const int rows = rowSource->rowCount();
    if (rows <= 0) return out;

    out.rows = rows;
    out.buckets.resize(bucketCount);

    auto *sfp = qobject_cast<const QSortFilterProxyModel*>(proxy);

    for (int r = 0; r < rows; ++r) {
        int sourceRow = r;
        if (sfp) {
            const QModelIndex src = sfp->mapToSource(sfp->index(r, 0));
            if (!src.isValid()) continue;
            sourceRow = src.row();
        }
        LogEntryPtr e = source->entryAt(sourceRow);
        if (!e) continue;

        // Nothing to draw for an ordinary row — skip the bucket write
        // entirely, since most rows are ordinary and this loop is the whole
        // cost of the widget.
        const bool err = (e->severity == Severity::Error);
        const bool wrn = (e->severity == Severity::Warn);
        if (!err && !wrn && !e->bookmarked) continue;

        int idx = int((qint64(r) * bucketCount) / rows);
        if (idx < 0)            idx = 0;
        if (idx >= bucketCount) idx = bucketCount - 1;

        RowMarks::Mark &m = out.buckets[idx];
        if (err)           m.error    = true;
        if (wrn)           m.warn     = true;
        if (e->bookmarked) m.bookmark = true;
    }
    return out;
}

MarkerScrollBar::MarkerScrollBar(QWidget *parent)
    : QScrollBar(Qt::Vertical, parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(600);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        if (!m_dirty) return;
        rebuild();
        update();
    });
    m_timer->start();
}

void MarkerScrollBar::setModels(QAbstractItemModel *proxy, LogModel *source)
{
    m_proxy  = proxy;
    m_source = source;
    m_dirty  = true;
    rebuild();
    update();
}

void MarkerScrollBar::refreshLater()
{
    m_dirty = true;
}

void MarkerScrollBar::rebuild()
{
    // One bucket per pixel of height. Anything finer is invisible.
    m_marks = binRowMarks(m_proxy, m_source, qMax(1, height()));
    m_dirty = false;
}

void MarkerScrollBar::paintEvent(QPaintEvent *ev)
{
    // Let the style draw the real scrollbar first; we only decorate it.
    QScrollBar::paintEvent(ev);

    if (m_marks.isEmpty()) return;

    QPainter p(this);

    // Inset from the arrow buttons so marks line up with the groove rather
    // than with the whole widget. Styles without buttons report zero, which
    // is also correct.
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_ScrollBar, &opt,
                                                 QStyle::SC_ScrollBarGroove,
                                                 this);
    const int top = groove.top();
    const int h   = qMax(1, groove.height());
    const int w   = width();

    // Marks are drawn narrow and on the left so the slider stays readable
    // through them.
    const int markW = qMax(3, w / 3);

    for (int i = 0; i < m_marks.buckets.size(); ++i) {
        const RowMarks::Mark &m = m_marks.buckets.at(i);
        if (!m.error && !m.warn && !m.bookmark) continue;

        const int y = top + int((qint64(i) * h) / m_marks.buckets.size());

        // Error wins over warning in the same bucket: it is the thing you
        // are hunting, and a bucket is only a few rows wide.
        QColor c = m.error ? UiColor::error()
                 : m.warn  ? UiColor::warning()
                           : UiColor::accent();
        p.fillRect(QRect(0, y, markW, 2), c);

        // A bookmark coinciding with a severity mark gets its own pip on
        // the right, so neither hides the other.
        if (m.bookmark && (m.error || m.warn)) {
            p.fillRect(QRect(w - markW, y, markW, 2), UiColor::accent());
        }
    }
}

int nextBookmarkedRow(const QAbstractItemModel *proxy, const LogModel *source,
                      int from, int dir)
{
    if (!source || dir == 0) { return -1; }

    const QAbstractItemModel *rowSource = proxy ? proxy : source;
    const int rows = rowSource->rowCount();
    if (rows <= 0) { return -1; }

    const auto *sfp = qobject_cast<const QSortFilterProxyModel *>(proxy);
    const int step = dir > 0 ? 1 : -1;

    for (int r = from + step; r >= 0 && r < rows; r += step) {
        int sourceRow = r;
        if (sfp) {
            const QModelIndex src = sfp->mapToSource(sfp->index(r, 0));
            if (!src.isValid()) { continue; }
            sourceRow = src.row();
        }
        const LogEntryPtr e = source->entryAt(sourceRow);
        if (e && e->bookmarked) { return r; }
    }
    return -1;
}

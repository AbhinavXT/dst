#include "followpill.h"

#include "uistyle.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QScrollBar>

FollowPill::FollowPill(QAbstractItemView *view)
    : QPushButton(view)
    , m_view(view)
{
    setObjectName(QStringLiteral("followPill"));
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Following is paused while you read up the log. "
                  "Click to jump to the newest row and follow again."));
    UiStyle::makePrimary(this);
    hide();

    QScrollBar *bar = view->verticalScrollBar();
    connect(bar, &QScrollBar::valueChanged, this, [this, bar](int v) {
        // Two rows of slack: a wheel notch that stops just short of the end
        // still counts as "at the end".
        m_atTail = v >= bar->maximum() - 2;
        if (m_atTail) resume();
    });
    connect(this, &QPushButton::clicked, this, &FollowPill::resumeRequested);
    view->installEventFilter(this);
}

void FollowPill::addPending(int rows)
{
    m_pending += rows;
    setText(m_pending == 1 ? tr("▼ 1 new row") : tr("▼ %1 new rows").arg(m_pending));
    setAccessibleName(text());
    adjustSize();
    place();
    show();
    raise();
}

void FollowPill::resume()
{
    m_pending = 0;
    hide();
}

bool FollowPill::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_view && event->type() == QEvent::Resize && isVisible()) place();
    return QPushButton::eventFilter(watched, event);
}

void FollowPill::place()
{
    // Centred over the bottom edge of the rows, clear of the horizontal bar.
    const QRect vp = m_view->viewport()->geometry();
    move(vp.center().x() - width() / 2, vp.bottom() - height() - UiStyle::space(3));
}

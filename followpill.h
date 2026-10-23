#ifndef FOLLOWPILL_H
#define FOLLOWPILL_H

// =============================================================================
//  followpill.h -- "▼ 312 new rows" over a live table (session 337)
//  ---------------------------------------------------------------------------
//  With Follow on, every batch scrolled the table back to the tail, so
//  reading a row a few screens up during live traffic was a fight with the
//  scroll bar. Now scrolling up pauses following for that table; new rows
//  are counted on a button floating over its bottom edge, and clicking it
//  (or scrolling back to the end) resumes. Follow itself stays on: this is
//  a pause, not the checkbox.
// =============================================================================

#include <QPushButton>

class QAbstractItemView;

class FollowPill : public QPushButton
{
    Q_OBJECT
public:
    explicit FollowPill(QAbstractItemView *view);

    // Was the view at its last row the last time the operator (or a
    // scrollToBottom) moved it? Row inserts do not move the value, so this
    // stays true across a batch until someone scrolls away.
    bool atTail() const { return m_atTail; }
    // A batch arrived while paused: count it and show the button.
    void addPending(int rows);
    int pending() const { return m_pending; }
    // Back at the tail: hide and reset the count.
    void resume();

signals:
    // Clicked: the owner scrolls to the end (resume() follows from that).
    void resumeRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void place();
    QAbstractItemView *m_view;
    bool m_atTail = true;
    int  m_pending = 0;
};

#endif  // FOLLOWPILL_H

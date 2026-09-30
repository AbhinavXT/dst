#ifndef NOTIFICATIONCENTER_H
#define NOTIFICATIONCENTER_H

// =============================================================================
//  NotificationCenter
//  -----------------------------------------------------------------------------
//  One place for everything the application tells the operator, with a
//  history that survives long enough to be read.
//
//  WHY
//    Feedback currently arrives through four unrelated mechanisms: a status
//    label that clears itself after five seconds, modal message boxes,
//    tooltips on status widgets, and qWarning() output nobody sees. The
//    five-second label is the worst of them — it is where "schema reloaded",
//    "disk logging resumed" and "that message is hidden by this tab's
//    filter" all go, and if you were looking at the log table instead of
//    the status bar when one appeared, it is simply gone. There is no way
//    to ask what it said.
//
//    Modal boxes have the opposite problem: they block the whole window for
//    things that are informational. A save failure genuinely warrants
//    stopping the operator; "loaded 40 records" does not.
//
//  DESIGN
//    Every message goes to the same log, tagged with a severity. The latest
//    one shows inline in the status bar as before, so nothing gets quieter
//    than it is today. Clicking it opens the history. Errors additionally
//    stay visible until acknowledged rather than expiring, because an error
//    that scrolls past unread is the same as no error at all.
//
//    Modality becomes a property of severity rather than of the call site,
//    which is what stopped it being applied consistently.
// =============================================================================

#include <QDateTime>
#include <QVector>
#include <QWidget>

class QLabel;
class QTableWidget;
class QTimer;

enum class NoteLevel { Info, Warning, Error };

struct Notification {
    NoteLevel level = NoteLevel::Info;
    QString   text;
    QString   detail;      // optional; shown in the history's tooltip
    QDateTime when;
};

// Status-bar strip: shows the most recent message and opens the history.
class NotificationBar : public QWidget
{
    Q_OBJECT

public:
    explicit NotificationBar(QWidget *parent = nullptr);

    void post(NoteLevel level, const QString &text,
              const QString &detail = QString());

    const QVector<Notification> &history() const { return m_history; }
    int unreadErrors() const { return m_unreadErrors; }

signals:
    void historyRequested();

protected:
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    void refresh();

    QLabel *m_label = nullptr;
    QTimer *m_clearTimer = nullptr;

    QVector<Notification> m_history;
    int m_unreadErrors = 0;

    // Bounded: a fault that repeats every second for an hour must not grow
    // without limit. Oldest entries are dropped.
    static constexpr int kMaxHistory = 500;
    // Info and warnings fade; errors do not — see the class comment.
    static constexpr int kInfoTimeoutMs = 8000;
};

// The history window itself.
class NotificationLog : public QWidget
{
    Q_OBJECT
public:
    explicit NotificationLog(const QVector<Notification> &history,
                             QWidget *parent = nullptr);
private:
    QTableWidget *m_table = nullptr;
};

#endif // NOTIFICATIONCENTER_H

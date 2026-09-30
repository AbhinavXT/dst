#include "testutil.h"
#include "notificationcenter.h"

#include <QSignalSpy>
#include <QMouseEvent>
#include <QCoreApplication>

// Notification behaviour. The value here is the retention policy: what is
// kept, what expires, and what refuses to expire. Getting that wrong
// reintroduces the five-second label this replaces.
TEST_SUITE(notifications)
{
    {
        NotificationBar bar;
        CHECK(bar.history().isEmpty(),   "starts empty");
        CHECK(bar.unreadErrors() == 0,   "no unread errors");

        bar.post(NoteLevel::Info, "loaded 40 records");
        CHECK(bar.history().size() == 1, "info is retained");
        CHECK(bar.history().last().text == "loaded 40 records", "text kept");
        CHECK(bar.history().last().level == NoteLevel::Info,    "level kept");
        CHECK(bar.history().last().when.isValid(),              "timestamped");
        CHECK(bar.unreadErrors() == 0, "info does not count as an error");

        bar.post(NoteLevel::Warning, "filter hides that row");
        CHECK(bar.history().size() == 2,  "warning retained");
        CHECK(bar.unreadErrors() == 0,    "warning is not an error");

        bar.post(NoteLevel::Error, "disk logging suspended", "free some space");
        CHECK(bar.history().size() == 3, "error retained");
        CHECK(bar.unreadErrors() == 1,   "error counted as unread");
        CHECK(bar.history().last().detail == "free some space",
              "detail preserved for the history view");

        bar.post(NoteLevel::Error, "second failure");
        CHECK(bar.unreadErrors() == 2, "unread errors accumulate");

        // Ordering must be chronological — the log window reverses it, but
        // the store itself is append-order.
        CHECK(bar.history().first().text == "loaded 40 records",
              "oldest first in the store");
        CHECK(bar.history().last().text == "second failure",
              "newest last in the store");
    }

    // History is bounded: a fault repeating every second for an hour must
    // not grow without limit.
    {
        NotificationBar bar;
        for (int i = 0; i < 600; ++i) {
            bar.post(NoteLevel::Info, QStringLiteral("msg %1").arg(i));
        }
        CHECK(bar.history().size() <= 500, "history is capped");
        CHECK(bar.history().size() == 500, "capped at exactly 500");
        // Oldest are dropped, newest kept — the opposite would make the cap
        // useless, since the recent messages are the ones that matter.
        CHECK(bar.history().last().text == "msg 599",  "newest retained");
        CHECK(bar.history().first().text == "msg 100", "oldest dropped");
    }

    // Every level reaches the history; none is silently discarded.
    {
        NotificationBar bar;
        bar.post(NoteLevel::Info, "a");
        bar.post(NoteLevel::Warning, "b");
        bar.post(NoteLevel::Error, "c");
        int info = 0, warn = 0, err = 0;
        for (const Notification &n : bar.history()) {
            if (n.level == NoteLevel::Info)         ++info;
            else if (n.level == NoteLevel::Warning) ++warn;
            else                                    ++err;
        }
        CHECK(info == 1 && warn == 1 && err == 1, "one of each level kept");
    }

    // Empty and long text must not be dropped or truncated in the store.
    {
        NotificationBar bar;
        bar.post(NoteLevel::Info, "");
        CHECK(bar.history().size() == 1, "empty text still recorded");
        const QString longText(5000, QLatin1Char('x'));
        bar.post(NoteLevel::Warning, longText);
        CHECK(bar.history().last().text.size() == 5000,
              "long text stored intact");
    }

    // Opening the history clears the unread count — the signal is what the
    // window listens for.
    {
        NotificationBar bar;
        bar.post(NoteLevel::Error, "boom");
        CHECK(bar.unreadErrors() == 1, "one unread");
        QSignalSpy spy(&bar, &NotificationBar::historyRequested);
        QMouseEvent ev(QEvent::MouseButtonRelease, QPointF(1, 1),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&bar, &ev);
        CHECK(spy.count() == 1, "click asks for the history");
        CHECK(bar.unreadErrors() == 0, "and marks errors read");
        CHECK(bar.history().size() == 1, "but does not discard them");
    }

    // The log window builds without a crash for every shape of input.
    {
        QVector<Notification> h;
        NotificationLog empty(h);
        CHECK(true, "log window handles an empty history");
        Notification n;
        n.level = NoteLevel::Error; n.text = "x"; n.when = QDateTime::currentDateTime();
        h.append(n);
        NotificationLog one(h);
        CHECK(true, "log window handles a populated history");
    }
}

#include "testutil.h"

#include "frameclock.h"

#include <QDate>
#include <QDateTime>
#include <QTime>

// =============================================================================
//  FRAME_NUM as a clock.
//
//  The relationship — FRAME_NUM = seconds since midnight + 1 — was confirmed
//  against the captures before any of this was written: across 15,800 frames
//  in replay/, FRAME_NUM minus the seconds-since-midnight of the capture
//  timestamp came out +1 for 14,839 of them, with +0 and +2 accounting for
//  the rest, which is exactly the spread a timestamp truncated to the second
//  produces.
//
//  What the tests below pin is the three places this arithmetic can go wrong
//  quietly: the off-by-one itself, midnight, and the asymmetric window the
//  far end applies.
// =============================================================================

namespace {

QDateTime at(int h, int m, int s)
{
    return QDateTime(QDate(2026, 9, 4), QTime(h, m, s));
}

}  // namespace

TEST_SUITE(frameclock)
{
    // ---- the +1 ------------------------------------------------------------
    CHECK(FrameClock::secondsSinceMidnight(1) == 0,
          "frame 1 is midnight, not frame 0");
    CHECK(FrameClock::timeText(1) == QStringLiteral("00:00:00"), "and reads as such");
    CHECK(FrameClock::secondsSinceMidnight(86400) == 86399,
          "frame 86400 is the last second of the day");
    CHECK(FrameClock::timeText(86400) == QStringLiteral("23:59:59"), "23:59:59");

    // One real frame from the captures: an ARP stamped 16:24:19 carried 59060.
    CHECK(FrameClock::timeText(59060) == QStringLiteral("16:24:19"),
          "a frame number lifted from replay/ reads back as its capture time");

    // ---- what is not a time ------------------------------------------------
    CHECK(FrameClock::secondsSinceMidnight(0) < 0,
          "frame 0 would be a second before midnight, so it is not a time");
    CHECK(FrameClock::secondsSinceMidnight(86401) < 0, "nor is one past the day");
    CHECK(FrameClock::secondsSinceMidnight(-5) < 0, "nor a negative one");
    CHECK(FrameClock::timeText(0).isEmpty(),
          "and none of them are dressed up as one");

    // ---- the inverse round-trips -------------------------------------------
    for (int s : { 0, 1, 3599, 43200, 86399 }) {
        CHECK(FrameClock::secondsSinceMidnight(FrameClock::frameNumFor(s)) == s,
              "frameNumFor and secondsSinceMidnight are inverses");
    }
    CHECK(FrameClock::frameNumFor(86400) < 0, "and the inverse refuses a non-time too");

    // ---- skew --------------------------------------------------------------
    {
        // 15:02:31 on the equipment, 15:02:31 here.
        const qint64 fn = FrameClock::frameNumFor(15 * 3600 + 2 * 60 + 31);
        CHECK(FrameClock::skewSeconds(fn, at(15, 2, 31)) == 0, "agreement is zero");
        CHECK(FrameClock::skewSeconds(fn, at(15, 2, 28)) == 3,
              "equipment ahead of us reads positive");
        CHECK(FrameClock::skewSeconds(fn, at(15, 2, 36)) == -5,
              "and behind us, negative");
    }

    // ---- midnight ----------------------------------------------------------
    //
    // The one place a clock readout is guaranteed to be looked at and the one
    // place naive subtraction gives most of a day. An acceptance run that
    // straddles midnight should not light up red for it.
    {
        const qint64 justAfter = FrameClock::frameNumFor(1);        // 00:00:01
        CHECK(FrameClock::skewSeconds(justAfter, at(23, 59, 59)) == 2,
              "00:00:01 against 23:59:59 is two seconds, not -86398");

        const qint64 justBefore = FrameClock::frameNumFor(86399);   // 23:59:59
        CHECK(FrameClock::skewSeconds(justBefore, at(0, 0, 1)) == -2,
              "and the other way round is minus two");
    }

    // ---- the window the far end applies ------------------------------------
    //
    // Asymmetric, and that is the point: a packet may be 4 s old but only 1 s
    // early, so a laptop running FAST is rejected sooner than one running
    // slow. Boundaries are checked on both sides because the firmware's test
    // is `<= -2 || > 4`, which includes 4 and excludes -2.
    using A = FrameClock::Accept;
    CHECK(FrameClock::classify(0) == A::Ok,   "no skew is fine");
    CHECK(FrameClock::classify(3) == A::Ok,   "three seconds behind is still fine");
    CHECK(FrameClock::classify(4) == A::Marginal,
          "four is the last accepted value, so it is worth flagging");
    CHECK(FrameClock::classify(5) == A::Stale,
          "five is rejected: our frames look more than 4 s old");
    CHECK(FrameClock::classify(-1) == A::Marginal,
          "one second early is the last accepted value on that side");
    CHECK(FrameClock::classify(-2) == A::Ahead,
          "minus two is ALREADY rejected — the test is <= -2, not < -2");
    CHECK(FrameClock::classify(-30) == A::Ahead, "and well past it");

    CHECK(!FrameClock::acceptText(A::Stale).isEmpty(),
          "each verdict says what it means for sending");
    CHECK(FrameClock::acceptText(A::Stale) != FrameClock::acceptText(A::Ahead),
          "and stale and early are told apart, since the fix differs");
}

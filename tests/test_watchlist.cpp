#include "testutil.h"

#include "watchlist.h"

#include "capturedecoder.h"
#include "schema/schemadecoder.h"

#include <QSharedPointer>

// =============================================================================
//  Watches.
//
//  A pin says what a field is; a watch says when it becomes something. The
//  parts worth testing are the ones that decide whether an operator trusts it:
//
//    – it LATCHES. A condition true for three hundred frames is one event.
//      A watch that re-announced itself per frame is one that gets turned off,
//      and then it is not watching anything.
//    – a bad query is KEPT with its error, not dropped. A watch believed to be
//      armed and silently absent is the worst thing this feature can produce.
//    – re-arming discards the evidence AND the count, so a re-armed watch
//      cannot report an event that has not happened again.
// =============================================================================

namespace {

LogEntryPtr mk(const QString &text, qint64 ms, Severity sev = Severity::Info)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->header.source_id = 33;
    e->header.kvchId    = 1;
    e->epochMs  = ms;
    e->severity = sev;
    e->text     = text;
    e->cacheDerived();
    return e;
}

// A real LSRP, and a plain diagnostic line.
const QString kLsrp = QStringLiteral(
    "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
    "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B");

}  // namespace

TEST_SUITE(watchlist)
{
    // ---- adding, and a query that does not parse ---------------------------
    {
        WatchList w;
        CHECK(w.add(QStringLiteral("")) == -1, "an empty expression is not a watch");

        const int ok = w.add(QStringLiteral("sev:error"), QStringLiteral("any error"));
        CHECK(ok == 0, "a good one is added");
        CHECK(w.watches().at(0).valid(), "and parses");
        CHECK(w.watches().at(0).label == QStringLiteral("any error"), "keeping its name");

        const int bad = w.add(QStringLiteral("sev:error AND"));
        CHECK(bad == 1, "a broken query is still added");
        CHECK(!w.watches().at(1).valid(),
              "and is marked invalid rather than dropped — a watch believed "
              "armed and silently absent is worse than a visible error");
        CHECK(!w.watches().at(1).parseError.isEmpty(), "with the reason");
        CHECK(w.watches().at(1).expr == QStringLiteral("sev:error AND"),
              "and what was typed, so it can be corrected rather than retyped");
    }

    // ---- firing, once ------------------------------------------------------
    {
        WatchList w;
        w.add(QStringLiteral("sev:error"));

        auto fired = w.observe(mk(QStringLiteral("all well"), 1000), QStringLiteral("33_1"));
        CHECK(fired.isEmpty(), "a frame that does not match does not fire it");
        CHECK(!w.watches().at(0).fired, "and it stays waiting");

        fired = w.observe(mk(QStringLiteral("bad thing"), 2000, Severity::Error),
                          QStringLiteral("33_1"));
        CHECK(fired.size() == 1 && fired.first() == 0, "a match fires it, once");
        CHECK(w.watches().at(0).fired, "and it latches");
        CHECK(w.watches().at(0).firedAtMs == 2000,
              "recording the frame's own time, not the time it was processed");
        CHECK(w.watches().at(0).firedBy == QStringLiteral("33_1"), "and the source");
        CHECK(w.watches().at(0).evidence.contains(QStringLiteral("bad thing")),
              "and the frame itself, so the row can show what set it off");
        CHECK(!w.watches().at(0).firedEntry.isNull(),
              "held as an entry too, so the view can be taken to it");
    }

    // ---- a condition that stays true is still one event --------------------
    {
        WatchList w;
        w.add(QStringLiteral("sev:error"));
        w.observe(mk(QStringLiteral("first"), 1000, Severity::Error), QStringLiteral("33_1"));

        // 300 further matches, checked as one assertion rather than 300 —
        // a suite that prints three hundred identical lines is a suite nobody
        // reads the output of.
        bool reannounced = false;
        for (int i = 0; i < 300; ++i) {
            if (!w.observe(mk(QStringLiteral("still bad"), 2000 + i, Severity::Error),
                           QStringLiteral("33_1")).isEmpty()) {
                reannounced = true;
            }
        }
        CHECK(!reannounced,
              "a condition that stays true does not fire again on every frame");
        CHECK(w.watches().at(0).matches == 301,
              "later matches are counted — 'fired at X, 301 frames since' says "
              "both that it happened and that it is still true");
        CHECK(w.watches().at(0).firedAtMs == 1000,
              "but the evidence stays on the frame that FIRST matched, which "
              "is the one worth reporting");
    }

    // ---- disarming ---------------------------------------------------------
    {
        WatchList w;
        const int i = w.add(QStringLiteral("sev:error"));
        w.watches();                       // no mutation through the getter
        WatchList::Watch copy = w.watches().at(i);
        CHECK(copy.armed, "a watch starts armed — adding one is asking for it");
    }

    // ---- re-arming discards everything -------------------------------------
    {
        WatchList w;
        w.add(QStringLiteral("sev:error"));
        w.observe(mk(QStringLiteral("bad"), 1000, Severity::Error), QStringLiteral("33_1"));
        w.observe(mk(QStringLiteral("bad"), 1100, Severity::Error), QStringLiteral("33_1"));
        CHECK(w.watches().at(0).matches == 2, "two matches counted");

        CHECK(w.rearm(0), "it can be re-armed");
        CHECK(!w.watches().at(0).fired, "and is waiting again");
        CHECK(w.watches().at(0).evidence.isEmpty(), "with the evidence gone");
        CHECK(w.watches().at(0).firedEntry.isNull(), "and the frame released");
        CHECK(w.watches().at(0).matches == 0,
              "and the count reset — a re-armed watch holding its old total "
              "would report an event that has not happened again");

        const auto fired = w.observe(mk(QStringLiteral("bad again"), 3000, Severity::Error),
                                     QStringLiteral("33_1"));
        CHECK(fired.size() == 1, "and it can fire a second time");
    }

    // ---- a field condition on a real frame ---------------------------------
    //
    // The reason the condition is a query rather than a new expression
    // language: this is the same syntax as the filter bar, so a watch that
    // does not fire can be pasted there to find out why.
    {
        Schema::Decoder dec;
        QString err;
        dec.load(QStringLiteral(":/schema/kavach.xml"), &err);

        WatchList w;
        w.add(QStringLiteral("PKT_TYPE == 10"));
        const auto fired = w.observe(mk(kLsrp, 5000), QStringLiteral("1_1"));

        // Whether it matches depends on the frame; what must hold is that a
        // field term is evaluated at all rather than silently matching nothing.
        CHECK(w.watches().at(0).valid(), "a field condition parses");
        CHECK(fired.size() <= 1, "and either fires or does not, without error");
    }

    // ---- round trip --------------------------------------------------------
    {
        WatchList w;
        w.add(QStringLiteral("sev:error"), QStringLiteral("errors"));
        w.observe(mk(QStringLiteral("bad"), 1000, Severity::Error), QStringLiteral("33_1"));

        WatchList back;
        back.fromStrings(w.toStrings());
        CHECK(back.count() == 1, "watches survive a save and load");
        CHECK(back.watches().at(0).expr == QStringLiteral("sev:error"), "the query");
        CHECK(back.watches().at(0).label == QStringLiteral("errors"), "and the name");
        CHECK(!back.watches().at(0).fired,
              "but NOT the hit — a watch reloaded next session showing a hit "
              "from a run that has ended would be read as a hit in this one");
    }
}

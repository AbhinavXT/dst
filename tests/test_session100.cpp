#include "testutil.h"

#include "capturedecoder.h"
#include "filterbar.h"
#include "logmodel.h"
#include "logquery.h"
#include "querylineedit.h"
#include "schema/schemadecoder.h"

#include <QDateTime>
#include <QSharedPointer>
#include <QSortFilterProxyModel>

// =============================================================================
//  Session 100 — the filter's query mode as a small query engine.
//
//  The reported bug: time queries in the filter bar said "could not read".
//  Some spellings were refused outright (9:05, "after: 14:02", 08-08-2026);
//  one parsed and silently asked a different question (after:2026-08-08
//  14:02 — the 14:02 became a separate text term); and every clock time
//  meant TODAY, so on any replay or log from another day a time filter
//  matched nothing and looked exactly like a quiet bus.
//
//  Real frame from replay/ where one exists: the LSRP line below.
// =============================================================================

namespace {

// Real capture (replay/loco_1_1_27062026_140226.cap): FRAME_NUM 50548
// (14:02:27 + 1 s), LOCO_MODE 7 (Trip), ABS_LOCO_LOC 163821 m,
// TRAIN_LENGTH 250 m, TRAIN_SPEED 0.
const QString kLsrp = QStringLiteral(
    "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
    "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B");

qint64 localMs(int y, int mo, int d, int h, int mi, int s, int ms = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s, ms)).toMSecsSinceEpoch();
}

LogEntry mk(const QString &text, qint64 ms, Severity sv = Severity::Info,
            quint8 src = 33, quint16 kv = 1, quint16 len = 10)
{
    LogEntry e;
    e.text = text;
    e.epochMs = ms;
    e.severity = sv;
    e.direction = LogDirection::In;
    e.header.source_id = src;
    e.header.kvchId = kv;
    e.header.message_len = len;
    e.cacheDerived();
    return e;
}

bool hit(const QString &query, const LogEntry &e, bool utc = false, qint64 dataEnd = 0)
{
    LogQuery lq;
    lq.setUtc(utc);
    lq.setDataEnd(dataEnd);
    return lq.parse(query) && lq.match(e);
}

bool parses(const QString &query)
{
    LogQuery lq;
    return lq.parse(query);
}

QString errorOf(const QString &query)
{
    LogQuery lq;
    lq.parse(query);
    return lq.errorString();
}

}  // namespace

TEST_SUITE(session100)
{
    // A row from a recorded session on 27 June — deliberately NOT today.
    const qint64 T = localMs(2026, 6, 27, 14, 2, 27, 250);
    const LogEntry row = mk(kLsrp, T);

    // ---- the reported failures ---------------------------------------------
    CHECK(hit("after:14:02", row),          "clock time matches a row from another day");
    CHECK(!hit("after:14:03", row),         "…and still excludes what is before it");
    CHECK(hit("before:14:03", row),         "before: with a clock time, another day");
    CHECK(hit("after:9:05", row),           "single-digit hour reads");
    CHECK(hit("after:14:2", row),           "single-digit minute reads");
    CHECK(hit("after: 14:02", row),         "a space after the colon is not 'no value'");
    CHECK(hit("after:27-06-2026 14:02", row),  "day-first date with a time");
    CHECK(hit("after:27/06/2026", row),     "day-first with slashes");
    CHECK(hit("after:27.06.2026", row),     "day-first with dots");
    CHECK(!hit("after:28-06-2026", row),    "day-first date excludes the day before");
    {
        // After:DATE TIME used to parse as after:DATE AND a TEXT term "14:02",
        // which this row's text does not contain — so it silently missed.
        LogQuery lq;
        CHECK(lq.parse("after:2026-06-27 14:02"), "unquoted date and time parse");
        CHECK(lq.match(row), "…as one time, not a date plus a text term");
        CHECK(!lq.explain().contains(QLatin1String("\"14:02\"")),
              "explain shows no stray text term");
    }
    CHECK(hit("after:\"2026-06-27 14:02\"", row), "quoted date and time");
    CHECK(hit("after:2026-06-27T14:02:27", row),  "ISO with T still reads");

    // ---- what the timeline ribbon writes ------------------------------------
    {
        const QString local = QDateTime::fromMSecsSinceEpoch(T).toString(Qt::ISODateWithMs);
        CHECK(hit(QStringLiteral("after:%1 before:%1").arg(local), row),
              "ribbon's local ISO round-trips to the same millisecond");
        const QString utc = QDateTime::fromMSecsSinceEpoch(T, Qt::UTC).toString(Qt::ISODateWithMs);
        CHECK(utc.endsWith(QLatin1Char('Z')), "UTC ribbon stamp carries its Z");
        CHECK(hit(QStringLiteral("after:%1 before:%1").arg(utc), row, true),
              "ribbon's UTC stamp round-trips in UTC mode");
        CHECK(hit(QStringLiteral("after:%1 before:%1").arg(utc), row, false),
              "…and the Z is honoured even in local mode");
    }

    // ---- an upper bound covers the unit typed --------------------------------
    CHECK(hit("before:14:02", row),          "before:14:02 includes 14:02:27 (that minute)");
    CHECK(!hit("before:14:01", row),         "before:14:01 does not");
    CHECK(hit("before:14:02:27", row),       "a whole second covers .250");
    CHECK(!hit("before:14:02:27.100", row),  "milliseconds are exact");
    CHECK(hit("time:14:02", row),            "time:HH:mm is that minute");
    CHECK(!hit("time:14:03", row),           "…and not the next");
    CHECK(hit("time:14:02:27", row),         "time:HH:mm:ss is that second");
    CHECK(hit("time:2026-06-27", row),       "time:DATE is that whole day");
    CHECK(hit("time:27-06-2026", row),       "…day-first as well");
    CHECK(!hit("time:2026-06-28", row),      "…and not the next day");

    // ---- ranges and comparisons --------------------------------------------
    CHECK(hit("time:14:00..14:05", row),     "clock range");
    CHECK(hit("time:14:00-14:05", row),      "clock range with a dash");
    CHECK(hit("time:14:00 .. 14:05", row),   "spaces around ..");
    CHECK(!hit("time:14:03..14:05", row),    "range excludes");
    CHECK(hit("time:14:00..", row),          "open-ended range, start only");
    CHECK(hit("time:..14:02", row),          "open-ended range, end only, whole minute");
    CHECK(hit("time:2026-06-27 14:00..14:05", row), "dated start, clock end takes its date");
    CHECK(hit("time:2026-06-27 14:00..2026-06-27 14:05", row), "both ends dated, unquoted");
    CHECK(hit("time>=14:02:27", row),        "colon-less >=");
    CHECK(!hit("time>14:02:27", row),        "> is past the whole second");
    CHECK(hit("time:>14:02:26", row),        "colon form with an operator");
    CHECK(hit("time < 14:03", row),          "spaces around the operator");
    CHECK(!hit("time:!=14:02", row),         "!= excludes that minute");
    CHECK(hit("time!=14:03", row),           "…and keeps others");
    {
        const LogEntry late  = mk(QStringLiteral("x"), localMs(2026, 6, 27, 23, 55, 0));
        const LogEntry early = mk(QStringLiteral("x"), localMs(2026, 6, 28, 0, 5, 0));
        const LogEntry noon  = mk(QStringLiteral("x"), localMs(2026, 6, 28, 12, 0, 0));
        CHECK(hit("time:23:50..00:10", late),  "midnight-wrapping window, before midnight");
        CHECK(hit("time:23:50..00:10", early), "…and after midnight");
        CHECK(!hit("time:23:50..00:10", noon), "…and not the middle of the day");
        CHECK(hit("time:2026-06-27 23:50..00:10", early),
              "dated start, clock end rolls over to the next day");
        CHECK(!hit("after:23:50 before:00:10", late),
              "two separate clock bounds AND to nothing — documented, use a range");
    }

    // ---- UTC mode reads the clock the column shows ---------------------------
    {
        const qint64 u = QDateTime(QDate(2026, 6, 27), QTime(3, 30, 0), Qt::UTC).toMSecsSinceEpoch();
        const LogEntry e = mk(QStringLiteral("x"), u);
        CHECK(hit("time:03:30", e, true),  "UTC mode: 03:30 UTC is time:03:30");
        CHECK(hit("time:2026-06-27 03:30", e, true), "UTC mode: dates are UTC too");
        const QString localClock = QDateTime::fromMSecsSinceEpoch(u).toString(QStringLiteral("HH:mm"));
        CHECK(hit(QStringLiteral("time:") + localClock, e, false),
              "local mode: the same row by its local clock");
    }

    // ---- relative and last: --------------------------------------------------
    CHECK(hit("last:5m", row, false, T + 60 * 1000),      "last: counts back from the newest row");
    CHECK(!hit("last:30s", row, false, T + 60 * 1000),    "…and excludes beyond it");
    CHECK(hit("last:1h30m", row, false, T + 3600 * 1000), "compound duration");
    CHECK(hit("last:90min", row, false, T + 3600 * 1000), "minute spelling");
    CHECK(!hit("last:5m", row),                           "with no data end, last: is from now");
    {
        const LogEntry recent = mk(QStringLiteral("x"), QDateTime::currentMSecsSinceEpoch() - 60 * 1000);
        CHECK(hit("after:-5m", recent),    "after:-5m still relative to now");
        CHECK(hit("after:-1h30m", recent), "compound relative");
        CHECK(!hit("after:-30s", recent),  "relative excludes");
    }

    // ---- lists ---------------------------------------------------------------
    const LogEntry info = mk(QStringLiteral("CAN OUT No Error"), T, Severity::Info, 21, 2, 4);
    const LogEntry err  = mk(QStringLiteral("RAD IN Link 1 Error"), T, Severity::Error, 33, 1, 10);
    CHECK(hit("sev:warn,error", err),   "severity list hits");
    CHECK(!hit("sev:warn,error", info), "severity list misses");
    CHECK(hit("src:21_2,33_1", err),    "source list");
    CHECK(hit("src:21_2,33_1", info),   "source list, other member");
    CHECK(hit("len:4,10", info),        "length list");
    CHECK(!hit("len:5,6", info),        "length list misses");
    {
        const LogEntry comma = mk(QStringLiteral("a,b reached"), T);
        CHECK(hit("msg:a,b", comma), "msg: keeps the comma as text");
    }

    // ---- len ranges and colon-less comparisons -------------------------------
    CHECK(hit("len:5..20", err),   "len range");
    CHECK(!hit("len:11..", err),   "len open range");
    CHECK(hit("len:..10", err),    "len range end inclusive");
    CHECK(hit("len>5", err),       "len> without colon");
    CHECK(hit("len >= 10", err),   "len >= with spaces");
    CHECK(!hit("len<10", err),     "len< without colon");
    CHECK(hit("sev!=info", err),   "sev!= negates");
    CHECK(!hit("sev!=error", err), "…and excludes");
    CHECK(hit("sev=error", err),   "sev= is sev:");
    CHECK(hit("src!=21_2,22_1", err), "!= with a list is none of");
    CHECK(hit("dir=in", err),      "dir=");

    // ---- decoded fields on a real frame --------------------------------------
    CHECK(kavachSchema().isLoaded(), "schema loaded");
    if (kavachSchema().isLoaded()) {
        CHECK(hit("FRAME_NUM=50548", row),     "decoded field without field: prefix");
        CHECK(hit("frame_num = 50548", row),   "case-insensitive, spaces around =");
        CHECK(hit("ABS_LOCO_LOC>163000", row), "decoded comparison");
        CHECK(!hit("ABS_LOCO_LOC<163000", row), "decoded comparison misses");
        CHECK(hit("ABS_LOCO_LOC=163000..164000", row), "decoded range");
        CHECK(!hit("ABS_LOCO_LOC=1..1000", row),       "decoded range misses");
        CHECK(hit("ABS_LOCO_LOC!=1..1000", row),       "decoded range negated");
        CHECK(hit("LOCO_MODE=6,7", row),       "decoded list: any of");
        CHECK(!hit("LOCO_MODE=1,2", row),      "decoded list misses");
        CHECK(hit("LOCO_MODE!=1,2", row),      "decoded list negated: none of");
        CHECK(hit("FRAME_NUM&7=4", row),       "mask without field: (50548 & 7 = 4)");
        CHECK(hit("field:TRAIN_LENGTH=250", row), "the field: form is unchanged");
        CHECK(hit("time:14:02 FRAME_NUM=50548", row), "time and decoded field together");
    }

    // ---- refused, by name, rather than silently matching nothing -------------
    CHECK(!parses("TRAN_SPEED>60"),  "an unknown field with an operator is an error");
    CHECK(errorOf("TRAN_SPEED>60").contains(QLatin1String("unknown field")),
          "…and says so");
    CHECK(parses("\"a=b\""),         "quoting makes it text again");
    CHECK(!parses("time:25:00"),     "hour out of range");
    CHECK(!parses("after:31-02-2026"), "no 31 February");
    CHECK(!parses("after:06-27-2026"), "month-first is refused, not guessed");
    CHECK(!parses("after:15m"),      "a duration without its sign");
    CHECK(errorOf("after:15m").contains(QLatin1String("last:15m")),
          "…and the error names the fix");
    CHECK(parses("time:14:05..14:00"),  "a clock range that 'ends first' wraps midnight");
    CHECK(!parses("time:2026-06-28..2026-06-27"), "a dated range that ends before it starts");
    CHECK(!parses("last:abc"),       "last: needs a duration");
    CHECK(!parses("time:/14/"),      "time: takes no regex");
    CHECK(!parses("sev:warn,"),      "empty list item");
    CHECK(!parses("len:.."),         "range with no ends");
    CHECK(!parses("after>14:02"),    "after takes ':' — time> is the comparison");
    CHECK(!parses("sev>info"),       "sev cannot be ordered");
    CHECK(!parses("len&7>2"),        "a mask is for decoded fields only");
    CHECK(!parses("time:14:00 .. 2026-06-27 14:05"), "clock start with a dated end");

    // ---- nothing that used to be text became something else -------------------
    {
        const LogEntry t = mk(QStringLiteral("time 14:02 reached"), T);
        CHECK(hit("14:02", t),          "a bare clock time is still a text search");
        CHECK(!hit("hex:0a 1b", t),     "unquoted hex still splits into two terms");
        const LogEntry dash = mk(QStringLiteral("sub-system down"), T);
        CHECK(hit("sub-system", dash),  "a hyphen inside a word is not negation");
        CHECK(hit("RAD -ZZZ", err),     "leading - is still NOT");
        CHECK(!hit("!RAD", err),        "leading ! is still NOT");
    }

    // ---- explain() shows how a time was understood --------------------------
    {
        LogQuery lq;
        lq.parse("time:23:50..00:10");
        CHECK(lq.explain().contains(QLatin1String("across midnight")), "explain names the wrap");
        lq.parse("after:9:05");
        CHECK(lq.explain().contains(QLatin1String("09:05:00.000"))
              && lq.explain().contains(QLatin1String("any date")),
              "explain shows a clock time as any-date");
    }

    // ---- completion offers the new forms -------------------------------------
    CHECK(queryCompletionsFor(QStringLiteral("time:")).contains(QStringLiteral("time:23:50..00:10")),
          "time: offers a midnight range");
    CHECK(queryCompletionsFor(QStringLiteral("last:")).contains(QStringLiteral("last:15m")),
          "last: offers durations");
    CHECK(queryCompletionsFor(QStringLiteral("l")).contains(QStringLiteral("last:")),
          "last: is a known field");

    // ---- end to end, the way the operator hits it: a filter bar over a
    //      replay recorded on another day ---------------------------------------
    {
        LogModel model;
        QVector<LogEntryPtr> batch;
        for (int i = 0; i < 10; ++i) {
            auto e = QSharedPointer<LogEntry>::create(
                mk(QStringLiteral("row %1").arg(i), localMs(2026, 6, 27, 14, i, 0)));
            batch << e;
        }
        model.appendEntries(batch);
        FilterBar bar(&model);

        bar.setQuery(QStringLiteral("time:14:02..14:04"));
        CHECK(bar.proxyModel()->rowCount() == 3, "filter bar: clock range on a past day shows 3 rows");
        bar.setQuery(QStringLiteral("after: 14:07"));
        CHECK(bar.proxyModel()->rowCount() == 3, "filter bar: 'after: 14:07' shows 3 rows");
        bar.setQuery(QStringLiteral("last:2m"));
        CHECK(bar.proxyModel()->rowCount() == 3,
              "filter bar: last:2m counts back from the newest row (14:09)");
        bar.setQuery(QStringLiteral("after:14h02"));
        CHECK(bar.proxyModel()->rowCount() == 10,
              "filter bar: an unreadable time shows everything, not nothing");

        // UTC toggle re-applies a clock-time query in the new zone.
        const qint64 first = localMs(2026, 6, 27, 14, 0, 0);
        const QString utcClock = QDateTime::fromMSecsSinceEpoch(first, Qt::UTC)
                                     .toString(QStringLiteral("HH:mm"));
        bar.setQuery(QStringLiteral("time:") + utcClock);
        const int beforeToggle = bar.proxyModel()->rowCount();
        model.setShowUtc(true);
        CHECK(bar.proxyModel()->rowCount() == 1,
              "filter bar: after the UTC toggle, the UTC clock time finds the row");
        model.setShowUtc(false);
        CHECK(bar.proxyModel()->rowCount() == beforeToggle, "…and toggling back restores it");
    }
}

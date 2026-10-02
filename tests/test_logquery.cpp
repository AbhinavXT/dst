#include "testutil.h"
#include "logquery.h"
#include "logentry.h"
#include "namemap.h"
#include <QDateTime>
#include <cstdio>

static LogEntry mk(const QString &text, quint8 src, quint16 kv,
                   Severity sv, LogDirection d, qint64 ms,
                   const QByteArray &raw=QByteArray(), quint16 len=0) {
    LogEntry e;
    e.text = text; e.severity = sv; e.direction = d; e.epochMs = ms;
    e.header.source_id = src; e.header.kvchId = kv; e.header.message_len = len;
    e.rawBytes = raw;
    return e;
}
static bool m(const char*q, const LogEntry&e, const NameMap*n=nullptr){
    LogQuery lq; lq.parse(q); return lq.match(e,n);
}
static bool bad(const char*q){ LogQuery lq; return !lq.parse(q); }

TEST_SUITE(logquery)
{

    const qint64 T = QDateTime(QDate(2026,8,8),QTime(14,5,0)).toMSecsSinceEpoch();
    LogEntry err = mk("RAD IN Link 1 Error",33,1,Severity::Error,LogDirection::In,T,
                      QByteArray::fromHex("21650a0007000a1b"),10);
    LogEntry ok  = mk("CAN OUT No Error",21,2,Severity::Info,LogDirection::Out,T,
                      QByteArray::fromHex("15650a000700ffff"),4);

    // --- plain text, backwards compatible with the old filter box ---
    CHECK(m("RAD",err),"bare term matches message");
    CHECK(!m("RAD",ok),"bare term rejects non-match");
    CHECK(m("rad",err),"case insensitive");
    CHECK(m("33_1",err),"bare term also searches source");

    // --- implicit AND (adjacency) ---
    CHECK(m("RAD Error",err),"adjacency = AND, both present");
    CHECK(!m("RAD Nonsense",err),"adjacency AND, one absent");
    CHECK(m("RAD AND Error",err),"explicit AND");
    CHECK(m("RAD && Error",err),"&& form");

    // --- OR / NOT / precedence ---
    CHECK(m("RAD OR ZZZ",err),"OR left");
    CHECK(m("ZZZ OR RAD",err),"OR right");
    CHECK(!m("ZZZ OR YYY",err),"OR neither");
    CHECK(m("NOT ZZZ",err),"NOT");
    CHECK(!m("NOT RAD",err),"NOT negates a hit");
    CHECK(m("!ZZZ",err),"! form");
    CHECK(m("-ZZZ",err),"- form");
    CHECK(m("NOT NOT RAD",err),"double negation");
    // AND binds tighter than OR: (ZZZ AND RAD) OR Link
    CHECK(m("ZZZ RAD OR Link",err),"AND binds tighter than OR");
    CHECK(!m("(ZZZ OR YYY) RAD",err),"parens override precedence");
    CHECK(m("(ZZZ OR RAD) Error",err),"parens grouping matches");

    // --- fields ---
    CHECK(m("sev:error",err),"sev:error");
    CHECK(!m("sev:error",ok),"sev:error rejects info");
    CHECK(m("sev:info",ok),"sev:info");
    CHECK(m("sev:err",err),"sev prefix form");
    CHECK(m("dir:in",err),"dir:in");
    CHECK(m("dir:out",ok),"dir:out");
    CHECK(!m("dir:in",ok),"dir mismatch");
    CHECK(m("src:33_1",err),"src exact");
    CHECK(!m("src:21_2",err),"src mismatch");
    CHECK(m("msg:Link",err),"msg field");
    CHECK(!m("msg:33_1",err),"msg field does NOT match source");

    // --- the compound case that motivated all this ---
    CHECK(m("sev:error src:33_1 RAD NOT \"No Error\"",err),"compound query hits");
    CHECK(!m("sev:error src:33_1 RAD NOT \"No Error\"",ok),"compound rejects");
    // "No Error" must be a phrase, not two terms
    CHECK(!m("NOT \"No Error\"",ok),"quoted phrase treated as one term");
    CHECK(m("NOT \"No Error\"",err),"phrase absent -> NOT passes");

    // --- regex ---
    CHECK(m("/Link\\s+\\d+/",err),"regex with escapes and space");
    CHECK(!m("/^CAN/",err),"regex anchored miss");
    CHECK(m("/^CAN/",ok),"regex anchored hit");
    CHECK(m("msg:/R.D/",err),"field + regex");

    // --- len ---
    CHECK(m("len:10",err),"len equality");
    CHECK(m("len:>5",err),"len >");
    CHECK(m("len:>=10",err),"len >=");
    CHECK(m("len:<5",ok),"len <");
    CHECK(m("len:!=4",err),"len !=");
    CHECK(!m("len:>100",err),"len > miss");

    // --- hex against raw bytes ---
    CHECK(m("hex:0a1b",err),"hex substring of rawBytes");
    CHECK(m("hex:\"0a 1b\"",err),"quoted hex tolerates spaces");
    CHECK(!m("hex:0a 1b",err),"unquoted space splits terms (1b is not in msg)");
    CHECK(m("hex:0A:1B",err),"hex tolerates separators/case");
    CHECK(!m("hex:0a1b",ok),"hex miss");

    // --- time ---
    CHECK(m("after:2026-08-08T14:00:00",err),"after ISO");
    CHECK(!m("after:2026-08-08T15:00:00",err),"after ISO excludes");
    CHECK(m("before:2026-08-08T15:00:00",err),"before ISO");
    CHECK(m("after:2026-08-08T14:00 before:2026-08-08T14:10",err),"time window");
    CHECK(!m("after:2026-08-08T14:06 before:2026-08-08T14:10",err),"window excludes");
    { LogEntry recent = mk("x",1,1,Severity::Info,LogDirection::None,
                           QDateTime::currentMSecsSinceEpoch()-60*1000);
      CHECK(m("after:-5m",recent),"relative -5m includes 1min ago");
      CHECK(!m("after:-30s",recent),"relative -30s excludes 1min ago"); }

    // --- friendly name via NameMap ---
    { NameMap nm; CHECK(m("name:33_1",err,&nm),"name falls back to key when unmapped"); }

    // --- empty query matches everything ---
    { LogQuery lq; CHECK(lq.parse(""),"empty parses"); CHECK(lq.isEmpty(),"empty isEmpty");
      CHECK(lq.match(err),"empty matches"); CHECK(lq.match(ok),"empty matches all"); }
    { LogQuery lq; CHECK(lq.parse("   "),"whitespace parses as empty"); }

    // --- errors are reported, never silently "match nothing" ---
    CHECK(bad("(RAD"),"unclosed paren is an error");
    CHECK(bad("RAD)"),"extra paren is an error");
    CHECK(bad("RAD AND"),"trailing operator is an error");
    CHECK(bad("\"unterminated"),"unterminated quote is an error");
    CHECK(bad("/unterminated"),"unterminated regex is an error");
    CHECK(bad("sev:banana"),"bad severity is an error");
    CHECK(bad("dir:sideways"),"bad direction is an error");
    CHECK(bad("len:abc"),"bad length is an error");
    CHECK(bad("after:notatime"),"bad time is an error");
    CHECK(bad("hex:xyz"),"bad hex is an error");
    CHECK(bad("hex:0a1"),"odd hex digit count is an error");
    CHECK(bad("/[unclosed/"),"invalid regex is an error");
    { LogQuery lq; lq.parse("(RAD"); CHECK(!lq.errorString().isEmpty(),"error has text");
      CHECK(lq.errorOffset()>=0,"error has an offset"); }
    // a failed parse must not filter anything out
    { LogQuery lq; lq.parse("(RAD"); CHECK(lq.match(err)&&lq.match(ok),
        "invalid query matches everything rather than hiding rows"); }

    // --- a colon that isn't a field must stay literal ---
    { LogEntry t=mk("time 14:02 reached",5,1,Severity::Info,LogDirection::None,T);
      CHECK(m("14:02",t),"non-field colon stays a literal"); }
    // a hyphen inside a word is not negation
    { LogEntry t=mk("sub-system down",5,1,Severity::Info,LogDirection::None,T);
      CHECK(m("sub-system",t),"hyphen inside word is literal");
}
}

// ---------------------------------------------------------------------------
// andConstraint: combining a user's query with a UI-supplied constraint.
// The parenthesisation is not cosmetic — without it, AND binding tighter
// than OR silently lets half the user's query escape the constraint. Three
// UI surfaces depend on this being right.
// ---------------------------------------------------------------------------
TEST_SUITE(queryconstraint)
{
    const QString SEV = "(sev:error OR sev:warn)";

    // Degenerate inputs.
    CHECK(LogQuery::andConstraint("", SEV) == SEV,
          "empty user query yields the constraint alone");
    CHECK(LogQuery::andConstraint("RAD", "") == "RAD",
          "empty constraint yields the user query alone");
    CHECK(LogQuery::andConstraint("", "") == "",
          "both empty yields empty");
    CHECK(LogQuery::andConstraint("  RAD  ", SEV) == "(RAD) " + SEV,
          "inputs are trimmed");

    // The bracketing rule itself.
    CHECK(LogQuery::andConstraint("RAD", SEV) == "(RAD) " + SEV,
          "user query is always bracketed");
    CHECK(LogQuery::andConstraint("a OR b", SEV) == "(a OR b) " + SEV,
          "an OR query is bracketed");

    // And the reason why: prove the bracketed form actually constrains both
    // branches, and that the naive form would not have.
    {
        LogEntry warnA;  warnA.text = "a";  warnA.severity = Severity::Warn;
        warnA.header.source_id = 1; warnA.header.kvchId = 1;
        LogEntry infoA;  infoA.text = "a";  infoA.severity = Severity::Info;
        infoA.header.source_id = 1; infoA.header.kvchId = 1;
        LogEntry infoB;  infoB.text = "b";  infoB.severity = Severity::Info;
        infoB.header.source_id = 1; infoB.header.kvchId = 1;

        LogQuery good;
        CHECK(good.parse(LogQuery::andConstraint("a OR b", SEV)),
              "combined query parses");
        CHECK(good.match(warnA),  "warn 'a' matches");
        CHECK(!good.match(infoA), "info 'a' is excluded by the constraint");
        CHECK(!good.match(infoB), "info 'b' is ALSO excluded — the whole point");

        LogQuery naive;
        naive.parse(QStringLiteral("a OR b ") + SEV);   // unbracketed
        CHECK(naive.match(infoA),
              "unbracketed form wrongly admits info 'a' — the bug avoided");
    }
}

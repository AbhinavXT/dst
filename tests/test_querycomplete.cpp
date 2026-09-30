#include "testutil.h"
#include "querylineedit.h"

static QString tok(const char *text, int cursor) {
    const QString s = QString::fromLatin1(text);
    const TokenRange r = queryTokenAt(s, cursor);
    return s.mid(r.start, r.length);
}
static bool offers(const char *token, const char *want) {
    return queryCompletionsFor(QString::fromLatin1(token))
               .contains(QString::fromLatin1(want));
}

// Token extraction and completion for the query box. The boundary rules are
// the whole risk here: get them wrong and completion either fires on a
// fragment of a quoted phrase or splices text into the middle of one.
TEST_SUITE(querycomplete)
{
    // ---- simple tokens ----------------------------------------------------
    CHECK(tok("", 0) == "",                      "empty text");
    CHECK(tok("sev", 3) == "sev",                "single token, cursor at end");
    CHECK(tok("sev", 1) == "s",                  "cursor mid-token takes the prefix");
    CHECK(tok("sev:error RAD", 13) == "RAD",     "last of several tokens");
    CHECK(tok("sev:error RAD", 9) == "sev:error", "cursor at end of first token");
    CHECK(tok("sev:error RAD", 10) == "",        "cursor just after a space is empty");
    CHECK(tok("a b c", 5) == "c",                "final token");
    CHECK(tok("  lead", 6) == "lead",            "leading whitespace skipped");

    // ---- parentheses are separators --------------------------------------
    CHECK(tok("(RAD", 4) == "RAD",               "open paren does not join the token");
    CHECK(tok("(a OR b)", 7) == "b",             "token before a closing paren");

    // ---- quoted phrases stay whole ---------------------------------------
    // This is the case that matters: a space inside "No Error" must not
    // split the term, or completion would fire on the fragment "Error".
    CHECK(tok("\"No Error", 9) == "\"No Error",  "space inside an open quote does not split");
    CHECK(tok("sev:error \"No Er", 16) == "\"No Er",
          "quoted run after other tokens stays whole");
    CHECK(tok("\"No Error\" RAD", 14) == "RAD",
          "token after a CLOSED quote is separate");

    // ---- regex runs stay whole -------------------------------------------
    CHECK(tok("/Link\\s+\\d", 10) == "/Link\\s+\\d", "regex body is one token");
    CHECK(tok("msg:/a b", 8) == "msg:/a b",       "space inside a regex does not split");

    // ---- cursor clamping --------------------------------------------------
    CHECK(tok("abc", 99) == "abc",               "cursor past the end clamps");
    CHECK(tok("abc", -5) == "",                  "negative cursor clamps to 0");

    // ---- field completion -------------------------------------------------
    CHECK(offers("s", "sev:"),      "partial 's' offers sev:");
    CHECK(offers("s", "src:"),      "partial 's' offers src:");
    CHECK(offers("h", "hex:"),      "offers hex: — the least guessable field");
    CHECK(offers("a", "after:"),    "offers after:");
    CHECK(offers("N", "NOT"),       "offers NOT, which no search box hints at");
    CHECK(offers("", "sev:"),       "empty token offers the full field list");

    // ---- value completion once a field is committed ----------------------
    CHECK(offers("sev:", "sev:error"),   "sev: offers its severities");
    CHECK(offers("sev:", "sev:warn"),    "…including warn");
    CHECK(offers("sev:e", "sev:error"),  "partial value still offered");
    CHECK(offers("dir:", "dir:in"),      "dir: offers directions");
    CHECK(offers("dir:", "dir:none"),    "…including none");
    CHECK(offers("after:", "after:-15m"),"after: offers relative times");
    CHECK(offers("len:", "len:>64"),     "len: offers a comparison form");

    // Fields with open-ended values must not invent suggestions.
    CHECK(queryCompletionsFor("msg:").isEmpty(),  "msg: has no fixed values");
    CHECK(queryCompletionsFor("src:").isEmpty(),  "src: has no fixed values");
    CHECK(queryCompletionsFor("hex:").isEmpty(),  "hex: has no fixed values");

    // A completion must always contain the token it completes, or splicing
    // it back would silently drop what the user typed.
    for (const char *t : { "s", "sev:", "dir:", "after:", "len:", "N" }) {
        const QString token = QString::fromLatin1(t);
        for (const QString &c : queryCompletionsFor(token)) {
            CHECK(c.startsWith(token, Qt::CaseInsensitive)
                  || token.compare(c.left(token.size()), Qt::CaseInsensitive) == 0
                  || c.startsWith(token.left(token.indexOf(':') + 1)),
                  "every completion extends the typed token");
        }
    }
}

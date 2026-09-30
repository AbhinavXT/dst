#include "testutil.h"

#include "findbar.h"
#include "logmodel.h"
#include "logentry.h"
#include "logquery.h"
#include "settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include "querylineedit.h"
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QtTest/QTest>

// =============================================================================
//  Find: the search modes.
//
//  Find used to be plain text, a regex box and a query box, with the scan
//  deciding which one won by greying the others out as it ran. The two
//  searches this program actually needs and did not have were:
//
//    – ESCAPES, so a tab or a \x1f inside a message can be searched for at
//      all. Typing a real tab into a one-line box is not possible.
//    – BYTES, because half of what is being looked at here is a datagram,
//      and the message column is a rendering of it. `hex:` existed in the
//      query language, so the bytes were reachable — but only by knowing
//      the language, and only through a mode that turns every other option
//      off.
//
//  Both are modes rather than checkboxes, because five ways of reading the
//  same box are a choice of one, not five independent switches.
// =============================================================================

namespace {

LogEntryPtr mk(const QString &text, qint64 ms, const QByteArray &raw = QByteArray())
{
    auto e = QSharedPointer<LogEntry>::create();
    e->header.source_id = 21;
    e->header.kvchId    = 1;
    e->epochMs   = ms;
    e->severity  = Severity::Info;
    e->text      = text;
    e->rawBytes  = raw;
    e->cacheDerived();
    return e;
}

// Controls are found by objectName rather than by their label: the labels
// change with the shape (the strip says "W", the window says "Whole word
// only"), and a test that matched on text would be a test of the strip.
QCheckBox *box(FindBar *bar, const char *name)
{
    return bar->findChild<QCheckBox *>(QString::fromLatin1(name));
}

QueryLineEdit *edit(FindBar *bar)
{
    return bar->findChild<QueryLineEdit *>(QStringLiteral("findEdit"));
}

}  // namespace

// =============================================================================
//  Extended mode: the escape rules, with no window in sight.
// =============================================================================
TEST_SUITE(findescapes)
{
    using U = FindBar::Unescaped;

    // ---- the ones people actually type ------------------------------------
    {
        const U u = FindBar::unescapeExtended(QStringLiteral("a\\tb"));
        CHECK(u.ok, "\\t parses");
        CHECK(u.text == QStringLiteral("a\tb"), "and becomes a real tab");
    }
    {
        const U u = FindBar::unescapeExtended(QStringLiteral("one\\ntwo"));
        CHECK(u.ok && u.text == QStringLiteral("one\ntwo"), "\\n becomes a newline");
    }
    {
        const U u = FindBar::unescapeExtended(QStringLiteral("\\r\\0"));
        CHECK(u.ok, "\\r and \\0 parse");
        CHECK(u.text.size() == 2, "as two characters");
        CHECK(u.text.at(0) == QChar('\r') && u.text.at(1) == QChar(0),
              "a carriage return and a NUL");
    }
    {
        const U u = FindBar::unescapeExtended(QStringLiteral("C:\\\\logs"));
        CHECK(u.ok && u.text == QStringLiteral("C:\\logs"),
              "\\\\ is how you search for one backslash");
    }

    // ---- \x, which is the one this program needs ---------------------------
    //
    // A KAVACH message can carry a separator byte that renders as nothing at
    // all. \x1f is the only way to type it.
    {
        const U u = FindBar::unescapeExtended(QStringLiteral("\\x1f"));
        CHECK(u.ok, "\\x1f parses");
        CHECK(u.text.size() == 1 && u.text.at(0) == QChar(0x1F),
              "as ONE character, U+001F — not U+01F1");
    }
    {
        // The greedy-escape bug from the previous session, in a place where a
        // user rather than a compiler is doing the reading: \x1f followed by
        // a digit must stop after two digits.
        const U u = FindBar::unescapeExtended(QStringLiteral("\\x1f1"));
        CHECK(u.ok, "\\x1f1 parses");
        CHECK(u.text.size() == 2, "as two characters");
        CHECK(u.text.at(0) == QChar(0x1F) && u.text.at(1) == QChar('1'),
              "U+001F then the literal '1', because \\x takes exactly two digits");
    }
    {
        const U u = FindBar::unescapeExtended(QStringLiteral("\\u00b5V"));
        CHECK(u.ok, "\\uHHHH parses");
        CHECK(u.text == QStringLiteral("\u00b5V"), "and gives the character it names");
    }

    // ---- the failures, which must be LOUD ----------------------------------
    //
    // Every editor passes an unknown escape through as literal text. Here
    // that would mean \s searching for a backslash and an s, finding nothing,
    // and nothing on screen saying why — the same silent-failure shape that
    // the round-trip validator was written to end.
    {
        const U u = FindBar::unescapeExtended(QStringLiteral("a\\sb"));
        CHECK(!u.ok, "an escape the mode does not know is refused");
        CHECK(u.error.contains(QStringLiteral("\\s")), "and the message names it");
        CHECK(u.errorOffset == 1, "pointing at the backslash, not the end of the line");
    }
    {
        const U u = FindBar::unescapeExtended(QStringLiteral("trailing\\"));
        CHECK(!u.ok, "a lone trailing backslash is refused");
        CHECK(u.errorOffset == 8, "at the backslash");
        CHECK(u.error.contains(QStringLiteral("\\\\")),
              "and says how to search for a backslash instead");
    }
    {
        const U u = FindBar::unescapeExtended(QStringLiteral("\\x1"));
        CHECK(!u.ok, "\\x with one digit is refused rather than guessed at");
    }
    {
        const U u = FindBar::unescapeExtended(QStringLiteral("\\xzz"));
        CHECK(!u.ok, "\\x with non-hex digits is refused");
    }
    {
        const U u = FindBar::unescapeExtended(QStringLiteral("\\u12"));
        CHECK(!u.ok, "\\u with two digits is refused");
    }

    // ---- text with no escapes is left exactly alone ------------------------
    {
        const U u = FindBar::unescapeExtended(QStringLiteral("STN_ID 4 upcoming"));
        CHECK(u.ok && u.text == QStringLiteral("STN_ID 4 upcoming"),
              "ordinary text passes through unchanged");
    }
}

// =============================================================================
//  The hex pattern parser, shared with the query language's hex: term.
//
//  One parser for both so that a pattern copied out of the raw-bytes panel
//  cannot be valid in the filter bar and invalid in Find, or the other way
//  round.
// =============================================================================
TEST_SUITE(findhexpattern)
{
    {
        const HexPattern p = parseHexPattern(QStringLiteral("0a1b"));
        CHECK(p.ok, "bare digits parse");
        CHECK(p.bytes == QByteArray::fromHex("0a1b"), "into the bytes they name");
    }
    {
        // The three separators an operator actually produces: the hex panel
        // uses spaces, Wireshark uses colons, a spreadsheet gives dashes.
        const HexPattern a = parseHexPattern(QStringLiteral("0a 1b"));
        const HexPattern b = parseHexPattern(QStringLiteral("0A:1B"));
        const HexPattern c = parseHexPattern(QStringLiteral("0a-1b"));
        CHECK(a.ok && b.ok && c.ok, "spaces, colons and dashes all parse");
        CHECK(a.bytes == b.bytes && b.bytes == c.bytes,
              "and all three mean the same two bytes");
    }
    {
        const HexPattern p = parseHexPattern(QStringLiteral("0g"));
        CHECK(!p.ok, "a letter that is not a hex digit is an error");
        CHECK(p.error.contains(QStringLiteral("g")), "naming the character");
        CHECK(p.errorOffset == 1, "at its position");
    }
    {
        // The old parser kept anything isLetterOrNumber() and handed it to
        // fromHex, which drops what it cannot read: "0a1z2c" quietly became
        // a different pattern with no matches and no explanation.
        const HexPattern p = parseHexPattern(QStringLiteral("0a1z2c"));
        CHECK(!p.ok, "a stray letter mid-pattern is refused, not skipped");
    }
    {
        const HexPattern p = parseHexPattern(QStringLiteral("0a 1"));
        CHECK(!p.ok, "an odd number of digits is refused");
        CHECK(p.error.contains(QStringLiteral("pairs")),
              "and the message says bytes come in pairs");
    }
    {
        const HexPattern p = parseHexPattern(QStringLiteral("   "));
        CHECK(!p.ok, "separators alone are not a pattern");
    }

    // ---- and the query language agrees ------------------------------------
    {
        LogQuery q;
        CHECK(q.parse(QStringLiteral("hex:\"0a 1b\"")), "hex: still takes a spaced pattern");
        CHECK(!q.parse(QStringLiteral("hex:0a1z2c")),
              "and now refuses the one the shared parser refuses");
    }
}

// =============================================================================
//  The modes, driven through a real bar over a real model.
// =============================================================================
TEST_SUITE(findmodes)
{
    const bool savedDetached = Settings::findDetached();
    const int  savedMode     = Settings::findMode();
    const bool savedWrap     = Settings::findWrap();
    Settings::setFindDetached(false);   // the strip: no window to manage here
    Settings::setFindMode(0);
    Settings::setFindWrap(true);

    LogModel model(nullptr, 1000);
    model.appendEntries({
        mk(QStringLiteral("STN_ID 4 upcoming"), 1000, QByteArray::fromHex("01020a1b03")),
        mk(QStringLiteral("col\tsep"),          2000, QByteArray::fromHex("ff00")),
        mk(QStringLiteral("nothing here"),      3000, QByteArray::fromHex("0a1b")),
        mk(QStringLiteral("STN 7 acknowledged"),4000, QByteArray()),
    });

    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    QTableView view;
    view.setModel(&proxy);
    view.resize(600, 400);
    view.show();

    FindBar bar(&view);
    bar.activate();
    QTest::qWait(20);

    CHECK(!bar.isDetached(), "the bar stays docked when that is the preference");
    CHECK(edit(&bar) != nullptr, "the input is findable by name");

    // ---- text mode, the default -------------------------------------------
    bar.setMode(FindBar::Mode::Text);
    bar.setSearchText(QStringLiteral("STN"));
    QTest::qWait(300);                       // the 200ms debounce
    CHECK(bar.matchCount() == 2, "plain text finds both STN rows");

    // ---- extended mode: a tab nobody can type ------------------------------
    bar.setMode(FindBar::Mode::Extended);
    bar.setSearchText(QStringLiteral("col\\tsep"));
    QTest::qWait(300);
    CHECK(bar.matchCount() == 1,
          "extended mode finds the tab-separated row, which no amount of "
          "typing into a one-line box could have matched");
    CHECK(!edit(&bar)->hasQueryError(), "and the input is not marked as bad");

    // A bad escape stops the search and says so in the box, rather than
    // searching for the two characters and reporting no matches.
    bar.setSearchText(QStringLiteral("col\\qsep"));
    QTest::qWait(300);
    CHECK(bar.matchCount() == 0, "an unknown escape matches nothing");
    CHECK(edit(&bar)->hasQueryError(), "and the input says why");

    // ---- hex mode: the datagram, not the rendering -------------------------
    bar.setMode(FindBar::Mode::Hex);
    bar.setSearchText(QStringLiteral("0a 1b"));
    QTest::qWait(300);
    CHECK(bar.matchCount() == 2,
          "hex finds both frames carrying those bytes — including the one "
          "whose message text says nothing about them");
    CHECK(!edit(&bar)->hasQueryError(), "a good pattern leaves the box clean");

    bar.setSearchText(QStringLiteral("ff"));
    QTest::qWait(300);
    CHECK(bar.matchCount() == 1, "a one-byte pattern matches the frame that carries it");

    bar.setSearchText(QStringLiteral("0a1"));
    QTest::qWait(300);
    CHECK(bar.matchCount() == 0 && edit(&bar)->hasQueryError(),
          "an odd-length pattern is refused in the box rather than half-searched");

    // ---- regex mode, and a pattern that will not compile -------------------
    bar.setMode(FindBar::Mode::Regex);
    bar.setSearchText(QStringLiteral("STN_?ID"));
    QTest::qWait(300);
    CHECK(bar.matchCount() == 1, "a regex matches");

    bar.setSearchText(QStringLiteral("STN(["));
    QTest::qWait(300);
    CHECK(bar.matchCount() == 0, "a broken regex matches nothing");
    CHECK(edit(&bar)->hasQueryError(),
          "and now SAYS it is broken — it used to return silently and leave "
          "'no matches' as the only clue");

    // ---- query mode --------------------------------------------------------
    bar.setMode(FindBar::Mode::Query);
    bar.setSearchText(QStringLiteral("src:21_1 AND upcoming"));
    QTest::qWait(300);
    CHECK(bar.matchCount() == 1, "the boolean language still works from Find");

    // ---- which controls each mode leaves meaningful ------------------------
    QCheckBox *whole = box(&bar, "findWholeWordBox");
    QCheckBox *aa    = box(&bar, "findCaseBox");
    auto *colBox = bar.findChild<QComboBox *>(QStringLiteral("findColumnBox"));
    CHECK(whole && aa && colBox, "the options are all present");

    bar.setMode(FindBar::Mode::Text);
    CHECK(whole->isEnabled() && aa->isEnabled() && colBox->isEnabled(),
          "text mode leaves every option live");

    bar.setMode(FindBar::Mode::Regex);
    CHECK(!whole->isEnabled(), "regex turns whole-word off — the pattern is the operator's");
    CHECK(aa->isEnabled(), "but case still applies to a regex");

    bar.setMode(FindBar::Mode::Hex);
    CHECK(!colBox->isEnabled(),
          "hex searches the whole frame, so the column selector is not offered");
    CHECK(!aa->isEnabled(), "and bytes have no case");

    bar.setMode(FindBar::Mode::Query);
    CHECK(!colBox->isEnabled() && !aa->isEnabled() && !whole->isEnabled(),
          "a query carries its own fields, case and phrases");

    // ---- the mode is remembered -------------------------------------------
    bar.setMode(FindBar::Mode::Hex);
    CHECK(Settings::findMode() == int(FindBar::Mode::Hex),
          "choosing a mode is remembered for the next Ctrl+F");

    Settings::setFindDetached(savedDetached);
    Settings::setFindMode(savedMode);
    Settings::setFindWrap(savedWrap);
}

// =============================================================================
//  Wrapping, and the window shape.
// =============================================================================
TEST_SUITE(findwindow)
{
    const bool savedDetached = Settings::findDetached();
    const bool savedWrap     = Settings::findWrap();
    const int  savedMode     = Settings::findMode();
    Settings::setFindDetached(false);
    Settings::setFindMode(0);

    LogModel model(nullptr, 1000);
    model.appendEntries({
        mk(QStringLiteral("STN 1"), 1000),
        mk(QStringLiteral("quiet"), 2000),
        mk(QStringLiteral("STN 2"), 3000),
    });
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    QTableView view;
    view.setModel(&proxy);
    view.resize(600, 400);
    view.show();

    FindBar bar(&view);
    bar.activate();
    QTest::qWait(20);
    bar.setSearchText(QStringLiteral("STN"));
    QTest::qWait(300);
    CHECK(bar.matchCount() == 2, "two matches to walk");
    CHECK(bar.matchCursor() == 0, "starting on the first");

    // ---- wrapping on, which is the default --------------------------------
    QCheckBox *wrap = box(&bar, "findWrapBox");
    CHECK(wrap != nullptr, "there is a wrap option");
    wrap->setChecked(true);

    bar.findChild<QPushButton *>(QStringLiteral("findNextBtn"))->click();
    CHECK(bar.matchCursor() == 1, "Next moves to the second");
    bar.findChild<QPushButton *>(QStringLiteral("findNextBtn"))->click();
    CHECK(bar.matchCursor() == 0, "and past the end comes back to the first");

    // ---- wrapping off ------------------------------------------------------
    //
    // Asked for because a log is read to be reported on: wrapping silently is
    // how the same three matches get read twice and written up as six.
    wrap->setChecked(false);
    CHECK(!Settings::findWrap(), "turning it off is remembered");

    bar.findChild<QPushButton *>(QStringLiteral("findNextBtn"))->click();
    CHECK(bar.matchCursor() == 1, "Next still moves while there is somewhere to go");
    bar.findChild<QPushButton *>(QStringLiteral("findNextBtn"))->click();
    CHECK(bar.matchCursor() == 1, "but stops at the last match rather than looping");

    bar.findChild<QPushButton *>(QStringLiteral("findPrevBtn"))->click();
    CHECK(bar.matchCursor() == 0, "Previous walks back");
    bar.findChild<QPushButton *>(QStringLiteral("findPrevBtn"))->click();
    CHECK(bar.matchCursor() == 0, "and stops at the first");

    // ---- the window shape --------------------------------------------------
    //
    // The strip and the window are the same widget with the same state. The
    // check that matters is that a search survives the move: an operator who
    // opens the window for more room must not lose the query that made them
    // want the room.
    bar.setDetached(true);
    QTest::qWait(20);
    CHECK(bar.isDetached(), "it opens as a window");
    CHECK(bar.searchText() == QStringLiteral("STN"), "carrying the search text over");
    CHECK(bar.matchCount() == 2, "and the matches it had already found");

    auto *adv    = bar.findChild<QWidget *>(QStringLiteral("findAdvancedPanel"));
    auto *advBtn = bar.findChild<QToolButton *>(QStringLiteral("findAdvancedBtn"));
    CHECK(adv && advBtn, "the window has an advanced section");
    CHECK(advBtn->isVisible(), "whose button is on screen once undocked");

    advBtn->setChecked(true);
    QTest::qWait(20);
    CHECK(adv->isVisible(), "opening it shows the panel");
    CHECK(Settings::findAdvancedOpen(), "and the window comes back the way it was left");
    advBtn->setChecked(false);
    QTest::qWait(20);
    CHECK(!adv->isVisible(), "closing it hides the panel again");

    // The strip has no room for the advanced section, and a widget left out
    // of a layout keeps its old geometry — so this is a check that it is
    // hidden rather than merely unplaced.
    bar.setDetached(false);
    QTest::qWait(20);
    CHECK(!adv->isVisible() && !advBtn->isVisible(),
          "docking hides the advanced section rather than leaving it floating "
          "over the strip");
    CHECK(bar.searchText() == QStringLiteral("STN"), "and the search survives docking too");

    bar.close();
    Settings::setFindDetached(savedDetached);
    Settings::setFindWrap(savedWrap);
    Settings::setFindMode(savedMode);
}

// =============================================================================
//  The scan limit.
//
//  It was a compiled-in 200,000 and the count label quoted that number
//  whether or not it was the one used. Now it is a choice, and the label
//  quotes what actually happened.
// =============================================================================
TEST_SUITE(findscanlimit)
{
    const bool savedDetached = Settings::findDetached();
    const int  savedLimit    = Settings::findScanLimit();
    const int  savedMode     = Settings::findMode();
    Settings::setFindDetached(false);
    Settings::setFindMode(0);
    Settings::setFindScanLimit(20000);

    LogModel model(nullptr, 5000);
    QVector<LogEntryPtr> rows;
    for (int i = 0; i < 60; ++i) {
        rows << mk(i % 2 ? QStringLiteral("hit %1").arg(i) : QStringLiteral("miss"),
                   1000 + i);
    }
    model.appendEntries(rows);

    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    QTableView view;
    view.setModel(&proxy);
    view.resize(600, 400);
    view.show();

    FindBar bar(&view);
    bar.activate();
    QTest::qWait(20);

    auto *limit = bar.findChild<QComboBox *>(QStringLiteral("findScanLimitBox"));
    CHECK(limit != nullptr, "the limit is a control, not a constant");
    CHECK(limit->currentData().toInt() == 20000,
          "and it opens on the saved value rather than the first item");

    bar.setSearchText(QStringLiteral("hit"));
    QTest::qWait(300);
    CHECK(bar.matchCount() == 30, "well under the cap, so everything is scanned");

    const QLabel *count = bar.findChild<QLabel *>(QStringLiteral("findCountLabel"));
    CHECK(count && !count->text().contains(QStringLiteral("first")),
          "and the count says nothing about a cap that was never reached");

    // "every row" is a real option, not a very large number.
    const int everyIdx = limit->findData(0);
    CHECK(everyIdx >= 0, "there is an unlimited option");
    limit->setCurrentIndex(everyIdx);
    QTest::qWait(300);
    CHECK(bar.matchCount() == 30, "which finds the same matches");
    CHECK(Settings::findScanLimit() == 0, "and is remembered as the choice it is");

    Settings::setFindDetached(savedDetached);
    Settings::setFindScanLimit(savedLimit);
    Settings::setFindMode(savedMode);
}

// =============================================================================
//  A view that binds a LogModel DIRECTLY, with no filter proxy.
//
//  This is what the compare window's panes do — they cast view->model() to
//  LogModel in half a dozen places, so no proxy can be slipped underneath
//  them — and patch 30 gave those panes a find bar.
//
//  The scan's fast path worked out the LogModel correctly for that case and
//  then reached it through `sortProxy->mapToSource(...)` anyway, which is
//  null when there is no proxy. So Ctrl+F in a compare pane, followed by the
//  first keystroke, dereferenced null on the default column. It is the shape
//  of bug that only real use finds: every other view in the program has a
//  proxy, and so did every test.
//
//  Written with nothing but the line edit and a wait, so it can be run
//  against the old code to watch it fall over.
// =============================================================================
TEST_SUITE(finddirectmodel)
{
    const bool savedDetached = Settings::findDetached();
    const int  savedMode     = Settings::findMode();
    Settings::setFindDetached(false);
    Settings::setFindMode(0);

    LogModel model(nullptr, 1000);
    model.appendEntries({
        mk(QStringLiteral("STN 1 upcoming"), 1000, QByteArray::fromHex("0a1b")),
        mk(QStringLiteral("quiet"),          2000),
        mk(QStringLiteral("STN 2 upcoming"), 3000),
    });

    QTableView view;
    view.setModel(&model);          // no proxy: the compare-pane shape
    view.resize(600, 400);
    view.show();

    FindBar bar(&view);
    bar.activate();
    QTest::qWait(20);

    auto *e = bar.findChild<QLineEdit *>();
    CHECK(e != nullptr, "the bar has an input");
    if (e) { e->setText(QStringLiteral("STN")); }
    QTest::qWait(300);

    CHECK(bar.matchCount() == 2,
          "a directly-bound model is searched rather than crashed into");
    CHECK(bar.matchCursor() == 0, "and the cursor lands on the first match");

    // The same path, in the mode that has to have the entry.
    bar.setMode(FindBar::Mode::Hex);
    if (e) { e->setText(QStringLiteral("0a1b")); }
    QTest::qWait(300);
    CHECK(bar.matchCount() == 1, "hex works without a proxy too");

    Settings::setFindDetached(savedDetached);
    Settings::setFindMode(savedMode);
}

// =============================================================================
//  Narrowing a search instead of rescanning it.
//
//  Typing rescanned every row from the top on each keystroke. It does not
//  have to: with a plain "contains" search, a row that does not contain "STN"
//  cannot contain "STN_ID", so the longer pattern's matches are a subset of
//  the shorter one's. The optimisation is only worth having if it finds
//  EXACTLY what a full scan finds, so that is what these check.
// =============================================================================

TEST_SUITE(findnarrowing)
{
    const bool savedDetached = Settings::findDetached();
    const int  savedMode     = Settings::findMode();
    Settings::setFindDetached(false);
    Settings::setFindMode(0);

    LogModel model(nullptr, 5000);
    QVector<LogEntryPtr> rows;
    for (int i = 0; i < 400; ++i) {
        rows << mk(QStringLiteral("@lsrp_21_1 STN_ID %1 FRAME_NUM %2")
                       .arg(i % 7).arg(i),
                   1000 + i);
    }
    rows << mk(QStringLiteral("quiet, no identifiers here"), 99000);
    model.appendEntries(rows);

    QTableView view;
    view.setModel(&model);
    view.resize(600, 400);
    view.show();

    FindBar bar(&view);
    bar.activate();
    QTest::qWait(20);

    // Type a pattern one character at a time, then the same pattern from a
    // cleared box. The first goes through the narrowing path, the second
    // cannot — there is no previous pattern to narrow from — so the two
    // must agree, row for row.
    auto typeUp = [&](const QString &target) {
        bar.setSearchText(QString());
        QTest::qWait(250);
        for (int n = 1; n <= target.size(); ++n) {
            bar.setSearchText(target.left(n));
            QTest::qWait(250);
        }
        return bar.matchRows();
    };
    auto typeWhole = [&](const QString &target) {
        bar.setSearchText(QString());
        QTest::qWait(250);
        bar.setSearchText(target);
        QTest::qWait(250);
        return bar.matchRows();
    };

    const QVector<int> narrowed = typeUp(QStringLiteral("STN_ID 3"));
    const QVector<int> full     = typeWhole(QStringLiteral("STN_ID 3"));
    CHECK(!full.isEmpty(), "the pattern matches something to compare");
    CHECK(narrowed == full,
          "typed one key at a time finds exactly what one full scan finds — "
          "the same rows, in the same order, not merely the same count");

    // A pattern that ends up matching nothing must still end up matching
    // nothing: narrowing an empty set is where an off-by-one hides.
    CHECK(typeUp(QStringLiteral("STN_ID 9"))
              == typeWhole(QStringLiteral("STN_ID 9")),
          "and agrees when the answer is no rows at all");

    // ---- where narrowing must NOT be used ---------------------------------
    //
    // Each of these breaks the subset property. If narrowing were applied
    // anyway the result would be a quiet undercount: rows that should match
    // never looked at, and nothing on screen saying so.
    {
        // Regex: /FRAME_NUM 1$/ matches rows /FRAME_NUM 1/ does not rule out,
        // but neither is a subset of the other in general.
        bar.setSearchText(QString());
        QTest::qWait(250);
        bar.setMode(FindBar::Mode::Regex);
        bar.setSearchText(QStringLiteral("STN_ID [0-9]"));
        QTest::qWait(250);
        const int broad = bar.matchCount();
        bar.setSearchText(QStringLiteral("STN_ID [0-9] FRAME_NUM 1$"));
        QTest::qWait(250);
        const int narrow = bar.matchCount();
        bar.setMode(FindBar::Mode::Text);
        CHECK(broad > 0 && narrow >= 0 && narrow < broad,
              "a regex search is rescanned in full — its longer pattern is "
              "not a subset of its shorter one");
    }

    // Rows arriving underneath renumber everything, so the next scan has to
    // be a full one or it would search stale row numbers.
    {
        bar.setSearchText(QString());
        QTest::qWait(250);
        bar.setSearchText(QStringLiteral("STN_ID 3"));
        QTest::qWait(250);
        const int before = bar.matchCount();

        model.appendEntries({ mk(QStringLiteral("@lsrp_21_1 STN_ID 3 late"),
                                 200000) });
        bar.setSearchText(QStringLiteral("STN_ID 3 "));
        QTest::qWait(300);
        CHECK(bar.matchCount() >= 1 && before > 0,
              "a row that arrives mid-search is still findable — the scan "
              "after it is a full one, not a narrowing of a stale set");
    }

    Settings::setFindDetached(savedDetached);
    Settings::setFindMode(savedMode);
}

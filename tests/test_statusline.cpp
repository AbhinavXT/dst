#include "testutil.h"

#include "statusline.h"
#include "uicolors.h"
#include "theme.h"

#include <QApplication>
#include <QElapsedTimer>

// =============================================================================
//  The shared status line.
//
//  Thirty-five of these existed across thirteen files, each with its own
//  wording, colour and idea of how long a message should last. The colours
//  were unified earlier; the conventions were not, and those are what an
//  operator actually reads.
//
//  The rule being tested: a transient message expires, a problem does not.
//  A success that lingers reads as current state; a failure that vanishes
//  before it is read may as well not have been shown.
// =============================================================================

TEST_SUITE(statusline)
{
    ThemeUtil::apply(Theme::Light);
    StatusLine s;

    // ---- it starts empty ----------------------------------------------------
    CHECK(s.kind() == StatusLine::Kind::None, "starts with nothing to say");
    CHECK(s.text().isEmpty(), "and no text");
    CHECK(!s.isSticky(), "nothing to be sticky about");

    // ---- the four verbs -----------------------------------------------------
    {
        s.say(QStringLiteral("Running…"));
        CHECK(s.kind() == StatusLine::Kind::Info, "say() is neutral");
        CHECK(!s.isSticky(), "and transient");
        // QStringLiteral, not QLatin1String: the ellipsis is three bytes of
        // UTF-8 and QLatin1String would read them as three separate
        // characters, so this comparison would fail for a reason that has
        // nothing to do with the class under test.
        CHECK(s.message() == QStringLiteral("Running…"),
              "message() gives the sentence without decoration");

        s.ok(QStringLiteral("Sent 40 datagrams"));
        CHECK(s.kind() == StatusLine::Kind::Ok, "ok() reports success");
        CHECK(!s.isSticky(), "which does not need to persist");

        s.warn(QStringLiteral("2 presets missing"));
        CHECK(s.kind() == StatusLine::Kind::Warn, "warn() qualifies");
        CHECK(s.isSticky(), "and stays until something replaces it");

        s.fail(QStringLiteral("Build failed: bad CRC"));
        CHECK(s.kind() == StatusLine::Kind::Fail, "fail() reports failure");
        CHECK(s.isSticky(), "and stays");
    }

    // ---- the glyph ----------------------------------------------------------
    // Colour alone is not a signal. For a colour-blind reader a red sentence
    // and a green one are the same sentence, so each kind carries a mark.
    {
        s.ok(QStringLiteral("done"));
        const QString okText = s.text();
        s.fail(QStringLiteral("done"));
        const QString failText = s.text();
        CHECK(okText != failText,
              "the same words under two verbs do not render identically");
        CHECK(okText.contains(QStringLiteral("✓")), "success is marked");
        CHECK(failText.contains(QStringLiteral("✕")), "and so is failure");
        s.warn(QStringLiteral("done"));
        CHECK(s.text().contains(QStringLiteral("⚠")), "and a qualification");
        CHECK(s.message() == QLatin1String("done"),
              "but message() is still the plain sentence");
    }

    // ---- colour comes from UiColor, in both themes -------------------------
    {
        for (Theme t : { Theme::Light, Theme::Dark }) {
            ThemeUtil::apply(t);
            s.fail(QStringLiteral("x"));
            CHECK(s.styleSheet().contains(UiColor::error().name()),
                  "failure is drawn in the error colour of the active theme");
            s.ok(QStringLiteral("x"));
            CHECK(s.styleSheet().contains(UiColor::ok().name()),
                  "and success in the ok colour");
        }
        ThemeUtil::apply(Theme::Light);
    }

    // ---- clearing -----------------------------------------------------------
    {
        s.warn(QStringLiteral("something"));
        s.clear();
        CHECK(s.kind() == StatusLine::Kind::None, "clear() resets the kind");
        CHECK(s.text().isEmpty() && s.message().isEmpty(), "and the text");
        CHECK(s.styleSheet().isEmpty(), "and the colour");

        // An empty message is a clear, not a blank coloured line.
        s.fail(QString());
        CHECK(s.kind() == StatusLine::Kind::None,
              "an empty message clears rather than showing an empty failure");
    }

    // ---- a failure is not wiped by an earlier message expiring --------------
    // The sequence that used to lose the important one: "sending…" starts a
    // timer, the send fails, and three seconds later the timer fires and
    // erases the failure.
    {
        s.say(QStringLiteral("sending…"));
        s.fail(QStringLiteral("send error: no route to host"));
        CHECK(s.kind() == StatusLine::Kind::Fail, "the failure is showing");

        // Run the loop for longer than the transient lifetime would be if a
        // stale timer were still armed.
        QElapsedTimer t; t.start();
        while (t.elapsed() < 150) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        CHECK(s.kind() == StatusLine::Kind::Fail,
              "and is still showing — the earlier transient's timer was stopped");
        CHECK(s.message().contains(QLatin1String("no route")),
              "with its own words, not the ones before it");
    }

    CHECK(StatusLine::transientMs() >= 3000 && StatusLine::transientMs() <= 15000,
          "a transient message lasts long enough to read and not long enough "
          "to mistake for current state");
}

// =============================================================================
//  state() versus say().
//
//  The distinction that made this class need five verbs rather than four.
//  say() reports an event — "Searching 12 files…" — and expires, because a
//  finished search should not still say it is running. state() is a caption:
//  "Select a row to decode it", "Paste a hex frame". A caption that expires
//  leaves a panel blank and unexplained, which is worse than a panel that
//  never explained itself.
// =============================================================================

TEST_SUITE(statuslinestate)
{
    StatusLine s;

    s.state(QStringLiteral("Select a row to decode it."));
    CHECK(s.kind() == StatusLine::Kind::State, "state() is its own kind");
    CHECK(s.isSticky(), "and does not expire");
    CHECK(s.message() == QStringLiteral("Select a row to decode it."),
          "carrying the caption");

    // No glyph: a caption is not a verdict, and ✓/⚠/✕ would imply one.
    CHECK(!s.text().contains(QStringLiteral("✓"))
          && !s.text().contains(QStringLiteral("⚠"))
          && !s.text().contains(QStringLiteral("✕")),
          "a caption carries no verdict glyph");
    CHECK(s.text() == s.message(), "so the label reads exactly as written");

    // It is dimmer than a verdict — the same muted colour as neutral
    // progress, because it is describing rather than reporting.
    CHECK(s.styleSheet().contains(UiColor::muted().name()),
          "drawn in the muted colour");

    // A caption is replaced by the next caption, not by a timer.
    s.say(QStringLiteral("Searching…"));
    CHECK(!s.isSticky(), "say() by contrast expires");
    s.state(QStringLiteral("18 field(s), 2 with byte ranges."));
    CHECK(s.isSticky(), "and setting a caption stops that expiry");
    CHECK(s.kind() == StatusLine::Kind::State, "leaving the caption showing");
}

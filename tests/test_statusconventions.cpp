#include "testutil.h"

#include "framediffwindow.h"
#include "statusline.h"

#include <QApplication>
#include <QTest>

// =============================================================================
//  Status feedback, after the conversion.
//
//  Thirty-five one-line labels across thirteen files, each with its own idea
//  of colour, wording and lifetime. uicolors unified the colours in patch 30;
//  StatusLine unified the CONVENTIONS, and eight surfaces adopted it. Three
//  did not, and they were the three with the most to gain:
//
//    dlrplayerdialog  a failed seek reported in the same plain text as
//                     "Finished.", and then overwritten by it
//    framediffwindow  a caption that read as an event
//    fieldplot        a warning that existed only as a stylesheet swap
//
//  What is tested here is the RULE, not the wording: which messages expire and
//  which do not, and that a failure cannot be papered over by the routine
//  message that follows it. Wording changes; the rule is what an operator
//  learns.
//
//  The audit that remains is in the comment at the bottom of this file: the
//  labels NOT converted, and why each is not a status line.
// =============================================================================

TEST_SUITE(statusconventions)
{
    // ---- the four verbs and one caption ------------------------------------
    {
        StatusLine s;
        s.say(QStringLiteral("working"));
        CHECK(s.kind() == StatusLine::Kind::Info, "say() is neutral progress");
        CHECK(!s.isSticky(), "and clears itself — a stale progress line is a lie");

        s.ok(QStringLiteral("done"));
        CHECK(!s.isSticky(), "a success expires too: it describes a moment that passed");

        s.warn(QStringLiteral("careful"));
        CHECK(s.isSticky(), "a warning stays until the operator moves on");

        s.fail(QStringLiteral("no"));
        CHECK(s.isSticky(), "so does a failure");

        s.state(QStringLiteral("Select a row"));
        CHECK(s.isSticky(),
              "and a caption stays, because it is not news — a panel that "
              "explains itself for six seconds and then goes blank is worse "
              "than one that never explained itself");

        CHECK(s.message() == QStringLiteral("Select a row"),
              "the message reads back without its glyph");
        CHECK(s.text().contains(s.message()),
              "and what is shown carries the message plus a glyph, because "
              "colour alone is not a signal to a colour-blind reader");
    }

    // ---- a failure is not overwritten by the next routine message ----------
    //
    // The player used to decide this with startsWith() against the exact
    // sentence "Seek query never matched", which is a comparison that survives
    // exactly as long as nobody rewords the sentence.
    {
        StatusLine s;
        s.fail(QStringLiteral("Seek query never matched — nothing was sent."));
        const bool wouldOverwrite = !s.isSticky();
        CHECK(!wouldOverwrite,
              "'Finished.' does not paper over the reason the run sent nothing");

        s.say(QStringLiteral("Finished."));
        CHECK(s.kind() == StatusLine::Kind::Info,
              "and when the caller does decide to move on, it moves on");
    }

    // ---- the frame diff window's line is a caption -------------------------
    {
        FrameDiffWindow win;
        win.show();
        QTest::qWait(20);

        auto *line = win.findChild<StatusLine *>();
        CHECK(line != nullptr, "the window's status is a StatusLine, not a bare label");
        if (line) {
            CHECK(line->kind() == StatusLine::Kind::State,
                  "and opens with a caption describing what to do");
            CHECK(line->isSticky(),
                  "which does not vanish while the operator is reading it");
            CHECK(!line->message().isEmpty(), "and says something");
        }
    }
}

// =============================================================================
//  The audit: what was deliberately NOT converted.
//
//  A status line reports an EVENT — something happened, here is how it went.
//  The labels below describe STANDING STATE or are fixed captions, and giving
//  them expiry and a glyph would be wrong in both directions: state that
//  expires is worse than no state, and a tick beside a permanent readout says
//  something just succeeded when nothing did.
//
//    mainwindow      m_lblBindStatus, m_lblDiskStatus — the socket is bound
//                    or it is not; that is true until it changes, and must
//                    never expire
//    filterbar       m_countLabel — a count of what is showing, recomputed
//                    per filter, not a report of an action
//    findbar         m_count, m_wrapHint, m_holdHint — same: a live readout
//                    of the current search
//    lococonsole     m_lblCrc, m_lblSeq — per-source health indicators
//    settingsdialog  the hint under each control — a fixed caption in the
//                    form, written once and never changed
//    gototimestamp   the format hint — likewise
//    comparewindow   timeLabel — the pane's current timestamp readout
//
//  sessionkeydialog's m_explain and sessionwindow's m_lblSummary sit on the
//  line between the two: both report the result of an action, but both also
//  describe the state the dialog is now in, and both are the only text in
//  their panel. Left alone deliberately rather than by oversight — converting
//  them would mean deciding whether "no key set loaded" is a failure or a
//  caption, and that is a question about the workflow, not about labels.
// =============================================================================

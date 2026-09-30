#include "testutil.h"
#include "testassertions.h"
#include "messagedispatcher.h"

#include <QSignalSpy>

namespace {
// An LSRP capture line, as a log row's text.
LogEntryPtr lsrp(quint32 frameNum, quint8 health, qint64 ms,
                 quint32 speed = 0, quint32 mode = 2,
                 quint32 tagLink = 0, quint32 brake = 0,
                 quint32 emerg = 0)
{
    QByteArray f(64, '\0');
    // The frame has to agree with itself, because the decoder now places the
    // body from message_len - PKT_LENGTH rather than from a constant 10. A
    // fixture with both fields left at zero is a frame no loco would send, and
    // it used to decode only because the offset was assumed.
    //
    //   message_len (LE, bytes 4..5) = 10-byte header + 29-byte packet = 39
    //   PKT_LENGTH  (7 bits at bit 84) = 29
    const int kPktLen = 29;
    f[4] = char(10 + kPktLen);        // 39, low byte
    f[5] = 0;
    int p = 80;
    auto put = [&](quint32 val, int n) {
        for (int i = n - 1; i >= 0; --i) {
            if ((val >> i) & 1) f[p >> 3] = f[p >> 3] | char(1 << (7 - (p & 7)));
            ++p;
        }
    };
    put(0,4); put(kPktLen,7); put(frameNum,17); put(0,20); put(0,3); put(0,23);
    put(0,9); put(0,9); put(0,2); put(0,11); put(speed,9); put(0,2); put(emerg,3);
    put(mode,4); put(0,10); put(0,1); put(tagLink,3); put(0,9); put(brake,3); put(0,2);
    put(0,4); put(0,1); put(0,4); put(0,2); put(health,6); put(0,32);

    auto e = QSharedPointer<LogEntry>::create();
    e->epochMs = ms; e->header.source_id = 21; e->header.kvchId = 1;
    e->text = QStringLiteral("@lsrp_1_1 2026-08-19T10:00:00 %1 %2")
                  .arg(frameNum).arg(QString::fromLatin1(f.toHex()));
    e->cacheDerived();
    return e;
}
const TestAssertion *find(const TestAssertionEngine &en, const QString &id) {
    for (const TestAssertion &a : en.assertions()) if (a.id == id) return &a;
    return nullptr;
}
}  // namespace

// A DIP1 frame: 24 single-bit inputs, cab1_active at bit 24 (1-based
// field 24, so the 24th bit read). lsb-first packet.
static LogEntryPtr dip1(bool cab1, bool cab2, qint64 ms, bool sos = false)
{
    QByteArray f(8, '\0');
    auto setBit = [&](int idx1, bool v) {      // idx1 is the 1-based field no.
        const int pos = idx1 - 1;
        if (v) f[pos >> 3] = f[pos >> 3] | char(1 << (pos & 7));   // lsb-first
    };
    setBit(2,  sos);      // dmi1_sos_no
    setBit(24, cab1);     // cab1_active
    setBit(27, cab2);     // cab2_active
    auto e = QSharedPointer<LogEntry>::create();
    e->epochMs = ms; e->header.source_id = 21; e->header.kvchId = 1;
    e->text = QStringLiteral("@dip1_1_1 2026-08-19T10:00:00 1 %1")
                  .arg(QString::fromLatin1(f.toHex()));
    e->cacheDerived();
    return e;
}

TEST_SUITE(assertions)
{
    // ---- loading ----------------------------------------------------------
    {
        TestAssertionEngine en;
        QString err;
        CHECK(!en.loadFromJson("not json", &err), "malformed JSON rejected");
        CHECK(!err.isEmpty(), "with a message");
        CHECK(!en.loadFromJson("{}", &err), "a bare object is rejected");
        CHECK(!en.loadFromJson("[]", &err), "an empty array is rejected");
        CHECK(en.assertions().isEmpty(), "nothing loaded from a bad file");
    }

    // A case with a broken query must be KEPT and flagged, never dropped —
    // a test that is silently not being checked is worse than a visibly
    // broken one.
    {
        TestAssertionEngine en;
        CHECK(en.loadFromJson(R"([
          {"id":"A","title":"ok","query":"field:FRAME_NUM&7=3"},
          {"id":"B","title":"broken","query":"field:FRAME_NUM & (("}
        ])"), "loads despite one bad query");
        CHECK(en.assertions().size() == 2, "both cases retained");
        const TestAssertion *b = find(en, "B");
        CHECK(b && !b->queryOk, "the bad one is marked unusable");
        CHECK(b && !b->queryError.isEmpty(), "and carries its error");
        CHECK(!b->observed(), "an unusable case is never observed");
    }

    // ---- observation ------------------------------------------------------
    {
        TestAssertionEngine en;
        CHECK(en.loadFromJson(R"([
          {"id":"32.9.3","title":"group 3","query":"field:FRAME_NUM&7=3"},
          {"id":"RADIO1","title":"radio1 fault",
           "query":"field:FRAME_NUM&7=3 field:RADIO1_LINK_FAIL=1"}
        ])"), "loaded");

        QSignalSpy spy(&en, &TestAssertionEngine::assertionObserved);

        en.observe(lsrp(1, 0x00, 1000));           // wrong group
        CHECK(!find(en,"32.9.3")->observed(), "a group-1 frame does not satisfy 32.9.3");
        CHECK(spy.count() == 0, "and fires no signal");

        en.observe(lsrp(3, 0x00, 2000));           // right group, no fault
        CHECK(find(en,"32.9.3")->observed(), "a group-3 frame satisfies it");
        CHECK(!find(en,"RADIO1")->observed(), "but not the fault case");
        CHECK(spy.count() == 1, "one signal for the newly observed case");

        en.observe(lsrp(11, 0x01, 3000));          // group 3 WITH the fault
        CHECK(find(en,"RADIO1")->observed(), "fault case now satisfied");
        CHECK(spy.count() == 2, "second signal");

        en.observe(lsrp(19, 0x01, 4000));
        CHECK(spy.count() == 2, "no repeat signal once already observed");
        CHECK(find(en,"RADIO1")->hits == 2, "but hits keep counting");

        const TestAssertion *a = find(en,"32.9.3");
        CHECK(a->firstMs == 2000, "first-seen time recorded");
        CHECK(a->lastMs == 4000,  "last-seen time updated");
        CHECK(!a->firstText.isEmpty(), "evidence text captured");
        CHECK(a->firstText.startsWith("@lsrp"), "and it is the frame itself");
        CHECK(en.observedCount() == 2, "observed count");
    }

    // ---- coverage: the document's simulate-with list ----------------------
    // 32.9.3 is not satisfied by one frame ending 03. The SIF asks for
    // thirteen specific frame numbers, and checking those by eye is exactly
    // where a run goes wrong.
    {
        TestAssertionEngine en;
        CHECK(en.loadFromJson(R"([
          {"id":"32.9.3","title":"group 3","query":"field:FRAME_NUM&7=3",
           "cover_field":"FRAME_NUM","cover_values":[3,11,19,27]}
        ])"), "loaded with coverage");

        const TestAssertion *a = find(en,"32.9.3");
        en.observe(lsrp(3, 0x00, 1000));
        CHECK(a->hits == 1, "matched");
        CHECK(!a->observed(), "one value is NOT enough when coverage is required");
        CHECK(a->coverMissing().size() == 3, "three values still missing");

        en.observe(lsrp(11, 0x00, 2000));
        en.observe(lsrp(19, 0x00, 3000));
        CHECK(!a->observed(), "still incomplete");
        CHECK(a->coverMissing().contains(27), "and it says which is missing");

        QSignalSpy spy(&en, &TestAssertionEngine::assertionObserved);
        en.observe(lsrp(27, 0x00, 4000));
        CHECK(a->observed(), "complete once every listed value is seen");
        CHECK(a->coverMissing().isEmpty(), "nothing missing");
        CHECK(spy.count() == 1, "signal fires on completion, not on first hit");
    }

    // Coverage is collected only from frames that MATCHED. A frame number
    // seen while the condition was absent must not count, or the
    // simulate-with list would report as exercised when it was not.
    {
        TestAssertionEngine en;
        en.loadFromJson(R"([
          {"id":"X","title":"faulted only",
           "query":"field:FRAME_NUM&7=3 field:RADIO1_LINK_FAIL=1",
           "cover_field":"FRAME_NUM","cover_values":[3,11]}
        ])");
        const TestAssertion *a = find(en,"X");
        en.observe(lsrp(3,  0x01, 1000));      // matches
        en.observe(lsrp(11, 0x00, 2000));      // right frame, NO fault
        CHECK(a->hits == 1, "only the matching frame counted");
        CHECK(a->coverMissing().contains(11),
              "a non-matching frame does not count toward coverage");
    }

    // ---- cross-packet implication -----------------------------------------
    // The shape most acceptance cases actually have: cause and effect arrive
    // in DIFFERENT packet types, seconds apart. Neither half can be written
    // as a single query over one message.
    {
        TestAssertionEngine en;
        CHECK(en.loadFromJson(R"([
          {"id":"STANDBY","title":"stand-by when no cab active",
           "given":"field:cab1_active=0 field:cab2_active=0",
           "then":"field:LOCO_MODE=1","within_ms":10000}
        ])"), "implication loads");
        const TestAssertion *a = find(en, "STANDBY");
        CHECK(a->kind == AssertKind::Sequence,
              "given/then normalises to a two-step sequence — one evaluator");
        CHECK(a->steps.size() == 2, "two steps");
        CHECK(a->queryOk, "both halves parsed");
        CHECK(a->queryText.contains("given") && a->queryText.contains("then"),
              "the report shows a readable condition");

        // Precondition alone proves nothing yet.
        en.observe(dip1(false, false, 1000));
        CHECK(!a->observed(), "armed but not yet satisfied");
        CHECK(a->satisfied == 0 && a->violated == 0, "no outcome yet");

        // The consequent arrives in a different packet, within the window.
        QSignalSpy spy(&en, &TestAssertionEngine::assertionObserved);
        en.observe(lsrp(1, 0x00, 3000, 0, /*mode*/1));
        CHECK(a->satisfied == 1, "answered");
        CHECK(a->observed(), "and therefore observed");
        CHECK(spy.count() == 1, "signalled once");
        CHECK(a->firstText.contains("given") && a->firstText.contains("then"),
              "evidence records both halves");
        CHECK(a->slowestMs == 2000, "the observed gap is recorded");
    }

    // A precondition whose consequent never arrives is a FINDING, and must
    // not be reported as merely untested.
    {
        TestAssertionEngine en;
        en.loadFromJson(R"([
          {"id":"STANDBY","title":"t",
           "given":"field:cab1_active=0 field:cab2_active=0",
           "then":"field:LOCO_MODE=1","within_ms":5000}
        ])");
        const TestAssertion *a = find(en, "STANDBY");
        QSignalSpy viol(&en, &TestAssertionEngine::assertionViolated);

        en.observe(dip1(false, false, 1000));       // arm
        en.observe(lsrp(1, 0x00, 3000, 0, /*mode*/6));   // wrong mode, in window
        CHECK(a->violated == 0, "still within the window");

        en.observe(lsrp(3, 0x00, 9000, 0, /*mode*/6));   // window has passed
        CHECK(a->violated == 1, "the unanswered precondition is recorded");
        CHECK(a->hasViolation(), "flagged as a violation");
        CHECK(!a->observed(), "and NOT reported as observed");
        CHECK(viol.count() == 1, "signalled during the run, not just in the report");
        CHECK(!a->violationText.isEmpty(), "the unanswered frame is kept");

        const QString html = en.buildReportHtml("T", 1000);
        CHECK(html.contains("NOT ANSWERED"),
              "the report distinguishes unanswered from untested");
        CHECK(html.contains("not answered") || html.contains("triggered but not answered"),
              "and explains what that means");
    }

    // A persisting precondition must not count as repeated violations: cab
    // inputs stay inactive across many DIP frames, and each frame is not a
    // separate failure.
    {
        TestAssertionEngine en;
        en.loadFromJson(R"([
          {"id":"P","title":"t","given":"field:cab1_active=0 field:cab2_active=0",
           "then":"field:LOCO_MODE=1","within_ms":10000}
        ])");
        const TestAssertion *a = find(en, "P");
        for (int i = 0; i < 10; ++i) en.observe(dip1(false, false, 1000 + i * 100));
        CHECK(a->violated == 0, "a persisting precondition arms once");
        en.observe(lsrp(1, 0x00, 2500, 0, 1));
        CHECK(a->satisfied == 1, "and is answered once");
        CHECK(a->slowestMs == 1500,
              "the window is measured from when the condition BEGAN");
    }

    // within_ms == 0 means "any time later in the run".
    {
        TestAssertionEngine en;
        en.loadFromJson(R"([
          {"id":"Q","title":"t","given":"field:cab1_active=0 field:cab2_active=0",
           "then":"field:LOCO_MODE=1","within_ms":0}
        ])");
        const TestAssertion *a = find(en, "Q");
        en.observe(dip1(false, false, 1000));
        en.observe(lsrp(1, 0x00, 999000, 0, 1));   // much later
        CHECK(a->violated == 0, "no window means no expiry");
        CHECK(a->satisfied == 1, "and it is still answered");
    }

    // An implication that was never triggered is untested, not passed.
    {
        TestAssertionEngine en;
        en.loadFromJson(R"([
          {"id":"R","title":"t","given":"field:cab1_active=0 field:cab2_active=0",
           "then":"field:LOCO_MODE=1","within_ms":5000}
        ])");
        const TestAssertion *a = find(en, "R");
        en.observe(lsrp(1, 0x00, 1000, 0, 1));     // consequent with no cause
        CHECK(!a->observed(), "consequent alone does not satisfy an implication");
        CHECK(a->satisfied == 0, "nothing answered");
        CHECK(!a->hasViolation(), "and nothing violated either — it is untested");
    }

    // A half with a bad query is reported against the half that failed.
    {
        TestAssertionEngine en;
        en.loadFromJson(R"([
          {"id":"S","title":"t","given":"field:cab1_active=0","then":"field:((("}
        ])");
        const TestAssertion *a = find(en, "S");
        CHECK(a && !a->queryOk, "unusable implication flagged");
        CHECK(a->queryError.startsWith("then:"), "and it names which half");
    }

    // ---- ordered sequences with a PRIOR STATE ----------------------------
    // Most SIF cases are three-part: a state the loco is already in, a
    // trigger, and a result. The trigger and result are often identical
    // across several numbered clauses — only the starting mode differs.
    {
        TestAssertionEngine en;
        CHECK(en.loadFromJson(R"([
          {"id":"FS","title":"FS to standby","sequence":[
            {"label":"in Full Supervision","match":"field:LOCO_MODE=4"},
            {"label":"no cab","match":"field:cab1_active=0 field:cab2_active=0"},
            {"label":"Stand_By","match":"field:LOCO_MODE=1","within_ms":10000}]}
        ])"), "sequence loads");
        const TestAssertion *a = find(en, "FS");
        CHECK(a->kind == AssertKind::Sequence, "recognised as a sequence");
        CHECK(a->steps.size() == 3, "three steps");
        CHECK(a->queryText.contains("Full Supervision"), "readable in the report");

        // Out of order proves nothing: cab inactive BEFORE the mode is seen.
        en.observe(dip1(false, false, 500));
        CHECK(!a->observed(), "a trigger without the prior state does nothing");
        CHECK(a->satisfied == 0, "and nothing completes");

        // Now in order.
        en.observe(lsrp(1, 0x00, 1000, 0, /*mode*/4));   // in FS
        en.observe(dip1(false, false, 2000));            // trigger
        CHECK(!a->observed(), "still incomplete before the result");
        en.observe(lsrp(3, 0x00, 3000, 0, /*mode*/1));   // Stand_By
        CHECK(a->satisfied == 1, "sequence completed in order");
        CHECK(a->observed(), "and therefore observed");
        CHECK(a->firstText.contains("Full Supervision")
              && a->firstText.contains("Stand_By"),
              "evidence records every step, not just the last");
    }

    // ---- one condition satisfying several cases --------------------------
    // The three mode clauses are separate SIF numbers, and a single run
    // should be able to satisfy whichever ones it exercises. Assertions are
    // independent: nothing consumes an entry.
    {
        TestAssertionEngine en;
        CHECK(en.loadFromJson(R"([
          {"id":"ANY-STANDBY","title":"reaches stand-by","query":"field:LOCO_MODE=1"},
          {"id":"NO-CAB","title":"cab inactive seen",
           "query":"field:cab1_active=0 field:cab2_active=0"},
          {"id":"FS","title":"FS to standby","sequence":[
            {"label":"FS","match":"field:LOCO_MODE=4"},
            {"label":"no cab","match":"field:cab1_active=0 field:cab2_active=0"},
            {"label":"Stand_By","match":"field:LOCO_MODE=1","within_ms":10000}]}
        ])"), "loaded");

        en.observe(lsrp(1, 0x00, 1000, 0, 4));
        en.observe(dip1(false, false, 2000));
        en.observe(lsrp(3, 0x00, 3000, 0, 1));

        CHECK(find(en,"FS")->observed(),          "the sequence completed");
        CHECK(find(en,"ANY-STANDBY")->observed(), "and the simple case also fired");
        CHECK(find(en,"NO-CAB")->observed(),      "and so did the other one");
        CHECK(en.observedCount() == 3,
              "one run satisfies every case it exercises, independently");
    }

    // ---- template expansion ----------------------------------------------
    // One shape, several numbered clauses. Writing them out by hand is what
    // produces the copy that gets edited in two places out of three.
    {
        TestAssertionEngine en;
        CHECK(en.loadFromJson(R"([
          {"id":"32.x/SB-{code}","title":"from {mode_name} to Stand_By",
           "sequence":[
             {"label":"in {mode_name}","match":"field:LOCO_MODE={mode}"},
             {"label":"no cab","match":"field:cab1_active=0 field:cab2_active=0"},
             {"label":"Stand_By","match":"field:LOCO_MODE=1","within_ms":10000}],
           "expand":[
             {"code":"FS","mode":"4","mode_name":"Full Supervision"},
             {"code":"LS","mode":"3","mode_name":"Limited Supervision"},
             {"code":"SR","mode":"2","mode_name":"Staff Responsible"}]}
        ])"), "template loads");
        CHECK(en.assertions().size() == 3, "one definition became three cases");
        CHECK(find(en,"32.x/SB-FS") && find(en,"32.x/SB-LS") && find(en,"32.x/SB-SR"),
              "each carries its own operation id");
        CHECK(find(en,"32.x/SB-LS")->title.contains("Limited Supervision"),
              "placeholders substituted in the title");
        CHECK(find(en,"32.x/SB-LS")->steps[0].matchText == "field:LOCO_MODE=3",
              "and inside nested sequence steps");
        CHECK(find(en,"32.x/SB-LS")->steps[0].label.contains("Limited"),
              "and in step labels");

        // Only the clause whose mode was exercised should complete.
        en.observe(lsrp(1, 0x00, 1000, 0, 3));    // Limited Supervision
        en.observe(dip1(false, false, 2000));
        en.observe(lsrp(3, 0x00, 3000, 0, 1));
        CHECK(find(en,"32.x/SB-LS")->observed(), "the LS clause completed");
        CHECK(!find(en,"32.x/SB-FS")->observed(),
              "the FS clause did NOT — it was never exercised");
        CHECK(!find(en,"32.x/SB-SR")->observed(), "nor SR");
    }

    // A sequence that stalls mid-way is a finding, not an untested case.
    {
        TestAssertionEngine en;
        en.loadFromJson(R"([
          {"id":"T","title":"t","sequence":[
            {"label":"FS","match":"field:LOCO_MODE=4"},
            {"label":"no cab","match":"field:cab1_active=0 field:cab2_active=0"},
            {"label":"Stand_By","match":"field:LOCO_MODE=1","within_ms":5000}]}
        ])");
        const TestAssertion *a = find(en,"T");
        QSignalSpy viol(&en, &TestAssertionEngine::assertionViolated);
        en.observe(lsrp(1, 0x00, 1000, 0, 4));
        en.observe(dip1(false, false, 2000));
        en.observe(lsrp(3, 0x00, 20000, 0, 6));    // long past the deadline
        CHECK(a->violated == 1, "the stalled sequence is recorded");
        CHECK(viol.count() == 1, "and signalled");
        CHECK(!a->observed(), "not reported as observed");
        const QString d = viol.at(0).at(2).toString();
        CHECK(d.contains("no cab") && d.contains("Stand_By"),
              "the message names the step reached and the step missed");
    }

    // ---- one trigger, several required consequences -----------------------
    // A foreign tag must produce the mode change AND the brake AND the
    // emergency status. They arrive in different packets, in no guaranteed
    // order, and every one is required.
    {
        TestAssertionEngine en;
        CHECK(en.loadFromJson(R"([
          {"id":"FTAG","title":"foreign tag reaction","sequence":[
            {"label":"foreign tag","match":"field:TAG_LINK_INFO!=0"},
            {"label":"reacts","within_ms":15000,"all_of":[
              {"label":"SR mode","match":"field:LOCO_MODE=2"},
              {"label":"brake","match":"field:Brake_Applied!=0"},
              {"label":"emergency","match":"field:EMERGENCY_STATUS!=0"}]}]}
        ])"), "all_of loads");
        const TestAssertion *a = find(en,"FTAG");
        CHECK(a->queryOk, "parsed");
        CHECK(a->steps.size() == 2, "two steps");
        CHECK(a->steps[1].isAllOf(), "second step is an all_of");
        CHECK(a->steps[1].parts.size() == 3, "three required parts");
        CHECK(a->queryText.contains("all of"), "readable in the report");

        en.observe(lsrp(1, 0x00, 1000, 0, 4, /*tagLink*/1));   // trigger
        // Consequences arrive separately and out of order.
        en.observe(lsrp(3, 0x00, 2000, 0, 4, 1, /*brake*/1));  // brake only
        CHECK(!a->observed(), "one consequence is not enough");
        en.observe(lsrp(5, 0x00, 3000, 0, 4, 1, 0, /*emerg*/3)); // emergency
        CHECK(!a->observed(), "two are not enough either");
        en.observe(lsrp(7, 0x00, 4000, 0, /*mode SR*/2, 1));   // mode
        CHECK(a->satisfied == 1, "complete once every consequence is seen");
        CHECK(a->observed(), "and therefore observed");
        CHECK(a->firstText.contains("SR mode")
              && a->firstText.contains("brake")
              && a->firstText.contains("emergency"),
              "evidence records each consequence separately");
    }

    // A PARTIAL reaction must fail, and must say which part was missing —
    // "the step did not complete" would be nearly useless here.
    {
        TestAssertionEngine en;
        en.loadFromJson(R"([
          {"id":"FTAG","title":"t","sequence":[
            {"label":"foreign tag","match":"field:TAG_LINK_INFO!=0"},
            {"label":"reacts","within_ms":5000,"all_of":[
              {"label":"SR mode","match":"field:LOCO_MODE=2"},
              {"label":"brake","match":"field:Brake_Applied!=0"},
              {"label":"emergency","match":"field:EMERGENCY_STATUS!=0"}]}]}
        ])");
        const TestAssertion *a = find(en,"FTAG");
        QSignalSpy viol(&en, &TestAssertionEngine::assertionViolated);

        en.observe(lsrp(1, 0x00, 1000, 0, 4, 1));              // trigger
        en.observe(lsrp(3, 0x00, 1500, 0, /*SR*/2, 1));         // mode only
        en.observe(lsrp(5, 0x00, 2000, 0, 2, 1, /*brake*/1));   // + brake
        en.observe(lsrp(7, 0x00, 20000, 0, 2, 1, 1));           // past deadline
        CHECK(a->violated == 1, "a partial reaction is a violation");
        CHECK(!a->observed(), "and not observed");
        const QString d = viol.count() ? viol.at(0).at(2).toString() : QString();
        CHECK(d.contains("MISSING"), "the detail names what was missing");
        CHECK(d.contains("emergency"), "specifically the absent consequence");
        CHECK(d.contains("SR mode") && d.contains("brake"),
              "and records what WAS satisfied");
    }

    // Declaring both match and all_of on one step is ambiguous.
    {
        TestAssertionEngine en;
        en.loadFromJson(R"([
          {"id":"B","title":"t","sequence":[
            {"label":"x","match":"field:LOCO_MODE=1","all_of":[
              {"label":"y","match":"field:LOCO_MODE=2"}]}]}
        ])");
        const TestAssertion *a = find(en,"B");
        CHECK(a && !a->queryOk, "rejected as ambiguous");
        CHECK(a->queryError.contains("not both"), "and says why");
    }

    // ---- reset and reports ------------------------------------------------
    {
        TestAssertionEngine en;
        en.loadFromJson(R"([{"id":"A","title":"t","query":"field:FRAME_NUM&7=3"}])");
        en.observe(lsrp(3, 0, 1000));
        CHECK(en.observedCount() == 1, "observed");
        en.resetObservations();
        CHECK(en.observedCount() == 0, "reset clears observations");
        CHECK(find(en,"A")->hits == 0, "hits cleared");
        CHECK(find(en,"A")->firstMs == 0, "times cleared");
        CHECK(en.assertions().size() == 1, "but the cases remain loaded");

        // The report must never claim a verdict. This is an acceptance test
        // signed by an inspector; a tool printing PASS would be asserting
        // an authority it does not have.
        const QString html = en.buildReportHtml("T", 1000);
        CHECK(html.contains("OBSERVED") || html.contains("not observed"),
              "report states observation");
        CHECK(!html.contains(">PASS<") && !html.contains(">FAIL<"),
              "and never a pass/fail verdict");
        CHECK(html.contains("not a verdict"), "the caveat is in the artefact");
        CHECK(html.contains("cannot be observed"),
              "and it says what the report does not cover");
        CHECK(html.startsWith("<!DOCTYPE html>"), "well-formed");

        const QString csv = en.buildReportCsv();
        CHECK(csv.startsWith("operation,title,observed"), "csv header");
        CHECK(csv.contains("\"A\""), "csv contains the case");
    }
}

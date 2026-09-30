#include "testutil.h"
#include "logquery.h"
#include "fieldindex.h"
#include "capturedecoder.h"
#include "schema/schemadecoder.h"

#include <QSharedPointer>

namespace {

// An LSRP capture line, as it appears in a log row's text.
LogEntry lsrpEntry(quint32 frameNum, quint8 health, quint32 speed = 0)
{
    QByteArray f(64, '\0');
    // The frame must agree with itself: the decoder places the body from
    // message_len - PKT_LENGTH, so a fixture leaving both at zero is a frame
    // no loco would send and no longer decodes.
    const int kPktLen = 29;
    f[4] = char(10 + kPktLen);   // message_len 39, LE
    f[5] = 0;
    int p = 80;
    auto put = [&](quint32 val, int n) {
        for (int i = n - 1; i >= 0; --i) {
            if ((val >> i) & 1) f[p >> 3] = f[p >> 3] | char(1 << (7 - (p & 7)));
            ++p;
        }
    };
    put(0,4); put(kPktLen,7); put(frameNum,17); put(0,20); put(0,3); put(0,23);
    put(0,9); put(0,9); put(0,2); put(0,11); put(speed,9); put(0,2); put(0,3);
    put(0,4); put(0,10); put(0,1); put(0,3); put(0,9); put(0,3); put(0,2);
    put(0,4); put(0,1); put(0,4); put(0,2);
    put(health, 6); put(0, 32);

    LogEntry e;
    e.epochMs = 1000; e.header.source_id = 21; e.header.kvchId = 1;
    e.text = QStringLiteral("@lsrp_1_1 2026-08-13T14:00:00 %1 %2")
                 .arg(frameNum).arg(QString::fromLatin1(f.toHex()));
    e.cacheDerived();
    return e;
}

bool q(const char *query, const LogEntry &e)
{
    LogQuery lq;
    if (!lq.parse(QString::fromLatin1(query))) return false;
    return lq.match(e, nullptr);
}
bool bad(const char *query) { LogQuery lq; return !lq.parse(QString::fromLatin1(query)); }

}  // namespace

// Querying SCHEMA-DECODED fields. This is what lets a SIF 0533 test case
// ("SIG_OV shall be set 1", "frame numbers ending 011") be expressed as a
// query instead of scrolled for by eye.
TEST_SUITE(fieldquery)
{
    CHECK(kavachSchema().isLoaded(), "schema loaded");
    if (!kavachSchema().isLoaded()) return;

    const LogEntry f3 = lsrpEntry(3, 0x01);          // RADIO1_LINK_FAIL
    const LogEntry f1 = lsrpEntry(1, 0x00);          // no faults
    const LogEntry f7 = lsrpEntry(7, 0x02, 55);      // SESSION_KEY_MISMATCH

    // ---- the cache actually populates ------------------------------------
    CHECK(FieldIndex::ensureDecoded(f3), "an LSRP line decodes");
    CHECK(f3.fieldsDecoded, "and is marked decoded");
    CHECK(!FieldIndex::names(f3).isEmpty(), "field names available");
    CHECK(FieldIndex::names(f3).contains("FRAME_NUM"), "FRAME_NUM present");

    // A text diagnostic decodes to nothing — a normal outcome, not an error.
    {
        LogEntry t; t.text = "---CONTROLLERS IN SYNC ---";
        t.header.source_id = 21; t.header.kvchId = 1; t.cacheDerived();
        CHECK(!FieldIndex::ensureDecoded(t), "plain text decodes to nothing");
        CHECK(FieldIndex::value(t, "SIG_OV").isNull(), "no fields on it");
        CHECK(!q("field:SIG_OV", t), "and no field query matches it");
    }

    // ---- equality and comparison -----------------------------------------
    CHECK(q("field:FRAME_NUM=3", f3),   "numeric equality");
    CHECK(!q("field:FRAME_NUM=4", f3),  "equality rejects");
    CHECK(q("field:FRAME_NUM!=4", f3),  "not-equal");
    CHECK(q("field:FRAME_NUM>1", f3),   "greater than");
    CHECK(q("field:FRAME_NUM>=3", f3),  "greater or equal");
    CHECK(q("field:FRAME_NUM<7", f3),   "less than");
    CHECK(q("field:FRAME_NUM<=3", f3),  "less or equal");
    CHECK(q("field:frame_num=3", f3),   "field names are case-insensitive");

    // ---- the mask form the 32.9.x health tests need ----------------------
    // "Simulate the test with frame numbers ending 03, 11, 19, 27..." is
    // exactly FRAME_NUM & 7 == 3.
    CHECK(q("field:FRAME_NUM&7=3", f3),  "mask selects the group");
    CHECK(!q("field:FRAME_NUM&7=1", f3), "and excludes the others");
    CHECK(q("field:FRAME_NUM&7=1", f1),  "group 1 frame");
    CHECK(q("field:FRAME_NUM&7=7", f7),  "group 7 frame");
    CHECK(q("field:FRAME_NUM&1=1", f3),  "odd-frame test");
    CHECK(!q("field:FRAME_NUM&1=0", f3), "even-frame test excludes odd");
    CHECK(bad("field:FRAME_NUM&zz=3"),   "a non-numeric mask is an error");

    // ---- substring, for enum and flag values -----------------------------
    CHECK(q("field:Loco_Health~RADIO1", f3),
          "flag name matched inside the rendered value");
    CHECK(q("field:Loco_Health~B6", f3),
          "and by its SIF B-number");
    CHECK(!q("field:Loco_Health~GPS2", f3), "a different flag does not match");
    CHECK(q("field:Loco_Health~SESSION_KEY", f7), "group 7 flag");
    // Prefix name match: the caller does not know which group the frame
    // carried, so "Loco_Health" must find "Loco_Health (faults 6-11)".
    CHECK(!FieldIndex::value(f3, "Loco_Health").isNull(),
          "prefix name match finds the multiplexed field");

    // ---- individual health bits are queryable -----------------------------
    // The row is labelled with its SIF bit number to cross-reference the
    // test document, but a query should not require remembering it.
    CHECK(q("field:RADIO1_LINK_FAIL=1", f3),
          "a set bit found by fault name alone");
    CHECK(q("field:GPS1_PPS1_FAIL=0", f3),
          "and a clear bit reports 0, so a test can assert healthy");
    CHECK(q("field:B6=1", f3), "also reachable by bit number");
    CHECK(!q("field:RADIO1_LINK_FAIL=1", f1),
          "a frame from another group does not match");

    // ---- bare existence means present AND meaningful ----------------------
    // Every LSRP frame has a SIG_OV field, so "field:SIG_OV" must mean
    // "SIG_OV is set", or it would match every frame and be useless.
    CHECK(!q("field:SIG_OV", f3), "a zero field is not a hit");
    CHECK(!q("field:Loco_Health", f1), "'(none)' flags are not a hit");
    CHECK(q("field:Loco_Health", f3), "a set flag IS a hit");
    CHECK(q("field:FRAME_NUM", f3), "a non-zero numeric field is a hit");

    // ---- composition with the rest of the language ------------------------
    CHECK(q("field:FRAME_NUM&7=3 field:Loco_Health~RADIO1", f3),
          "two field terms AND together");
    CHECK(q("src:21_1 field:FRAME_NUM&7=3", f3),
          "field terms compose with non-field terms");
    CHECK(q("field:FRAME_NUM&7=1 OR field:FRAME_NUM&7=3", f3), "OR");
    CHECK(!q("NOT field:FRAME_NUM&7=3", f3), "NOT");
    CHECK(q("field:TRAIN_SPEED>50", f7), "another numeric field");
    CHECK(!q("field:TRAIN_SPEED>50", f3), "speed 0 excluded");

    // ---- absent fields and malformed terms --------------------------------
    CHECK(!q("field:NO_SUCH_FIELD=1", f3), "unknown field never matches");
    CHECK(!q("field:NO_SUCH_FIELD", f3),   "nor its bare form");
    CHECK(bad("field:"), "field: with no name is an error");
    CHECK(bad("field:=5"), "field: with no name before the operator is an error");

    // ---- 'field' is advertised, so completion offers it -------------------
    CHECK(LogQuery::knownFields().contains("field"), "field: is a known field");
}

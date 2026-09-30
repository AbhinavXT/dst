#include "testutil.h"

#include "capturedecoder.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// =============================================================================
//  What the loco will not act on.
//
//  A frame can decode perfectly and still have most of its content thrown away
//  at the far end. None of that is visible in a field table: an LC entry with
//  id 0 and one with id 7 look equally real, and a TSR list looks the same
//  whether the loco reads it or not.
//
//  These check the reasons that can be judged FROM THE FRAME. The loco also
//  drops entries whose absolute location computes to zero or less, and that
//  depends on the reference tag's absolute location — which lives in the
//  loco's tag queue and not in anything on the wire. There is deliberately no
//  note for it: a confident note that is wrong whenever the tag table differs
//  is worse than none.
// =============================================================================

namespace {

struct Bits {
    QByteArray b;
    int n = 0;
    void put(quint32 v, int bits) {
        for (int i = bits - 1; i >= 0; --i) {
            if ((n % 8) == 0) { b.append(char(0)); }
            if ((v >> i) & 1) { b[n / 8] = b[n / 8] | char(1 << (7 - (n % 8))); }
            ++n;
        }
    }
    void padToByte() { while (n % 8) { put(0, 1); } }
};

// An SLRP with one TSR sub-packet carrying one entry.
//   header 13 B, tsr 4+7+2+5 + (8+15+15+1+6+2) = 65 bits -> 9 B, tail 8 B
QString slrpWithTsr(int tsrStatus, int refProfId)
{
    const int kTsrBytes = 9;
    const int kTotal    = 13 + kTsrBytes + 8;

    Bits h;
    h.put(2, 4);            // PKT_TYPE
    h.put(kTotal, 10);      // PKT_LENGTH
    h.put(43200, 17);       // FRAME_NUM
    h.put(33, 16);          // SOURCE_STN_ID
    h.put(1, 3);            // STN_VERSION
    h.put(1, 20);           // DEST_LOCO_ID
    h.put(refProfId, 4);    // REF_PROF_ID
    h.put(7, 10);           // LAST_REF_RFID
    h.put(100, 15);         // DIST_PKT_START
    h.put(1, 2);            // PKT_DIR
    h.padToByte();

    Bits t;
    t.put(7, 4);            // sub-packet type: TSR
    t.put(kTsrBytes, 7);
    t.put(tsrStatus, 2);
    t.put(1, 5);            // TSR_Info_CNT
    t.put(42, 8);           // id
    t.put(500, 15);         // dist
    t.put(300, 15);         // len
    t.put(0, 1);            // class: universal
    t.put(10, 6);           // speed
    t.put(1, 2);            // whistle
    t.padToByte();

    QStringList out;
    out << QStringLiteral("@slrp_33_1") << QStringLiteral("2026-09-04T12:00:00")
        << QStringLiteral("100");
    for (unsigned char ch : (h.b + t.b + QByteArray(8, '\0'))) {
        out << QStringLiteral("%1").arg(ch, 2, 16, QLatin1Char('0')).toUpper();
    }
    return out.join(QLatin1Char(' '));
}

QStringList notesIn(const CaptureLine &c)
{
    QStringList out;
    for (const auto &r : CaptureDecoder::describe(c)) {
        // Exactly these two labels. A prefix match on "loco " also catches
        // the ARP's own "loco" row, which is a field and not a note — the
        // first version of this helper did, and reported a note on a packet
        // that has none.
        const QString f = r.field.trimmed();
        if (f == QStringLiteral("loco ignores") || f == QStringLiteral("loco keeps")) {
            out << r.value.trimmed();
        }
    }
    return out;
}

bool anyContains(const QStringList &l, const QString &needle)
{
    for (const QString &s : l) { if (s.contains(needle)) { return true; } }
    return false;
}

}  // namespace

TEST_SUITE(locowillignore)
{
    // ---- TSR entries the loco does not read --------------------------------
    //
    // ProcessTSR walks the entry list only when TSR_STATUS is 2. At any other
    // value the entries are on the wire, decoded and displayed — and ignored.
    // Nothing in a field table says so.
    for (int status : { 0, 1, 3 }) {
        const CaptureLine c = CaptureDecoder::parseLine(slrpWithTsr(status, 2));
        CHECK(c.valid, "the frame parses");
        CHECK(anyContains(notesIn(c), QStringLiteral("TSR entries")),
              qPrintable(QStringLiteral("TSR_STATUS %1 is called out").arg(status)));
    }

    {
        const CaptureLine c = CaptureDecoder::parseLine(slrpWithTsr(2, 2));
        CHECK(!anyContains(notesIn(c), QStringLiteral("TSR entries")),
              "and status 2 is not — the entries are read, so there is "
              "nothing to warn about and a note on every packet would be "
              "noise rather than information");
    }

    // ---- the profile the station is not changing ---------------------------
    //
    // REF_PROF_ID 0 is not a missing value. It is the station saying it does
    // not know the route ahead, and the loco answers by KEEPING what it holds
    // rather than clearing it. A packet that changes nothing otherwise looks
    // like a packet that did nothing.
    {
        const CaptureLine c = CaptureDecoder::parseLine(slrpWithTsr(2, 0));
        CHECK(anyContains(notesIn(c), QStringLiteral("REF_PROF_ID 0")),
              "REF_PROF_ID 0 is explained");
        CHECK(anyContains(notesIn(c), QStringLiteral("current profile")),
              "as the profile being kept, not as an absent value");
    }
    {
        const CaptureLine c = CaptureDecoder::parseLine(slrpWithTsr(2, 5));
        CHECK(!anyContains(notesIn(c), QStringLiteral("REF_PROF_ID 0")),
              "a real profile id says nothing");
    }

    // ---- notes are additions, not replacements -----------------------------
    //
    // The fields must still be there. A note that quietly took the place of
    // the decode would be a bad trade.
    {
        const CaptureLine c = CaptureDecoder::parseLine(slrpWithTsr(0, 0));
        bool sawStatus = false;
        bool sawEntry  = false;
        for (const auto &r : CaptureDecoder::describe(c)) {
            if (r.field.trimmed() == QStringLiteral("TSR_STATUS")) { sawStatus = true; }
            if (r.value.contains(QStringLiteral("id=42")))         { sawEntry  = true; }
        }
        CHECK(sawStatus, "the status field is still shown");
        CHECK(sawEntry,
              "and so is the entry the loco is going to ignore — what is on "
              "the wire is what an acceptance test reports on");
        CHECK(notesIn(c).size() >= 2, "with both notes alongside it");
    }

    // ---- a type with no notes -----------------------------------------------
    {
        const CaptureLine c = CaptureDecoder::parseLine(QStringLiteral(
            "@arp_1_1 2026-06-27T14:02:26 21436 02 07 0D 00 27 00 00 00 0F 02 "
            "D3 AC 57 30 00 01 40 9F FB 47 D0 01 0E 04 1F C3 15 00 00 00 00 00 "
            "08 32 00 C9 5E DE 2F"));
        CHECK(notesIn(c).isEmpty(),
              "an ARP carries none of this, and gets no notes");
    }
}

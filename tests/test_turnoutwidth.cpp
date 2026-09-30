#include "testutil.h"

#include "capturedecoder.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// =============================================================================
//  Turnout entries are FIXED WIDTH.
//
//  The schema used to carry:
//
//      <field name="start"   bits="15" when="speed in 1..18"/>
//      <field name="release" bits="12" when="speed in 1..18"/>
//
//  The loco does not read them that way. Both branches of ProcessTurnout
//  advance the cursor by DIFF_DIST + REL_DIST — one reads the pair, the other
//  skips it — so an entry is always 5 + 15 + 12 bits whatever the speed.
//
//  Under the conditional reading, an entry with a speed outside 1..18 would be
//  27 bits short, and every entry after it in the sub-packet would be read
//  from the wrong bit. That is the failure this suite exists to catch, so the
//  frame below puts an out-of-range speed FIRST and checks that the entry
//  after it still decodes.
//
//  The captures cannot catch it: all five turnout sub-packets in replay/ carry
//  speed 6 entries only, where the two readings agree exactly.
// =============================================================================

namespace {

// msb-first bit writer, matching the wire.
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

// An SLRP carrying one turnout sub-packet with two entries.
//
//   header  13 bytes
//   turnout 4 + 7 + 2 + 2*(5+15+12) = 77 bits -> 10 bytes
//   tail     8 bytes (CRC + MAC), reserved but not checked here
QString slrpWithTurnout(int speed0, int start0, int release0,
                        int speed1, int start1, int release1)
{
    const int kTurnoutBytes = 10;
    const int kTotal        = 13 + kTurnoutBytes + 8;

    Bits h;
    h.put(2, 4);            // PKT_TYPE
    h.put(kTotal, 10);      // PKT_LENGTH — the walk runs while off < len-8
    h.put(43200, 17);       // FRAME_NUM
    h.put(33, 16);          // SOURCE_STN_ID
    h.put(1, 3);            // STN_VERSION
    h.put(1, 20);           // DEST_LOCO_ID
    h.put(2, 4);            // REF_PROF_ID
    h.put(7, 10);           // LAST_REF_RFID
    h.put(100, 15);         // DIST_PKT_START
    h.put(1, 2);            // PKT_DIR
    h.padToByte();          // 101 bits -> 13 bytes

    Bits t;
    t.put(4, 4);            // sub-packet type: turnout
    t.put(kTurnoutBytes, 7);
    t.put(2, 2);            // TO_CNT
    t.put(speed0, 5);   t.put(start0, 15);  t.put(release0, 12);
    t.put(speed1, 5);   t.put(start1, 15);  t.put(release1, 12);
    t.padToByte();

    QByteArray frame = h.b + t.b + QByteArray(8, '\0');

    QStringList out;
    out << QStringLiteral("@slrp_33_1") << QStringLiteral("2026-09-04T12:00:00")
        << QStringLiteral("100");
    for (unsigned char c : frame) {
        out << QStringLiteral("%1").arg(c, 2, 16, QLatin1Char('0')).toUpper();
    }
    return out.join(QLatin1Char(' '));
}

bool anyRowContains(const CaptureLine &c, const QString &needle)
{
    for (const auto &r : CaptureDecoder::describe(c)) {
        if (r.value.contains(needle)) { return true; }
    }
    return false;
}

}  // namespace

TEST_SUITE(turnoutwidth)
{
    // ---- the case the captures cannot produce -------------------------------
    //
    // Speed 31 first. Under the old conditional reading its start and release
    // would not be consumed, and the second entry would be read 27 bits early.
    {
        const CaptureLine c =
            CaptureDecoder::parseLine(slrpWithTurnout(31, 1234, 500,
                                                       6, 4321, 900));
        CHECK(c.valid, "the frame parses");

        CHECK(anyRowContains(c, QStringLiteral("start=1234 m")),
              "an out-of-range speed still carries a start distance");
        CHECK(anyRowContains(c, QStringLiteral("release=500 m")),
              "and a release distance");

        // The point of the suite: the entry AFTER it lands on the right bits.
        CHECK(anyRowContains(c, QStringLiteral("start=4321 m")),
              "so the next entry is read from the right bit, not 27 early");
        CHECK(anyRowContains(c, QStringLiteral("release=900 m")),
              "in both of its distance fields");
    }

    // ---- the case the captures do produce, unchanged ------------------------
    //
    // Two in-range speeds read identically under either rule, which is why
    // real traffic never showed the fault.
    {
        const CaptureLine c =
            CaptureDecoder::parseLine(slrpWithTurnout(6, 111, 222,
                                                      12, 333, 444));
        CHECK(anyRowContains(c, QStringLiteral("start=111 m")),  "first entry");
        CHECK(anyRowContains(c, QStringLiteral("release=222 m")), "first release");
        CHECK(anyRowContains(c, QStringLiteral("start=333 m")),  "second entry");
        CHECK(anyRowContains(c, QStringLiteral("release=444 m")), "second release");
    }

    // ---- speed 0, the one value the firmware actually acts on ---------------
    //
    // ProcessTurnout skips every entry whose speed is not 0, which looks
    // inverted and is theirs to answer. Whatever it means, the bits are laid
    // out the same, and the decoder shows what is on the wire.
    {
        const CaptureLine c =
            CaptureDecoder::parseLine(slrpWithTurnout(0, 777, 88,
                                                      19, 999, 66));
        CHECK(anyRowContains(c, QStringLiteral("start=777 m")),  "speed 0 entry");
        CHECK(anyRowContains(c, QStringLiteral("start=999 m")),
              "and the entry after it, at speed 19");
    }
}

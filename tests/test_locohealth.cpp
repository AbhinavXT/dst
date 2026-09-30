#include "testutil.h"
#include "capturedecoder.h"
#include "schema/schemadecoder.h"

// LSRP Loco_Health_Status: six bits carrying 24 fault types, multiplexed by
// FRAME_NUM & 7. Two things here are easy to get wrong and both decode
// cleanly while meaning something else entirely:
//   - bit ORDER (msb-first wire vs LSB-numbered SET_BIT in the firmware)
//   - even frames, where the firmware does not refresh the field at all
namespace {

QByteArray buildLsrp(quint32 frameNum, quint8 healthBits, quint32 mac = 0)
{
    QByteArray f(64, '\0');
    int p = 80;                              // body_offset
    auto put = [&](quint32 val, int n) {
        for (int i = n - 1; i >= 0; --i) {
            if ((val >> i) & 1) f[p >> 3] = f[p >> 3] | char(1 << (7 - (p & 7)));
            ++p;
        }
    };
    put(0,4); put(0,7); put(frameNum,17); put(0,20); put(0,3); put(0,23);
    put(0,9); put(0,9); put(0,2); put(0,11); put(0,9); put(0,2); put(0,3);
    put(0,4); put(0,10); put(0,1); put(0,3); put(0,9); put(0,3); put(0,2);
    put(0,4); put(0,1); put(0,4); put(0,2);
    put(healthBits, 6);
    put(mac, 32);
    return f;
}

// Only the decoded flags rows, which are named "Loco_Health (faults N-M)".
// Deliberately excludes the raw "Loco_Health_Status" field and the stale
// note row, both of which also start with "Loco_Health".
QString healthOf(const QVector<FieldRow> &rows)
{
    for (const FieldRow &r : rows)
        if (r.field.contains("faults")) return r.value;
    return QString();
}
QString rawOf(const QVector<FieldRow> &rows)
{
    for (const FieldRow &r : rows)
        if (r.field.contains("Health_Raw_Stale")) return r.value;
    return QString();
}
QString macOf(const QVector<FieldRow> &rows)
{
    for (const FieldRow &r : rows)
        if (r.field.contains("MAC_CODE")) return r.value;
    return QString();
}
}  // namespace

TEST_SUITE(locohealth)
{
    const Schema::Decoder &d = kavachSchema();
    CHECK(d.isLoaded(), "schema loaded");
    if (!d.isLoaded()) return;

    // ---- group selection by FRAME_NUM & 7 --------------------------------
    struct { quint32 frame; quint8 bits; const char *want; } cases[] = {
        { 1, 0x01, "B0  SYSTEM_INTERNAL_FAULT" },
        { 1, 0x20, "B5  RFID_READER2_LINK_FAIL" },
        { 3, 0x01, "B6  RADIO1_LINK_FAIL" },
        { 3, 0x20, "B11 GPS2_PPS2_FAIL" },
        { 5, 0x01, "B12 GPS1_VIEW_NOT_AVAILABLE" },
        { 5, 0x04, "B14 TAG_LINKING_INCORRECT" },
        { 7, 0x01, "B18 RADIO2_RSSI_WEAK" },
        { 7, 0x02, "B19 SESSION_KEY_MISMATCH" },
        { 7, 0x20, "B23 CAB_INPUT_FAULT" },
    };
    for (const auto &c : cases) {
        const QString got = healthOf(d.decode(buildLsrp(c.frame, c.bits), {}, "lsrp"));
        CHECK(got == QLatin1String(c.want),
              "bit maps to the fault the firmware set");
        if (got != QLatin1String(c.want)) {
            printf("      frame=%u bits=0x%02X wanted '%s' got '%s'\n",
                   c.frame, c.bits, c.want, qPrintable(got));
        }
    }

    // Bit ORDER: bit 0 and bit 5 must be opposite ends of the table. If the
    // table were reversed both would still decode to a real fault name —
    // this is the check that catches it.
    {
        const QString lo = healthOf(d.decode(buildLsrp(1, 0x01), {}, "lsrp"));
        const QString hi = healthOf(d.decode(buildLsrp(1, 0x20), {}, "lsrp"));
        CHECK(lo == "B0  SYSTEM_INTERNAL_FAULT",  "bit 0 is the FIRST #define");
        CHECK(hi == "B5  RFID_READER2_LINK_FAIL", "bit 5 is the SIXTH #define");
        CHECK(lo != hi, "the two ends differ");
    }

    // ---- SIF B-numbers ---------------------------------------------------
    // The displayed B-number is what an engineer uses to find the row in
    // SIF 0533 clause 32.9. A wrong number is worse than none: it sends
    // them confidently to the wrong fault.
    {
        struct { quint32 frame; quint8 bits; const char *prefix; } bn[] = {
            { 1, 0x01, "B0"  }, { 1, 0x02, "B1"  }, { 1, 0x04, "B2"  },
            { 1, 0x08, "B3"  }, { 1, 0x10, "B4"  }, { 1, 0x20, "B5"  },
            { 3, 0x01, "B6"  }, { 3, 0x20, "B11" },
            { 5, 0x01, "B12" }, { 5, 0x20, "B17" },
            { 7, 0x01, "B18" }, { 7, 0x20, "B23" },
        };
        for (const auto &b : bn) {
            const QString got = healthOf(d.decode(buildLsrp(b.frame, b.bits),
                                                  {}, "lsrp"));
            CHECK(got.startsWith(QLatin1String(b.prefix)),
                  "bit carries its SIF B-number");
            if (!got.startsWith(QLatin1String(b.prefix)))
                printf("      frame=%u bits=0x%02X wanted %s got '%s'\n",
                       b.frame, b.bits, b.prefix, qPrintable(got));
        }
    }

    // ---- multiple and zero ----------------------------------------------
    {
        const QString both = healthOf(d.decode(buildLsrp(1, 0x21), {}, "lsrp"));
        CHECK(both.contains("SYSTEM_INTERNAL_FAULT")
              && both.contains("RFID_READER2_LINK_FAIL"),
              "two set bits report both faults");
        CHECK(healthOf(d.decode(buildLsrp(1, 0x00), {}, "lsrp")) == "(none)",
              "no set bits means no faults — 0 is healthy, per !(health)");
        const QString all = healthOf(d.decode(buildLsrp(7, 0x3F), {}, "lsrp"));
        for (const char *n : { "RADIO2_RSSI_WEAK", "SESSION_KEY_MISMATCH",
                               "REMAINING_KEYS_LESS_THAN_5",
                               "BIU_CONNECTIVITY_FAULT", "SPEED_SENSOR2_FAULT",
                               "CAB_INPUT_FAULT" }) {
            CHECK(all.contains(QLatin1String(n)), "all six faults reported");
        }
    }

    // ---- even frames are stale and must NOT be interpreted ---------------
    // The firmware's switch has no case for them, so the field still holds
    // the previous group's bits. Decoding it would report plausible faults
    // under the wrong names.
    for (quint32 fr : { 0u, 2u, 4u, 6u, 8u }) {
        const QVector<FieldRow> rows = d.decode(buildLsrp(fr, 0x15), {}, "lsrp");
        CHECK(healthOf(rows).isEmpty(),
              "no fault names decoded on an even frame");
        CHECK(!rawOf(rows).isEmpty(), "raw value still shown");
        bool noted = false;
        for (const FieldRow &r : rows)
            if (r.value.contains("stale")) noted = true;
        CHECK(noted, "and it is explicitly marked stale");
    }

    // ---- every bit states its own condition -------------------------------
    // A summary listing only faults cannot distinguish "healthy" from
    // "not decoded", and a SIF case asks you to confirm a SPECIFIC bit is
    // 0 or 1. Absence from a list is not evidence of either.
    {
        // Frame 60679 & 7 == 7 with B18 and B22 set — the frame from a real
        // capture, so this doubles as a regression against hardware output.
        const QVector<FieldRow> rows = d.decode(buildLsrp(60679, 0x11), {}, "lsrp");

        int stated = 0, faults = 0, healthy = 0;
        QStringList seen;
        for (const FieldRow &r : rows) {
            const QString f = r.field.trimmed();
            if (!f.startsWith(QLatin1String("B1")) && !f.startsWith(QLatin1String("B2")))
                continue;
            ++stated;
            seen << f;
            if (r.value.contains("FAULT")) ++faults;
            if (r.value.contains("ok"))    ++healthy;
        }
        CHECK(stated == 6, "all six bits of the group are stated, not just faults");
        CHECK(faults == 2,  "the two set bits report FAULT");
        CHECK(healthy == 4, "the four clear bits report ok explicitly");

        for (const char *n : { "B18 RADIO2_RSSI_WEAK", "B19 SESSION_KEY_MISMATCH",
                               "B20 REMAINING_KEYS_LESS_THAN_5",
                               "B21 BIU_CONNECTIVITY_FAULT",
                               "B22 SPEED_SENSOR2_FAULT", "B23 CAB_INPUT_FAULT" }) {
            CHECK(seen.contains(QLatin1String(n)), "every bit named");
        }

        // Values must be numerically comparable as well as readable, so
        // field:SPEED_SENSOR2_FAULT=1 works in a query.
        for (const FieldRow &r : rows) {
            if (r.field.contains("B22")) {
                CHECK(r.value.startsWith(QLatin1Char('1')),
                      "a set bit is numerically 1");
            }
            if (r.field.contains("B19")) {
                CHECK(r.value.startsWith(QLatin1Char('0')),
                      "a clear bit is numerically 0");
            }
        }

        // The summary row survives alongside the expansion.
        CHECK(healthOf(rows).contains("RADIO2_RSSI_WEAK")
              && healthOf(rows).contains("SPEED_SENSOR2_FAULT"),
              "the fault summary is still produced");
    }

    // Even frames must NOT expand: there is nothing valid to state.
    {
        const QVector<FieldRow> rows = d.decode(buildLsrp(60680, 0x11), {}, "lsrp");
        int stated = 0;
        for (const FieldRow &r : rows) {
            const QString f = r.field.trimmed();
            if (f.startsWith(QLatin1String("B1")) || f.startsWith(QLatin1String("B2")))
                ++stated;
        }
        CHECK(stated == 0, "a stale frame states no bit conditions at all");
    }

    // ---- alignment: exactly one branch consumes the six bits ------------
    // If none fired, or two did, MAC_CODE would read from the wrong offset.
    for (quint32 fr = 1; fr <= 8; ++fr) {
        const QString mac = macOf(d.decode(buildLsrp(fr, 0x2A, 0xDEADBEEF),
                                           {}, "lsrp"));
        CHECK(mac.contains("deadbeef", Qt::CaseInsensitive),
              "MAC_CODE stays aligned whichever branch fired");
    }
}

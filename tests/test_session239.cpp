#include "testutil.h"

#include "frameclock.h"
#include "packetbuilder.h"
#include "packetmakerdialog.h"

#include <QDateTime>
#include <QPlainTextEdit>
#include <QPushButton>

// =============================================================================
//  Session 239 — Packet Maker says what the receiver would do with a built
//  frame: FRAME_NUM aged against the loco's live counter (or this laptop's
//  clock), the MAC placeholder, and every rejectrules.xml finding. The frame
//  is the bufferload suite's real SLRP reference frame 6 (from replay/).
// =============================================================================

namespace {

const char kSlrpRef6[] =
    "91 B9 AA E6 04 1E 80 00 08 40 6F 8E 68 01 80 00 00 00 00 01 7C AA 08 20 "
    "00 11 63 0D 0C 58 10 B1 00 F4 84 00 22 05 09 BA 20 14 00 00 34 C6 08 5A "
    "40 F4 93 00 54 E8 60 00 00 87 BC 04 99 E0 1A 3E 80 80 F1 02 84 38 0C 11 "
    "30 38 22 41 08 7A 04 83 23 8A 29 0E 08 41 38 40 95 0A 60 61 64 E0 00 03 "
    "43 38 68 60 42 C0 3E A4 18 46 67 50 A7 6E";

using RL = QVector<PacketMakerDialog::ReceiverLine>;

FrameNumberWatch::Seen seen(qint64 value, qint64 atMs)
{
    FrameNumberWatch::Seen s; s.value = value; s.atMs = atMs; s.captype = QStringLiteral("arp");
    return s;
}

bool anyNot(const RL &l) { for (const auto &x : l) if (x.notProcessed) return true; return false; }

} // namespace

TEST_SUITE(session239)
{
    const qint64 now = 1700000000000;
    const QHash<QString, qint64> fn1000{ { QStringLiteral("FRAME_NUM"), 1000 } };

    // ---- the window, against the live counter -----------------------------------------------
    RL l = PacketMakerDialog::receiverCheck(QStringLiteral("lsrp"), fn1000, {}, false, seen(1004, now), now, 0);
    CHECK(l.size() == 1 && !l[0].notProcessed && l[0].text.contains(QLatin1String("age 4 s")),
          "4 s old: still processed, at the edge (" + (l.isEmpty() ? QByteArray() : l[0].text.toUtf8()) + ")");
    l = PacketMakerDialog::receiverCheck(QStringLiteral("lsrp"), fn1000, {}, false, seen(1003, now - 2500), now, 0);
    CHECK(l.size() == 1 && l[0].notProcessed && l[0].text.contains(QLatin1String("5 s old")),
          "seen 2.5 s ago at 1003: 5 s old, would not be processed");
    l = PacketMakerDialog::receiverCheck(QStringLiteral("lsrp"), fn1000, {}, false, seen(998, now), now, 0);
    CHECK(l.size() == 1 && l[0].notProcessed && l[0].text.contains(QLatin1String("2 s early")),
          "2 s early: would not be processed");
    l = PacketMakerDialog::receiverCheck(QStringLiteral("lsrp"), fn1000, {}, false, seen(999, now), now, 0);
    CHECK(l.size() == 1 && !l[0].notProcessed, "1 s early: processed");

    // ---- no recent live number: this laptop's clock -------------------------------------------
    l = PacketMakerDialog::receiverCheck(QStringLiteral("lsrp"), fn1000, {}, false, seen(5000, now - 60000), now, 999);
    CHECK(l.size() == 1 && !l[0].notProcessed && l[0].text.contains(QLatin1String("laptop's clock")),
          "a minute-old live number is not used; the clock is (FRAME_NUM = secs + 1)");
    l = PacketMakerDialog::receiverCheck(QStringLiteral("lsrp"), fn1000, {}, false, FrameNumberWatch::Seen(), now, 1010);
    CHECK(l.size() == 1 && l[0].notProcessed, "11 s behind the clock: would not be processed");
    // 23:59:59 built (FRAME_NUM 86400), checked at 00:00:01 (would use 2): 2 s old, not a day
    const QHash<QString, qint64> lateFn{ { QStringLiteral("FRAME_NUM"), FrameClock::frameNumFor(86399) } };
    l = PacketMakerDialog::receiverCheck(QStringLiteral("lsrp"), lateFn, {}, false, FrameNumberWatch::Seen(), now, 1);
    CHECK(l.size() == 1 && !l[0].notProcessed && l[0].text.contains(QLatin1String("age 2 s")),
          "across midnight the age is 2 s");

    // ---- MAC and rule findings --------------------------------------------------------------
    l = PacketMakerDialog::receiverCheck(QStringLiteral("lsrp"), {}, {}, true, FrameNumberWatch::Seen(), now, 0);
    CHECK(l.size() == 1 && l[0].notProcessed && l[0].text.contains(QLatin1String("MAC")),
          "a zero MAC is named");
    CHECK(PacketMakerDialog::receiverCheck(QStringLiteral("lsrp"), {}, {}, false, FrameNumberWatch::Seen(), now, 0).isEmpty(),
          "nothing to check, nothing said");

    // ---- the real SLRP ----------------------------------------------------------------------
    PacketMakerDialog d;
    d.loadBuffer(QString::fromLatin1(kSlrpRef6));
    const PacketBuilder::Result r = d.buildSendForTest(-1);
    int mas = 0;
    for (const Schema::SubEntry &se : r.subs) mas += se.type == 0 ? 1 : 0;
    CHECK(r.ok && r.raw.contains(QStringLiteral("FRAME_NUM")) && mas > 0,
          "a build carries its header numbers and its sub-packets read back");
    const qint64 fn = r.raw.value(QStringLiteral("FRAME_NUM"));
    l = PacketMakerDialog::receiverCheck(QStringLiteral("slrp"), r.raw, r.subs, false, seen(fn, now), now, 0);
    CHECK(!anyNot(l), "reference frame 6, fresh: no reject condition matched (TRN_LEN_INFO_STS 0 is not one)");
    // a TSR sub-packet with the reserved status, as a second sub-packet
    QVector<Schema::SubEntry> subs = r.subs;
    Schema::SubEntry tsr; tsr.type = 7; tsr.values.insert(QStringLiteral("TSR_STATUS"), 3);
    subs << tsr;
    l = PacketMakerDialog::receiverCheck(QStringLiteral("slrp"), r.raw, subs, false, seen(fn, now), now, 0);
    int hits = 0;
    for (const auto &x : l)
        hits += x.notProcessed && x.text.contains(QLatin1String("TSR_STATUS = 3"))
                && x.text.contains(QLatin1String("31.55.5"))
                && x.text.contains(QStringLiteral("sub-packet %1").arg(subs.size())) ? 1 : 0;
    CHECK(hits == 1, "TSR_STATUS 3 in the last sub-packet: would not be processed, clause 31.55.5, named once");

    // ---- the window shows it ----------------------------------------------------------------
    QPushButton *build = nullptr;
    for (QPushButton *b : d.findChildren<QPushButton *>())
        if (b->text() == QLatin1String("Build && Verify")) build = b;
    CHECK(build, "the Build button");
    if (build) build->click();
    auto *out = d.findChild<QPlainTextEdit *>(QStringLiteral("pmOutput"));
    CHECK(out && out->toPlainText().contains(QLatin1String("receiver would (at build time):"))
              && out->toPlainText().contains(QLatin1String("FRAME_NUM")),
          "Output lists what the receiver would do");
}

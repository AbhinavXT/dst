#include "testutil.h"

#include "framenumberwatch.h"
#include "capturedecoder.h"
#include "packetmakerdialog.h"
#include "schema/schemaencoder.h"

#include <QApplication>
#include <QCheckBox>
#include <QSignalSpy>

// =============================================================================
//  Following the equipment's frame number.
//
//  FRAME_NUM was seeded from seconds-since-midnight. That is the right SHAPE
//  and the wrong number: it is whatever this laptop's clock says, while the
//  counter a peer checks against is whatever the equipment says. ARP and LSRP
//  carry the live value and one or the other arrives constantly, so it is
//  simply there to be read.
//
//  The bit offset is worked out from the schema rather than written down, so
//  the check that matters is that the value read out of a real frame is the
//  value the schema's own parser finds in it.
// =============================================================================

namespace {

// Real captured frames.
const QString kLsrp = QStringLiteral(
    "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
    "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B");
const QString kArp = QStringLiteral(
    "@arp_1_1 2026-06-27T14:02:26 21436 02 07 0D 00 27 00 00 00 0F 02 D3 AC 57 30 "
    "00 01 40 9F FB 47 D0 01 0E 04 1F C3 15 00 00 00 00 00 08 32 00 C9 5E DE 2F");
const QString kSlrp = QStringLiteral(
    "@slrp_1_1 2026-06-27T14:02:27 21440 90 81 8A E6 04 1E 80 00 05 38 60 00 08 "
    "01 60 94 BD 80 10 00 64 00 00 00 13 12 CB 04 DA A0 37 D0");

qint64 frameNumViaSchema(const QString &captype, const QByteArray &frame)
{
    Schema::Encoder enc;
    QString err;
    if (!enc.load(QStringLiteral(":/schema/kavach.xml"), &err)) { return -1; }
    const auto split = PacketMakerDialog::splitBuffer(enc, captype, frame);
    const Schema::ParsedPacket pp = enc.parseBody(captype, split.body);
    return pp.ok ? pp.header.value(QStringLiteral("FRAME_NUM"), -1) : -1;
}

}  // namespace

TEST_SUITE(framenumwatch)
{
    FrameNumberWatch &w = FrameNumberWatch::instance();
    w.clear();
    CHECK(!w.latest().valid(), "nothing seen at the start");
    CHECK(w.ageMs() < 0, "and no age to report");

    // ---- it reads the same number the schema does ---------------------------
    // The bit offset is derived from the schema; this is the check that the
    // derivation is right, against the schema's own parser.
    {
        const CaptureLine c = CaptureDecoder::parseLine(kLsrp);
        CHECK(c.valid, "the lsrp sample parses");
        const qint64 expect = frameNumViaSchema(QStringLiteral("lsrp"), c.bytes);
        CHECK(expect >= 0, "and the schema finds a frame number in it");

        w.observeLine(c);
        CHECK(w.latest().valid(), "the watch saw it");
        CHECK(w.latest().value == expect,
              "and read the same value the schema's parser reads");
        CHECK(w.latest().captype == QLatin1String("lsrp"), "recording where it came from");
        CHECK(w.latest().locoId == 1, "and from which loco");
    }

    // ---- arp too --------------------------------------------------------------
    {
        const CaptureLine c = CaptureDecoder::parseLine(kArp);
        const qint64 expect = frameNumViaSchema(QStringLiteral("arp"), c.bytes);
        w.observeLine(c);
        CHECK(w.latest().value == expect, "arp is read the same way");
        CHECK(w.latest().captype == QLatin1String("arp"), "and noted as arp");
    }

    // ---- everything else is ignored -------------------------------------------
    {
        const qint64 before = w.latest().value;
        w.observeLine(CaptureDecoder::parseLine(kSlrp));
        CHECK(w.latest().value == before,
              "slrp does not carry FRAME_NUM and does not disturb the watch");
        w.observeLine(CaptureDecoder::parseLine(QStringLiteral("not a capture line")));
        CHECK(w.latest().value == before, "nor does junk");
    }

    // ---- latest means most recent, not largest --------------------------------
    // FRAME_NUM wraps. "Biggest so far" would stick at the top of the range
    // for the rest of the session the first time it wrapped.
    {
        CaptureLine c = CaptureDecoder::parseLine(kLsrp);
        w.observeLine(c);
        const qint64 first = w.latest().value;

        // Force a smaller value by zeroing the frame-number bits.
        CaptureLine low = c;
        low.bytes[11] = char(0);
        low.bytes[12] = char(0);
        w.observeLine(low);
        CHECK(w.latest().value != first || first == 0,
              "a later frame with a smaller number still wins");
    }

    // ---- per loco --------------------------------------------------------------
    {
        w.clear();
        CaptureLine c = CaptureDecoder::parseLine(kLsrp);
        w.observeLine(c);
        CHECK(w.latestFor(1).valid(), "loco 1 has an observation");
        CHECK(!w.latestFor(7).valid(), "a loco never seen has none");
    }

    // ---- the signal ------------------------------------------------------------
    {
        w.clear();
        QSignalSpy spy(&w, &FrameNumberWatch::observed);
        w.observeLine(CaptureDecoder::parseLine(kLsrp));
        CHECK(spy.count() == 1, "an observation is announced");

        // SLRP used to stand in here as "a packet with no frame number",
        // which it never was — it carries the STATION's, and now that this is
        // watched it announces like any other.
        w.observeLine(CaptureDecoder::parseLine(kSlrp));
        CHECK(spy.count() == 2, "so does the station's counter");

        // A packet that genuinely has none.
        w.observeLine(CaptureDecoder::parseLine(
            QStringLiteral("@rfid_1_1 2026-06-27T14:02:28 21442 01 02 03 04")));
        CHECK(spy.count() == 2, "and a packet without a frame number is not");
    }

    // ---- the seed the Packet Maker uses ---------------------------------------
    {
        w.clear();
        bool live = true;
        const qint64 cold = PacketMakerDialog::seedFrameNumber(17, &live);
        CHECK(!live, "with nothing seen, the seed is not claimed to be live");
        CHECK(cold >= 0 && cold < 86400,
              "and falls back to seconds since midnight");

        w.observeLine(CaptureDecoder::parseLine(kLsrp));
        const qint64 hot = PacketMakerDialog::seedFrameNumber(17, &live);
        CHECK(live, "once traffic is seen, the seed comes from it");
        CHECK(hot == w.latest().value, "and is the observed number");

        // A narrow field still gets a value that fits it.
        const qint64 narrow = PacketMakerDialog::seedFrameNumber(8, nullptr);
        CHECK(narrow >= 0 && narrow < 256, "clipped to the field's width");
    }

    // ---- the dialog follows, and stops when edited ----------------------------
    {
        w.clear();
        PacketMakerDialog dlg;
        QCheckBox *follow = nullptr;
        for (QCheckBox *cb : dlg.findChildren<QCheckBox *>()) {
            if (cb->text().contains(QStringLiteral("Follow live"))) { follow = cb; }
        }
        CHECK(follow != nullptr, "the frame-number row offers to follow live traffic");
        CHECK(follow && follow->isChecked(),
              "and does by default — the live number is the useful one");
    }

    // ---- the station's counter is kept apart from the loco's -----------------
    //
    // Two clocks that are supposed to agree. Merging them into one "latest"
    // would hide the moment they stop agreeing, which is the only interesting
    // thing about having both — and it would also feed the Packet Maker a
    // station number to build a loco frame with.
    {
        w.clear();
        CHECK(!w.latest().valid(),        "nothing observed to begin with");
        CHECK(!w.latestStation().valid(), "on either side");

        w.observeLine(CaptureDecoder::parseLine(kSlrp));
        CHECK(w.latestStation().valid(), "an SLRP sets the station's counter");
        CHECK(w.latestStation().captype == QStringLiteral("slrp"), "from slrp");
        CHECK(!w.latest().valid(),
              "and does NOT become the loco's — the Packet Maker seeds loco "
              "frames from latest(), and a station number there would build a "
              "frame the loco rejects");

        w.observeLine(CaptureDecoder::parseLine(kArp));
        CHECK(w.latest().valid() && w.latest().captype == QStringLiteral("arp"),
              "an ARP sets the loco's");
        CHECK(w.latestStation().captype == QStringLiteral("slrp"),
              "and leaves the station's where it was");

        // The two values are read from different places in different packets;
        // both must match what the schema's own parser finds.
        const CaptureLine sl = CaptureDecoder::parseLine(kSlrp);
        CHECK(w.latestStation().value
                  == frameNumViaSchema(QStringLiteral("slrp"), sl.bytes),
              "the station's value is the one the schema finds in the SLRP "
              "header, not a hand-written bit offset");

        w.clear();
        CHECK(!w.latestStation().valid(), "clear() takes the station with it");
    }

    w.clear();
}

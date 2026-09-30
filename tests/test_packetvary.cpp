// =============================================================================
//  Per-send variation, presets, and the interval-send rebuild.
//
//  The defect being fixed was not visible in a datagram count: interval send
//  delivered every packet it promised, on time, and the message-header
//  sequence advanced. What did not advance was the packet body, so FRAME_NUM
//  stayed put and the MAC and CRC computed over that body were byte-identical
//  on every send. So the checks here are about the bytes DIFFERING, which is
//  the property that was silently absent.
// =============================================================================

#include "testutil.h"

#include "framenumberwatch.h"
#include "packetbuilder.h"
#include "packetpreset.h"
#include "packetvariation.h"
#include "udpsender.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QUdpSocket>

using PacketVary::Rule;

// Session 95: the live frame-number watch is no longer process-wide; the
// Packet Maker is handed one, as MainWindow does.
static FrameNumberWatch g_watch95;

TEST_SUITE(packetvary)
{
    // ---- Increment -------------------------------------------------------

    {
        Rule r;
        r.field = QStringLiteral("FRAME_NUM");
        r.mode  = Rule::Increment;
        r.start = 0;
        r.step  = 1;
        r.max   = 0;              // wrap at the field width

        CHECK(r.valueFor(0, 4) == 0, "increment starts at start");
        CHECK(r.valueFor(1, 4) == 1, "increment advances by step");
        CHECK(r.valueFor(5, 4) == 5, "increment is a function of the index");

        // A 4-bit field holds 0..15, so send 16 must be back at 0 rather than
        // 16 masked down to 0 by accident — the sequence has to stay
        // contiguous or a receiver watching for gaps sees phantom ones.
        CHECK(r.valueFor(15, 4) == 15, "increment reaches the top of the field");
        CHECK(r.valueFor(16, 4) == 0,  "increment wraps at the field width");
        CHECK(r.valueFor(17, 4) == 1,  "increment continues cleanly after wrapping");

        // Nothing accumulates between sends: send N must be computable
        // without replaying 0..N-1, so a run can be checked against a capture
        // after the fact.
        CHECK(r.valueFor(1000, 17) == 1000 % 131072,
              "a far-off index is computable directly");
    }

    {
        Rule r;
        r.field = QStringLiteral("X");
        r.mode  = Rule::Increment;
        r.start = 100;
        r.step  = 5;
        r.max   = 120;            // an explicit wrap point beats the width

        CHECK(r.valueFor(0, 16) == 100, "explicit start is honoured");
        CHECK(r.valueFor(1, 16) == 105, "explicit step is honoured");
        CHECK(r.valueFor(4, 16) == 120, "the explicit max is reachable");
        CHECK(r.valueFor(21, 16) == 100, "wrapping uses the explicit max, not the width");

        Rule z = r;
        z.step = 0;
        CHECK(z.valueFor(3, 16) >= 100, "a zero step does not divide by zero");
    }

    // ---- Sweep and Random ------------------------------------------------

    {
        Rule r;
        r.field = QStringLiteral("SPEED");
        r.mode  = Rule::Sweep;
        r.start = 10;
        r.step  = 10;
        r.max   = 40;

        CHECK(r.valueFor(0, 8) == 10, "sweep begins at the low bound");
        CHECK(r.valueFor(3, 8) == 40, "sweep reaches the high bound");
        CHECK(r.valueFor(4, 8) == 10, "sweep returns to the low bound");

        Rule degenerate = r;
        degenerate.max = 10;      // no span
        CHECK(degenerate.valueFor(7, 8) == 10, "a zero-span sweep holds its value");
    }

    {
        Rule r;
        r.field = QStringLiteral("NOISE");
        r.mode  = Rule::Random;
        r.start = 5;
        r.max   = 9;

        bool inRange = true;
        QSet<qint64> seen;
        for (int i = 0; i < 200; ++i) {
            const qint64 v = r.valueFor(i, 8);
            if (v < 5 || v > 9) { inRange = false; }
            seen.insert(v);
        }
        CHECK(inRange, "random stays inside its bounds");
        // Random is the one mode that is deliberately not a function of the
        // index, so over 200 draws from five values it must actually vary.
        CHECK(seen.size() > 1, "random actually varies");
    }

    // ---- clamping --------------------------------------------------------

    {
        Rule r;
        r.field = QStringLiteral("SMALL");
        r.mode  = Rule::Increment;
        r.start = 0;
        r.step  = 1;
        r.max   = 1000;           // deliberately past a 3-bit field

        bool clamped = true;
        for (int i = 0; i < 50; ++i) {
            const qint64 v = r.valueFor(i, 3);
            if (v < 0 || v > 7) { clamped = false; }
        }
        CHECK(clamped, "values never exceed the field's declared width");
    }
}


TEST_SUITE(packetpreset)
{
    PacketBuilder builder;
    CHECK(builder.ready(), "schema loaded for preset tests");
    if (!builder.ready()) { return; }

    const Schema::Encoder &enc = builder.encoder();
    const Schema::PacketInfo pi = enc.packet(QStringLiteral("slrp"));
    CHECK(pi.ok, "slrp is defined");
    if (!pi.ok) { return; }

    // A minimal but real slrp: header fields at zero except a frame number.
    PacketPreset p;
    p.captype    = QStringLiteral("slrp");
    p.dest       = QStringLiteral("127.0.0.1");
    p.port       = 50002;
    p.intervalMs = 250;
    p.msgSrc     = 2;
    p.msgDest    = 101;
    p.msgSeq     = 7;
    for (const Schema::FieldInfo &f : pi.header) {
        if (f.isPad) { continue; }
        if (f.name == QLatin1String("PKT_LENGTH") || f.name == QLatin1String("MAC_CODE")) { continue; }
        p.header.insert(f.name, 0);
    }
    CHECK(p.header.contains(QStringLiteral("FRAME_NUM")), "slrp carries FRAME_NUM");

    Rule fr;
    fr.field = QStringLiteral("FRAME_NUM");
    fr.mode  = Rule::Increment;
    fr.step  = 1;
    p.vary.push_back(fr);

    // ---- JSON round trip -------------------------------------------------

    {
        QString err;
        PacketPreset back;
        CHECK(back.fromJson(p.toJson(), &err), "a preset round-trips through JSON");
        CHECK(back.captype == p.captype,       "captype survives");
        CHECK(back.port == p.port,             "port survives");
        CHECK(back.intervalMs == p.intervalMs, "interval survives");
        CHECK(back.msgSeq == p.msgSeq,         "message seq survives");
        CHECK(back.header.size() == p.header.size(), "every header field survives");
        CHECK(back.vary.size() == 1,           "the vary rule survives");
        if (back.vary.size() == 1) {
            CHECK(back.vary[0].field == QStringLiteral("FRAME_NUM"),
                  "the rule's field survives");
            CHECK(back.vary[0].mode == Rule::Increment, "the rule's mode survives");
        }

        QJsonObject notAPreset;
        notAPreset["format"] = QStringLiteral("something.else");
        PacketPreset rejected;
        QString rerr;
        CHECK(!rejected.fromJson(notAPreset, &rerr), "a foreign file is refused");
        CHECK(!rerr.isEmpty(), "the refusal says why");
    }

    // ---- save and load ---------------------------------------------------

    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid(), "temp dir for preset files");
        if (tmp.isValid()) {
            const QString path = tmp.filePath("t.packet.json");
            QString err;
            CHECK(p.save(path, &err), "a preset writes to disk");
            PacketPreset loaded;
            CHECK(PacketPreset::load(path, &loaded, &err), "and reads back");
            CHECK(loaded.header == p.header, "the header survives the file");

            PacketPreset missing;
            CHECK(!PacketPreset::load(tmp.filePath("nope.json"), &missing, &err),
                  "a missing preset is an error, not an empty packet");
        }
    }

    // ---- headerFor applies variation -------------------------------------

    {
        const auto h0 = p.headerFor(0, enc);
        const auto h1 = p.headerFor(1, enc);
        const auto hBase = p.headerFor(-1, enc);

        CHECK(h0.value("FRAME_NUM") == 0, "send 0 uses the rule's start");
        CHECK(h1.value("FRAME_NUM") == 1, "send 1 has advanced");
        CHECK(hBase.value("FRAME_NUM") == p.header.value("FRAME_NUM"),
              "index -1 leaves the base values alone");

        // Only the varied field moves; everything else must be untouched, or
        // a rule would be quietly rewriting the packet around it.
        bool othersStable = true;
        for (auto it = h0.constBegin(); it != h0.constEnd(); ++it) {
            if (it.key() == QLatin1String("FRAME_NUM")) { continue; }
            if (h1.value(it.key()) != it.value()) { othersStable = false; }
        }
        CHECK(othersStable, "variation touches only the fields it names");
    }

    // ---- the actual fix: successive sends differ in their BYTES -----------

    {
        const PacketBuilder::Result r0 = p.build(builder, 0, QByteArray());
        const PacketBuilder::Result r1 = p.build(builder, 1, QByteArray());
        const PacketBuilder::Result r2 = p.build(builder, 2, QByteArray());

        CHECK(r0.ok, "send 0 builds and self-verifies");
        CHECK(r1.ok, "send 1 builds and self-verifies");
        if (r0.ok && r1.ok && r2.ok) {
            CHECK(r0.frame != r1.frame,
                  "consecutive sends produce DIFFERENT bytes — the whole point");
            CHECK(r1.frame != r2.frame, "and keep differing");
            CHECK(r0.frame.size() == r1.frame.size(),
                  "varying a fixed-width field does not change the frame length");

            // The CRC lives in the last four bytes. If only FRAME_NUM moved
            // and the CRC did not, the frame would be internally inconsistent
            // and a receiver would drop it — so this is a real check, not a
            // restatement of the one above.
            CHECK(r0.frame.right(4) != r1.frame.right(4),
                  "the trailing CRC is recomputed for each send");
        }
    }

    // ---- with no rules, sends are identical ------------------------------
    //
    // The old behaviour has to remain reachable: some tests genuinely want
    // the same bytes twice, and it should be a choice rather than a bug.

    {
        PacketPreset fixed = p;
        fixed.vary.clear();
        const PacketBuilder::Result a = fixed.build(builder, 0, QByteArray());
        const PacketBuilder::Result b = fixed.build(builder, 9, QByteArray());
        CHECK(a.ok && b.ok, "a preset with no rules still builds");
        if (a.ok && b.ok) {
            CHECK(a.frame == b.frame, "with no rules every send is identical");
        }
    }
}


TEST_SUITE(intervalsend)
{
    // UdpSender must ASK for each frame rather than repeating one buffer,
    // and must stop rather than fall back to stale bytes when a build fails.

    QUdpSocket rx;
    CHECK(rx.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)), "bound a receiver");

    // ---- the index advances and the bytes follow it ----------------------

    {
        UdpSender s;
        QVector<int> indices;
        s.startInterval(
            [&indices](int i) -> QByteArray {
                indices.push_back(i);
                return QByteArray("frame-") + QByteArray::number(i);
            },
            QStringLiteral("127.0.0.1"), rx.localPort(), 10);

        QVector<QByteArray> got;
        QElapsedTimer w;
        w.start();
        while (w.elapsed() < 400 && got.size() < 5) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            while (rx.hasPendingDatagrams()) {
                QByteArray b(int(rx.pendingDatagramSize()), '\0');
                const qint64 n = rx.readDatagram(b.data(), b.size());
                if (n <= 0) { break; }
                b.truncate(int(n));
                got.push_back(b);
            }
            QThread::msleep(2);
        }
        s.stop();

        CHECK(got.size() >= 3, "interval mode sends repeatedly");
        if (got.size() >= 3) {
            CHECK(got[0] == QByteArray("frame-0"), "the first send is index 0");
            CHECK(got[1] == QByteArray("frame-1"), "the index advances");
            CHECK(got[0] != got[1],
                  "consecutive datagrams differ — the frame is rebuilt, not repeated");
        }
        CHECK(indices.size() >= 3, "the frame source was called per send");
        if (indices.size() >= 2) {
            CHECK(indices[1] == indices[0] + 1, "indices are consecutive");
        }
    }

    // ---- a failing build stops the run -----------------------------------

    {
        UdpSender s;
        int calls = 0;
        QString errText;
        bool runningAfter = true;
        QObject::connect(&s, &UdpSender::error, &s,
                         [&errText](const QString &e) { errText = e; });

        s.startInterval(
            [&calls](int) -> QByteArray {
                ++calls;
                // Two good sends, then refuse.
                return calls <= 2 ? QByteArray("ok") : QByteArray();
            },
            QStringLiteral("127.0.0.1"), rx.localPort(), 10);

        QElapsedTimer w;
        w.start();
        while (w.elapsed() < 300) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            if (!s.isRunning()) { runningAfter = false; break; }
            QThread::msleep(2);
        }

        CHECK(!runningAfter, "a failed build stops the run");
        CHECK(!errText.isEmpty(), "and says so");
        CHECK(calls == 3, "it stops on the first refusal rather than retrying");
    }

    // ---- the fixed-buffer mode is still available ------------------------

    {
        UdpSender s;
        s.startIntervalFixed(QByteArray("same"), QStringLiteral("127.0.0.1"),
                             rx.localPort(), 10);

        QVector<QByteArray> got;
        QElapsedTimer w;
        w.start();
        while (w.elapsed() < 200 && got.size() < 3) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            while (rx.hasPendingDatagrams()) {
                QByteArray b(int(rx.pendingDatagramSize()), '\0');
                const qint64 n = rx.readDatagram(b.data(), b.size());
                if (n <= 0) { break; }
                b.truncate(int(n));
                got.push_back(b);
            }
            QThread::msleep(2);
        }
        s.stop();

        CHECK(got.size() >= 2, "the fixed mode sends repeatedly");
        if (got.size() >= 2) {
            CHECK(got[0] == got[1],
                  "repeating identical bytes is still possible when asked for");
        }
    }
}

// =============================================================================
//  FRAME_NUM seeded from the clock.
//
//  A form full of zeros produces a frame that is obviously synthetic, and for
//  LSRP a zero frame number also pins the health group to the
//  FRAME_NUM & 7 == 0 branch on every build — so the one branch you happen to
//  test is the only one you ever send. Seeding with seconds since midnight is
//  what the equipment's own frames carry.
//
//  Seeded, not driven: the value stays editable and nothing rewrites it.
// =============================================================================

#include "packetmakerdialog.h"
#include <QTime>

TEST_SUITE(framenumseed)
{
    // ---- the value itself ---------------------------------------------------
    {
        const qint64 expect = QTime::currentTime().msecsSinceStartOfDay() / 1000;
        const qint64 got    = PacketMakerDialog::secondsSinceMidnight(17);
        // A second may tick between the two reads.
        CHECK(qAbs(got - expect) <= 1, "seconds since local midnight");
        CHECK(got >= 0 && got < 86400, "and it is a time of day");
    }

    // ---- it fits the field FRAME_NUM actually has --------------------------
    {
        // 17 bits holds 131,071; a day holds 86,399. The whole point of
        // passing the width in is that this is true for THIS field and not
        // in general.
        CHECK(PacketMakerDialog::secondsSinceMidnight(17) < (1 << 17),
              "a day of seconds fits in FRAME_NUM's 17 bits");
    }

    // ---- a narrower field wraps, and says so --------------------------------
    {
        for (int bits : { 4, 8, 12 }) {
            const qint64 v = PacketMakerDialog::secondsSinceMidnight(bits);
            CHECK(v >= 0 && v < (qint64(1) << bits),
                  "a narrower field wraps into range rather than overflowing");
        }
    }

    // ---- degenerate widths --------------------------------------------------
    {
        const qint64 raw = QTime::currentTime().msecsSinceStartOfDay() / 1000;
        CHECK(qAbs(PacketMakerDialog::secondsSinceMidnight(0) - raw) <= 1,
              "a zero width is not a modulo by zero");
        CHECK(qAbs(PacketMakerDialog::secondsSinceMidnight(64) - raw) <= 1,
              "and a width past 63 bits does not shift off the end of the type");
    }
}

// The dialog itself: the seed reaches the form, and anything the operator
// loads afterwards still wins over it.
#include "capturedecoder.h"
#include "schema/schemaencoder.h"
#include <QApplication>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>

TEST_SUITE(framenumform)
{
    PacketMakerDialog dlg(nullptr, &g_watch95);

    // The Now button is the visible half of the feature: without it the seed
    // goes stale while the dialog sits open and nothing says so.
    QPushButton *now = nullptr;
    for (QPushButton *b : dlg.findChildren<QPushButton *>()) {
        if (b->text() == QLatin1String("Now")) { now = b; break; }
    }
    CHECK(now != nullptr, "the frame-number row carries a Now button");

    // A real captured lsrp frame. Loading it must overwrite the seeded
    // frame number with the captured one — a buffer the operator loaded is
    // a statement about what they want to send.
    const QString line = QStringLiteral(
        "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
        "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B");
    const QByteArray frame = CaptureDecoder::parseLine(line).bytes;

    Schema::Encoder enc;
    QString err;
    CHECK(enc.load(QStringLiteral(":/schema/kavach.xml"), &err), "schema loads");
    const auto split = PacketMakerDialog::splitBuffer(enc, QStringLiteral("lsrp"), frame);
    const Schema::ParsedPacket pp = enc.parseBody(QStringLiteral("lsrp"), split.body);
    CHECK(pp.ok, "the sample parses");
    const qint64 captured = pp.header.value(QStringLiteral("FRAME_NUM"), -1);
    CHECK(captured >= 0, "and carries a frame number");

    dlg.loadBuffer(line);
    QCoreApplication::processEvents();

    bool foundCaptured = false;
    for (QLineEdit *le : dlg.findChildren<QLineEdit *>()) {
        if (le->text().toLongLong() == captured) { foundCaptured = true; break; }
    }
    CHECK(foundCaptured,
          "after filling from a buffer the form holds the captured frame "
          "number, not the clock");
}

// =============================================================================
//  Room to work in.
//
//  Reported from use: the header form is too cramped to edit comfortably. Two
//  causes, both structural rather than cosmetic.
//
//  The sub-packet pane took a third of the width for every packet type, but
//  only SLRP and AEP have sub-packets — for LSRP, ARP, DMI and the rest it was
//  an empty list next to a squeezed form. And "Fill from buffer" held about
//  140 px permanently for a paste box used once at the start of a session.
// =============================================================================

#include <QComboBox>
#include <QGroupBox>
#include <QPlainTextEdit>

TEST_SUITE(packetmakerlayout)
{
    PacketMakerDialog dlg(nullptr, &g_watch95);
    dlg.resize(1080, 860);
    dlg.show();
    QCoreApplication::processEvents();

    QComboBox *packets = nullptr;
    for (QComboBox *c : dlg.findChildren<QComboBox *>()) {
        if (c->findText(QStringLiteral("slrp")) >= 0) { packets = c; break; }
    }
    CHECK(packets != nullptr, "the packet chooser is there");
    if (!packets) { return; }

    QGroupBox *subBox = nullptr, *bufBox = nullptr;
    for (QGroupBox *g : dlg.findChildren<QGroupBox *>()) {
        if (g->title().contains(QStringLiteral("Sub-packets")))    { subBox = g; }
        if (g->title().contains(QStringLiteral("Fill from buffer"))) { bufBox = g; }
    }
    CHECK(subBox != nullptr, "the sub-packet pane exists");
    CHECK(bufBox != nullptr, "and the fill-from-buffer box");

    // ---- the paste box is folded away until asked for -----------------------
    if (bufBox) {
        CHECK(bufBox->isCheckable(), "the buffer box folds");
        CHECK(!bufBox->isChecked(), "and starts folded");
        auto *edit = bufBox->findChild<QPlainTextEdit *>();
        CHECK(edit && !edit->isVisible(),
              "with its paste area actually hidden — a checkable group box "
              "only disables its children, which would not buy the space back");
        bufBox->setChecked(true);
        QCoreApplication::processEvents();
        CHECK(edit && edit->isVisible(), "and it comes back when ticked");
        bufBox->setChecked(false);
    }

    // ---- the sub-packet pane appears only where it applies ------------------
    if (subBox) {
        packets->setCurrentIndex(packets->findText(QStringLiteral("slrp")));
        QCoreApplication::processEvents();
        CHECK(subBox->isVisible(), "slrp has sub-packets, so the pane is shown");

        packets->setCurrentIndex(packets->findText(QStringLiteral("lsrp")));
        QCoreApplication::processEvents();
        CHECK(!subBox->isVisible(),
              "lsrp has none, so the header form gets the whole width instead "
              "of sharing it with an empty list");

        packets->setCurrentIndex(packets->findText(QStringLiteral("slrp")));
        QCoreApplication::processEvents();
        CHECK(subBox->isVisible(), "and it comes back for a packet that has them");
    }

    // ---- the window can be maximised ----------------------------------------
    CHECK(dlg.windowFlags() & Qt::WindowMaximizeButtonHint,
          "the Packet Maker has a maximise button");
}

// =============================================================================
//  The increment section and the live frame number.
//
//  Reported from use: the frame number in the vary table ignored the live
//  ARP/LSRP value. It did — the seeded rule was "increment from 0", a counter
//  of OUR own, which drifts away from the equipment's the moment it starts.
//  The number a peer checks a frame against is the one in the traffic arriving
//  now, not one this program invented.
//
//  Live is the second exception to "a rule is a pure function of the send
//  index", alongside Random, and for a better reason: the value wanted is not
//  one this program chooses at all.
// =============================================================================

#include "framenumberwatch.h"
#include "capturedecoder.h"

TEST_SUITE(varylive)
{
    // ---- the mode itself -----------------------------------------------------
    {
        PacketVary::Rule r;
        r.field = QStringLiteral("FRAME_NUM");
        r.mode  = PacketVary::Rule::Live;
        r.start = 7;

        PacketVary::LiveFrame live;
        live.value = 4242;

        CHECK(r.valueFor(0, 17, live) == 4242, "the live value is used as given");
        CHECK(r.valueFor(9, 17, live) == 4242,
              "and does not move with the run index — every send carries what "
              "the equipment is using now");

        // Nothing observed: fall back to a number the operator chose rather
        // than sending zeros and calling it live.
        CHECK(r.valueFor(0, 17, PacketVary::LiveFrame()) == 7,
              "with nothing seen it falls back to start");

        // ---- advancing between observations --------------------------------
        // If the loco sends at 1 Hz and this runs at 200 ms, four consecutive
        // frames carry the same observed number. Whether that is acceptable
        // is a question about the specification, so it is a choice: step 0
        // repeats, step 1 advances within the observation.
        PacketVary::Rule adv = r;
        adv.step = 1;
        live.sendsSinceChange = 0;
        CHECK(adv.valueFor(0, 17, live) == 4242, "the first send is the observed number");
        live.sendsSinceChange = 3;
        CHECK(adv.valueFor(3, 17, live) == 4245, "the fourth is three past it");

        // And it RE-ANCHORS: a counter of our own would now be at 4245 and
        // keep climbing; this starts again from whatever the equipment says.
        live.value = 5000;
        live.sendsSinceChange = 0;
        CHECK(adv.valueFor(4, 17, live) == 5000,
              "when the equipment moves on, the run follows rather than drifting");
    }

    // ---- it still respects the field width -----------------------------------
    {
        PacketVary::Rule r;
        r.mode = PacketVary::Rule::Live;
        PacketVary::LiveFrame wide; wide.value = 0x1FF;
        const qint64 v = r.valueFor(0, 8, wide);
        CHECK(v >= 0 && v < 256, "a live value too wide for the field is clamped");
    }

    // ---- round trip through the preset format --------------------------------
    {
        PacketVary::Rule r;
        r.field = QStringLiteral("FRAME_NUM");
        r.mode  = PacketVary::Rule::Live;
        const QVector<PacketVary::Rule> back =
            PacketVary::fromJson(PacketVary::toJson({ r }));
        CHECK(back.size() == 1, "a live rule survives a saved preset");
        CHECK(back.first().mode == PacketVary::Rule::Live, "as a live rule");
        CHECK(PacketVary::modeName(PacketVary::Rule::Live) == QLatin1String("live"),
              "under a name that will still read as live next year");
    }

    // ---- and it describes itself honestly ------------------------------------
    {
        PacketVary::Rule r;
        r.field = QStringLiteral("FRAME_NUM");
        r.mode  = PacketVary::Rule::Live;
        r.step  = 0;
        CHECK(r.describe(17).contains(QStringLiteral("live")),
              "the table says where the number comes from");
        CHECK(!r.describe(17).contains(QStringLiteral("+")),
              "and, with no step, does not claim to be incrementing anything");

        r.step = 1;
        CHECK(r.describe(17).contains(QStringLiteral("+1")),
              "with a step it says it advances");
        CHECK(r.describe(17).contains(QStringLiteral("until it moves")),
              "and that the advance is only until the equipment moves on — the "
              "distinction between following and drifting");
    }

    // ---- the seeded rule ------------------------------------------------------
    // The default rule for a packet with FRAME_NUM is the one an operator gets
    // without touching anything, so it is the one that has to be right.
    {
        g_watch95.clear();
        g_watch95.observeLine(CaptureDecoder::parseLine(
            QStringLiteral("@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 "
                           "0F 02 A3 AC 57 40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 "
                           "20 8D 00 F2 F3 26 DD C6 ED 59 9B")));
        const qint64 seen = g_watch95.latest().value;
        CHECK(seen >= 0, "a frame number was observed");

        PacketMakerDialog dlg(nullptr, &g_watch95);
        QCoreApplication::processEvents();

        const QVector<PacketVary::Rule> rules = dlg.varyRules();
        bool found = false;
        for (const PacketVary::Rule &r : rules) {
            if (r.field != QLatin1String("FRAME_NUM")) { continue; }
            found = true;
            CHECK(r.mode == PacketVary::Rule::Live,
                  "the seeded frame-number rule follows live traffic");
            PacketVary::LiveFrame lf; lf.value = seen; lf.sendsSinceChange = 3;
            CHECK(r.valueFor(3, 17, lf) == seen,
                  "so send 4 carries the observed number, not 3");
            CHECK(r.step == 0,
                  "and repeats it by default — whether every frame must advance "
                  "is a question about SIF 0533, not one to guess at");
        }
        CHECK(found, "a frame-number rule is seeded for a packet that has one");
        g_watch95.clear();
    }
}

// =============================================================================
//  Sending a frame that is still current.
//
//  Reported from the field: the loco's frame number moves on while the dialog
//  sits there, and a frame more than about four seconds behind is rejected.
//  Two causes, both fixed here:
//
//    * Send Once transmitted whatever Build had left in m_lastFrame, however
//      long ago that was.
//    * A confirmation dialog sat between the click and the datagram — dead
//      time in which the number went stale, guarding a decision the operator
//      had already made by pressing Send.
// =============================================================================

TEST_SUITE(framefreshness)
{
    g_watch95.clear();

    // A real LSRP, so the observed number is a real one.
    const QString lsrp = QStringLiteral(
        "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
        "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B");
    g_watch95.observeLine(CaptureDecoder::parseLine(lsrp));
    const qint64 first = g_watch95.latest().value;
    CHECK(first >= 0, "a frame number was observed");

    PacketMakerDialog dlg(nullptr, &g_watch95);
    QCoreApplication::processEvents();

    // The header the dialog would build with must be the observed number,
    // not whatever the editor happened to hold when it was created.
    const qint64 inHeader = dlg.headerForTest().value(QStringLiteral("FRAME_NUM"), -1);
    CHECK(inHeader == first,
          "the header takes the frame number from the watch while following");

    // Now the equipment moves on, as it does every second.
    CaptureLine moved = CaptureDecoder::parseLine(lsrp);
    moved.bytes[12] = char(quint8(moved.bytes.at(12)) ^ 0x0F);
    g_watch95.observeLine(moved);
    const qint64 second = g_watch95.latest().value;
    CHECK(second != first, "the observed number changed");

    QCoreApplication::processEvents();
    const qint64 afterMove = dlg.headerForTest().value(QStringLiteral("FRAME_NUM"), -1);
    CHECK(afterMove == second,
          "and the next build follows it — this is the difference between a "
          "frame the loco accepts and one it rejects as stale");

    g_watch95.clear();
}

// =============================================================================
//  Arm / disarm.
//
//  The confirmation dialogs went because a modal between the click and the
//  datagram is dead time in which the frame number goes stale. That left Send
//  as a single unguarded click, so the guard moved somewhere it costs no time:
//  arming is a separate, earlier act, and after it the send is immediate.
//
//  Arming refers to the frame in front of the operator, so anything that
//  REPLACES that frame disarms. Rebuilding for a new frame number does not —
//  that is the same frame with the number it should have had, and disarming
//  on it would make the switch useless exactly when it matters.
// =============================================================================

#include <QPushButton>

TEST_SUITE(armdisarm)
{
    PacketMakerDialog dlg(nullptr, &g_watch95);
    QCoreApplication::processEvents();

    QCheckBox *arm = nullptr;
    for (QCheckBox *cb : dlg.findChildren<QCheckBox *>()) {
        if (cb->text().contains(QStringLiteral("Arm"), Qt::CaseInsensitive)
            || cb->text().contains(QStringLiteral("ARMED"))) { arm = cb; break; }
    }
    CHECK(arm != nullptr, "there is an arm switch");
    CHECK(arm && !arm->isChecked(), "and it starts off");

    QPushButton *sendOnce = nullptr, *interval = nullptr;
    for (QPushButton *b : dlg.findChildren<QPushButton *>()) {
        if (b->text().contains(QStringLiteral("Send Once")))     { sendOnce = b; }
        if (b->text().contains(QStringLiteral("Start Interval"))) { interval = b; }
    }
    CHECK(sendOnce && interval, "both send buttons are there");

    // ---- disarmed means disabled, whatever the build says --------------------
    {
        CHECK(sendOnce && !sendOnce->isEnabled(), "Send Once is dead while disarmed");
        CHECK(interval && !interval->isEnabled(), "and so is Start Interval");

        arm->setChecked(true);
        QCoreApplication::processEvents();
        CHECK(sendOnce && !sendOnce->isEnabled(),
              "arming alone does not enable sending — there is still nothing built");
        arm->setChecked(false);
    }

    // ---- the label says which state it is in --------------------------------
    // A tick box labelled the same either way is a switch you have to look
    // twice at, on a control that transmits.
    {
        const QString off = arm->text();
        arm->setChecked(true);
        QCoreApplication::processEvents();
        const QString on = arm->text();
        CHECK(off != on, "the label changes with the state");
        CHECK(on.contains(QStringLiteral("ARMED")),
              "and says so plainly when it is live");
        arm->setChecked(false);
    }

    // ---- replacing the frame disarms ----------------------------------------
    {
        arm->setChecked(true);
        QCoreApplication::processEvents();
        CHECK(arm->isChecked(), "armed");

        // A real captured frame: loading it replaces every field.
        dlg.loadBuffer(QStringLiteral(
            "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
            "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B"));
        QCoreApplication::processEvents();
        CHECK(!arm->isChecked(),
              "filling from a buffer disarms — the frame that was armed is gone");
    }

    // ---- a new frame number does NOT disarm ---------------------------------
    {
        arm->setChecked(true);
        QCoreApplication::processEvents();

        g_watch95.clear();
        g_watch95.observeLine(CaptureDecoder::parseLine(
            QStringLiteral("@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 "
                           "0F 02 A3 AC 57 40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 "
                           "20 8D 00 F2 F3 26 DD C6 ED 59 9B")));
        QCoreApplication::processEvents();

        CHECK(arm->isChecked(),
              "the frame number moving on leaves it armed — otherwise the switch "
              "would clear itself once a second and guard nothing");
        arm->setChecked(false);
        g_watch95.clear();
    }
}

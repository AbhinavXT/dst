// =============================================================================
//  DlrPlayer — timed replay of .dlr archives over UDP.
//
//  The claim under test is not "packets come out" but "packets come out with
//  the timing and ordering they went in with", so every check here is about
//  fidelity: byte-identity, merge order across archives, honoured gaps,
//  honoured speed multiplier, gap compression, and seeking.
//
//  Timing assertions run against a real loopback socket and a real clock, so
//  the tolerances are deliberately loose enough not to fail on a loaded CI
//  box while still being tight enough to catch a pacing bug. A player that
//  ignored timing entirely would finish a 600 ms archive in under 10 ms and
//  fail every one of them.
// =============================================================================

#include "testutil.h"

#include "dlrplayer.h"
#include "capturedecoder.h"
#include "colorrules.h"
#include "messagedispatcher.h"
#include "sessionfile.h"
#include "Structures.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <QVector>

namespace {

// A datagram the receiver would accept: dest 101, message_len consistent with
// the payload. Matching the real archives matters — a replay that could not
// pass UDPCommunication::drainSocket()'s checks would be testing nothing.
QByteArray makeWire(quint8 sourceId, quint16 kvchId, quint8 msgId,
                    const QByteArray &payload)
{
    QByteArray wire(int(sizeof(STRUCT_MESSAGE_HEADER)), '\0');
    char *p = wire.data();
    p[0] = char(sourceId);
    p[1] = char(101);                 // kThisConsoleId
    p[2] = char(msgId);
    SessionFile::putU16(p + 3, quint16(payload.size()));
    SessionFile::putU16(p + 5, kvchId);
    wire.append(payload);
    return wire;
}

// Write a .dlr containing exactly the given (arrivalMs, wire) records.
bool writeDlr(const QString &path, quint8 sourceId, quint16 kvchId,
              const QVector<QPair<qint64, QByteArray>> &recs)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }

    SessionFile::FileHeader h;
    h.createdMs = recs.isEmpty() ? 0 : recs.first().first;
    h.sourceId  = sourceId;
    h.kvchId    = kvchId;
    f.write(SessionFile::encodeHeader(h));

    for (const auto &r : recs) {
        f.write(SessionFile::encodeRecord(r.first, r.second));
    }
    f.close();
    return true;
}

// What one run of the player produced: the datagrams in arrival order, with
// the millisecond each was observed at, measured from the first.
struct Capture {
    QVector<QByteArray> wires;
    QVector<qint64>     atMs;
    qint64              totalMs = 0;
};

// Run the player to completion against a bound loopback socket and collect
// everything that arrives. Pumps the event loop rather than sleeping so
// datagrams are drained as they land and the timestamps mean something.
Capture runPlayer(DlrPlayer &player, const QVector<DlrPlayer::Source> &srcs,
                  double speed, int maxGapMs = 0, qint64 startOffsetMs = 0,
                  int budgetMs = 15000)
{
    Capture cap;

    QUdpSocket rx;
    if (!rx.bind(QHostAddress(QHostAddress::LocalHost), quint16(0))) { return cap; }

    player.setSources(srcs);
    player.setTarget(QHostAddress::LocalHost, rx.localPort());
    player.setSpeed(speed);
    player.setMaxGapMs(maxGapMs);
    player.setStartOffsetMs(startOffsetMs);

    QElapsedTimer clock;
    bool done = false;
    QObject::connect(&player, &DlrPlayer::finished,
                     &player, [&done](bool) { done = true; });

    clock.start();
    player.play();

    QElapsedTimer budget;
    budget.start();
    // Keep draining for a short grace period after finished(), because the
    // last datagrams are still in flight when the sender thread exits.
    qint64 doneAt = -1;
    for (;;) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);

        while (rx.hasPendingDatagrams()) {
            QByteArray buf(int(rx.pendingDatagramSize()), '\0');
            const qint64 n = rx.readDatagram(buf.data(), buf.size());
            if (n <= 0) { break; }
            buf.truncate(int(n));
            cap.wires.push_back(buf);
            cap.atMs.push_back(clock.elapsed());
        }

        if (done && doneAt < 0) { doneAt = budget.elapsed(); }
        if (doneAt >= 0 && budget.elapsed() - doneAt > 150) { break; }
        if (budget.elapsed() > budgetMs) { break; }

        QThread::msleep(1);
    }

    player.wait(2000);
    cap.totalMs = clock.elapsed();
    return cap;
}

}  // namespace


TEST_SUITE(dlrplayer)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir for archives");
    if (!tmp.isValid()) { return; }

    // ---- probe() ---------------------------------------------------------
    //
    // The dialog needs the timeline before it can offer to play anything, so
    // probe has to agree with what playback will actually produce.

    const qint64 t0 = 1'750'000'000'000LL;   // arbitrary fixed epoch ms

    QVector<QPair<qint64, QByteArray>> a;
    a.push_back({ t0 +   0, makeWire(21, 2, 151, QByteArray("alpha")) });
    a.push_back({ t0 + 100, makeWire(21, 2, 151, QByteArray("bravo")) });
    a.push_back({ t0 + 100, makeWire(21, 2, 151, QByteArray("charlie")) });  // same ms
    a.push_back({ t0 + 400, makeWire(21, 2, 151, QByteArray("delta")) });

    const QString pathA = tmp.filePath("a.dlr");
    CHECK(writeDlr(pathA, 21, 2, a), "wrote archive A");

    DlrPlayer::Source srcA;
    QString err;
    CHECK(DlrPlayer::probe(pathA, &srcA, &err), "probe A succeeds");
    CHECK(srcA.records == 4,               "probe A counts 4 records");
    CHECK(srcA.sourceId == 21,             "probe A reads sourceId");
    CHECK(srcA.kvchId == 2,                "probe A reads kvchId");
    CHECK(srcA.firstMs == t0,              "probe A first arrival");
    CHECK(srcA.lastMs == t0 + 400,         "probe A last arrival");
    CHECK(!srcA.truncated,                 "probe A is not truncated");

    DlrPlayer::Source bogus;
    CHECK(!DlrPlayer::probe(tmp.filePath("nope.dlr"), &bogus, &err),
          "probe rejects a missing file");

    // ---- byte fidelity and ordering at high speed ------------------------

    {
        DlrPlayer p;
        const Capture cap = runPlayer(p, { srcA }, 25.0);

        CHECK(cap.wires.size() == 4, "all four datagrams arrive");
        if (cap.wires.size() == 4) {
            bool identical = true;
            for (int i = 0; i < 4; ++i) {
                if (cap.wires[i] != a[i].second) { identical = false; }
            }
            CHECK(identical, "every datagram is byte-identical to the archive");

            // The header has to survive verbatim or the receiver would route
            // it to the wrong tab — or drop it on the dest-id filter.
            CHECK(quint8(cap.wires[0][1]) == 101,
                  "destination stays 101, so the receiver would accept it");
            CHECK(SessionFile::getU16(cap.wires[0].constData() + 5) == 2,
                  "kvchId survives, so the tab key is unchanged");
            CHECK(SessionFile::getU16(cap.wires[0].constData() + 3)
                      == quint16(cap.wires[0].size() - int(sizeof(STRUCT_MESSAGE_HEADER))),
                  "message_len still matches the payload");
        }
    }

    // ---- real-time pacing ------------------------------------------------
    //
    // 400 ms of archive at 1x should take about 400 ms. The upper bound is
    // generous; the lower bound is the one that matters, because ignoring
    // timing altogether is the failure mode this whole feature exists to
    // avoid.

    {
        DlrPlayer p;
        const Capture cap = runPlayer(p, { srcA }, 1.0);
        CHECK(cap.wires.size() == 4, "1x delivers all records");
        if (cap.atMs.size() == 4) {
            const qint64 span = cap.atMs[3] - cap.atMs[0];
            CHECK(span >= 340, "1x takes at least roughly the archive's span");
            CHECK(span <= 700, "1x is not wildly slower than the archive");

            // Records sharing a millisecond must stay together rather than
            // being spread out by a per-record sleep.
            const qint64 sameMsGap = cap.atMs[2] - cap.atMs[1];
            CHECK(sameMsGap <= 40, "records sharing a timestamp arrive together");

            // And the 300 ms gap has to actually be a gap.
            const qint64 bigGap = cap.atMs[3] - cap.atMs[2];
            CHECK(bigGap >= 240, "the 300 ms gap is reproduced");
        }
    }

    // ---- speed multiplier ------------------------------------------------

    {
        DlrPlayer p;
        const Capture cap = runPlayer(p, { srcA }, 4.0);
        CHECK(cap.wires.size() == 4, "4x delivers all records");
        if (cap.atMs.size() == 4) {
            const qint64 span = cap.atMs[3] - cap.atMs[0];
            CHECK(span >= 60,  "4x is not instantaneous");
            CHECK(span <= 260, "4x is roughly four times faster than 1x");
        }
    }

    // ---- burst mode ------------------------------------------------------
    //
    // Speed 0 means no pacing at all. This is the mode that exists to push
    // the receiver into its backpressure path, so it must genuinely ignore
    // the timeline rather than merely running fast.

    {
        DlrPlayer p;
        const Capture cap = runPlayer(p, { srcA }, 0.0);
        CHECK(cap.wires.size() == 4, "burst delivers all records");
        if (cap.atMs.size() == 4) {
            CHECK(cap.atMs[3] - cap.atMs[0] < 100,
                  "burst ignores the archive's 400 ms span");
        }
    }

    // ---- gap compression -------------------------------------------------

    {
        QVector<QPair<qint64, QByteArray>> g;
        g.push_back({ t0,          makeWire(21, 2, 151, QByteArray("before")) });
        g.push_back({ t0 + 5000,   makeWire(21, 2, 151, QByteArray("after")) });
        const QString pathG = tmp.filePath("gap.dlr");
        CHECK(writeDlr(pathG, 21, 2, g), "wrote archive with a 5 s gap");

        DlrPlayer::Source srcG;
        CHECK(DlrPlayer::probe(pathG, &srcG, &err), "probe gap archive");

        DlrPlayer p;
        const Capture cap = runPlayer(p, { srcG }, 1.0, /*maxGapMs=*/200);
        CHECK(cap.wires.size() == 2, "both records arrive with gaps capped");
        if (cap.atMs.size() == 2) {
            const qint64 gap = cap.atMs[1] - cap.atMs[0];
            CHECK(gap >= 150,  "the cap is applied, not skipped entirely");
            CHECK(gap <= 1000, "a 5 s idle gap is clamped, not replayed in full");
        }
    }

    // ---- merging two archives -------------------------------------------
    //
    // A session is one .dlr per tab, and the two archives from one recording
    // interleave throughout. Playing them back to back instead of merged
    // would destroy exactly the cross-source ordering that makes the merged
    // view worth having.

    {
        QVector<QPair<qint64, QByteArray>> b;
        b.push_back({ t0 +  50, makeWire(81, 2, 160, QByteArray("B1")) });
        b.push_back({ t0 + 150, makeWire(81, 2, 160, QByteArray("B2")) });
        b.push_back({ t0 + 450, makeWire(81, 2, 160, QByteArray("B3")) });
        const QString pathB = tmp.filePath("b.dlr");
        CHECK(writeDlr(pathB, 81, 2, b), "wrote archive B");

        DlrPlayer::Source srcB;
        CHECK(DlrPlayer::probe(pathB, &srcB, &err), "probe B succeeds");
        CHECK(srcB.sourceId == 81, "probe B reads its own sourceId");

        DlrPlayer p;
        const Capture cap = runPlayer(p, { srcA, srcB }, 25.0);
        CHECK(cap.wires.size() == 7, "all records from both archives arrive");

        if (cap.wires.size() == 7) {
            // Expected merge on arrivalMs: A0(0) B1(50) A1(100) A2(100)
            // B2(150) A3(400) B3(450).
            const QVector<QByteArray> want = {
                QByteArray("alpha"), QByteArray("B1"),
                QByteArray("bravo"), QByteArray("charlie"),
                QByteArray("B2"), QByteArray("delta"), QByteArray("B3"),
            };
            bool ordered = true;
            for (int i = 0; i < 7; ++i) {
                const QByteArray payload =
                    cap.wires[i].mid(int(sizeof(STRUCT_MESSAGE_HEADER)));
                if (payload != want[i]) { ordered = false; }
            }
            CHECK(ordered, "the two archives are merged in arrival order");

            // Sources must stay distinguishable, or the receiver would put
            // everything in one tab.
            bool sawA = false, sawB = false;
            for (const QByteArray &w : cap.wires) {
                if (quint8(w[0]) == 21) { sawA = true; }
                if (quint8(w[0]) == 81) { sawB = true; }
            }
            CHECK(sawA && sawB, "both source ids appear in the merged stream");
        }
    }

    // ---- seeking ---------------------------------------------------------

    {
        DlrPlayer p;
        // Start 200 ms in: alpha(0), bravo(100) and charlie(100) are before
        // that point, so only delta(400) should be sent.
        const Capture cap = runPlayer(p, { srcA }, 25.0, 0, /*startOffsetMs=*/200);
        CHECK(cap.wires.size() == 1, "seeking skips records before the offset");
        if (cap.wires.size() == 1) {
            CHECK(cap.wires[0].mid(int(sizeof(STRUCT_MESSAGE_HEADER)))
                      == QByteArray("delta"),
                  "the record after the seek point is the one sent");
        }
    }

    // ---- stop ------------------------------------------------------------
    //
    // Stop has to be prompt even when the next record is far away, otherwise
    // a long idle gap makes the dialog appear hung.

    {
        QVector<QPair<qint64, QByteArray>> s;
        s.push_back({ t0,          makeWire(21, 2, 151, QByteArray("first")) });
        s.push_back({ t0 + 30000,  makeWire(21, 2, 151, QByteArray("much later")) });
        const QString pathS = tmp.filePath("stop.dlr");
        CHECK(writeDlr(pathS, 21, 2, s), "wrote archive with a 30 s gap");

        DlrPlayer::Source srcS;
        CHECK(DlrPlayer::probe(pathS, &srcS, &err), "probe stop archive");

        QUdpSocket rx;
        CHECK(rx.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)), "bound receiver for stop test");

        DlrPlayer p;
        p.setSources({ srcS });
        p.setTarget(QHostAddress::LocalHost, rx.localPort());
        p.setSpeed(1.0);
        p.play();

        // Let the first record go out, then stop while the thread is parked
        // waiting on the 30 s gap.
        QElapsedTimer w;
        w.start();
        while (w.elapsed() < 200) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(5);
        }

        QElapsedTimer stopClock;
        stopClock.start();
        p.stop();
        const bool joined = p.wait(2000);
        const qint64 stopMs = stopClock.elapsed();

        CHECK(joined, "the player thread joins after stop()");
        CHECK(stopMs < 500, "stop() returns promptly even inside a 30 s gap");

        const DlrPlayer::Stats st = p.stats();
        CHECK(st.sent == 1, "only the record before the gap was sent");
    }

    // ---- truncated archive ----------------------------------------------
    //
    // A torn tail is what an archive from a killed run looks like. Everything
    // before the tear is still good and must still play, or a crash would
    // cost you the recording you most want.

    {
        const QString pathT = tmp.filePath("torn.dlr");
        CHECK(writeDlr(pathT, 21, 2, a), "wrote archive to truncate");

        QFile f(pathT);
        CHECK(f.open(QIODevice::ReadWrite), "opened archive for truncation");
        // Lop off part of the final record.
        f.resize(f.size() - 12);
        f.close();

        DlrPlayer::Source srcT;
        CHECK(DlrPlayer::probe(pathT, &srcT, &err), "probe accepts a torn archive");
        CHECK(srcT.truncated, "probe flags the torn tail");
        CHECK(srcT.records == 3, "the three intact records are still counted");

        DlrPlayer p;
        const Capture cap = runPlayer(p, { srcT }, 25.0);
        CHECK(cap.wires.size() == 3, "the intact prefix still plays");
    }
}


// =============================================================================
//  Seeking by content.
//
//  The claim is "stop the archive at the record I describe, then play on from
//  there at true speed". Three things have to hold for that to be true, and
//  each is checked separately: nothing before the match goes out, everything
//  after it does (matching or not), and the pacing re-anchors so the landed
//  record plays immediately instead of waiting out the time it skipped.
//
//  That last one is the part that silently breaks. If the anchor is not reset,
//  the virtual clock has leapt forward while the wall clock has not, every
//  remaining record reads as overdue, and the rest of the archive floods out
//  in one burst — which still delivers every packet, so a test that only
//  counted datagrams would pass.
// =============================================================================

TEST_SUITE(dlrseek)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir for seek archives");
    if (!tmp.isValid()) { return; }

    const qint64 t0 = 1'750'000'000'000LL;
    ColorRules rules;   // default: everything Info, which is all seek needs

    // A deliberately long lead-in, so a seek that works is obviously
    // different from one that does not.
    QVector<QPair<qint64, QByteArray>> a;
    a.push_back({ t0 +    0, makeWire(21, 2, 151, QByteArray("routine one")) });
    a.push_back({ t0 +  500, makeWire(21, 2, 151, QByteArray("routine two")) });
    a.push_back({ t0 + 1000, makeWire(21, 2, 151, QByteArray("BRAKE APPLIED emergency")) });
    a.push_back({ t0 + 1100, makeWire(21, 2, 151, QByteArray("routine three")) });
    a.push_back({ t0 + 1200, makeWire(21, 2, 151, QByteArray("routine four")) });

    const QString pathA = tmp.filePath("seek.dlr");
    CHECK(writeDlr(pathA, 21, 2, a), "wrote seek archive");

    DlrPlayer::Source srcA;
    QString err;
    CHECK(DlrPlayer::probe(pathA, &srcA, &err), "probe seek archive");

    // ---- query validation ------------------------------------------------
    //
    // An unparseable query must be refused up front. Accepting it and then
    // matching nothing is indistinguishable from an archive that genuinely
    // lacks the event, which is the one confusion this feature cannot afford.

    {
        DlrPlayer p;
        QString qerr;
        CHECK(!p.setSeekQuery(QStringLiteral("sev:(("), rules, &qerr),
              "an unparseable seek query is refused");
        CHECK(!qerr.isEmpty(), "the parse error is reported");
        CHECK(!p.hasSeekQuery(), "a refused query is not installed");

        CHECK(p.setSeekQuery(QStringLiteral("BRAKE"), rules, &qerr),
              "a valid seek query is accepted");
        CHECK(p.hasSeekQuery(), "a valid query is installed");

        CHECK(p.setSeekQuery(QString(), rules, &qerr), "an empty query clears");
        CHECK(!p.hasSeekQuery(), "clearing leaves no query");
    }

    // ---- landing and arming ----------------------------------------------

    {
        DlrPlayer p;
        QString qerr;
        CHECK(p.setSeekQuery(QStringLiteral("BRAKE"), rules, &qerr), "install BRAKE query");

        qint64 landedAt = -1;
        QString landedText;
        QObject::connect(&p, &DlrPlayer::seekLanded, &p,
                         [&](qint64 ms, QString t) { landedAt = ms; landedText = t; });

        const Capture cap = runPlayer(p, { srcA }, 25.0);

        CHECK(cap.wires.size() == 3, "the two records before the match are not sent");
        if (cap.wires.size() == 3) {
            const int kHdr = int(sizeof(STRUCT_MESSAGE_HEADER));
            CHECK(cap.wires[0].mid(kHdr) == QByteArray("BRAKE APPLIED emergency"),
                  "playback starts at the matching record");
            // The records after the match do NOT match the query, and must
            // still be sent: the point is to reach an event and watch what
            // follows, not to filter down to hits.
            CHECK(cap.wires[1].mid(kHdr) == QByteArray("routine three"),
                  "non-matching records after the match are still sent");
            CHECK(cap.wires[2].mid(kHdr) == QByteArray("routine four"),
                  "playback continues to the end of the archive");
        }

        CHECK(landedAt == 1000, "seekLanded reports the position it landed at");
        CHECK(landedText.contains(QStringLiteral("BRAKE")),
              "seekLanded reports the text that matched");

        const DlrPlayer::Stats st = p.stats();
        CHECK(st.seekSkipped == 2, "the discarded records are counted, not lost silently");
        CHECK(st.armed, "the player reports itself armed after landing");
    }

    // ---- pacing re-anchors at the landing point --------------------------
    //
    // At 1x the match sits 1000 ms into the archive. It must go out
    // immediately, and the 100 ms gaps AFTER it must still be honoured.

    {
        DlrPlayer p;
        QString qerr;
        CHECK(p.setSeekQuery(QStringLiteral("BRAKE"), rules, &qerr), "install query for pacing");

        const Capture cap = runPlayer(p, { srcA }, 1.0);
        CHECK(cap.wires.size() == 3, "1x seek delivers the tail of the archive");
        if (cap.atMs.size() == 3) {
            CHECK(cap.atMs[0] < 300,
                  "the landed record plays at once, not after the skipped 1000 ms");
            const qint64 tail = cap.atMs[2] - cap.atMs[0];
            CHECK(tail >= 150, "the 200 ms of gaps after the match are still paced");
            CHECK(tail <= 600, "pacing after the match is not a catch-up burst");
        }
    }

    // ---- a query that never matches --------------------------------------

    {
        DlrPlayer p;
        QString qerr;
        CHECK(p.setSeekQuery(QStringLiteral("NOTHING_LIKE_THIS"), rules, &qerr),
              "install a query with no hits");

        bool exhausted = false;
        QObject::connect(&p, &DlrPlayer::seekExhausted, &p, [&]() { exhausted = true; });

        const Capture cap = runPlayer(p, { srcA }, 25.0);
        CHECK(cap.wires.isEmpty(), "a query with no hits sends nothing");
        CHECK(exhausted, "seekExhausted is emitted so the empty run is explained");

        const DlrPlayer::Stats st = p.stats();
        CHECK(!st.armed, "the player never armed");
        CHECK(st.seekSkipped == 5, "every record was examined");
    }

    // ---- seeking on a header field across merged archives -----------------
    //
    // src: matches the tab key, which comes from the wire header rather than
    // the payload, so this also confirms the seek sees the same key the
    // receiver would route on.

    {
        QVector<QPair<qint64, QByteArray>> b;
        b.push_back({ t0 +  200, makeWire(81, 2, 160, QByteArray("station one")) });
        b.push_back({ t0 +  700, makeWire(81, 2, 160, QByteArray("station two")) });
        const QString pathB = tmp.filePath("seekb.dlr");
        CHECK(writeDlr(pathB, 81, 2, b), "wrote second seek archive");

        DlrPlayer::Source srcB;
        CHECK(DlrPlayer::probe(pathB, &srcB, &err), "probe second seek archive");

        DlrPlayer p;
        QString qerr;
        CHECK(p.setSeekQuery(QStringLiteral("src:81_2"), rules, &qerr),
              "install a source-key query");

        const Capture cap = runPlayer(p, { srcA, srcB }, 25.0);
        // Merged order is 21@0, 81@200, 21@500, 81@700, 21@1000, 21@1100,
        // 21@1200 — so the first 81_2 record is second, leaving six.
        CHECK(cap.wires.size() == 6, "seek lands on the first record from 81_2");
        if (!cap.wires.isEmpty()) {
            CHECK(quint8(cap.wires[0][0]) == 81, "the landed record is from source 81");
            bool sawOther = false;
            for (const QByteArray &w : cap.wires) {
                if (quint8(w[0]) == 21) { sawOther = true; }
            }
            CHECK(sawOther, "the other source still plays after the seek lands");
        }
    }

    // ---- skip to next match mid-playback ---------------------------------
    //
    // The workflow this exists for: start playing, then jump ahead. The skip
    // has to break a wait rather than queue behind it, or a long gap makes
    // the button look dead.

    {
        QVector<QPair<qint64, QByteArray>> c;
        c.push_back({ t0,          makeWire(21, 2, 151, QByteArray("start here")) });
        c.push_back({ t0 + 20000,  makeWire(21, 2, 151, QByteArray("quiet filler")) });
        c.push_back({ t0 + 20050,  makeWire(21, 2, 151, QByteArray("TARGET event")) });
        c.push_back({ t0 + 20100,  makeWire(21, 2, 151, QByteArray("after target")) });
        const QString pathC = tmp.filePath("skip.dlr");
        CHECK(writeDlr(pathC, 21, 2, c), "wrote skip archive");

        DlrPlayer::Source srcC;
        CHECK(DlrPlayer::probe(pathC, &srcC, &err), "probe skip archive");

        QUdpSocket rx;
        CHECK(rx.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)),
              "bound receiver for skip test");

        DlrPlayer p;
        QString qerr;
        CHECK(p.setSeekQuery(QStringLiteral("TARGET"), rules, &qerr),
              "install a query to skip toward");
        // Empty start: the first record is sent because the query is only
        // consulted while disarmed, and a skip is what disarms it again.
        p.setSeekQuery(QString(), rules, &qerr);
        p.setSources({ srcC });
        p.setTarget(QHostAddress::LocalHost, rx.localPort());
        p.setSpeed(1.0);
        p.setSeekQuery(QStringLiteral("TARGET"), rules, &qerr);

        QVector<QByteArray> got;
        bool done = false;
        QObject::connect(&p, &DlrPlayer::finished, &p, [&](bool) { done = true; });

        p.play();

        // Let it land on TARGET (which is 20 s in, so at 1x it would never
        // arrive inside the test budget without the seek doing the work).
        QElapsedTimer w;
        w.start();
        while (w.elapsed() < 800 && !done) {
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

        const int kHdr = int(sizeof(STRUCT_MESSAGE_HEADER));
        CHECK(!got.isEmpty(), "the seek reaches the target across a 20 s gap");
        if (!got.isEmpty()) {
            CHECK(got[0].mid(kHdr) == QByteArray("TARGET event"),
                  "the seek skipped the 20 s of lead-in entirely");
        }

        QElapsedTimer stopClock;
        stopClock.start();
        p.stop();
        CHECK(p.wait(2000), "the seeking player joins after stop()");
        CHECK(stopClock.elapsed() < 500, "stop() is prompt during a seek");
    }
}


// =============================================================================
//  The live tap.
//
//  The decode panel on the streaming dialog samples the outgoing stream rather
//  than decoding every record, because in burst mode the send loop moves two
//  hundred thousand records a second and a panel can render maybe ten. So the
//  player keeps one record and the panel takes what is there on its own tick.
//
//  The two properties that matter: it costs nothing when nobody is looking,
//  and the record a seek lands on is always inspectable — that one is the
//  reason the seek was run, and it must not depend on the panel happening to
//  be open at the right moment.
// =============================================================================

TEST_SUITE(dlrtap)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir for tap archives");
    if (!tmp.isValid()) { return; }

    const qint64 t0 = 1'750'000'000'000LL;
    ColorRules rules;

    QVector<QPair<qint64, QByteArray>> a;
    a.push_back({ t0 +   0, makeWire(21, 2, 151, QByteArray("first line")) });
    a.push_back({ t0 + 100, makeWire(21, 2, 151, QByteArray("MARKER here")) });
    a.push_back({ t0 + 200, makeWire(21, 2, 151, QByteArray("last line")) });

    const QString path = tmp.filePath("tap.dlr");
    CHECK(writeDlr(path, 21, 2, a), "wrote tap archive");

    DlrPlayer::Source src;
    QString err;
    CHECK(DlrPlayer::probe(path, &src, &err), "probe tap archive");

    // ---- off by default --------------------------------------------------

    {
        DlrPlayer p;
        CHECK(!p.tapEnabled(), "the tap is off unless asked for");
        const Capture cap = runPlayer(p, { src }, 25.0);
        CHECK(cap.wires.size() == 3, "the run still delivers everything");
        CHECK(p.tappedWire().isEmpty(),
              "nothing is captured while the tap is off");
    }

    // ---- on: holds the most recent record --------------------------------

    {
        DlrPlayer p;
        p.setTapEnabled(true);
        CHECK(p.tapEnabled(), "the tap reports itself enabled");

        const Capture cap = runPlayer(p, { src }, 25.0);
        CHECK(cap.wires.size() == 3, "the run delivers everything with the tap on");

        qint64 tappedMs = 0;
        const QByteArray w = p.tappedWire(&tappedMs);
        CHECK(!w.isEmpty(), "something was captured");
        CHECK(w == a.last().second,
              "the tap holds the LAST record sent, not the first");
        CHECK(tappedMs == t0 + 200,
              "the tap reports the record's own arrival time, not the wall clock");

        // The panel builds its LogEntry from exactly these two values, so
        // the round trip through the tap has to survive the same checks the
        // receiver would apply.
        const LogEntryPtr e = MessageDispatcher::buildEntry(w, tappedMs, &rules, false);
        CHECK(!e.isNull(), "a tapped record builds an entry");
        if (e) {
            CHECK(e->text == QStringLiteral("last line"),
                  "the entry decodes to the record's payload");
            CHECK(e->header.source_id == 21, "the entry keeps the source id");
            CHECK(e->header.kvchId == 2,     "the entry keeps the kvch id");
            CHECK(e->epochMs == t0 + 200,    "the entry keeps the archived time");
            CHECK(e->rawBytes == w,          "the entry keeps the wire bytes verbatim");
        }
    }

    // ---- a seek landing is captured even with the tap off ----------------

    {
        DlrPlayer p;
        QString qerr;
        CHECK(p.setSeekQuery(QStringLiteral("MARKER"), rules, &qerr),
              "install a query to land on");
        CHECK(!p.tapEnabled(), "tap deliberately left off");

        // Stop as soon as the seek lands, so the tap cannot be overwritten
        // by the records that follow — this checks the landing itself was
        // captured, not merely that something was.
        QObject::connect(&p, &DlrPlayer::seekLanded, &p,
                         [&p](qint64, QString) { p.stop(); });

        const Capture cap = runPlayer(p, { src }, 25.0);
        Q_UNUSED(cap);

        qint64 tappedMs = 0;
        const QByteArray w = p.tappedWire(&tappedMs);
        CHECK(!w.isEmpty(), "the landed record is captured despite the tap being off");
        CHECK(w == a[1].second, "the captured record is the one that matched");
        CHECK(tappedMs == t0 + 100, "the landed record's own time is reported");
    }

    // ---- a capture frame in the payload still reads as one ---------------
    //
    // Most archived records are plain text, but the ones worth decoding
    // arrive as capture lines — @slrp_2_1 and friends — and the panel
    // decodes from that text, not from the datagram bytes. The tap must
    // hand back something that still parses as a capture line, or the
    // Decoded fields tab would be permanently empty on exactly the frames
    // it exists for.

    {
        const QByteArray line =
            "@slrp_2_1 2026-08-26T15:10:42 1552 91 B9 AA E6 04 1E 80 00";
        QVector<QPair<qint64, QByteArray>> c;
        c.push_back({ t0, makeWire(21, 2, 151, line) });
        const QString pathC = tmp.filePath("capline.dlr");
        CHECK(writeDlr(pathC, 21, 2, c), "wrote a capture-line archive");

        DlrPlayer::Source srcC;
        CHECK(DlrPlayer::probe(pathC, &srcC, &err), "probe capture-line archive");

        DlrPlayer p;
        p.setTapEnabled(true);
        const Capture cap = runPlayer(p, { srcC }, 25.0);
        CHECK(cap.wires.size() == 1, "the capture-line record is sent");

        qint64 ms = 0;
        const QByteArray w = p.tappedWire(&ms);
        const LogEntryPtr e = MessageDispatcher::buildEntry(w, ms, &rules, false);
        CHECK(!e.isNull(), "the capture line builds an entry");
        if (e) {
            const CaptureLine parsed = CaptureDecoder::parseLine(e->text);
            CHECK(parsed.valid, "the tapped text still parses as a capture line");
            CHECK(parsed.typeToken == QStringLiteral("slrp"),
                  "the capture type survives the round trip");
            CHECK(parsed.bytes.size() == 8, "the frame bytes survive the round trip");
        }
    }
}

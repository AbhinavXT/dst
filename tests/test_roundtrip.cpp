#include "testutil.h"

#include "roundtrip.h"
#include "capturedecoder.h"
#include "schema/schemaencoder.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>

// =============================================================================
//  The round-trip validator.
//
//  Two things here must not be wrong, because both are load-bearing for
//  anything this program transmits:
//
//    * the verdict on a frame — "reproduced" has to mean the bytes really
//      are identical, and "not encodable" has to stay separate from "came
//      back wrong", since one is a property of the schema and the other of
//      the frame;
//    * where the body starts. parseBody() reads from bit 0 of whatever it
//      is handed, so feeding it a captured arp/lsrp frame whole reads the
//      message header as PKT_TYPE and produces a confident wrong answer
//      rather than an error. There is a check below that pins exactly that.
// =============================================================================

namespace {

// Real frames from replay/. Embedded rather than read from disk so the suite
// runs anywhere, and chosen to cover each outcome the tool reports.
const QString kSlrp = QStringLiteral(
    "@slrp_1_1 2026-06-27T14:02:27 21440 90 81 8A E6 04 1E 80 00 05 38 60 00 08 "
    "01 60 94 BD 80 10 00 64 00 00 00 13 12 CB 04 DA A0 37 D0");
const QString kLsrp = QStringLiteral(
    "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
    "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B");
const QString kArp = QStringLiteral(
    "@arp_1_1 2026-06-27T14:02:26 21436 02 07 0D 00 27 00 00 00 0F 02 D3 AC 57 30 "
    "00 01 40 9F FB 47 D0 01 0E 04 1F C3 15 00 00 00 00 00 08 32 00 C9 5E DE 2F");
const QString kDop1 = QStringLiteral(
    "@dop1_1_1 2026-06-27T14:02:26 21431 0C 17 17 17 17 0C 0C 0C 0C 17 17 17 17 17 17 17");
const QString kDmi = QStringLiteral(
    "@dmi_1_1 2026-06-27T14:02:27 21437 AA AA 74 02 01 0A 6F 00 FD A5 00 00 00 00 "
    "00 00 00 00 00 00 1B 06 EA 07 0E 02 1B 08 00");

CaptureLine line(const QString &s) { return CaptureDecoder::parseLine(s); }

}  // namespace

TEST_SUITE(roundtrip)
{
    Schema::Encoder enc;
    QString loadErr;
    const bool loaded = enc.load(QStringLiteral(":/schema/kavach.xml"), &loadErr);
    CHECK(loaded, "the schema loads");
    if (!loaded) { return; }

    using namespace RoundTrip;

    // ---- where the body starts ---------------------------------------------
    {
        CHECK(bodyStart(enc.packet(QStringLiteral("slrp"))) == 0,
              "slrp has no in-capture envelope");
        CHECK(bodyStart(enc.packet(QStringLiteral("lsrp"))) == 10,
              "lsrp's body starts 10 bytes in (body_offset=80 bits)");
        CHECK(bodyStart(enc.packet(QStringLiteral("arp"))) == 10,
              "and so does arp's");
    }

    // ---- real frames reproduce ---------------------------------------------
    {
        const CaptureLine c = line(kSlrp);
        CHECK(c.valid, "the slrp sample parses as a capture line");
        const Result r = check(enc, c.typeToken, c.bytes);
        CHECK(r.outcome == Outcome::Identical, "a real slrp frame is rebuilt byte for byte");
        CHECK(r.bodyStart == 0, "and its body starts at byte 0");
        CHECK(r.bodyBytes > 0 && r.rebuiltBytes == r.bodyBytes, "lengths agree");
    }
    {
        const CaptureLine c = line(kLsrp);
        const Result r = check(enc, c.typeToken, c.bytes);
        CHECK(r.outcome == Outcome::Identical, "a real lsrp frame is rebuilt byte for byte");
        CHECK(r.bodyStart == 10, "past its message header and station id");
    }
    {
        const CaptureLine c = line(kArp);
        CHECK(check(enc, c.typeToken, c.bytes).outcome == Outcome::Identical,
              "a real arp frame is rebuilt byte for byte");
    }

    // ---- the envelope trap -------------------------------------------------
    // Stripping the 8-byte message header (rather than the 10 bytes
    // body_offset describes) leaves parseBody two bytes out of step. It does
    // NOT fail: it reads plausible values, gates the FRAME_NUM-dependent
    // health branch on the wrong number, and rebuilds something close. This
    // is the failure the tool exists to make impossible to miss, so it is
    // pinned rather than left to be rediscovered.
    {
        const CaptureLine c = line(kLsrp);
        const Schema::ParsedPacket good = enc.parseBody(QStringLiteral("lsrp"), c.bytes.mid(10));
        const Schema::ParsedPacket bad  = enc.parseBody(QStringLiteral("lsrp"), c.bytes.mid(8));
        CHECK(good.ok, "the correctly-offset parse succeeds");
        CHECK(bad.ok,  "and so does the misaligned one — it does not announce itself");
        CHECK(good.header.value("L_DOUBTOVER") != bad.header.value("L_DOUBTOVER"),
              "but it reads a different value for the same field");
    }

    // ---- a frame that cannot be reproduced ---------------------------------
    // The two bits of <pad> before the health field are written as zero by
    // the encoder and skipped by the parser, so a frame with one of them set
    // cannot be rebuilt. That is a true finding about the schema, not a
    // contrived one: those bits carry something the schema does not name.
    {
        CaptureLine c = line(kLsrp);
        QByteArray mutated = c.bytes;
        const int padByte = 10 + 20;                 // body byte 20 holds the pad
        mutated[padByte] = char(quint8(mutated.at(padByte)) | 0x80);
        const Result r = check(enc, c.typeToken, mutated, /*keepBody=*/true);
        CHECK(r.outcome == Outcome::Differs, "a set pad bit cannot be reproduced");
        CHECK(r.firstDiff == 20, "and the difference is reported at body byte 20");
        CHECK(r.diffBytes.size() == 1, "exactly one byte differs");
        CHECK(r.rebuiltBody.size() == r.bodyBytes, "the rebuild is still body-length");
        CHECK(!r.detail.isEmpty(), "and the result says what happened");
    }

    // ---- outcomes that are about the schema, not the frame -----------------
    {
        const CaptureLine c = line(kDop1);
        const Result r = check(enc, c.typeToken, c.bytes);
        CHECK(r.outcome == Outcome::NotEncodable, "dop1 uses a repeat the encoder can't emit");
        CHECK(r.detail.contains(QLatin1String("repeat")), "and says so");
        CHECK(!isFrameVerdict(r.outcome),
              "'not encodable' is not a verdict on this frame");
    }
    {
        const CaptureLine c = line(kDmi);
        const Result r = check(enc, c.typeToken, c.bytes);
        CHECK(r.outcome == Outcome::NotEncodable,
              "an lsb-first packet is refused rather than packed msb-first");
        CHECK(r.detail.contains(QLatin1String("lsb-first")), "naming the bit order");
    }
    {
        // NMSHLTH is a single <eventstream>, which the encoder has no writer
        // for. It used to be skipped silently, leaving a zero-field packet
        // that "parsed" every buffer and encoded to nothing.
        const Result r = check(enc, QStringLiteral("nmshlth"), QByteArray(40, '\0'));
        CHECK(r.outcome == Outcome::NotEncodable,
              "an eventstream packet is refused, not silently emptied");
    }
    {
        const Result r = check(enc, QStringLiteral("nosuchtype"), QByteArray(8, '\0'));
        CHECK(r.outcome == Outcome::NoPacket, "an unknown captype has no packet");
        CHECK(!isFrameVerdict(r.outcome), "and is not counted against anything");
    }
    {
        const Result r = check(enc, QStringLiteral("lsrp"), QByteArray(4, '\0'));
        CHECK(r.outcome == Outcome::TooShort, "a frame shorter than its envelope");
    }
    {
        const Result r = check(enc, QStringLiteral("slrp"), QByteArray(6, '\0'));
        CHECK(r.outcome == Outcome::ParseFailed, "a truncated body is refused by the parser");
    }

    // ---- the diff cap ------------------------------------------------------
    {
        CaptureLine c = line(kSlrp);
        QByteArray wrecked = c.bytes;
        for (int i = 0; i < wrecked.size(); ++i) { wrecked[i] = char(~quint8(wrecked.at(i))); }
        const Result r = check(enc, c.typeToken, wrecked);
        // Whatever it decides about a frame of inverted bytes, it must not
        // collect an unbounded list of differences.
        CHECK(r.diffBytes.size() <= kMaxDiffBytes, "differing byte list is capped");
    }

    // ---- field attribution -------------------------------------------------
    {
        QVector<FieldRow> rows;
        FieldRow a; a.field = "ALPHA"; a.bitOffset = 80;  a.bitLength = 16;  // bytes 10-11
        FieldRow b; b.field = "BETA";  b.bitOffset = 96;  b.bitLength = 4;   // byte 12
        FieldRow c; c.field = "GAMMA"; c.bitOffset = 100; c.bitLength = 4;   // byte 12 too
        FieldRow n; n.field = "NOSPAN";                                       // no span
        rows << a << b << c << n;
        CHECK(fieldAtByte(rows, 10) == QLatin1String("ALPHA"), "a byte inside one field");
        CHECK(fieldAtByte(rows, 12) == QLatin1String("BETA, GAMMA"),
              "a byte two fields share names both — the wire is bit-packed");
        CHECK(fieldAtByte(rows, 40).isEmpty(),
              "a byte no field claims is reported as unclaimed, not guessed at");
    }

    // ---- the capture line handed to Frame Diff -----------------------------
    {
        const CaptureLine c = line(kLsrp);
        const QString rebuilt = captureLineFor(c, c.bytes);
        const CaptureLine back = CaptureDecoder::parseLine(rebuilt);
        CHECK(back.valid, "a synthesised capture line parses back");
        CHECK(back.bytes == c.bytes, "with the same bytes");
        CHECK(back.typeToken == c.typeToken && back.locoId == c.locoId
              && back.ctrlId == c.ctrlId, "and the same tag");
        CHECK(back.seq == c.seq, "and the same sequence number");
    }

    // ---- tallies -----------------------------------------------------------
    {
        Report rep;
        const CaptureLine slrp = line(kSlrp);
        const CaptureLine dop  = line(kDop1);
        rep.add(slrp, check(enc, slrp.typeToken, slrp.bytes), 4);
        rep.add(slrp, check(enc, slrp.typeToken, slrp.bytes), 4);
        rep.add(dop,  check(enc, dop.typeToken,  dop.bytes),  4);

        const Tally &s = rep.byType.value(QStringLiteral("slrp"));
        CHECK(s.frames == 2 && s.identical == 2, "both slrp frames counted as reproduced");
        CHECK(s.crcChecked == 2 && s.crcOk == 2, "and their CRCs were checked and passed");
        CHECK(s.samples.isEmpty(), "a reproduced frame is not kept as a sample");

        const Tally &d = rep.byType.value(QStringLiteral("dop1"));
        CHECK(d.notEncodable == 1, "the dop1 frame is counted as not encodable");
        CHECK(d.differs == 0, "and NOT as a frame that came back wrong");
        CHECK(d.answerable() == 0, "the question does not apply to it at all");

        CHECK(rep.captypes() == QStringList({ "dop1", "slrp" }), "types come back sorted");
        CHECK(rep.totalIdentical() == 2 && rep.totalAnswerable() == 2, "totals");
    }

    // ---- samples are bounded, and splice back into a whole frame -----------
    {
        Report rep;
        CaptureLine c = line(kLsrp);
        QByteArray mutated = c.bytes;
        mutated[30] = char(quint8(mutated.at(30)) | 0x80);
        c.bytes = mutated;
        for (int i = 0; i < 10; ++i) {
            rep.add(c, check(enc, c.typeToken, c.bytes, /*keepBody=*/true), 3);
        }
        const Tally &t = rep.byType.value(QStringLiteral("lsrp"));
        CHECK(t.differs == 10, "every failing frame is counted");
        CHECK(t.samples.size() == 3, "but only the sample cap is kept");
        CHECK(t.samples.first().rebuiltFrame.size() == c.bytes.size(),
              "the rebuilt sample is a whole frame, not just a body");
        CHECK(t.samples.first().rebuiltFrame.left(10) == c.bytes.left(10),
              "with the captured envelope in front of it");
        CHECK(t.diffHistogram.value(20) == 10, "and the failing byte is counted once per frame");
    }

    // ---- report text -------------------------------------------------------
    {
        Report rep;
        rep.startedAt = QDateTime::currentDateTime();
        const CaptureLine slrp = line(kSlrp);
        const CaptureLine dmi  = line(kDmi);
        rep.add(slrp, check(enc, slrp.typeToken, slrp.bytes), 4);
        rep.add(dmi,  check(enc, dmi.typeToken,  dmi.bytes),  4);
        const QString txt = rep.toText();
        CHECK(txt.contains(QLatin1String("slrp")) && txt.contains(QLatin1String("dmi")),
              "both types appear in the report");
        CHECK(txt.contains(QLatin1String("built into the binary")),
              "the report names the schema it used");
        CHECK(txt.contains(QLatin1String("not encodable")),
              "and says outright that dmi could not be built");
        CHECK(txt.contains(QLatin1String("Reproduction is not correctness")),
              "the report states its own limit");
    }

    // ---- scanning text -----------------------------------------------------
    {
        Report rep;
        const QStringList lines{
            kSlrp,
            QStringLiteral("some ordinary diagnostic line, not a capture"),
            QString(),
            kLsrp,
        };
        scanLines(enc, lines, &rep, 4);
        CHECK(rep.linesSeen == 4, "every line offered is counted");
        CHECK(rep.framesSeen == 2, "but only the two capture lines are frames");
        CHECK(rep.totalIdentical() == 2, "and both reproduce");
    }
    {
        // Cancellation stops the scan and says so, rather than reporting a
        // partial count as if it were the whole corpus.
        Report rep;
        QStringList lines;
        for (int i = 0; i < 50; ++i) { lines << kSlrp; }
        scanLines(enc, lines, &rep, 4, [] { return true; });
        CHECK(rep.cancelled, "a cancelled scan is marked cancelled");
        CHECK(rep.toText().contains(QLatin1String("CANCELLED")),
              "and the report says so where someone will read it");
    }

    // ---- the worker, end to end --------------------------------------------
    {
        QTemporaryDir dir;
        CHECK(dir.isValid(), "temp dir created");
        const QString path = dir.filePath(QStringLiteral("sample.cap"));
        {
            QFile f(path);
            f.open(QIODevice::WriteOnly | QIODevice::Text);
            QTextStream ts(&f);
            ts << kSlrp << "\n" << kLsrp << "\n" << kDop1 << "\n"
               << "not a capture line\n";
        }

        Runner::Job job;
        job.capFiles << path;
        Runner runner(job);

        Report got;
        bool done = false;
        // Direct connection: the suite has no event loop, and what is being
        // checked is the report the worker produced, not the delivery.
        QObject::connect(&runner, &Runner::finished, &runner,
                         [&got, &done](RoundTrip::Report r) { got = r; done = true; },
                         Qt::DirectConnection);
        runner.start();
        CHECK(runner.wait(30000), "the worker finishes");
        CHECK(done, "and emits its report");
        CHECK(got.filesScanned == 1, "one file read");
        CHECK(got.framesSeen == 3, "three capture frames in it");
        CHECK(got.totalIdentical() == 2, "the two radio frames reproduce");
        CHECK(got.byType.value(QStringLiteral("dop1")).notEncodable == 1,
              "and dop1 is reported as not encodable");
        CHECK(!got.cancelled, "an uninterrupted scan is not marked cancelled");
    }
    {
        // An unreadable file is a warning, not a crash and not a silent zero.
        Runner::Job job;
        job.capFiles << QStringLiteral("/nonexistent/nowhere.cap");
        Runner runner(job);
        Report got;
        QObject::connect(&runner, &Runner::finished, &runner,
                         [&got](RoundTrip::Report r) { got = r; },
                         Qt::DirectConnection);
        runner.start();
        runner.wait(30000);
        CHECK(got.filesUnreadable == 1, "an unreadable file is counted");
        CHECK(got.warnings.size() == 1, "and warned about");
        CHECK(got.framesSeen == 0, "with nothing invented to fill the gap");
    }
}

// =============================================================================
//  The window. A correct core does not mean the window shows it — the empty
//  Panels submenu and the self-closing column menus were both found this way.
//  These checks drive the real widgets: pick a corpus, run the scan, and read
//  what ends up on screen.
// =============================================================================

#include "roundtripwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>

namespace {

// Spin the event loop until `done` or the deadline. The scan runs on a
// worker thread and reports back through a queued connection, so without
// this the window never learns the scan finished.
bool spin(const std::function<bool()> &done, int ms = 20000)
{
    QElapsedTimer t; t.start();
    while (!done() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return done();
}

int rowFor(QTableWidget *t, const QString &captype)
{
    for (int r = 0; r < t->rowCount(); ++r) {
        if (t->item(r, 0) && t->item(r, 0)->text() == captype) { return r; }
    }
    return -1;
}

}  // namespace

TEST_SUITE(roundtripwindow)
{
    QTemporaryDir dir;
    CHECK(dir.isValid(), "temp dir created");
    const QString path = dir.filePath(QStringLiteral("w.cap"));
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        QTextStream ts(&f);
        // Two frames that reproduce, one type that cannot be encoded, and a
        // frame that cannot: enough for every column to have to say something.
        CaptureLine bad = line(kLsrp);
        QByteArray mutated = bad.bytes;
        mutated[30] = char(quint8(mutated.at(30)) | 0x80);
        ts << kSlrp << "\n" << kLsrp << "\n" << kDop1 << "\n"
           << RoundTrip::captureLineFor(bad, mutated) << "\n";
    }

    RoundTripWindow win;
    auto *table  = win.findChild<QTableWidget *>();
    auto *runBtn = win.findChild<QPushButton *>();
    CHECK(table != nullptr, "the window has a results table");
    CHECK(table && table->rowCount() == 0, "which starts empty");

    // With no corpus at all there is nothing to run. A Run button that
    // launches a scan of nothing and reports 0/0 would look like a result.
    win.setLiveEntries({});
    auto *live = win.findChild<QCheckBox *>();
    CHECK(live && !live->isChecked(), "an empty live log is not offered as a corpus");

    // Reach the file list the way the dialog fills it, without a file dialog.
    QMetaObject::invokeMethod(&win, "onClearFiles");

    // Drive a scan over the file by pushing the job in through the same slot
    // the Run button uses. The file list is private, so this uses the public
    // shape of the feature: live entries built from the same lines.
    QVector<LogEntryPtr> entries;
    for (const QString &s : { kSlrp, kLsrp, kDop1 }) {
        auto e = LogEntryPtr::create();
        e->text = s;
        entries.push_back(e);
    }
    win.setLiveEntries(entries);
    CHECK(live && live->isChecked(), "a non-empty live log is offered and ticked");

    QMetaObject::invokeMethod(&win, "onRun");
    const bool finished = spin([table] { return table->rowCount() > 0; });
    CHECK(finished, "the scan finishes and fills the table");

    if (finished) {
        const int slrpRow = rowFor(table, QStringLiteral("slrp"));
        const int dopRow  = rowFor(table, QStringLiteral("dop1"));
        CHECK(slrpRow >= 0 && dopRow >= 0, "both types are listed");
        if (slrpRow >= 0) {
            CHECK(table->item(slrpRow, 2)->text() == QLatin1String("1"),
                  "slrp is shown as reproduced");
        }
        if (dopRow >= 0) {
            // The distinction the whole report rests on: a type nothing can
            // be built for shows a dash, not a zero. A zero in the
            // "reproduced" column reads as a failed check.
            CHECK(table->item(dopRow, 2)->text() == QString::fromUtf8("—"),
                  "a not-encodable type shows no reproduction count");
            CHECK(table->item(dopRow, 3)->text() == QLatin1String("0"),
                  "and is not counted as a frame that came back wrong");
        }

        // Selecting a row must produce a detail pane that says something.
        table->selectRow(0);
        QCoreApplication::processEvents();
        auto *detail = win.findChild<QTextEdit *>();
        CHECK(detail && !detail->toPlainText().trimmed().isEmpty(),
              "selecting a type explains it");
    }
    (void)runBtn;
}

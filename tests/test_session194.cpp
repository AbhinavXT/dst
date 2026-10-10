#include "testutil.h"
#include "layoutaudit.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "readerdir.h"
#include "readerdirwindow.h"
#include "statusline.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QSignalSpy>
#include <QTableWidget>

// =============================================================================
//  Session 194 — reader direction (@rdir READER_INFO) and RDSO FRS 18.
//
//  SYNTHETIC @rdir: schema/fixtures/rdir_synthetic.log (tests/rdirgen), the
//  seven test cases 18.1-18.7 a minute apart, made by simulating LKAVACH
//  v1.2.9's DetermineTrainDirectionReader1/2; after each step a REAL ARP of
//  loco 1 (SR) with LAST_RFID_TAG set to the tag the RDSO table reports and
//  its CRC recomputed. Tags R1..R4 = 101..104. The @rfid frames of the
//  location check are real (replay/loco_1_1_26062026_162418.cap).
// =============================================================================

namespace {

LogModel *loadFixture(MessageDispatcher &d)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/schema/fixtures/rdir_synthetic.log"));
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        d.ingestLocal(1, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    d.drainNow();
    return d.modelForKey(QStringLiteral("1_1"));
}

// The first read of each test case: reads more than 30 s after the one before.
QVector<int> caseStarts(const ReaderDir::Log &log)
{
    QVector<int> s;
    for (int i = 0; i < log.reads.size(); ++i)
        if (i == 0 || log.reads.at(i).ms - log.reads.at(i - 1).ms > 30000) s << i;
    return s;
}

}  // namespace

TEST_SUITE(session194)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher d;
    LogModel *m = loadFixture(d);
    CHECK(m != nullptr, "fixture: the seven FRS 18 cases");
    if (!m) return;

    // ---- decode, against the schema --------------------------------------------------------
    int frames = 0, agree = 0, arpPass = 0, arps = 0;
    for (int i = 0; i < m->count(); ++i) {
        const CaptureLine c = CaptureDecoder::parseLine(m->entryAt(i)->text);
        if (c.type == CapType::ARP) { ++arps; arpPass += (c.crcChecked && c.crcOk) ? 1 : 0; continue; }
        if (c.type != CapType::Rdir) continue;
        ++frames;
        QHash<QString, qint64> raw;
        CaptureDecoder::describe(c, nullptr, 0, &raw);
        ReaderDir::Read r;
        if (ReaderDir::decode(c.bytes, &r) && raw.value(QStringLiteral("reader_id")) == r.reader
            && raw.value(QStringLiteral("tag_id")) == r.tag && raw.value(QStringLiteral("reader_dir")) == r.readerDir
            && raw.value(QStringLiteral("movement_dir")) == r.movementDir)
            ++agree;
    }
    CHECK(frames == 39 && agree == frames, "39 @rdir frames; the decoder agrees with the schema on all");
    CHECK(arps == 28 && arpPass == arps, "the 28 ARPs (a real one, LAST_RFID_TAG set) pass their CRC");
    ReaderDir::Read bad;
    CHECK(!ReaderDir::decode(QByteArray(6, '\0'), &bad) && !ReaderDir::decode(QByteArray(4, '\0'), &bad),
          "anything but 5 bytes is not read");
    CHECK(ReaderDir::letter(0) == QLatin1String("U") && ReaderDir::letter(1) == QLatin1String("N")
              && ReaderDir::letter(2) == QLatin1String("R"),
          "0 U, 1 N, 2 R");

    // ---- the log ------------------------------------------------------------------------------
    const ReaderDir::Log log = ReaderDir::extract(m);
    CHECK(log.reads.size() == 39 && log.arpFrames == 28 && log.badFrames == 0, "39 reads, 28 ARPs");
    const QVector<int> starts = caseStarts(log);
    CHECK(starts.size() == 7, "seven test runs, a minute apart");
    if (starts.size() != 7) return;

    // Every case, bounded to its own run: every RDSO row seen, in order.
    for (int k = 0; k < 7; ++k) {
        const ReaderDir::TestCase &tc = ReaderDir::frs18().at(k);
        const int to = k + 1 < 7 ? starts.at(k + 1) - 1 : -1;
        const QVector<ReaderDir::Read> reads = ReaderDir::fromRead(log, starts.at(k), to);
        const QVector<int> mt = ReaderDir::match(tc, reads, ReaderDir::tagOrder(log, reads));
        bool all = mt.size() == tc.rows.size();
        for (int j : mt) all = all && j >= 0;
        CHECK(all, QByteArray(tc.id.toUtf8()) + ": every row of the RDSO table seen in order");
    }

    // A run checked against the wrong table says so.
    {
        const QVector<ReaderDir::Read> r181 = ReaderDir::fromRead(log, starts.at(0), starts.at(1) - 1);
        const QVector<int> mt = ReaderDir::match(ReaderDir::frs18().at(3), r181, ReaderDir::tagOrder(log, r181));
        int seen = 0;
        for (int j : mt) seen += j >= 0 ? 1 : 0;
        CHECK(mt.size() == 4 && seen < 4 && mt.first() < 0,
              "18.1's reads against 18.4's table: its first row (R2 first) is not seen");
    }

    // 18.4 in detail: R2 reported first, then R1, then R2 again with reader-2 N.
    {
        const QVector<ReaderDir::Read> r = ReaderDir::fromRead(log, starts.at(3), starts.at(4) - 1);
        CHECK(r.size() == 5 && r.at(0).reader == 1 && r.at(0).tag == 102 && r.at(0).reported == 102
                  && r.at(1).reader == 2 && r.at(1).tag == 101 && r.at(1).reported == 101
                  && r.at(2).r1Dir == ReaderDir::U && r.at(2).r2Dir == ReaderDir::N && r.at(2).movementDir == ReaderDir::N
                  && r.at(2).reported == 102,
              "18.4: R2 by reader-1 (reported R2); R1 by reader-2 (reported R1); R2 by reader-2: U N N, reported R2");
    }

    // ---- R1.. by location, from real @rfid frames ----------------------------------------------
    {
        MessageDispatcher d2;
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_26062026_162418.cap"));
        CHECK(f.open(QIODevice::ReadOnly), "fixture: real @rfid frames");
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            if (!l.startsWith("@rfid_")) continue;
            const QList<QByteArray> tok = l.split(' ');
            d2.ingestLocal(1, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
        }
        // Reads of tags 598 (153 562 m), 560 (152 720 m) and 8 (160 819 m).
        const QByteArray reads[] = { "@rdir_1_1 2026-06-26T16:40:00 1 01 56 02 00 00",
                                     "@rdir_1_1 2026-06-26T16:40:01 2 01 30 02 00 00",
                                     "@rdir_1_1 2026-06-26T16:40:02 3 01 08 00 01 01" };
        for (const QByteArray &l : reads)
            d2.ingestLocal(1, 1, l, QDateTime::fromString(QString::fromLatin1(l.split(' ').at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
        d2.drainNow();
        const ReaderDir::Log l2 = ReaderDir::extract(d2.modelForKey(QStringLiteral("1_1")));
        bool byLoc = false;
        const QVector<quint16> order = ReaderDir::tagOrder(l2, l2.reads, &byLoc);
        CHECK(byLoc && order == QVector<quint16>({ 560, 598, 8 }),
              "R1 = 560, R2 = 598, R3 = 8: by location from @rfid, not by id");
        CHECK(ReaderDir::tagLabel(8, order) == QLatin1String("R3 (8)") && ReaderDir::tagLabel(-1, order).isEmpty(), "labels");
    }

    // ---- the window -------------------------------------------------------------------------------
    {
        ReaderDirWindow w(&d);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.resize(1100, 640);
        w.show();
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        CHECK(w.sourceKey() == QLatin1String("1_1") && w.observedTable()->rowCount() == 39, "opens on the @rdir tab, every read");
        CHECK(w.mapping()->text().startsWith(QLatin1String("R1 = tag 101"))
                  && w.mapping()->text().contains(QLatin1String("ordered by tag id")),
              "the R1.. mapping, and how it was made");

        w.setFromRead(starts.at(6));
        w.setTestCase(QStringLiteral("18.7"));
        QTableWidget *ex = w.expectedTable();
        CHECK(ex->rowCount() == 7, "18.7's table: 7 rows");
        bool allSeen = true;
        for (int k = 0; k < ex->rowCount(); ++k) allSeen = allSeen && ex->item(k, 4)->text().startsWith(QLatin1String("seen at read "));
        CHECK(allSeen && ex->item(3, 3)->text().isEmpty() && ex->item(5, 3)->text() == QLatin1String("R4")
                  && ex->item(0, 5)->text().isEmpty(),
              "every row seen; blank and R4 rows as in the RDSO table; OK / Not OK left blank");
        QTableWidget *ob = w.observedTable();
        CHECK(ob->rowCount() == 8 && ob->item(0, 0)->text() == QString::number(starts.at(6) + 1)
                  && ob->item(0, 4)->text() == QLatin1String("U") && ob->item(0, 7)->text() == QLatin1String("R1 (101)")
                  && ob->item(0, 8)->text() == QLatin1String("1"),
              "18.7's 8 reads, numbered as in the log; the first: U, reported R1, test row 1");
        CHECK(w.status()->text().contains(QLatin1String("18.7: 7 of 7 expected rows seen in order")),
              QByteArray("the status counts them: ") + w.status()->text().toUtf8());
        const QString text = w.asText();
        CHECK(text.contains(QLatin1String("Reader-1 dir\tReader-2 dir\tOVK dir\tTag reported to SVK\tObserved\tOK / Not OK")),
              "Copy: both tables, tab-separated");

        w.setFromRead(starts.at(0));
        w.setToRead(starts.at(1) - 1);
        w.setTestCase(QStringLiteral("18.4"));
        // 18.1's reads give U U U R1 and N N N R3 (rows 2 and 4 of 18.4), never U U U R2 or U N N R2.
        CHECK(w.status()->text().contains(QLatin1String("18.4: 2 of 4 expected rows seen in order")),
              QByteArray("18.1's run against 18.4: 2 of 4 rows seen: ") + w.status()->text().toUtf8());
        CHECK(w.expectedTable()->item(0, 4)->text() == QLatin1String("not seen in order"), "row 1 not seen");

        QSignalSpy jumps(&w, &ReaderDirWindow::jumpRequested);
        emit ob->cellDoubleClicked(0, 1);
        CHECK(jumps.size() == 1 && jumps.first().at(0).toString() == QLatin1String("1_1"), "double-click: that read in the log");

        CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700,
              QByteArray("fits a laptop (minimum ") + QByteArray::number(w.minimumSizeHint().width()) + " x "
                  + QByteArray::number(w.minimumSizeHint().height()) + ")");
        const QStringList loose = LayoutAudit::orphans(&w);
        CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
    }
    {
        MessageDispatcher none;
        none.ingestLocal(9, 1, QByteArray("@dmi_9_1 2026-10-09T10:00:00 1 00"), 1, QString());
        none.drainNow();
        ReaderDirWindow w(&none);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(w.log().reads.isEmpty() && w.status()->text().contains(QLatin1String("No @rdir in this log")),
              "a log without @rdir says why");
    }
}

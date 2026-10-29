#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "nmslog.h"
#include "nmslogwindow.h"
#include "xlsxbook.h"

#include <QCoreApplication>
#include <QFile>
#include <QLabel>
#include <QTextStream>

// =============================================================================
//  Session 403 — @dlcount: LKAVACH's counter message to the LDLU
//  (Send_Counter_Messege_To_Data_Logger, message 105). 13 bytes, LE, PACK:
//  the 8-byte header, counter_value (0 isolation, 1 SOS, 2 OV trip), the CRC
//  (crcFast over bytes 0-8, u32 LE: ASSUMED u32). No real @dlcount line has
//  been captured yet: the fixture is packed from the pasted firmware code
//  (tests/dlcountgen), and the token "dlcount" is assumed.
// =============================================================================

namespace {

QVector<LogEntryPtr> entries403(const QString &path)
{
    QVector<LogEntryPtr> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    qint64 ms = 1792130400000;   // an arbitrary PC clock
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (!line.startsWith(QLatin1Char('@'))) continue;
        auto e = LogEntryPtr::create();
        e->header.source_id = 21;
        e->header.kvchId = 1;
        e->epochMs = ms += 1000;
        e->severity = Severity::Info;
        e->text = line;
        e->cacheDerived();
        out << e;
    }
    return out;
}

QHash<QString, QString> rows403(const QString &line)
{
    QHash<QString, QString> h;
    for (const FieldRow &r : CaptureDecoder::describe(CaptureDecoder::parseLine(line)))
        h.insert(r.field.trimmed(), r.value.trimmed());
    return h;
}

}  // namespace

TEST_SUITE(session403)
{
    const QVector<LogEntryPtr> es = entries403(QStringLiteral("../schema/fixtures/dlcount_synthetic.log"));
    CHECK(es.size() == 6, "fixture: schema/fixtures/dlcount_synthetic.log, 6 @dlcount lines");
    if (es.size() != 6) return;

    // ---- the packet ------------------------------------------------------------------------
    CHECK(CaptureDecoder::typeFromToken(QStringLiteral("dlcount")) == CapType::DlCount, "@dlcount is CapType::DlCount");
    bool allOk = true;
    for (const LogEntryPtr &e : es) {
        const CaptureLine c = CaptureDecoder::parseLine(e->text);
        allOk = allOk && c.valid && c.type == CapType::DlCount && c.bytes.size() == 13 && c.crcChecked && c.crcOk;
    }
    CHECK(allOk, "every line: 13 bytes, CRC checked and matching (crcFast over bytes 0-8)");

    const QHash<QString, QString> sos = rows403(es.at(1)->text);
    CHECK(sos.value(QStringLiteral("msg.src_id")).startsWith(QLatin1String("2 (LKAVACH)"))
              && sos.value(QStringLiteral("msg.dest_id")).startsWith(QLatin1String("6 (LDLU)"))
              && sos.value(QStringLiteral("msg.message_id")).startsWith(QLatin1String("105 (LKAVACH_LDLU_COUNTER_MESSAGE)"))
              && sos.value(QStringLiteral("msg.message_length")).startsWith(QLatin1String("13")),
          "the header: LKAVACH to LDLU, message 105, length 13");
    CHECK(sos.value(QStringLiteral("counter_value")).startsWith(QLatin1String("1 (SOS counter)")),
          qPrintable(QStringLiteral("counter_value 1: SOS counter (%1)").arg(sos.value(QStringLiteral("counter_value")))));
    CHECK(rows403(es.at(0)->text).value(QStringLiteral("counter_value")).contains(QLatin1String("Isolation"))
              && rows403(es.at(2)->text).value(QStringLiteral("counter_value")).contains(QLatin1String("OV trip")),
          "0: isolation counter; 2: OV trip counter");
    CHECK(sos.value(QStringLiteral("crc")).endsWith(QLatin1String("PASS")), "the schema's CRC row: PASS");

    // A changed counter byte: the CRC no longer matches.
    QString bent = es.at(1)->text;
    bent.replace(QStringLiteral(" 00 01 10 DC"), QStringLiteral(" 00 02 10 DC"));
    const CaptureLine bad = CaptureDecoder::parseLine(bent);
    CHECK(bent != es.at(1)->text && bad.crcChecked && !bad.crcOk, "the counter byte changed: CRC FAIL");
    // Not the struct's 13 bytes: not checked against a guessed span.
    const CaptureLine shortLine = CaptureDecoder::parseLine(es.at(1)->text.left(es.at(1)->text.size() - 3));
    CHECK(shortLine.bytes.size() == 12 && !shortLine.crcChecked, "12 bytes: the CRC is not checked");

    // ---- the table -------------------------------------------------------------------------
    NmsLog::Table t;
    for (const LogEntryPtr &e : es) NmsLog::append(t, NmsLog::Kind::DlCount, *e);
    CHECK(t.rows.size() == 6, "DL counts: one row per packet");
    CHECK(t.index.contains(QStringLiteral("msg.message_id")) && t.groupAt(t.index.value(QStringLiteral("msg.message_id"))) == NmsLog::Group::Header
              && t.groupAt(t.index.value(QStringLiteral("counter_value"))) == NmsLog::Group::Body,
          "msg.* as the header columns, counter_value the packet's own");
    CHECK(t.gaps == 0, "no gap marks: sent on events, seq_num never set");
    const QVector<int> n = NmsLog::dlCounts(t);
    CHECK(n.size() == 4 && n[0] == 2 && n[1] == 3 && n[2] == 1 && n[3] == 0, "the totals: isolation 2, SOS 3, OV trip 1");
    CHECK(NmsLog::dlCountTotals(t) == QStringLiteral("Isolation 2 · SOS 3 · OV trip 1"), "as the window says them");
    NmsLog::Table other;
    CHECK(!NmsLog::append(other, NmsLog::Kind::DlLog, *es.at(0)) && !NmsLog::append(other, NmsLog::Kind::Health, *es.at(0)),
          "the DL log and NMS health tables refuse @dlcount");
    QString err;
    const QVector<XlsxBook::Sheet> back = XlsxBook::read(XlsxBook::write(NmsLog::toSheets(t, NmsLog::Kind::DlCount)), &err);
    CHECK(back.size() == 1 && back.at(0).name == QLatin1String("dlcount") && back.at(0).rows.size() == t.rows.size() + 1,
          "xlsx: one sheet, dlcount, every packet");
    CHECK(NmsLog::title(NmsLog::Kind::DlCount) == QLatin1String("DL counts") && NmsLog::fileStem(NmsLog::Kind::DlCount) == QLatin1String("dl_counts"),
          "titled DL counts, exported as dl_counts");

    // ---- the window ------------------------------------------------------------------------
    {
        LogModel model(nullptr, 100000);
        model.appendEntries(es.mid(0, 3));
        NmsLogWindow w(NmsLog::Kind::DlCount, &model, QStringLiteral("1_1"), QStringLiteral("Loco 1"));
        CHECK(w.windowTitle().startsWith(QLatin1String("DL counts")) && w.table().rows.size() == 3,
              "window: titled DL counts, the packets the tab held");
        model.appendEntries(es.mid(3));
        QCoreApplication::processEvents();
        CHECK(w.table().rows.size() == 6, "window: packets the tab gets are added");
        bool totals = false;
        for (QLabel *label : w.findChildren<QLabel *>())
            totals = totals || label->text().contains(QStringLiteral("Isolation 2 · SOS 3 · OV trip 1"));
        CHECK(totals, "the count line gives the total for each counter");
    }
}

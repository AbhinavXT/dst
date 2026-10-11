#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "nmslog.h"
#include "nmslogwindow.h"
#include "xlsxbook.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextStream>

// =============================================================================
//  Session 247 — NMS health / fault / RSSI logs: one table row per packet,
//  following the tab, exported as CSV or .xlsx. Real frames from replay/:
//    loco_1_2_27062026_170159.cap  @nmshlth and @nmsrssi (and everything else)
//    loco_2_1_29062026_134128.cap  @nmsflt, 5 faults then 1
// =============================================================================

namespace {

QVector<LogEntryPtr> entriesOf(const QString &path, int maxLines)
{
    QVector<LogEntryPtr> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    qint64 ms = 1782579720000;   // 2026-06-27, an arbitrary PC clock
    while (!ts.atEnd() && out.size() < maxLines) {
        const QString line = ts.readLine().trimmed();
        if (!line.startsWith(QLatin1Char('@'))) continue;
        auto e = LogEntryPtr::create();
        e->header.source_id = 21;
        e->header.kvchId = 1;
        e->epochMs = ms += 100;
        e->severity = Severity::Info;
        e->text = line;
        e->cacheDerived();
        out << e;
    }
    return out;
}

int countOf(const QVector<LogEntryPtr> &es, const QString &token)
{
    int n = 0;
    for (const LogEntryPtr &e : es) if (e->text.startsWith(QLatin1Char('@') + token + QLatin1Char('_'))) ++n;
    return n;
}

NmsLog::Table tableOf(NmsLog::Kind k, const QVector<LogEntryPtr> &es)
{
    NmsLog::Table t;
    for (const LogEntryPtr &e : es) NmsLog::append(t, k, *e);
    return t;
}

QString cellAt(const NmsLog::Table &t, int row, const QString &column)
{
    return t.index.contains(column) ? t.cell(row, t.index.value(column)) : QStringLiteral("<no column>");
}

// The frame bytes as the capture line writes them: the hex after the sequence number.
QString hexOfLine(const QString &line)
{
    return line.section(QLatin1Char(' '), 3).trimmed().toUpper();
}

}  // namespace

TEST_SUITE(session247)
{
    const QVector<LogEntryPtr> a = entriesOf(QStringLiteral("../replay/loco_1_2_27062026_170159.cap"), 4000);
    const QVector<LogEntryPtr> b = entriesOf(QStringLiteral("../replay/loco_2_1_29062026_134128.cap"), 4000);
    CHECK(countOf(a, QStringLiteral("nmshlth")) >= 2 && countOf(a, QStringLiteral("nmsrssi")) >= 1,
          "fixture: replay/loco_1_2_27062026_170159.cap has @nmshlth and @nmsrssi");
    CHECK(countOf(b, QStringLiteral("nmsflt")) >= 2, "fixture: replay/loco_2_1_29062026_134128.cap has @nmsflt");

    // ---- one row per packet of the type, nothing else ----
    const NmsLog::Table hl = tableOf(NmsLog::Kind::Health, a);
    const NmsLog::Table rs = tableOf(NmsLog::Kind::Rssi, a);
    const NmsLog::Table fl = tableOf(NmsLog::Kind::Fault, b);
    CHECK(hl.rows.size() == countOf(a, QStringLiteral("nmshlth")), "health: one row per @nmshlth line");
    CHECK(rs.rows.size() == countOf(a, QStringLiteral("nmsrssi")), "RSSI: one row per @nmsrssi line");
    CHECK(fl.rows.size() == countOf(b, QStringLiteral("nmsflt")), "fault: one row per @nmsflt line");
    CHECK(tableOf(NmsLog::Kind::Fault, a).rows.size() == countOf(a, QStringLiteral("nmsflt")),
          "a tab's other packets are not rows");
    {
        NmsLog::Table t;
        const LogEntryPtr lsrp = [&]() { for (auto &e : a) if (e->text.startsWith(QLatin1String("@lsrp_"))) return e; return LogEntryPtr(); }();
        CHECK(lsrp && !NmsLog::append(t, NmsLog::Kind::Health, *lsrp) && t.rows.isEmpty(),
              "an @lsrp line is refused by the health table");
    }

    // ---- the columns: PC time first, the frame last, the decoded fields between ----
    CHECK(hl.header(0) == QLatin1String("PC time") && hl.header(hl.columnCount() - 1) == QLatin1String("Frame (hex)"),
          "PC time first, Frame (hex) last");
    CHECK(!hl.index.contains(QStringLiteral("type")), "no 'type' column: the window is one type");
    CHECK(hl.index.contains(QStringLiteral("loco / ctrl")) && hl.index.contains(QStringLiteral("CRC"))
              && hl.index.contains(QStringLiteral("nms.message_seq")),
          "the capture line and NMS header fields are columns");

    // The order: when, then the packet's own fields, then the header details.
    {
        const auto at = [&](const QString &c) { return fl.index.value(c, -1); };
        CHECK(at(QStringLiteral("rtc")) == 1 && at(QStringLiteral("seq")) == 2
                  && at(QStringLiteral("reporting_subsystem")) == 3,
              "columns: PC time, rtc, seq, then the packet's own fields");
        CHECK(at(QStringLiteral("fault[5] fault_id")) < at(QStringLiteral("loco / ctrl"))
                  && at(QStringLiteral("fault[5] fault_id")) < at(QStringLiteral("nms.message_seq")),
              "columns: the capture and NMS header details after the body");
    }

    // Values as the decoder gives them, for the first frame of each type.
    CHECK(cellAt(hl, 0, QStringLiteral("RADIO_1_HEALTH")) == QLatin1String("1  (OK)"), "health row 1: RADIO_1_HEALTH 1 (OK)");
    CHECK(cellAt(hl, 0, QStringLiteral("TRAIN_CONFIG_DATA_CHECKSUM")) == QLatin1String("1248270630  (0x4a671926)"),
          "health row 1: the config checksum");
    CHECK(cellAt(hl, 0, QStringLiteral("seq")) == QLatin1String("3819"), "health row 1: capture seq 3819");
    CHECK(cellAt(rs, 0, QStringLiteral("stationary_kavach_id")) == QLatin1String("527"), "RSSI row 1: stationary Kavach 527");
    CHECK(cellAt(rs, 0, QStringLiteral("radio1_abs_rfid")) == QLatin1String("161060"), "RSSI row 1: radio 1 abs RFID 161060");
    CHECK(cellAt(fl, 0, QStringLiteral("reporting_subsystem")) == QLatin1String("34 (VCC Card-1)"), "fault row 1: VCC Card-1");
    CHECK(cellAt(fl, 0, QStringLiteral("fault_count")) == QLatin1String("5"), "fault row 1: 5 faults");
    CHECK(cellAt(fl, 0, QStringLiteral("fault[5] fault_id")) == QLatin1String("26 (safety error)"),
          "fault row 1: fault[5] fault_id in its own column");
    CHECK(cellAt(fl, 1, QStringLiteral("fault_count")) == QLatin1String("1")
              && cellAt(fl, 1, QStringLiteral("fault[1] module")) == QLatin1String("2 (VCC Mc-2)")
              && cellAt(fl, 1, QStringLiteral("fault[2] module")).isEmpty(),
          "fault row 2: one fault, fault[2] … left empty");
    {
        QString firstHlth;
        for (auto &e : a) if (e->text.startsWith(QLatin1String("@nmshlth_"))) { firstHlth = e->text; break; }
        CHECK(hl.cell(0, hl.columnCount() - 1) == hexOfLine(firstHlth), "the frame column is the capture line's bytes");
    }

    // ---- CSV ----
    {
        const QString csv = NmsLog::toCsv(fl);
        const QStringList lines = csv.split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
        CHECK(lines.size() == fl.rows.size() + 1, "CSV: a header line and one line per packet");
        CHECK(lines.value(0).startsWith(QLatin1String("PC time,")), "CSV: the header names the columns");
        NmsLog::Table t;
        t.columnOf(QStringLiteral("x"));
        t.rows << QStringList{ QStringLiteral("t"), QStringLiteral("a,\"b\"") };
        t.hex << QStringLiteral("AA");
        t.epochMs << 0;
        CHECK(NmsLog::toCsv(t).endsWith(QStringLiteral("t,\"a,\"\"b\"\"\",AA\r\n")), "CSV: commas and quotes quoted (RFC 4180)");
    }

    // ---- .xlsx, read back ----
    {
        QString err;
        const QVector<XlsxBook::Sheet> back = XlsxBook::read(XlsxBook::write(NmsLog::toSheets(rs, NmsLog::Kind::Rssi)), &err);
        CHECK(back.size() == 1 && back.at(0).name == QLatin1String("nmsrssi"), "xlsx: one sheet, nmsrssi");
        const XlsxBook::Sheet sh = back.value(0);
        CHECK(sh.rows.size() == rs.rows.size() + 1, "xlsx: a header row and one row per packet");
        const int c = rs.index.value(QStringLiteral("stationary_kavach_id"));
        const QVariant id = sh.rows.value(1).value(c);
        CHECK(id.toLongLong() == 527 && id.typeName() == QByteArray("qlonglong"), "xlsx: a whole number is a number (527)");
        const QVariant type = sh.rows.value(1).value(rs.index.value(QStringLiteral("nms.message_type")));
        CHECK(type.toString() == QLatin1String("0X20"), "xlsx: a code (0X20) stays text");
        CHECK(sh.rows.value(0).value(0).toString() == QLatin1String("PC time"), "xlsx: the header row");
    }

    // ---- the window: what the tab holds, then what it gets ----
    {
        QVector<LogEntryPtr> faults;
        for (auto &e : b) if (e->text.startsWith(QLatin1String("@nmsflt_"))) faults << e;
        LogModel model(nullptr, 100000);
        // The one-fault frame first: the five-fault one then widens the table live.
        model.appendEntries({ faults.at(1) });
        NmsLogWindow w(NmsLog::Kind::Fault, &model, QStringLiteral("2_1"), QStringLiteral("Loco 2"));
        CHECK(w.table().rows.size() == 1, "window: the packet the tab already held");
        const int narrow = w.view()->model()->columnCount();
        model.appendEntries({ faults.at(0) });
        QCoreApplication::processEvents();
        CHECK(w.table().rows.size() == 2, "window: a packet the tab gets is added");
        CHECK(w.view()->model()->columnCount() > narrow
                  && w.view()->model()->data(w.view()->model()->index(0, w.table().index.value(QStringLiteral("fault[5] fault_id")))).toString().isEmpty()
                  && w.view()->model()->data(w.view()->model()->index(1, w.table().index.value(QStringLiteral("fault[5] fault_id")))).toString()
                         == QLatin1String("26 (safety error)"),
              "window: five faults widen the table; the earlier row's new cells are empty");
        CHECK(w.table().index.value(QStringLiteral("fault[5] fault_id")) < w.table().index.value(QStringLiteral("CRC")),
              "window: a column added live joins the body, not the end");
        model.appendEntries(b.mid(0, 50));
        CHECK(w.table().rows.size() == 2 + countOf(b.mid(0, 50), QStringLiteral("nmsflt")),
              "window: a batch of mixed packets adds only the faults");

        qint64 jumped = -1;
        QObject::connect(&w, &NmsLogWindow::jumpRequested, [&](const QString &, qint64 ms) { jumped = ms; });
        emit w.view()->doubleClicked(w.view()->model()->index(1, 0));
        CHECK(jumped == faults.at(0)->epochMs, "window: double-click asks for that frame in its tab");

        QTemporaryDir dir;
        const QString csvPath = dir.filePath(QStringLiteral("f.csv")), xlsxPath = dir.filePath(QStringLiteral("f.xlsx"));
        CHECK(w.saveCsv(csvPath) && w.saveXlsx(xlsxPath), "window: saves CSV and .xlsx");
        QFile cf(csvPath);
        const QByteArray csv = cf.open(QIODevice::ReadOnly) ? cf.readAll() : QByteArray();
        CHECK(csv.startsWith("\xEF\xBB\xBFPC time,"), "CSV file: UTF-8 with a BOM, for Excel");
        CHECK(XlsxBook::readFile(xlsxPath).value(0).rows.size() == w.table().rows.size() + 1, "xlsx file: every packet");

        model.clear();
        QCoreApplication::processEvents();
        CHECK(w.table().rows.isEmpty(), "window: the tab cleared, the table rebuilt (empty)");
    }
}

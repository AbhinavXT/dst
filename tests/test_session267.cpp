#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "nmslog.h"
#include "nmslogwindow.h"
#include "xlsxbook.h"

#include <QCoreApplication>
#include <QFile>
#include <QTableView>
#include <QTextStream>

// =============================================================================
//  Session 267 — the DL log (@dllog): LKAVACH's log message to the LDLU,
//  decoded from the schema and listed like the NMS logs.
//  NO REAL @dllog CAPTURE YET: schema/fixtures/dllog_synthetic.log
//  (tests/dlloggen) packs every sub-log type from the firmware's structs. Its
//  RFID-tag packets carry REAL tags, @rfid frames of
//  replay/loco_1_1_29062026_134509.cap without the reader id, and are
//  checked against those frames' own decode.
// =============================================================================

namespace {

QVector<LogEntryPtr> entriesOf(const QString &path)
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

QHash<QString, QString> rowsOf(const QString &line, QVector<FieldRow> *all = nullptr)
{
    const CaptureLine c = CaptureDecoder::parseLine(line);
    const QVector<FieldRow> rows = CaptureDecoder::describe(c);
    if (all) *all = rows;
    QHash<QString, QString> h;
    for (const FieldRow &r : rows) h.insert(r.field.trimmed(), r.value.trimmed());
    return h;
}

// The first line whose bytes 18 / 19 are this log / sub-log type.
QString lineOf(const QVector<LogEntryPtr> &es, int logType, int sub, int nth = 0)
{
    for (const LogEntryPtr &e : es) {
        const CaptureLine c = CaptureDecoder::parseLine(e->text);
        if (c.bytes.size() >= 21 && quint8(c.bytes.at(18)) == logType && quint8(c.bytes.at(19)) == sub && nth-- == 0)
            return e->text;
    }
    return QString();
}

QString cellAt(const NmsLog::Table &t, int row, const QString &column)
{
    return t.index.contains(column) ? t.cell(row, t.index.value(column)) : QStringLiteral("<no column>");
}

}  // namespace

TEST_SUITE(session267)
{
    const QVector<LogEntryPtr> es = entriesOf(QStringLiteral("../schema/fixtures/dllog_synthetic.log"));
    CHECK(es.size() == 49, "fixture: schema/fixtures/dllog_synthetic.log, 49 @dllog lines");

    // ---- the type ----
    bool allDl = !es.isEmpty();
    for (const LogEntryPtr &e : es) allDl = allDl && CaptureDecoder::parseLine(e->text).type == CapType::DlLog;
    CHECK(allDl, "every @dllog line is CapType::DlLog");
    CHECK(CaptureDecoder::typeFromToken(QStringLiteral("dl_log")) == CapType::DlLog, "@dl_log reads as the same type");

    // ---- the header ----
    {
        const QHash<QString, QString> h = rowsOf(es.value(0)->text);
        CHECK(h.value("msg.src_id") == QLatin1String("2 (LKAVACH)") && h.value("msg.dest_id") == QLatin1String("6 (LDLU)")
                  && h.value("msg.message_id") == QLatin1String("102 (LKAVACH_LDLU_LOG_MESSAGE)"),
              "header: LKAVACH -> LDLU, message 102");
        CHECK(h.value("msg.message_length") == QLatin1String("32") && h.value("frame len") == QLatin1String("32 B"),
              "header: message_length is the frame's length (32, periodic)");
        CHECK(h.value("log_time") == QLatin1String("13-10-26 10:00:00"), "header: log_time as sent (year - BASE_YEAR)");
        CHECK(h.value("abs_location") == QLatin1String("100000 m"), "header: abs_location");
        CHECK(h.value("log_type") == QLatin1String("3 (Periodic)") && h.value("sub_log_type") == QLatin1String("1 (LKAVACH periodic)")
                  && h.value("field_applicable") == QLatin1String("2 (LKAVACH_PERIODIC_EVENT)"),
              "header: periodic / LKAVACH periodic / LKAVACH_PERIODIC_EVENT");
        CHECK(h.value("loco_id") == QLatin1String("1") && h.value("tin") == QLatin1String("21")
                  && h.value("train_movement_direction") == QLatin1String("1 (nominal)")
                  && h.value("brake_type") == QLatin1String("0 (NO_OVER_SPEED_NO_BRAKES)"),
              "periodic: loco_id, tin, direction, brake type");
        CHECK(h.value("CRC") == QLatin1String("n/a"), "no CRC to check");
    }

    // ---- the bodies ----
    {
        const QHash<QString, QString> h = rowsOf(lineOf(es, 4, 1));
        CHECK(h.value("sub_log_type") == QLatin1String("1 (SPAD)") && h.value("station_id") == QLatin1String("3001")
                  && h.value("signal_info") == QLatin1String("74565") && h.value("ref_rfid") == QLatin1String("560")
                  && h.value("dist_from_ref_rfid") == QLatin1String("210 m"),
              "SPAD: station, signal_info (u32), ref RFID, distance");
    }
    {
        const QHash<QString, QString> h = rowsOf(lineOf(es, 4, 2));
        CHECK(h.value("collision_loco_id") == QLatin1String("2") && h.value("self_loco_dist_from_rfid") == QLatin1String("300 m"),
              "head-on: the other loco and this loco's distance");
    }
    {
        const QHash<QString, QString> gen = rowsOf(lineOf(es, 4, 9)), cancel = rowsOf(lineOf(es, 4, 10));
        CHECK(gen.value("sub_log_type") == QLatin1String("9 (Manual SOS generated)") && gen.value("sos_reason") == QLatin1String("1")
                  && cancel.value("sos_reason") == QLatin1String("2"),
              "manual SoS generated / cancelled: sos_reason raw (the DL_* values are not known)");
    }
    {
        const QHash<QString, QString> h = rowsOf(lineOf(es, 4, 7));
        CHECK(h.value("alarm_code") == QLatin1String("Head-On Collision with Loco"), "LP-OCIP: alarm bit 17 by name");
        CHECK(h.value("context_values") == QLatin1String("FSB will be Applied in XX s"), "LP-OCIP: context bit 10 by name");
        CHECK(h.value("popup_time") == QLatin1String("8 s") && h.value("popup_distance") == QLatin1String("1500 m")
                  && h.value("popup_speed") == QLatin1String("30 km/h") && h.value("popup_train_type") == QLatin1String("3 (PASSENGER_ICF)")
                  && h.value("popup_station_id") == QLatin1String("3001"),
              "LP-OCIP: the popup values after the two bit words");
    }
    {
        const QHash<QString, QString> h = rowsOf(lineOf(es, 1, 8));
        CHECK(h.value("brake_applied") == QLatin1String("3 (FULL_SERVICE_BRAKE)")
                  && h.value("start_time") == QLatin1String("13-10-2026 10:00:20")
                  && h.value("end_time") == QLatin1String("13-10-2026 10:00:27")
                  && h.value("speed") == QLatin1String("54 km/h") && h.value("distance") == QLatin1String("105 m"),
              "brake status: STRUCT_DATE_TIME times (u16 year), speed, distance");
    }
    {
        const QHash<QString, QString> h = rowsOf(lineOf(es, 1, 12));
        CHECK(h.value("previous_mode") == QLatin1String("1 (Stand_By)") && h.value("current_mode") == QLatin1String("2 (Staff_Responsible)"),
              "mode transition: previous then current");
    }
    {
        const QHash<QString, QString> h = rowsOf(lineOf(es, 1, 19));
        CHECK(h.value("gate_id") == QLatin1String("61") && h.value("gate_id_alpha") == QLatin1String("1")
                  && h.value("gate_type") == QLatin1String("0"),
              "LC gate: id, alpha (u16, LC_GATE_ID), type");
    }
    {
        QVector<FieldRow> all;
        const QHash<QString, QString> h = rowsOf(lineOf(es, 1, 1), &all);
        CHECK(h.value("sub_log_type") == QLatin1String("1 (Onboard comm fail)") && h.value("frame len") == QLatin1String("21 B")
                  && all.last().field.trimmed() == QLatin1String("field_applicable"),
              "comm fail: no handler in the firmware, so the header and nothing after it");
    }

    // ---- the RFID tag: the real @rfid frame's decode, without its reader id ----
    {
        QStringList rfidLines;
        QFile f(QStringLiteral("../replay/loco_1_1_29062026_134509.cap"));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            while (!ts.atEnd()) {
                const QString l = ts.readLine().trimmed();
                if (l.startsWith(QLatin1String("@rfid_"))) rfidLines << l;
            }
        }
        for (int nth = 0; nth < 2; ++nth) {
            QVector<FieldRow> all;
            const QHash<QString, QString> dl = rowsOf(lineOf(es, 1, 14, nth), &all);
            const QByteArray tag = CaptureDecoder::parseLine(lineOf(es, 1, 14, nth)).bytes.mid(21);
            QString src;
            for (const QString &l : rfidLines)
                if (CaptureDecoder::parseLine(l).bytes.mid(1) == tag) { src = l; break; }
            const QHash<QString, QString> rf = rowsOf(src);
            bool same = !src.isEmpty() && rf.contains(QStringLiteral("type"));
            int compared = 0;
            for (auto it = rf.constBegin(); it != rf.constEnd(); ++it) {
                const QString k = it.key();
                if (k == "type" || k == "reader_id" || k == "loco / ctrl" || k == "rtc" || k == "seq"
                    || k == "frame len" || k == "CRC")
                    continue;
                same = same && dl.value(QStringLiteral("tag.") + k) == it.value();
                ++compared;
            }
            CHECK(same && compared >= 5 && dl.value("tag.CRC-30").endsWith(QLatin1String("PASS"))
                      && !dl.contains(QStringLiteral("tag.reader_id")),
                  qPrintable(QStringLiteral("RFID tag %1: every row of the real @rfid frame's decode, as tag.*, CRC-30 PASS")
                                 .arg(nth + 1)));
            bool spans = true;
            for (const FieldRow &r : all)
                if (r.field.trimmed().startsWith(QLatin1String("tag.")) && r.hasSpan())
                    spans = spans && r.byteStart() >= 21 && r.byteEnd() < 37;
            CHECK(spans, qPrintable(QStringLiteral("RFID tag %1: its rows highlight bytes 21..36").arg(nth + 1)));
        }
    }

    // ---- the table ----
    const NmsLog::Table t = [&]() { NmsLog::Table x; for (auto &e : es) NmsLog::append(x, NmsLog::Kind::DlLog, *e); return x; }();
    CHECK(t.rows.size() == es.size(), "table: one row per @dllog line");
    CHECK(t.header(0) == QLatin1String("PC time") && t.header(t.columnCount() - 1) == QLatin1String("Frame (hex)"),
          "table: PC time first, Frame (hex) last");
    CHECK(t.groupAt(t.index.value(QStringLiteral("msg.message_length"))) == NmsLog::Group::Header
              && t.groupAt(t.index.value(QStringLiteral("log_type"))) == NmsLog::Group::Body,
          "table: msg.* are the header columns, log_type the body's");
    CHECK(t.index.value(QStringLiteral("log_type")) < t.index.value(QStringLiteral("loco_id"))
              && t.index.value(QStringLiteral("loco_id")) < t.index.value(QStringLiteral("station_id")),
          "table: log_type, then the fields in the order first seen");
    CHECK(cellAt(t, 0, QStringLiteral("station_id")).isEmpty() && cellAt(t, 0, QStringLiteral("loco_id")) == QLatin1String("1"),
          "table: a packet that does not carry a field leaves its cell empty");
    CHECK(t.index.contains(QStringLiteral("tag.CRC-30")) && t.index.contains(QStringLiteral("alarm_code"))
              && t.index.contains(QStringLiteral("start_time")) && t.index.contains(QStringLiteral("current_mode")),
          "table: a column for every sub-type's fields");
    CHECK(t.gaps == 0, "table: no gap marks (events are not periodic; seq_num is 0)");
    {
        int warn = 0, dangerous = 0;
        const int c = t.index.value(QStringLiteral("log_type"));
        for (int r = 0; r < t.rows.size(); ++r) {
            if (t.cell(r, c).contains(QLatin1String("(Dangerous)"))) ++dangerous;
            if (NmsLog::markOf(t.header(c), t.cell(r, c)) == NmsLog::Mark::Warn) ++warn;
        }
        CHECK(dangerous == 10 && warn == dangerous, "table: the 10 dangerous events marked, nothing else on log_type");
    }
    {
        NmsLog::Table x;
        auto alias = LogEntryPtr::create(*es.value(0));
        alias->text.replace(QLatin1String("@dllog_"), QLatin1String("@dl_log_"));
        CHECK(NmsLog::append(x, NmsLog::Kind::DlLog, *alias) && !NmsLog::append(x, NmsLog::Kind::Health, *es.value(0)),
              "table: @dl_log is taken; the health table refuses a @dllog");
    }
    {
        QString err;
        const QVector<XlsxBook::Sheet> back = XlsxBook::read(XlsxBook::write(NmsLog::toSheets(t, NmsLog::Kind::DlLog)), &err);
        CHECK(back.size() == 1 && back.at(0).name == QLatin1String("dllog") && back.at(0).rows.size() == t.rows.size() + 1,
              "xlsx: one sheet, dllog, every packet");
    }

    // ---- the window ----
    {
        LogModel model(nullptr, 100000);
        model.appendEntries(es.mid(0, 10));
        NmsLogWindow w(NmsLog::Kind::DlLog, &model, QStringLiteral("1_1"), QStringLiteral("Loco 1"));
        CHECK(w.windowTitle().startsWith(QLatin1String("DL log")) && w.table().rows.size() == 10,
              "window: titled DL log, the packets the tab held");
        model.appendEntries(es.mid(10));
        QCoreApplication::processEvents();
        CHECK(w.table().rows.size() == es.size(), "window: packets the tab gets are added");
    }
}

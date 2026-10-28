#include "testutil.h"

#include "logentry.h"
#include "logmodel.h"
#include "nmslog.h"
#include "nmslogwindow.h"
#include "uicolors.h"

#include <QBrush>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QTableView>
#include <QTextStream>

// =============================================================================
//  Session 250 — the NMS tables mark what a packet reports: a failure in the
//  error colour, an event in the warning colour, both bold, with the reason
//  in the tooltip. Real frames from replay/ (every capture, searched for the
//  cells that carry each case).
// =============================================================================

namespace {

QVector<LogEntryPtr> allNms(const QString &token)
{
    QVector<LogEntryPtr> out;
    const QDir d(QStringLiteral("../replay"));
    qint64 ms = 1782465000000;
    for (const QString &name : d.entryList({ QStringLiteral("*.cap") }, QDir::Files, QDir::Name)) {
        QFile f(d.filePath(name));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream ts(&f);
        while (!ts.atEnd()) {
            const QString line = ts.readLine().trimmed();
            if (!line.startsWith(QLatin1Char('@') + token + QLatin1Char('_'))) continue;
            auto e = LogEntryPtr::create();
            e->header.source_id = 21;
            e->header.kvchId = 1;
            e->epochMs = ms += 1000;
            e->severity = Severity::Info;
            e->text = line;
            e->cacheDerived();
            out << e;
        }
    }
    return out;
}

// The first row whose `column` is not empty and reads `value` (contains), or -1.
int rowWith(const NmsLog::Table &t, const QString &column, const QString &value)
{
    if (!t.index.contains(column)) return -1;
    const int c = t.index.value(column);
    for (int r = 0; r < t.rows.size(); ++r)
        if (!t.cell(r, c).isEmpty() && t.cell(r, c).contains(value)) return r;
    return -1;
}

NmsLog::Mark markAt(const NmsLog::Table &t, int row, const QString &column)
{
    return row < 0 ? NmsLog::Mark::None : NmsLog::markOf(column, t.cell(row, t.index.value(column)));
}

}  // namespace

TEST_SUITE(session250)
{
    using NmsLog::Mark;
    NmsLog::Table hl, fl;
    for (const LogEntryPtr &e : allNms(QStringLiteral("nmshlth"))) NmsLog::append(hl, NmsLog::Kind::Health, *e);
    const QVector<LogEntryPtr> faults = allNms(QStringLiteral("nmsflt"));
    for (const LogEntryPtr &e : faults) NmsLog::append(fl, NmsLog::Kind::Fault, *e);
    CHECK(hl.rows.size() > 10000 && fl.rows.size() > 100, "fixture: every @nmshlth and @nmsflt in replay/");

    // Real cells, each case.
    const struct { const char *column, *value; Mark mark; } cases[] = {
        { "RADIO_1_HEALTH",            "(OK)",             Mark::None },
        { "DMI_1_LINK_STATUS",         "(OK)",             Mark::None },
        { "DMI_1_LINK_STATUS",         "NOT OK",           Mark::Fail },
        { "RFID_READER_1_LINK_STATUS", "NOT OK",           Mark::Fail },
        { "REMAINING_KEY_NUMBERS",     "No keys",          Mark::Fail },
        { "REMAINING_KEY_NUMBERS",     "10 key sets left", Mark::None },
        { "ACTIVE_GPS_NUMBER",         "No Active GPS",    Mark::Warn },
        { "ACTIVE_GPS_NUMBER",         "Both GPS",         Mark::None },
        { "GPS_1_VIEW_STATUS",         "No Data",          Mark::Warn },
        { "BRAKE_APPLICATION_REASON",  "Overspeed",        Mark::Warn },
        { "LOCO_SELF_SOS",             "Unusual stop",     Mark::Warn },
        { "COLLISION_DETECTION",       "loco",             Mark::Warn },
        { "DUPLICATE_MISSING_RFID",    "",                 Mark::Warn },
        { "EB_BYPASSED",               "EB Connected",     Mark::None },
        { "BIU_ISOLATED",              "Connected",        Mark::None },
        { "TRAIN_CONFIG_CHANGE",       "No",               Mark::None },
        { "RADIO_1_FORWARD_POWER",     "0.0 W",            Mark::None },
        { "CRC",                       "PASS",             Mark::None },
    };
    // A fault's code type reads Faulty (0x01, red) or Recovered (0x02, green), on real @nmsflt rows.
    {
        int faulty = 0, recovered = 0;
        bool marked = true;
        for (int c = 0; c < fl.columns.size(); ++c) {
            const QString col = fl.header(c);
            if (!col.startsWith(QLatin1String("fault[")) || !col.endsWith(QLatin1String("code_type"))) continue;
            for (int r = 0; r < fl.rows.size(); ++r) {
                const QString v = fl.cell(r, c);
                if (v.startsWith(QLatin1String("0X01"))) { ++faulty; marked = marked && v == QLatin1String("0X01 (Faulty)") && markAt(fl, r, col) == Mark::Fail; }
                if (v.startsWith(QLatin1String("0X02"))) { ++recovered; marked = marked && v == QLatin1String("0X02 (Recovered)") && markAt(fl, r, col) == Mark::Ok; }
            }
        }
        CHECK(faulty > 0 && recovered > 0 && marked,
              qPrintable(QStringLiteral("code types: %1 Faulty, %2 Recovered, all marked").arg(faulty).arg(recovered)));
        CHECK(NmsLog::faultStatus(3).isEmpty(), "another code type reads as it is");
    }
    for (const auto &k : cases) {
        const int r = rowWith(hl, QLatin1String(k.column), QLatin1String(k.value));
        CHECK(r >= 0 && markAt(hl, r, QLatin1String(k.column)) == k.mark,
              qPrintable(QStringLiteral("health: %1 '%2' -> %3").arg(k.column, k.value)
                             .arg(k.mark == Mark::Fail ? "fail" : k.mark == Mark::Warn ? "warn" : "none")));
    }
    {
        const int r = rowWith(fl, QStringLiteral("fault[1] fault_id"), QStringLiteral("("));
        CHECK(markAt(fl, r, QStringLiteral("fault[1] fault_id")) == Mark::Warn, "fault: a reported fault_id -> warn");
        CHECK(markAt(fl, r, QStringLiteral("fault[1] module")) == Mark::None
                  && markAt(fl, r, QStringLiteral("reporting_subsystem")) == Mark::None,
              "fault: the module and subsystem are not marked");
    }
    // Labels the replay captures do not carry: the rule, from the decoder's text.
    QString why;
    CHECK(NmsLog::markOf(QStringLiteral("CRC"), QStringLiteral("FAIL"), &why) == Mark::Fail && why.contains("CRC"),
          "CRC FAIL -> fail, and says so");
    CHECK(NmsLog::markOf(QStringLiteral("RADIO_2_HEALTH"), QStringLiteral("3  (Radio Fail)")) == Mark::Fail,
          "Radio Fail -> fail");
    CHECK(NmsLog::markOf(QStringLiteral("RADIO_2_HEALTH (2)"), QStringLiteral("2  (Diagnostic Link Fail)")) == Mark::Fail,
          "a repeated column name is the same event");
    CHECK(NmsLog::markOf(QStringLiteral("REMAINING_KEY_NUMBERS"), QStringLiteral("4  (4 key sets left)")) == Mark::Warn,
          "fewer than 5 key sets -> warn");
    CHECK(NmsLog::markOf(QStringLiteral("GPS_1_LINK_STATUS"), QStringLiteral("2  (link ok / PPS fail)")) == Mark::Warn
              && NmsLog::markOf(QStringLiteral("GPS_1_LINK_STATUS"), QStringLiteral("3  (link ok / PPS ok)")) == Mark::None,
          "GPS link: a failure -> warn, ok -> none");
    CHECK(NmsLog::markOf(QStringLiteral("ONBOARD_KAVACH_MODULES_HEALTH"), QStringLiteral("33  (module 2 (VCC Mc-2) = NOT OK)")) == Mark::Fail,
          "module NOT OK -> fail");
    CHECK(NmsLog::markOf(QStringLiteral("DMI_1_LINK_STATUS"), QString()) == Mark::None, "an empty cell -> none");

    // The window: colour, bold and tooltip.
    {
        LogModel model(nullptr, 1000);
        QVector<LogEntryPtr> some;
        int notOk = -1;
        for (const LogEntryPtr &e : allNms(QStringLiteral("nmshlth"))) {
            NmsLog::Table one;
            NmsLog::append(one, NmsLog::Kind::Health, *e);
            if (rowWith(one, QStringLiteral("DMI_1_LINK_STATUS"), QStringLiteral("NOT OK")) == 0) { some << e; notOk = some.size() - 1; break; }
        }
        CHECK(notOk == 0, "fixture: a health packet with DMI_1_LINK_STATUS NOT OK");
        if (notOk != 0) return;
        model.appendEntries(some);
        NmsLogWindow w(NmsLog::Kind::Health, &model, QStringLiteral("1_1"), QStringLiteral("L1"));
        QAbstractItemModel *m = w.view()->model();
        const QModelIndex bad = m->index(0, w.table().index.value(QStringLiteral("DMI_1_LINK_STATUS")));
        const QModelIndex fine = m->index(0, w.table().index.value(QStringLiteral("RADIO_1_HEALTH")));
        CHECK(bad.data(Qt::ForegroundRole).value<QBrush>().color() == UiColor::error(), "window: NOT OK in the error colour");
        CHECK(bad.data(Qt::FontRole).value<QFont>().bold(), "window: and bold");
        CHECK(bad.data(Qt::ToolTipRole).toString().contains(QLatin1String("NOT OK"))
                  && bad.data(Qt::ToolTipRole).toString().contains(QLatin1String("reported")),
              "window: the tooltip has the value and what was reported");
        CHECK(!fine.data(Qt::ForegroundRole).isValid() && !fine.data(Qt::FontRole).isValid(),
              "window: an OK cell is left as it is");
        CHECK(bad.data(Qt::DisplayRole).toString().contains(QLatin1String("NOT OK")), "window: the text is unchanged");
    }
}

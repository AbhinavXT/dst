#include "testutil.h"

#include "logentry.h"
#include "logmodel.h"
#include "nmslog.h"
#include "nmslogwindow.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QFile>
#include <QLabel>
#include <QTableView>
#include <QTextStream>

// =============================================================================
//  Session 251 — NMS tables: "Changes only". A packet is shown when any of its
//  events reads differently from the last time that event was carried, or its
//  CRC failed. Real frames: replay/loco_1_1_29062026_134509.cap (2081 health
//  packets, DMI links going NOT OK and back) and the fault log of
//  replay/loco_1_1_26062026_162418.cap.
// =============================================================================

namespace {

QVector<LogEntryPtr> entries(const QString &path, const QString &token)
{
    QVector<LogEntryPtr> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    qint64 ms = 1782465000000;
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
    return out;
}

// The rule, worked out again from the finished table by column name: which
// rows does "changes only" keep? Event columns are those after nms.time.
// (Since session 252 the gap rows too: those are checked in its own suite.)
QVector<bool> expectedChanged(const NmsLog::Table &t)
{
    const int firstEvent = t.index.value(QStringLiteral("nms.time")) + 1;
    const int crc = t.index.value(QStringLiteral("CRC"));
    QHash<int, QString> last;
    QVector<bool> out;
    for (int r = 0; r < t.rows.size(); ++r) {
        // Session 252: a gap before the row (packets missing) is a change too.
        bool ch = t.cell(r, crc) == QLatin1String("FAIL") || !t.gapColumn.value(r).isEmpty();
        for (int c = firstEvent; c < t.columnCount() - 1; ++c) {
            const QString v = t.cell(r, c);
            if (v.isEmpty()) continue;
            if (!last.contains(c) || last.value(c) != v) ch = true;
            last.insert(c, v);
        }
        out << ch;
    }
    return out;
}

}  // namespace

TEST_SUITE(session251)
{
    const QVector<LogEntryPtr> hl = entries(QStringLiteral("../replay/loco_1_1_29062026_134509.cap"), QStringLiteral("nmshlth"));
    CHECK(hl.size() == 2081, "fixture: 2081 @nmshlth packets");

    NmsLog::Table t;
    for (const LogEntryPtr &e : hl) NmsLog::append(t, NmsLog::Kind::Health, *e);
    const QVector<bool> want = expectedChanged(t);
    CHECK(t.changed == want, "the table's 'changed' matches the rule worked out again by column");
    const int nChanged = int(std::count(want.begin(), want.end(), true));
    CHECK(t.changed.value(0) && nChanged > 1 && nChanged < t.rows.size() / 2,
          qPrintable(QStringLiteral("the first packet is a change; %1 of %2 packets are").arg(nChanged).arg(t.rows.size())));

    // The window: open on half, the rest arrives live, then toggle.
    LogModel model(nullptr, 100000);
    model.appendEntries(hl.mid(0, 1000));
    NmsLogWindow w(NmsLog::Kind::Health, &model, QStringLiteral("1_1"), QStringLiteral("L1"));
    model.appendEntries(hl.mid(1000));
    QCoreApplication::processEvents();
    CHECK(w.table().changed == want, "built in two batches: the same 'changed' (the state carries over)");
    QCheckBox *box = w.findChild<QCheckBox *>(QStringLiteral("nmsChangesOnly"));
    CHECK(box && !box->isChecked() && w.view()->model()->rowCount() == 2081, "off at open: every packet shown");
    if (!box) return;
    box->setChecked(true);
    CHECK(w.view()->model()->rowCount() == nChanged, "Changes only: just the changed packets");
    QLabel *count = nullptr;
    for (QLabel *l : w.findChildren<QLabel *>()) if (l->text().contains(QLatin1String("packets"))) count = l;
    CHECK(count && count->text().startsWith(QStringLiteral("2081 packets, %1 shown").arg(nChanged)),   // then "; 4 gaps" (252)
          qPrintable("the count says how many are shown: " + (count ? count->text() : QString())));

    // A DMI link going NOT OK is a change; the row after it, back to OK, too.
    {
        const int dmi = t.index.value(QStringLiteral("DMI_1_LINK_STATUS"));
        int notOk = -1;
        for (int r = 0; r < t.rows.size() && notOk < 0; ++r)
            if (t.cell(r, dmi).contains(QLatin1String("NOT OK"))) notOk = r;
        CHECK(notOk > 0 && want.value(notOk), "a DMI link going NOT OK is shown");
        bool shown = false;
        for (int v = 0; v < w.view()->model()->rowCount(); ++v)
            if (w.tableRow(w.view()->model()->index(v, 0)) == notOk) shown = true;
        CHECK(shown, "and the window shows that row");
    }

    // Double-click a filtered row: the frame of that packet, not of the view row.
    {
        qint64 jumped = -1;
        QObject::connect(&w, &NmsLogWindow::jumpRequested, [&](const QString &, qint64 ms) { jumped = ms; });
        const QModelIndex second = w.view()->model()->index(1, 0);
        const int r = w.tableRow(second);
        emit w.view()->doubleClicked(second);
        CHECK(r > 1 && jumped == w.table().epochMs.at(r), "double-click on a filtered row: that packet's time");
    }

    // A packet arriving while filtered: shown only if it changed something.
    {
        const int before = w.view()->model()->rowCount();
        model.appendEntries({ hl.last() });          // the same events as the packet before it
        QCoreApplication::processEvents();
        CHECK(w.table().rows.size() == 2082 && !w.table().changed.last() && w.view()->model()->rowCount() == before,
              "a repeat of the last packet arrives: added, not shown");
    }
    box->setChecked(false);
    CHECK(w.view()->model()->rowCount() == 2082 && count && count->text().startsWith(QLatin1String("2082 packets"))
              && !count->text().contains(QLatin1String("shown")),
          "off again: every packet, and the count without 'shown'");

    // Fault log: every packet that reports a different set of faults.
    {
        const QVector<LogEntryPtr> fl = entries(QStringLiteral("../replay/loco_1_1_26062026_162418.cap"), QStringLiteral("nmsflt"));
        NmsLog::Table f;
        for (const LogEntryPtr &e : fl) NmsLog::append(f, NmsLog::Kind::Fault, *e);
        CHECK(f.rows.size() == 21 && f.changed == expectedChanged(f), "fault log: the same rule");
    }
}

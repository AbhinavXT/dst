#include "testutil.h"

#include "logentry.h"
#include "logmodel.h"
#include "nmslog.h"
#include "nmslogwindow.h"
#include "xlsxbook.h"

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QFile>
#include <QLabel>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextStream>

// =============================================================================
//  Session 256 — NMS tables: the find and filter bar. Words (every one must
//  appear, any case, "_" as a space) in one column or the whole row; matches
//  tinted with Next / Previous, or only the matching rows; Marked rows only.
//  Export asks: what is shown, or everything. Real frames from replay/.
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

// Rows where some cell (lower-cased) contains `text`: worked out here.
QVector<int> rowsContaining(const NmsLog::Table &t, const QString &text, const QString &column = QString())
{
    QVector<int> out;
    for (int r = 0; r < t.rows.size(); ++r)
        for (int c = 0; c < t.columnCount(); ++c) {
            if (!column.isEmpty() && t.header(c) != column) continue;
            if (t.cell(r, c).toLower().contains(text)) { out << r; break; }
        }
    return out;
}

void settle()
{
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
}

}  // namespace

TEST_SUITE(session256)
{
    CHECK(NmsLog::findWords(QStringLiteral("  DMI_1  Link ")) == QStringList({ "dmi", "1", "link" }),
          "words: lower-case, '_' a space, blanks dropped");
    CHECK(NmsLog::cellMatches(QStringLiteral("0  (NOT OK)"), NmsLog::findWords(QStringLiteral("not ok")))
              && !NmsLog::cellMatches(QStringLiteral("1  (OK)"), NmsLog::findWords(QStringLiteral("not ok"))),
          "a cell matches when every word is in it");

    const QVector<LogEntryPtr> hl = entries(QStringLiteral("../replay/loco_1_1_29062026_134509.cap"), QStringLiteral("nmshlth"));
    NmsLog::Table all;
    for (const LogEntryPtr &e : hl) NmsLog::append(all, NmsLog::Kind::Health, *e);
    const QVector<int> notOk = rowsContaining(all, QStringLiteral("not ok"));
    CHECK(hl.size() == 2081 && notOk.size() >= 7, qPrintable(QStringLiteral("fixture: %1 packets with a NOT OK").arg(notOk.size())));
    {
        bool same = true;
        for (int r = 0; r < all.rows.size(); ++r)
            same = same && NmsLog::rowMatches(all, r, NmsLog::findWords(QStringLiteral("not ok"))) == notOk.contains(r);
        CHECK(same, "rowMatches (whole row) finds exactly those rows");
        // Words in different cells of one row: "dmi_1 not" is not one cell's text.
        CHECK(NmsLog::rowMatches(all, notOk.first(), NmsLog::findWords(QStringLiteral("2026-06-29 not ok"))),
              "the words may be in different cells of the row");
    }

    LogModel model(nullptr, 100000);
    model.appendEntries(hl.mid(0, 2045));   // NOT OK in packets 2036 and 2042, then more
    NmsLogWindow w(NmsLog::Kind::Health, &model, QStringLiteral("1_1"), QStringLiteral("L1"));
    w.resize(1100, 600);
    w.show();
    settle();
    QAbstractItemModel *vm = w.view()->model();

    // ---- find: tint, count, next / previous ----
    w.setFindText(QStringLiteral("not ok"));
    QVector<int> want;
    for (int r : notOk) if (r < 2045) want << r;
    CHECK(w.findMatches() == want.size() && vm->rowCount() == 2045 && want.size() >= 2,
          "find: every NOT OK packet counted, nothing hidden");
    if (want.size() < 2) return;
    const int dmi = w.table().index.value(QStringLiteral("DMI_1_LINK_STATUS"));
    CHECK(vm->index(want.first(), dmi).data(Qt::BackgroundRole).value<QBrush>().style() != Qt::NoBrush,
          "the matching cell is tinted");
    CHECK(vm->index(want.first() + 1, dmi).data().toString() == QLatin1String("1  (OK)")
              && !vm->index(want.first() + 1, dmi).data(Qt::BackgroundRole).isValid(),
          "\"1 (OK)\" holds 'ok' but not 'not': not tinted");
    w.findNext();
    auto *count = w.findChild<QLabel *>(QStringLiteral("nmsFindCount"));
    CHECK(w.findCurrent() == want.first() && count && count->text() == QStringLiteral("1 of %1 matching packets").arg(want.size()),
          qPrintable("Next: the first match, '1 of N': " + (count ? count->text() : QString())));
    CHECK(w.tableRow(w.view()->currentIndex()) == want.first(), "and the view's current row is that packet");
    CHECK(w.view()->currentIndex().data().toString().toLower().contains(QLatin1String("not ok")),
          qPrintable("and its current cell is the match (all columns): " + w.view()->currentIndex().data().toString()));
    // With columns frozen, the match is scrolled clear of them.
    w.freezeColumns(6);
    settle();
    w.findNext();
    CHECK(w.findCurrent() == want.value(1), "Next again: the second");
    {
        auto *frozen = w.findChild<QTableView *>(QStringLiteral("frozenColumns"));
        const QRect cell = w.view()->visualRect(w.view()->currentIndex());
        CHECK(frozen && frozen->isVisible() && cell.left() >= frozen->width() && cell.right() <= w.view()->viewport()->width(),
              qPrintable(QStringLiteral("frozen columns: the match is scrolled clear of them (cell at %1, frozen to %2)")
                             .arg(cell.left()).arg(frozen ? frozen->width() : -1)));
    }
    w.freezeColumns(0);
    w.findPrevious();
    w.findPrevious();
    CHECK(w.findCurrent() == want.last(), "Previous from the first: wraps to the last");

    // ---- one column ----
    {
        // "ok": every packet has an OK somewhere (the radios), but only some
        // carry the DMI link at all.
        w.setFindText(QStringLiteral("ok"));
        const int everywhere = w.findMatches();
        w.setFindColumn(QStringLiteral("DMI_1_LINK_STATUS"));
        const QVector<int> inDmi = rowsContaining(w.table(), QStringLiteral("ok"), QStringLiteral("DMI_1_LINK_STATUS"));
        CHECK(everywhere == 2045 && w.findMatches() == inDmi.size() && !inDmi.isEmpty() && inDmi.size() < 100,
              qPrintable(QStringLiteral("in one column: only its matches (%1, of %2 anywhere)").arg(inDmi.size()).arg(everywhere)));
        const int radio = w.table().index.value(QStringLiteral("RADIO_1_HEALTH"));
        CHECK(!vm->index(inDmi.first(), radio).data(Qt::BackgroundRole).isValid()
                  && vm->index(inDmi.first(), dmi).data(Qt::BackgroundRole).isValid(),
              "only that column's cells are tinted");
        w.setFindColumn(QString());
        // Words spread across cells: "14:20:52 not ok" is in no one cell of
        // packet 2036, but in the row; those cells are tinted.
        w.setFindText(QStringLiteral("14:20:52 not ok"));
        const int rtc = w.table().index.value(QStringLiteral("rtc"));
        CHECK(w.findMatches() == 1 && vm->index(2035, rtc).data(Qt::BackgroundRole).isValid()
                  && vm->index(2035, dmi).data(Qt::BackgroundRole).isValid()
                  && !vm->index(2036, rtc).data(Qt::BackgroundRole).isValid(),
              "words across cells: that row's cells holding a word are tinted, no other row's");
        w.setFindText(QStringLiteral("not ok"));
    }

    CHECK(!NmsLog::rowMatches(w.table(), 0, NmsLog::findWords(QStringLiteral("ok")), -2),
          "a column the table does not have (-2): no match, not the whole row");

    // ---- only the matching rows ----
    w.setFilterRows(true);
    settle();
    CHECK(vm->rowCount() == want.size(), "Only matching rows: just those packets");
    // Live: the rest arrives, its NOT OK rows join.
    model.appendEntries(hl.mid(2045));
    settle();
    CHECK(notOk.size() > want.size() && vm->rowCount() == notOk.size() && w.findMatches() == notOk.size(),
          qPrintable(QStringLiteral("live: new matching packets are shown and counted (%1)").arg(notOk.size())));
    QCheckBox *changes = w.findChild<QCheckBox *>(QStringLiteral("nmsChangesOnly"));
    changes->setChecked(true);
    settle();
    int both = 0;
    for (int r : notOk) if (w.table().changed.at(r)) ++both;
    CHECK(vm->rowCount() == both && w.findMatches() == both, "with Changes only: both must hold");
    changes->setChecked(false);
    w.setFilterRows(false);
    w.setFindText(QString());
    settle();
    CHECK(vm->rowCount() == 2081 && w.findMatches() == 0 && count->text().isEmpty(), "cleared: every packet, no count");

    // ---- marked rows only ----
    {
        int marked = 0;
        for (int r = 0; r < w.table().rows.size(); ++r) if (NmsLog::rowMarked(w.table(), r)) ++marked;
        w.setMarkedOnly(true);
        settle();
        CHECK(marked > 0 && marked < 2081 && vm->rowCount() == marked,
              qPrintable(QStringLiteral("Marked rows only: the %1 packets with a bold cell").arg(marked)));
        bool each = true;
        for (int v = 0; v < vm->rowCount(); ++v) {
            const int r = w.tableRow(vm->index(v, 0));
            bool bold = false;
            for (int c = 0; c < vm->columnCount(); ++c) bold = bold || vm->index(v, c).data(Qt::FontRole).isValid();
            each = each && bold;
            Q_UNUSED(r);
        }
        CHECK(each, "every row shown has a bold cell");
        w.setMarkedOnly(false);
    }

    // ---- export: what is shown ----
    {
        w.setFindText(QStringLiteral("not ok"));
        w.setFilterRows(true);
        w.setColumnShown(QStringLiteral("seq"), false);
        settle();
        QTemporaryDir dir;
        const QString shownPath = dir.filePath(QStringLiteral("shown.csv")), allPath = dir.filePath(QStringLiteral("all.csv"));
        CHECK(w.saveCsv(shownPath, true) && w.saveCsv(allPath, false), "export: shown and everything, saved");
        QFile a(shownPath), b(allPath);
        a.open(QIODevice::ReadOnly);
        b.open(QIODevice::ReadOnly);
        const QList<QByteArray> shown = a.readAll().split('\n'), every = b.readAll().split('\n');
        CHECK(shown.size() - 2 == notOk.size() && every.size() - 2 == 2081,
              qPrintable(QStringLiteral("rows: %1 shown, %2 in all").arg(shown.size() - 2).arg(every.size() - 2)));
        CHECK(!shown.first().contains(",seq,") && every.first().contains(",seq,"), "columns: the hidden seq only in 'everything'");
        const QString xlsx = dir.filePath(QStringLiteral("shown.xlsx"));
        CHECK(w.saveXlsx(xlsx, true) && XlsxBook::readFile(xlsx).value(0).rows.size() == notOk.size() + 1,
              "xlsx: what is shown");
        w.setColumnShown(QStringLiteral("seq"), true);
    }

    // ---- the fault log: by fault ----
    {
        const QVector<LogEntryPtr> fl = entries(QStringLiteral("../replay/loco_1_1_26062026_162418.cap"), QStringLiteral("nmsflt"));
        LogModel m2(nullptr, 1000);
        m2.appendEntries(fl);
        NmsLogWindow w2(NmsLog::Kind::Fault, &m2, QStringLiteral("1_1"), QStringLiteral("L1"));
        w2.setFindText(QStringLiteral("safety_error"));
        w2.setFindColumn(QStringLiteral("fault[1] fault_id"));
        w2.setFilterRows(true);
        settle();
        const QVector<int> safety = rowsContaining(w2.table(), QStringLiteral("safety error"), QStringLiteral("fault[1] fault_id"));
        CHECK(!safety.isEmpty() && w2.view()->model()->rowCount() == safety.size(),
              qPrintable(QStringLiteral("fault log, fault[1] fault_id 'safety_error': %1 packets").arg(safety.size())));
    }
}

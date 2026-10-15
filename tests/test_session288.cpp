#include "testutil.h"

#include "logmodel.h"
#include "logtableview.h"
#include "settings.h"
#include "uicolors.h"

#include <QBrush>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QTableView>
#include <QTemporaryDir>

// =============================================================================
//  Session 288 — the Δt type column: time since the previous line of the
//  same kind (capture tag), ▲ when more than twice that kind's usual gap.
//  Abhinav chose "Δt column, but different for each type". Real lines:
//  replay/loco_1_1_27062026_140226.cap, timed by their own timestamps.
// =============================================================================

namespace {
struct Line { QString tag; qint64 ms; QString text; };

QVector<Line> realLines(int max)
{
    QVector<Line> out;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd() && out.size() < max) {
        const QString l = QString::fromUtf8(f.readLine()).trimmed();
        const QStringList parts = l.split(QLatin1Char(' '));
        if (!l.startsWith(QLatin1Char('@')) || parts.size() < 3) continue;
        const QDateTime t = QDateTime::fromString(parts.at(1), Qt::ISODate);
        if (!t.isValid()) continue;
        out.push_back({ parts.at(0), t.toMSecsSinceEpoch(), l });
    }
    return out;
}

LogEntryPtr entry(const QString &text, qint64 ms)
{
    LogEntryPtr e(new LogEntry);
    e->text = text;
    e->epochMs = ms;
    return e;
}

QString cell(const LogModel &m, int row, int role = Qt::DisplayRole)
{
    return m.data(m.index(row, LogModel::ColTypeGap), role).toString();
}
}  // namespace

TEST_SUITE(session288)
{
    const QByteArray savedPkg = qgetenv("DLCONSOLE_PACKAGE");
    QTemporaryDir where;
    const QString exe = where.filePath(QStringLiteral("DLConsole.exe"));
    { QFile f(exe); CHECK(f.open(QIODevice::WriteOnly), "a stand-in for the .exe"); }
    qputenv("DLCONSOLE_PACKAGE", exe.toLocal8Bit());

    LogModel m;
    CHECK(m.columnCount() == 7 && LogModel::ColMessage == m.columnCount() - 1,
          "seven columns, Message still the last (it stretches)");
    CHECK(m.headerData(LogModel::ColTypeGap, Qt::Horizontal, Qt::DisplayRole).toString() == QStringLiteral("Δt type"),
          "header: Δt type");
    CHECK(!m.headerData(LogModel::ColTypeGap, Qt::Horizontal, Qt::ToolTipRole).toString().isEmpty(), "and a header tooltip");

    const QVector<Line> lines = realLines(600);
    CHECK(lines.size() == 600, "fixture: 600 real capture lines");
    QVector<LogEntryPtr> batch;
    for (const Line &l : lines) batch << entry(l.text, l.ms);
    m.appendEntries(batch);

    // Each row's gap: the time since the previous row of the same tag,
    // worked out here independently of the model.
    QHash<QString, qint64> last;
    int firsts = 0, gapsRight = 0, gapsSeen = 0;
    bool firstsBlank = true;
    for (int i = 0; i < lines.size(); ++i) {
        const Line &l = lines.at(i);
        if (!last.contains(l.tag)) {
            ++firsts;
            firstsBlank = firstsBlank && cell(m, i).isEmpty()
                          && cell(m, i, Qt::ToolTipRole).startsWith(QStringLiteral("The first ") + l.tag);
        } else {
            ++gapsSeen;
            gapsRight += m.entryAt(i)->typeGapMs == l.ms - last.value(l.tag) ? 1 : 0;
        }
        last[l.tag] = l.ms;
    }
    CHECK(firsts == last.size() && firstsBlank,
          QByteArray("the first line of each of the ") + QByteArray::number(firsts) + " tags: blank, tooltip says so");
    CHECK(gapsSeen > 0 && gapsRight == gapsSeen,
          QByteArray("every other line: the gap to its own tag's previous line (") + QByteArray::number(gapsRight)
              + "/" + QByteArray::number(gapsSeen) + ")");

    // A steady real stream is not late: no ▲ anywhere in it.
    int late = 0;
    for (int i = 0; i < m.rowCount(); ++i) late += m.entryAt(i)->typeGapLate ? 1 : 0;
    CHECK(late == 0, QByteArray("the real capture as recorded: nothing marked late (") + QByteArray::number(late) + ")");

    // A real gap: at 14:06:20 every periodic kind in this capture had been
    // silent for 16 s. The @dmi_1_1 line that ends it is late; the steady
    // stream before it is not.
    {
        const QVector<Line> more = realLines(20000);
        LogModel whole;
        QVector<LogEntryPtr> all;
        for (const Line &l : more) all << entry(l.text, l.ms);
        whole.appendEntries(all);
        int gapRow = -1, dmiLateBefore = 0;
        for (int i = 0; i < more.size(); ++i) {
            if (more.at(i).tag != QLatin1String("@dmi_1_1")) continue;
            if (more.at(i).text.contains(QStringLiteral("T14:06:20"))) { gapRow = i; break; }
            dmiLateBefore += whole.entryAt(i)->typeGapLate ? 1 : 0;
        }
        CHECK(gapRow >= 0 && whole.entryAt(gapRow)->typeGapMs == 16000 && whole.entryAt(gapRow)->typeGapLate,
              "the real 16 s gap at 14:06:20: @dmi_1_1 marked late, 16.000 s");
        CHECK(dmiLateBefore == 0, "the @dmi_1_1 lines before it: none late");
    }

    // The same lines with eight seconds of one tag taken out: its next line
    // is late, and only it.
    const QString victim = lines.at(lines.size() / 2).tag;
    const qint64 cutFrom = lines.at(lines.size() / 2).ms, cutTo = cutFrom + 8000;
    LogModel cut;
    QVector<LogEntryPtr> kept;
    int lateRow = -1;
    for (const Line &l : lines) {
        if (l.tag == victim && l.ms >= cutFrom && l.ms < cutTo) continue;
        kept << entry(l.text, l.ms);
        if (lateRow < 0 && l.tag == victim && l.ms >= cutTo) lateRow = kept.size() - 1;
    }
    cut.appendEntries(kept);
    int lateCut = 0;
    for (int i = 0; i < cut.rowCount(); ++i) lateCut += cut.entryAt(i)->typeGapLate ? 1 : 0;
    CHECK(lateRow >= 0 && cut.entryAt(lateRow)->typeGapLate && lateCut == 1,
          QByteArray("eight seconds of ") + victim.toUtf8() + " missing: the line after is late, no other");
    CHECK(lateRow >= 0 && cell(cut, lateRow).startsWith(QStringLiteral("▲ ")), "drawn with ▲");
    CHECK(lateRow >= 0 && cut.data(cut.index(lateRow, LogModel::ColTypeGap), Qt::ForegroundRole).value<QBrush>().color() == UiColor::warning(),
          "in the warning colour");
    CHECK(lateRow >= 0 && cell(cut, lateRow, Qt::ToolTipRole).contains(QStringLiteral("may be missing")),
          "tooltip: lines of this kind may be missing");

    // Lines that are not capture lines are one kind of their own.
    LogModel plain;
    plain.appendEntry(entry(QStringLiteral("hello"), 1000));
    plain.appendEntry(entry(QStringLiteral("@dmi_1_1 x"), 1500));
    plain.appendEntry(entry(QStringLiteral("world"), 2250));
    CHECK(cell(plain, 2) == QStringLiteral("1.250 s"), "plain text: the gap to the previous plain line, 1.250 s");
    CHECK(cell(plain, 1).isEmpty(), "the first @dmi_1_1: blank");
    plain.appendEntry(entry(QStringLiteral("again"), 2250 + 75'000));
    CHECK(cell(plain, 3) == QStringLiteral("1 min 15 s"), "a minute or more: \"1 min 15 s\"");

    // Two lines in the same millisecond: 0.000 s, not the first.
    plain.appendEntry(entry(QStringLiteral("again"), 2250 + 75'000));
    CHECK(cell(plain, 4) == QStringLiteral("0.000 s"), "the same millisecond: 0.000 s");

    // An entry shared with a second model keeps the gap its first model saw.
    LogModel other;
    other.appendEntry(plain.entryAt(2));
    CHECK(other.entryAt(0)->typeGapMs == 1250, "shared with another table: the same gap");

    // Clearing forgets the kinds.
    plain.clear();
    plain.appendEntry(entry(QStringLiteral("hello"), 9000));
    CHECK(cell(plain, 0).isEmpty(), "after Clear: the first line again");

    // Hidden unless turned on; widths saved before the column keep theirs.
    QTableView view;
    view.setModel(&m);
    CHECK(!Settings::showTypeGap(), "a fresh INI: Δt type off");
    LogTableView::applyColumnVisibility(&view, true);
    CHECK(view.isColumnHidden(LogModel::ColTypeGap), "hidden by default");
    Settings::setShowTypeGap(true);
    LogTableView::applyColumnVisibility(&view, true);
    CHECK(!view.isColumnHidden(LogModel::ColTypeGap) && !view.isColumnHidden(LogModel::ColMessage),
          "turned on: shown, Message too");
    Settings::setShowTypeGap(false);

    Settings::setLogColumnWidths({ 111, 0, 0, 0, 0, 0 });   // six: written before session 288
    LogTableView::applyColumnWidths(&view);
    CHECK(view.columnWidth(LogModel::ColTime) == 111, "six saved widths still apply to their columns");

    if (savedPkg.isNull()) qunsetenv("DLCONSOLE_PACKAGE");
    else qputenv("DLCONSOLE_PACKAGE", savedPkg);
}

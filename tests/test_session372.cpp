#include "testutil.h"

#include "colorrules.h"
#include "dmichanges.h"
#include "dmipanel.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "sessionfile.h"
#include "sessionwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDockWidget>
#include <QFile>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QToolBar>

#include <limits>

// =============================================================================
//  Session 372 — what changed on the DMI: each @dmi frame of a loco compared
//  with the one before (DmiDiff, session 371), one entry per frame that
//  changed something, in a dock of the recorded-session window; and
//  Previous / Next DMI change on the Playback bar. Real frames: the
//  2026-10-08 run replay/2026-10-08/loco_1_1_08102026_105200.cap (Limited
//  Supervision, Stand By, then System Failure with EB), @dmi in tab 81_1 and
//  the rest in 21_1.
// =============================================================================

namespace {

qint64 at372(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}

QString writeDlr372(const QString &path, LogModel *m, quint8 src)
{
    QFile f(path);
    if (!m || !f.open(QIODevice::WriteOnly)) return QString();
    SessionFile::FileHeader h;
    h.createdMs = at372("10:52:00");
    h.sourceId = src;
    h.kvchId = 1;
    f.write(SessionFile::encodeHeader(h));
    for (int r = 0; r < m->rowCount(); ++r) {
        const LogEntryPtr e = m->entryAt(r);
        if (e && !e->rawBytes.isEmpty()) f.write(SessionFile::encodeRecord(e->epochMs, e->rawBytes));
    }
    return path;
}

}  // namespace

TEST_SUITE(session372)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher disp;
    QVector<QPair<qint64, QString>> dmiLines;
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
        CHECK(f.open(QIODevice::ReadOnly), "fixture: the capture opens");
        const qint64 from = 0, to = std::numeric_limits<qint64>::max();
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            const QList<QByteArray> tok = l.split(' ');
            if (tok.size() < 3 || !l.startsWith('@')) continue;
            const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
            if (ms < from) continue;
            if (ms >= to) break;
            const bool isDmi = l.startsWith("@dmi_");
            if (l.startsWith("@dmi_1_1 ")) dmiLines.append({ ms, QString::fromLatin1(l) });
            disp.ingestLocal(isDmi ? 81 : 21, 1, l, ms, QString());
        }
        disp.drainNow();
    }
    LogModel *radio = disp.modelForKey(QStringLiteral("21_1"));
    LogModel *dmi = disp.modelForKey(QStringLiteral("81_1"));
    CHECK(radio && dmi && dmiLines.size() > 100, "fixture: @dmi of loco 1_1 in tab 81_1, the rest in 21_1");
    if (!radio || !dmi) return;

    // ---- the list, against the frames read one by one -----------------------------------------
    // Independently: every frame where the mode or the brake is not the
    // frame before's.
    QVector<qint64> modeOrBrake;
    {
        QString mode;
        int brake = -1;
        bool first = true;
        for (const auto &p : dmiLines) {
            const DmiState s = dmiStateFromLine(p.second);
            if (!s.valid) continue;
            if (!first && (s.modeText != mode || s.brakeType != brake)) modeOrBrake.append(p.first);
            mode = s.modeText;
            brake = s.brakeType;
            first = false;
        }
    }
    CHECK(!modeOrBrake.isEmpty(), "fixture: the run changes mode or brake");
    CHECK(DmiChanges::locos(dmi) == QStringList{ QStringLiteral("1_1") } && DmiChanges::locos(radio).isEmpty(),
          "the locos with @dmi in a tab");
    const QVector<DmiChanges::Change> changes = DmiChanges::list(dmi, QStringLiteral("1_1"));
    CHECK(!changes.isEmpty(), "changes listed: " + QByteArray::number(changes.size()));
    bool noneMoving = true, ordered = true;
    for (int i = 0; i < changes.size(); ++i) {
        for (const DmiDiff::Item &it : changes.at(i).items) noneMoving = noneMoving && !it.continuous;
        if (i) ordered = ordered && changes.at(i).ms >= changes.at(i - 1).ms;
    }
    CHECK(noneMoving && ordered, "in time order; speed, distances and location left out by default");
    bool everyModeOrBrake = true;
    for (qint64 ms : modeOrBrake) {
        bool found = false;
        for (const DmiChanges::Change &c : changes)
            if (c.ms == ms)
                for (const DmiDiff::Item &it : c.items) found = found || it.key == QLatin1String("mode") || it.key == QLatin1String("brake");
        everyModeOrBrake = everyModeOrBrake && found;
    }
    CHECK(everyModeOrBrake, "every frame where the mode or brake changed is there, saying so");
    bool eb = false, failure = false;
    for (const DmiChanges::Change &c : changes)
        for (const DmiDiff::Item &it : c.items) {
            if (it.key == QLatin1String("brake") && it.b == QLatin1String("EB")) eb = true;
            if (it.key == QLatin1String("mode") && it.b == QLatin1String("System Failure Mode")) failure = true;
        }
    CHECK(eb && failure, "including System Failure Mode and the brake going to EB");

    // A message counting down is the same message: moving, like speed.
    {
        DmiState a, b;
        a.valid = b.valid = true;
        b.systemMessages = QStringList{ QStringLiteral("End of Authority in 3250m") };
        const QVector<DmiDiff::Item> appears = DmiDiff::compare(a, b);
        CHECK(appears.size() == 1 && !appears.first().continuous, "a message appearing is a change");
        a.systemMessages = b.systemMessages;
        b.systemMessages = QStringList{ QStringLiteral("End of Authority in 3242m") };
        const QVector<DmiDiff::Item> counts = DmiDiff::compare(a, b);
        CHECK(counts.size() == 1 && counts.first().continuous, "the same message with its distance moving is not");
        b.systemMessages = QStringList{ QStringLiteral("PSR in 0143m with speed limit 80Kmph") };
        CHECK(!DmiDiff::compare(a, b).first().continuous, "another message in its place is a change");
    }
    const QVector<DmiChanges::Change> all = DmiChanges::list(dmi, QStringLiteral("1_1"), true);
    CHECK(all.size() >= changes.size(), "with speed and distances too: at least as many");
    CHECK(DmiChanges::next(changes, changes.first().ms) == (changes.size() > 1 ? &changes.at(1) : nullptr)
              && DmiChanges::previous(changes, changes.first().ms) == nullptr
              && DmiChanges::previous(changes, changes.last().ms + 1) == &changes.last(),
          "next and previous are strictly after and before");

    // ---- in the recorded-session window ----------------------------------------------------------
    QTemporaryDir tmp;
    const QStringList paths = { writeDlr372(tmp.filePath(QStringLiteral("21_1.dlr")), radio, 21),
                                writeDlr372(tmp.filePath(QStringLiteral("81_1.dlr")), dmi, 81) };
    ColorRules rules;
    NameMap names;
    SessionWindow w(&rules, &names, Theme::Light);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(w.loadFiles(paths) == radio->count() + dmi->count(), "every record loads");
    w.show();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

    QDockWidget *dock = w.findChild<QDockWidget *>(QStringLiteral("sessionDmiChangesDock"));
    CHECK(!dock || dock->isHidden(), "the DMI changes dock waits to be asked for");
    w.showDmiChanges();
    dock = w.findChild<QDockWidget *>(QStringLiteral("sessionDmiChangesDock"));
    auto *table = w.findChild<QTableWidget *>(QStringLiteral("dmiChangesTable"));
    CHECK(dock && dock->isVisible() && table, "Tools ▸ What changed on the DMI… shows it");
    if (!table) return;
    CHECK(w.dmiChanges().size() == changes.size() && table->rowCount() == changes.size()
              && table->item(0, 1)->text() == changes.first().text(),
          "loco 1_1, found in the other tab: one row per change, in words");
    auto *moving = w.findChild<QCheckBox *>(QStringLiteral("dmiChangesContinuous"));
    CHECK(moving, "a box for speed, distances and location");
    if (moving) {
        moving->setChecked(true);
        CHECK(table->rowCount() == all.size(), "ticked: those too");
        moving->setChecked(false);
    }

    // The radio tab is on show: a change is a moment, landed on there.
    QTableView *view = nullptr;
    for (QTableView *tv : w.findChildren<QTableView *>())
        if (tv->isVisible() && tv->model() && tv->model()->rowCount() == radio->count()) view = tv;
    CHECK(view != nullptr, "the radio tab is on show");
    if (!view) return;
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(view->model());
    auto *model = qobject_cast<LogModel *>(proxy->sourceModel());
    auto timeOfCursor = [&]() { return model->entryAt(proxy->mapToSource(view->currentIndex()).row())->epochMs; };
    auto nearest = [&](qint64 ms) {
        qint64 best = 0;
        for (int r = 0; r < model->count(); ++r)
            if (!best || qAbs(model->entryAt(r)->epochMs - ms) < qAbs(best - ms)) best = model->entryAt(r)->epochMs;
        return best;
    };
    emit table->cellDoubleClicked(0, 0);
    CHECK(view->currentIndex().isValid() && timeOfCursor() == nearest(changes.first().ms),
          "double-click: the tab on show lands on that moment");

    // ---- Previous / Next DMI change on the Playback bar -------------------------------------------
    QAction *next = w.findChild<QAction *>(QStringLiteral("playbackNextDmiChange"));
    QAction *prev = w.findChild<QAction *>(QStringLiteral("playbackPrevDmiChange"));
    CHECK(next && prev && next->shortcut() == QKeySequence(QStringLiteral("F8"))
              && prev->shortcut() == QKeySequence(QStringLiteral("Shift+F8")),
          "Playback bar: Previous (Shift+F8) and Next DMI change (F8)");
    if (!next || !prev) return;
    view->setCurrentIndex(proxy->index(0, 0));
    next->trigger();
    CHECK(timeOfCursor() == nearest(changes.first().ms), "Next from the start: the first change");
    if (changes.size() > 1) {
        next->trigger();
        CHECK(timeOfCursor() == nearest(changes.at(1).ms), "Next again: the second");
        prev->trigger();
        CHECK(timeOfCursor() == nearest(changes.first().ms), "Previous: back to the first");
    }
    view->setCurrentIndex(proxy->index(proxy->rowCount() - 1, 0));
    const qint64 lastRow = timeOfCursor();
    next->trigger();
    CHECK(timeOfCursor() == lastRow, "after the last change: Next stays put, and says so");
}

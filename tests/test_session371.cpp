#include "testutil.h"

#include "colorrules.h"
#include "dmitimetravel.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "sessionfile.h"
#include "sessionwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QToolBar>

// =============================================================================
//  Session 371 — Play in the recorded-session window: the cursor walks the
//  recording in time, at 1×-16×, and the DMI (and everything that follows the
//  cursor) plays with it. Real frames: 14:02-14:05 of
//  replay/loco_1_1_27062026_140226.cap, @dmi in tab 81_1, the rest in 21_1,
//  with every frame of 14:03:00-14:03:20 taken out for a quiet span.
// =============================================================================

namespace {

qint64 at371(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-06-27T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}

QString writeDlr371(const QString &path, LogModel *m, quint8 src)
{
    QFile f(path);
    if (!m || !f.open(QIODevice::WriteOnly)) return QString();
    SessionFile::FileHeader h;
    h.createdMs = at371("14:02:00");
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

TEST_SUITE(session371)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher disp;
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
        CHECK(f.open(QIODevice::ReadOnly), "fixture: the capture opens");
        const qint64 from = at371("14:02:00"), to = at371("14:05:00");
        const qint64 cutFrom = at371("14:03:00"), cutTo = at371("14:03:20");
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            const QList<QByteArray> tok = l.split(' ');
            if (tok.size() < 3 || !l.startsWith('@')) continue;
            const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
            if (ms < from || (ms >= cutFrom && ms < cutTo)) continue;
            if (ms >= to) break;
            disp.ingestLocal(l.startsWith("@dmi_") ? 81 : 21, 1, l, ms, QString());
        }
        disp.drainNow();
    }
    LogModel *radio = disp.modelForKey(QStringLiteral("21_1"));
    LogModel *dmi = disp.modelForKey(QStringLiteral("81_1"));
    CHECK(radio && dmi && radio->count() > 400 && dmi->count() > 100, "fixture: two tabs of real frames");
    if (!radio || !dmi) return;
    QTemporaryDir tmp;
    const QStringList paths = { writeDlr371(tmp.filePath(QStringLiteral("21_1.dlr")), radio, 21),
                                writeDlr371(tmp.filePath(QStringLiteral("81_1.dlr")), dmi, 81) };

    ColorRules rules;
    NameMap names;
    SessionWindow w(&rules, &names, Theme::Light);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(w.loadFiles(paths) == radio->count() + dmi->count(), "every record loads");
    w.show();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

    QTableView *view = nullptr;
    for (QTableView *tv : w.findChildren<QTableView *>())
        if (tv->isVisible() && tv->model() && tv->model()->rowCount() == radio->count()) view = tv;
    CHECK(view != nullptr, "the radio tab is on show");
    if (!view) return;
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(view->model());
    auto *model = qobject_cast<LogModel *>(proxy->sourceModel());
    auto timeAt = [&](int proxyRow) { return model->entryAt(proxy->mapToSource(proxy->index(proxyRow, 0)).row())->epochMs; };
    auto lastAtOrBefore = [&](qint64 ms) {
        int r = -1;
        for (int i = 0; i < proxy->rowCount() && timeAt(i) <= ms; ++i) r = i;
        return r;
    };

    // ---- the bar --------------------------------------------------------------------------------
    QToolBar *bar = w.findChild<QToolBar *>(QStringLiteral("playbackBar"));
    QAction *play = w.findChild<QAction *>(QStringLiteral("playbackPlay"));
    auto *speed = w.findChild<QComboBox *>(QStringLiteral("playbackSpeed"));
    CHECK(bar && play && speed && speed->count() == 5, "a Playback bar: Play and a speed of 1×, 2×, 4×, 8×, 16×");
    if (!bar || !play || !speed) return;
    CHECK(play->shortcut() == QKeySequence(QStringLiteral("F5")), "F5 plays and pauses");

    // Someone follows, as the DMI window does.
    QObject follower;
    DmiTimeTravel::instance()->reset();
    DmiTimeTravel::instance()->follow(&follower);

    // ---- play from the selected row at 4× ----------------------------------------------------
    view->setCurrentIndex(proxy->index(5, 0));
    const qint64 t5 = timeAt(5);
    w.setPlaybackSpeed(4);
    play->trigger();
    CHECK(w.isPlaying() && w.playbackClock() == t5 && play->text().contains(QLatin1String("Pause")),
          "Play starts at the selected row; the button reads Pause");
    w.advancePlayback(1000);   // one second of wall time
    CHECK(w.playbackClock() == t5 + 4000, "4×: one second plays four seconds of recording");
    CHECK(view->currentIndex().row() == lastAtOrBefore(t5 + 4000), "the cursor is on the last row of that moment");
    const DmiMoment &mo = DmiTimeTravel::instance()->last();
    CHECK(mo.valid && mo.atMs == timeAt(view->currentIndex().row()) && mo.frameFor(QStringLiteral("1_1")) != nullptr,
          "the DMI follows: loco 1_1's @dmi from the other tab, at the cursor's moment");

    // ---- a quiet span is skipped, and said ---------------------------------------------------
    const int before = lastAtOrBefore(at371("14:02:59"));
    view->setCurrentIndex(proxy->index(before, 0));
    play->trigger();   // pause
    CHECK(!w.isPlaying(), "F5 again pauses");
    play->trigger();   // play from the moved cursor
    CHECK(w.playbackClock() == timeAt(before), "Play resumes from the row the cursor was moved to");
    w.setPlaybackSpeed(1);
    w.advancePlayback(3000);
    CHECK(timeAt(view->currentIndex().row()) >= at371("14:03:20") && w.statusBar()->currentMessage().contains(QLatin1String("skipped")),
          "over 3 s with nothing: skipped to the next row, and the status bar says so: "
              + w.statusBar()->currentMessage().toUtf8());
    w.setSkipQuiet(false);
    view->setCurrentIndex(proxy->index(before, 0));
    play->trigger();
    play->trigger();
    w.advancePlayback(3000);
    CHECK(view->currentIndex().row() == before && w.isPlaying(), "skip off: the quiet span plays as it was, the cursor waits");

    // ---- the end -----------------------------------------------------------------------------
    w.setPlaybackSpeed(16);
    for (int i = 0; i < 40 && w.isPlaying(); ++i) w.advancePlayback(1000);
    CHECK(!w.isPlaying() && view->currentIndex().row() == proxy->rowCount() - 1
              && w.statusBar()->currentMessage().contains(QLatin1String("End of")),
          "at the last row it stops, and says so");
    CHECK(play->text().contains(QLatin1String("Play")), "the button reads Play again");

    DmiTimeTravel::instance()->unfollow(&follower);
}

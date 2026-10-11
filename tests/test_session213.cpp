#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "odometrywindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>
#include <QTableWidget>
#include <cmath>

// =============================================================================
//  Session 213 — odometry at the tags.
//  Real frames: replay/2026-10-08/loco_1_1_odometry.log, the @dmi and @rfid
//  lines of 81_1.001.log, 05:28:30-05:34:30 and 08:22:30-08:27:00 UTC, as
//  DLConsole logged them (receive time to the ms in the first column: the
//  @-line's own time is whole seconds, too coarse at 30 m/s).
//  Expected, from schema/engine.py with the same rule (scratch oracle):
//    23 main-tag reads; 800 and 802 with the loco not localised; 21 measured,
//    one over 50 m (tag 14 at 08:26:28.661: 161800 where the loco was at
//    ~161604 -- it then lost its location); 20 counted: median +0.56 m,
//    mean -0.39 m, largest 22.0 m (tag 18: the DMI said 0 km/h a moment
//    before a read 22 m on). Tag 804 at 05:28:54.343: DMI 159491 m, 110 km/h,
//    249 ms before -> 159498.6 m, error -1.39 m.
// =============================================================================

namespace {

qint64 at213(const char *iso) { return QDateTime::fromString(QLatin1String(iso), Qt::ISODateWithMs).toMSecsSinceEpoch(); }

LogModel *loadLog(MessageDispatcher &d)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_odometry.log"));
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const int at = l.indexOf('@');
        if (l.startsWith('#') || at < 0) continue;
        const qint64 ms = QDateTime::fromString(QString::fromLatin1(l.left(24)), Qt::ISODateWithMs).toMSecsSinceEpoch();
        d.ingestLocal(81, 1, l.mid(at), ms, QString());
    }
    d.drainNow();
    return d.modelForKey(QStringLiteral("81_1"));
}

const Odometry::Read *readAt(const Odometry::Report &r, qint64 ms)
{
    for (const Odometry::Read &rd : r.reads)
        if (rd.ms == ms) return &rd;
    return nullptr;
}

}  // namespace

TEST_SUITE(session213)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    MessageDispatcher disp;
    LogModel *model = loadLog(disp);
    CHECK(model != nullptr, "fixture: the tab");
    if (!model) return;

    const Odometry::Report r = Odometry::build(model);
    CHECK(r.reads.size() == 23, QByteArray("23 main-tag reads, one per pass (") + QByteArray::number(r.reads.size()) + ")");
    CHECK(r.counted == 20, QByteArray("20 counted (") + QByteArray::number(r.counted) + ")");
    int notLocalised = 0;
    for (const Odometry::Read &rd : r.reads) notLocalised += rd.notCounted.contains(QStringLiteral("not localised")) ? 1 : 0;
    CHECK(notLocalised == 2 && r.reads.at(0).tag == 800 && r.reads.at(1).tag == 802, "tags 800 and 802: loco not localised");

    const Odometry::Read *t804 = readAt(r, at213("2026-10-08T05:28:54.343Z"));
    CHECK(t804 && t804->tag == 804 && t804->dmiLocM == 159491 && t804->speedKmh == 110 && t804->sinceDmiMs == 249
              && t804->direction == 1 && std::fabs(t804->errorM - (-1.39)) < 0.01,
          QByteArray("tag 804: 159491 m at 110 km/h, 249 ms before -> -1.39 m (") + QByteArray::number(t804 ? t804->errorM : 0) + ")");
    CHECK(t804 && t804->spacingM < 0, "the first measured read of a run has no spacing");
    const Odometry::Read *t2 = readAt(r, at213("2026-10-08T05:28:59.559Z"));
    CHECK(t2 && t2->spacingM == 160 && std::fabs(t2->perKm() - t2->errorM / 0.16) < 1e-9, "tag 2: 160 m since tag 804, error per km");
    const Odometry::Read *again804 = readAt(r, at213("2026-10-08T08:22:52.325Z"));
    CHECK(again804 && again804->counted() && again804->spacingM < 0, "a pass hours later starts a new run: no spacing");
    const Odometry::Read *jump = readAt(r, at213("2026-10-08T08:26:28.661Z"));
    CHECK(jump && jump->tag == 14 && !jump->counted() && jump->notCounted.contains(QStringLiteral("relocation")),
          "tag 14 at 161800 m, ~195 m off: a relocation, not counted");

    CHECK(std::fabs(r.medianM - 0.56) < 0.01 && std::fabs(r.meanM - (-0.3865)) < 0.01 && std::fabs(r.maxAbsM - 22.0) < 0.01,
          QByteArray("median 0.56, mean -0.39, largest 22.0 (") + QByteArray::number(r.medianM) + " " + QByteArray::number(r.meanM)
              + " " + QByteArray::number(r.maxAbsM) + ")");
    const QString text = Odometry::summaryText(r);
    CHECK(text.contains(QStringLiteral("23 tag read(s), 20 measured")) && text.contains(QStringLiteral("0.6 m from the tag (median")),
          QByteArray("the summary: ") + text.toUtf8());
    CHECK(Odometry::toTsv(r).count(QLatin1Char('\n')) == 24, "the copy: a header and one line per read");

    // The statistics alone.
    {
        Odometry::Report h;
        Odometry::Read a; a.errorM = 1.0; a.spacingM = 500;
        Odometry::Read b; b.errorM = 3.0; b.spacingM = 1000;
        Odometry::Read c; c.errorM = -2.0;
        Odometry::Read x; x.errorM = 99; x.notCounted = QStringLiteral("x");
        h.reads = { a, b, c, x };
        Odometry::summarise(h);
        CHECK(h.counted == 3 && h.medianM == 1.0 && std::fabs(h.meanM - 2.0 / 3.0) < 1e-9 && h.maxAbsM == 3.0
                  && h.withSpacing == 2 && h.medianPerKm == 2.5,
              "median of three, mean, largest |error|, per km over those with a spacing; not-counted left out");
    }

    // The window.
    OdometryWindow w(model, QStringLiteral("81_1"), QStringLiteral("81_1"));
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1000, 600);
    w.show();
    CHECK(w.summary() == text && w.table()->rowCount() == 23, "the window: the summary over one row per read");
    if (!qgetenv("DL_SHOTS").isEmpty()) w.grab().save(QString::fromLocal8Bit(qgetenv("DL_SHOTS")) + QStringLiteral("/odometry.png"));
    qint64 jumped = -1;
    QObject::connect(&w, &OdometryWindow::jumpRequested, [&jumped](const QString &, qint64 ms) { jumped = ms; });
    emit w.table()->cellDoubleClicked(2, 0);
    CHECK(jumped == r.reads.at(2).ms, "a double-click jumps the log to that read");
    CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700, "fits a laptop");
}

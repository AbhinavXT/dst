#include "testutil.h"

#include "laneband.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "runreport.h"
#include "sosfixture.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QSet>
#include <QToolButton>
#include <QVBoxLayout>

// =============================================================================
//  Session 318 — the lane band: a slim Lanes ▾, and the Safety lane split
//  into one row per kind.
//
//  Abhinav's photos: the Lanes ▾ button was a tall framed box, and on the
//  Safety lane station-dest SoS, loco SoS and unusual stop were drawn in
//  strips of one row and could not be told apart. Each kind present in the
//  window now has its own named row.
//
//  Real captures:
//    replay/2026-10-08/loco_1_1_08102026_105200.cap: a collision detection
//      (11:03:34) and the loco's own SoS (unusual stop from 11:08:14);
//  and, as no real capture has an @sos threat yet, the synthetic SoS scenario
//  (tests/sosfixture.h).
// =============================================================================

namespace {

LogModel *load318(MessageDispatcher &d, const QString &path, quint8 source)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        d.ingestLocal(source, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(),
                      QString());
    }
    d.drainNow();
    return d.modelForKey(QStringLiteral("%1_1").arg(source));
}

// The point on `row` at `ms`, as the band maps time to x.
QPoint at318(const LaneBand &b, int row, qint64 ms)
{
    const QRect r = b.laneRect(row);
    const double t = double(ms - b.fromMs()) / double(qMax<qint64>(1, b.toMs() - b.fromMs()));
    return QPoint(r.left() + int(t * r.width()), r.center().y());
}

}  // namespace

TEST_SUITE(session318)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // ---- own SoS and a collision detection: two rows -------------------------------
    {
        MessageDispatcher d;
        LogModel *m = load318(d, QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"), 81);
        CHECK(m != nullptr, "fixture: 10:52-11:13");
        if (!m) return;
        QWidget host;
        auto *lay = new QVBoxLayout(&host);
        auto *band = new LaneBand;
        lay->addWidget(band);
        host.resize(1200, 400);
        host.show();
        band->setModel(m, QStringLiteral("81_1"), QStringLiteral("81_1"));
        QCoreApplication::processEvents();

        const QStringList kinds = band->safetyKinds();
        CHECK(kinds.contains(QStringLiteral("ownsos")) && kinds.contains(QStringLiteral("collision")),
              QByteArray("own SoS and collision each have a row (") + kinds.join(QLatin1Char(',')).toUtf8() + ")");
        const int own = band->safetyRow(QStringLiteral("ownsos"));
        const int col = band->safetyRow(QStringLiteral("collision"));
        CHECK(own != col && own >= 1 && col >= 1, "on different rows");
        CHECK(band->laneNames().contains(QStringLiteral("Safety")), "the lane is still Safety in the lane list");
        CHECK(band->fixedLaneRow(2) == 1 + kinds.size(), "RFID follows the Safety rows");
        const qint64 collisionMs = QDateTime::fromString(QStringLiteral("2026-10-08T11:03:34"), Qt::ISODate).toMSecsSinceEpoch();
        const qint64 sosMs = QDateTime::fromString(QStringLiteral("2026-10-08T11:10:00"), Qt::ISODate).toMSecsSinceEpoch();
        CHECK(band->describeAt(at318(*band, col, collisionMs)).startsWith(QStringLiteral("Collision detection (NMS) at")),
              "the collision row names the detection");
        CHECK(band->describeAt(at318(*band, own, sosMs)).startsWith(QStringLiteral("Own SoS (NMS): Unusual stop")),
              "the own-SoS row names the unusual stop");
        CHECK(!band->describeAt(at318(*band, col, sosMs)).startsWith(QStringLiteral("Own SoS")),
              "and the collision row does not answer for it");
        const int rowsBefore = band->sizeHint().height();
        band->setHiddenLanes({ QStringLiteral("Safety") });
        CHECK(band->safetyRow(QStringLiteral("ownsos")) == -1 && band->fixedLaneRow(2) == 1
                  && band->sizeHint().height() < rowsBefore,
              "Safety hidden: all its rows go, RFID is the second row");
    }

    // ---- the firmware's threats: a row per threat ----------------------------------
    {
        // No real capture has a threat yet: the synthetic SoS scenario (sessions 184-187).
        MessageDispatcher d;
        LogModel *m = SosFixture::load(d, 1);
        CHECK(m != nullptr, "fixture: the synthetic SoS capture, loco 1");
        if (!m) return;
        QWidget host;
        auto *lay = new QVBoxLayout(&host);
        auto *band = new LaneBand;
        lay->addWidget(band);
        host.resize(1200, 400);
        host.show();
        band->setModel(m, QStringLiteral("1_1"), QStringLiteral("1_1"));
        QCoreApplication::processEvents();
        const RunReport::Summary &s = band->summary();
        QStringList threatRows;
        for (const QString &k : band->safetyKinds())
            if (k.startsWith(QLatin1String("threat:"))) threatRows << k;
        QSet<QString> names;
        for (const RunReport::Episode &e : s.sosThreats) names.insert(e.what.section(QStringLiteral(" from "), 0, 0));
        CHECK(!names.isEmpty(), "fixture: the capture has firmware SoS threats");
        CHECK(threatRows.size() == names.size(), QByteArray("one row per threat type (") + threatRows.join(QLatin1Char(',')).toUtf8() + ")");
        bool each = true;
        for (const RunReport::Episode &e : s.sosThreats) {
            const int row = band->safetyRow(QStringLiteral("threat:") + e.what.section(QStringLiteral(" from "), 0, 0));
            const QString tip = band->describeAt(at318(*band, row, (qMax(e.fromMs, band->fromMs()) + qMin(e.toMs, band->toMs())) / 2));
            each = each && tip.contains(e.what);
        }
        CHECK(each, "each threat is named on its own row");
    }

    // ---- the slim Lanes ▾ ----------------------------------------------------------
    {
        LaneBand band;
        band.resize(800, 200);
        auto *button = band.findChild<QToolButton *>(QStringLiteral("laneMenuButton"));
        CHECK(button && button->styleSheet().contains(QStringLiteral("border: none")), "drawn as text, no frame");
        CHECK(button && band.laneRect(0).top() <= 6 + button->sizeHint().height() + 2, "the header is one text line high");
        CHECK(button && band.laneRect(0).top() >= button->geometry().bottom(), "and the first lane starts below it");
    }
}

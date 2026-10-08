#include "testutil.h"

#include "laneband.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QVBoxLayout>
#include <cstdio>

// =============================================================================
//  Session 164 — the lanes over the log.
//    RFID: the last tag read as a span until the next read, named (was a
//    tick per read). Custom lanes: any packet field, spans or a graph.
//  Real run: replay/loco_1_1_27062026_170217.cap.
// =============================================================================

namespace {

// The point in `lane` at time `ms`, as the band maps time to x.
QPoint at(const LaneBand &b, int lane, qint64 ms)
{
    const QRect r = b.laneRect(lane);
    const double t = double(ms - b.fromMs()) / double(qMax<qint64>(1, b.toMs() - b.fromMs()));
    return QPoint(r.left() + int(t * r.width()), r.center().y());
}

}  // namespace

TEST_SUITE(session164)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher disp;
    QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap"));
    CHECK(run.open(QIODevice::ReadOnly), "fixture: the run");
    while (!run.atEnd()) {
        const QByteArray l = run.readLine().trimmed();
        if (!l.startsWith('@')) continue;
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3) continue;
        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    disp.drainNow();
    LogModel *model = disp.modelForKey(QStringLiteral("21_1"));
    CHECK(model && model->count() > 1000, "fixture: the tab's rows");

    QWidget host;                       // the band only builds while its tab shows
    auto *lay = new QVBoxLayout(&host);
    auto *band = new LaneBand;
    lay->addWidget(band);
    lay->addStretch(1);
    host.resize(1200, 400);
    host.show();
    band->setModel(model, QStringLiteral("21_1"), QStringLiteral("21_1"));
    QCoreApplication::processEvents();

    // ---- RFID as spans -----------------------------------------------------------
    const QVector<RunReport::Change> &tags = band->summary().tagReads;
    CHECK(tags.size() >= 2, QByteArray("fixture: tag reads in the window (") + QByteArray::number(tags.size()) + ")");
    if (tags.size() >= 2) {
        const qint64 mid = (tags.at(0).ms + tags.at(1).ms) / 2;
        const QString tip = band->describeAt(at(*band, 2, mid));
        CHECK(tip.startsWith(QStringLiteral("RFID tag %1, read at").arg(tags.at(0).to)) && tip.contains(QStringLiteral("until")),
              QByteArray("between two reads, the lane is the first tag's span (") + tip.toUtf8() + ")");
        const qint64 afterLast = (tags.last().ms + band->toMs()) / 2;
        CHECK(band->describeAt(at(*band, 2, afterLast)).contains(QStringLiteral("until now"))
                  || tags.last().ms >= band->toMs() - 1000,
              "the last tag's span runs to the end of the window");
        // Drawn, not ticked: the middle of the first span is painted in a colour.
        const QImage img = band->grab().toImage();
        const QPoint p = at(*band, 2, mid);
        const QPoint empty(band->laneRect(2).left() + 1, band->laneRect(2).center().y());
        CHECK(tags.at(0).ms <= band->fromMs() + 1000
                  || img.pixelColor(p * img.devicePixelRatio()) != img.pixelColor(empty * img.devicePixelRatio()),
              "the span between reads is filled, not empty track");
    }

    // ---- custom lanes ---------------------------------------------------------------
    const QHash<QString, QStringList> fields = band->addableFields();
    CHECK(fields.value(QStringLiteral("lsrp")).contains(QStringLiteral("TRAIN_SPEED"))
              && fields.value(QStringLiteral("lsrp")).contains(QStringLiteral("LOCO_MODE")),
          "Add lane offers this tab's packets and their fields (lsrp: TRAIN_SPEED, LOCO_MODE)");

    const int fixedHeight = band->sizeHint().height();
    band->setCustomLanes({ CustomLane{ QStringLiteral("lsrp"), QStringLiteral("TRAIN_SPEED") },
                           CustomLane{ QStringLiteral("lsrp"), QStringLiteral("LOCO_MODE") },
                           CustomLane{ QStringLiteral("nmsflt"), QStringLiteral("NO_SUCH") } });
    QCoreApplication::processEvents();
    const QVector<CustomLane> shown = band->shownCustomLanes();
    CHECK(shown.size() == 2 && shown.at(0).field == QStringLiteral("TRAIN_SPEED") && shown.at(1).field == QStringLiteral("LOCO_MODE"),
          "the two lanes this tab carries are shown; one it does not carry is not");
    CHECK(band->laneNames().mid(5) == (QStringList{ QStringLiteral("TRAIN_SPEED"), QStringLiteral("LOCO_MODE") }),
          "named after their fields");
    CHECK(band->sizeHint().height() > fixedHeight, "the band grows a lane's height for each");
    CHECK(band->customLaneIsGraph(0), "TRAIN_SPEED (\"15 km/h\": a number with a unit, even at a few speeds): a graph");
    CHECK(!band->customLaneIsGraph(1), "LOCO_MODE (\"6 (On_Sight)\": names): spans, like MODE");

    const qint64 midWin = (band->fromMs() + band->toMs()) / 2;
    const QString speedTip = band->describeAt(at(*band, 5, midWin));
    CHECK(speedTip.startsWith(QStringLiteral("@lsrp TRAIN_SPEED = ")) && speedTip.contains(QStringLiteral("km/h")),
          QByteArray("hover a graph: its value then (") + speedTip.toUtf8() + ")");
    const QString modeTip = band->describeAt(at(*band, 6, midWin));
    CHECK(modeTip.startsWith(QStringLiteral("@lsrp LOCO_MODE = ")) && modeTip.contains(QLatin1Char('(')),
          QByteArray("hover spans: the named value (") + modeTip.toUtf8() + ")");
    CHECK(band->customLaneAt(QPoint(4, band->laneRect(6).center().y())) == 1, "right-click on a custom lane's label finds it");
    CHECK(band->customLaneAt(QPoint(4, band->laneRect(2).center().y())) == -1, "and the RFID lane is not a custom one");

    band->setCustomLanes({});
    CHECK(band->shownCustomLanes().isEmpty() && band->sizeHint().height() == fixedHeight, "removed: back to the five lanes");
}

#include "testutil.h"

#include "capturedecoder.h"
#include "incidentreport.h"
#include "laneband.h"
#include "lococonsolewindow.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "runreport.h"
#include "theme.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QVBoxLayout>

// =============================================================================
//  Session 168 — start of mission, from ARP: Stand_By (LOCO_MODE 1),
//  MOVEMENT_DIR 0, LAST_RFID_TAG 0, ABS_LOCO_LOC 0.
//  Real frames: Abhinav's @arp_2_1 of 2026-10-08 16:38:18, and
//  replay/loco_1_1_27062026_140226.cap, whose ARPs (checked with
//  schema/engine.py) are in that state 14:06:46-14:07:30 (then
//  2 Staff_Responsible) and from 14:59:44 to the end of the capture.
// =============================================================================

namespace {

const char *kUserArp =
    "@arp_2_1 2026-10-08T16:38:18 41683 02 07 0D 00 27 00 00 00 00 00 D3 AE 9F B0 00 02 40 00 00 07 D0 "
    "00 02 00 00 00 00 00 00 00 00 00 05 43 00 BF 23 B5 5E";

qint64 ms168(const char *iso) { return QDateTime::fromString(QString::fromLatin1(iso), Qt::ISODate).toMSecsSinceEpoch(); }

QString rowValue(const QVector<FieldRow> &rows, const QString &name)
{
    for (const FieldRow &r : rows) if (r.field.trimmed() == name) return r.value;
    return QString();
}

// The n-th line of `type` in a capture.
QString nthOf(const QString &file, const QByteArray &type, int n)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/") + file);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    int seen = 0;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.startsWith("@" + type + "_") && seen++ == n) return QString::fromLatin1(l);
    }
    return QString();
}

QPoint at168(const LaneBand &b, int lane, qint64 ms)
{
    const QRect r = b.laneRect(lane);
    const double t = double(ms - b.fromMs()) / double(qMax<qint64>(1, b.toMs() - b.fromMs()));
    return QPoint(r.left() + int(t * r.width()), r.center().y());
}

}  // namespace

TEST_SUITE(session168)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    const QString file = QStringLiteral("loco_1_1_27062026_140226.cap");

    // ---- the decoder: the state, and the row note --------------------------------------
    {
        const CaptureLine c = CaptureDecoder::parseLine(QString::fromLatin1(kUserArp));
        CHECK(c.valid && c.type == CapType::ARP && c.crcChecked && c.crcOk, "Abhinav's ARP frame parses, CRC passes");
        QHash<QString, qint64> raw;
        const QVector<FieldRow> rows = CaptureDecoder::describe(c, nullptr, 0, &raw);
        CHECK(raw.value(QStringLiteral("LOCO_MODE")) == 1 && raw.value(QStringLiteral("MOVEMENT_DIR")) == 0
                  && raw.value(QStringLiteral("LAST_RFID_TAG")) == 0 && raw.value(QStringLiteral("ABS_LOCO_LOC")) == 0
                  && raw.value(QStringLiteral("FRAME_NUM")) == 59899 && raw.value(QStringLiteral("SOURCE_LOCO_ID")) == 2,
              "decoded bit for bit: Stand_By, dir 0, tag 0, loc 0, FRAME_NUM 59899, loco 2");
        CHECK(CaptureDecoder::isStartOfMission(raw), "it is the start-of-mission state");
        CHECK(rowValue(rows, QStringLiteral("start of mission")).contains(QStringLiteral("Stand_By")),
              "a 'start of mission' row says so");
        const QVector<FieldRow> noRaw = CaptureDecoder::describe(c, nullptr, 0, nullptr);
        CHECK(!rowValue(noRaw, QStringLiteral("start of mission")).isEmpty(), "also when the caller wants no raw values");

        QHash<QString, qint64> other = raw;
        for (const char *k : { "LOCO_MODE", "MOVEMENT_DIR", "LAST_RFID_TAG", "ABS_LOCO_LOC" }) {
            QHash<QString, qint64> h = raw;
            h[QString::fromLatin1(k)] = 2;
            CHECK(!CaptureDecoder::isStartOfMission(h), QByteArray("not when ") + k + " is not the start value");
        }
        other.remove(QStringLiteral("ABS_LOCO_LOC"));
        CHECK(!CaptureDecoder::isStartOfMission(other), "not when a field is missing");

        // A real ARP after the mission started (Staff_Responsible, tag 10, located).
        const CaptureLine later = CaptureDecoder::parseLine(nthOf(file, "arp", 400));
        QHash<QString, qint64> lraw;
        const QVector<FieldRow> lrows = CaptureDecoder::describe(later, nullptr, 0, &lraw);
        CHECK(later.valid && !CaptureDecoder::isStartOfMission(lraw) && rowValue(lrows, QStringLiteral("start of mission")).isEmpty(),
              "an ARP of a running mission has no such row");
        // LSRP carries the same fields; the rule is ARP's.
        const CaptureLine lsrp = CaptureDecoder::parseLine(nthOf(file, "lsrp", 0));
        CHECK(lsrp.valid && rowValue(CaptureDecoder::describe(lsrp, nullptr, 0, nullptr), QStringLiteral("start of mission")).isEmpty(),
              "an LSRP frame is not annotated");
    }

    // ---- the run, as the console holds it ----------------------------------------------
    MessageDispatcher disp;
    QFile run(QStringLiteral(DL_SRC_DIR "/replay/") + file);
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
    if (!model) return;

    // ---- run report ----------------------------------------------------------------------
    {
        const RunReport::Summary s = RunReport::summarise(model, QStringLiteral("21_1"), QString());
        CHECK(s.missionStarts.size() == 2, QByteArray("two starts of mission (") + QByteArray::number(s.missionStarts.size()) + ")");
        if (s.missionStarts.size() == 2) {
            const RunReport::Episode &a = s.missionStarts.at(0), &b = s.missionStarts.at(1);
            CHECK(a.fromMs == ms168("2026-06-27T14:06:46") && a.toMs == ms168("2026-06-27T14:07:30"),
                  "the first: 14:06:46 to 14:07:30, as engine.py reads the ARPs");
            CHECK(a.what == QStringLiteral("2 (Staff_Responsible)"), "then Staff_Responsible");
            CHECK(a.row >= 0 && model->entryAt(a.row)->text.startsWith(QStringLiteral("@arp_")),
                  "its row is the first such ARP");
            CHECK(b.fromMs == ms168("2026-06-27T14:59:44") && b.toMs == ms168("2026-06-27T15:10:44") && b.what.isEmpty(),
                  "the second: from 14:59:44, still so at the end");
        }
        const QString html = RunReport::toHtml(s);
        CHECK(html.contains(QStringLiteral("Start of mission (ARP): 2")) && html.contains(QStringLiteral("14:06:46"))
                  && html.contains(QStringLiteral("still so at the end")),
              "the report lists both");
    }

    // ---- incident report around the first ------------------------------------------------
    {
        IncidentReport::Options io;
        const IncidentReport::Summary s = IncidentReport::build(model, QStringLiteral("21_1"), QString(),
                                                                ms168("2026-06-27T14:07:00"), io);
        bool moment = false;
        for (const IncidentReport::KeyMoment &k : s.keyMoments) moment |= k.label == QLatin1String("Start of mission (ARP)");
        CHECK(moment, "a key moment: the DMI as it was at the start of mission");
        const QString html = IncidentReport::toHtml(s, io);
        CHECK(html.contains(QStringLiteral("Start of mission (ARP): 1")), "and a section for it");
    }

    // ---- the lanes over the newest 15 minutes (the second start) --------------------------
    {
        QWidget host;
        auto *lay = new QVBoxLayout(&host);
        auto *band = new LaneBand;
        lay->addWidget(band);
        host.resize(1200, 300);
        host.show();
        band->setModel(model, QStringLiteral("21_1"), QStringLiteral("21_1"));
        QCoreApplication::processEvents();
        const QVector<RunReport::Episode> &m = band->summary().missionStarts;
        CHECK(m.size() == 1 && m.first().fromMs == ms168("2026-06-27T14:59:44"), "the band's window holds the second start");
        if (!m.isEmpty()) {
            const QString tip = band->describeAt(at168(*band, 0, m.first().fromMs + 1000));
            CHECK(tip.startsWith(QStringLiteral("Start of mission from 14:59:44")) && tip.contains(QStringLiteral("still so")),
                  QByteArray("hover the Mode lane there: ") + tip.toUtf8());
            const QImage img = band->grab().toImage();
            if (!qgetenv("DL_SHOTS").isEmpty()) img.save(QString::fromLocal8Bit(qgetenv("DL_SHOTS")) + QStringLiteral("/lanes_som.png"));
            const QPoint p = at168(*band, 0, m.first().fromMs + 30000) * img.devicePixelRatio();
            CHECK(img.pixelColor(p) == UiColor::accent() || img.pixelColor(p + QPoint(1, 0)) == UiColor::accent(),
                  "drawn in the accent colour over the Mode lane");
        }
    }

    // ---- the Live Loco Console's header ---------------------------------------------------
    {
        LocoConsoleWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        QLabel *chip = w.missionChip();
        auto feed = [&w](const QString &line) {
            LogEntryPtr e(new LogEntry);
            e->text = line;
            e->epochMs = QDateTime::currentMSecsSinceEpoch();
            QMetaObject::invokeMethod(&w, "onEntryAppended", Q_ARG(QString, QStringLiteral("1_1")), Q_ARG(LogEntryPtr, e));
            QMetaObject::invokeMethod(&w, "onRefreshTick");
        };
        // The ARPs either side of the first start, in order.
        int first = -1;
        for (int i = 0; i < 400 && first < 0; ++i) {
            const QString l = nthOf(file, "arp", i);
            if (l.contains(QStringLiteral("2026-06-27T14:06:46"))) first = i;
        }
        CHECK(first > 0, "fixture: the first start-of-mission ARP");
        feed(nthOf(file, "arp", first - 1));
        CHECK(chip && chip->text() == QStringLiteral("start of mission --"), "before it: none seen");
        feed(nthOf(file, "arp", first));
        CHECK(chip && chip->text().contains(QStringLiteral("at start of mission since 14:06:46")),
              QByteArray("in it: ") + (chip ? chip->text().toUtf8() : QByteArray()));
        int k = first + 1;
        for (; k < first + 200; ++k) {
            const QString l = nthOf(file, "arp", k);
            feed(l);
            if (!chip->text().contains(QStringLiteral("since"))) break;
        }
        CHECK(chip && chip->text() == QString::fromUtf8("mission started 14:06:46 → Staff_Responsible"),
              QByteArray("after it: ") + (chip ? chip->text().toUtf8() : QByteArray()));
        CHECK(chip && chip->toolTip().contains(QStringLiteral("14:07:30")), "the tooltip has the last such ARP");
        CHECK(w.minimumSizeHint().width() <= 1100,
              QByteArray("the longest chip still lets the console fit a laptop (minimum width ")
                  + QByteArray::number(w.minimumSizeHint().width()) + ")");
        if (!qgetenv("DL_SHOTS").isEmpty()) {
            w.resize(1100, 500);
            w.show();
            QCoreApplication::processEvents();
            w.grab().save(QString::fromLocal8Bit(qgetenv("DL_SHOTS")) + QStringLiteral("/console_som.png"));
        }
    }
}

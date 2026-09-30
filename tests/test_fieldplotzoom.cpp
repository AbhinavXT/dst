#include "testutil.h"

#include "fieldplot.h"
#include "logmodel.h"
#include "schema/schemadecoder.h"
#include "capturedecoder.h"

#include <QDir>
#include <QMenu>
#include <QMouseEvent>
#include <QTest>
#include <QToolButton>
#include <QWheelEvent>

// =============================================================================
//  Field over time, session 80: one packet type per series (the Packet ▸
//  Field menu), zoom and pan, and the legend / axis-title data it draws
//  from. Fed real LSRP and ARP frames, which both carry LOCO_MODE.
// =============================================================================

namespace {

const QString kLsrp = QStringLiteral(
    "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
    "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B");
const QString kArp = QStringLiteral(
    "@arp_1_1 2026-06-27T14:02:26 21436 02 07 0D 00 27 00 00 00 0F 02 D3 AC 57 30 "
    "00 01 40 9F FB 47 D0 01 0E 04 1F C3 15 00 00 00 00 00 08 32 00 C9 5E DE 2F");

LogEntryPtr entry(const QString &line, qint64 ms)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->text = line;
    e->epochMs = ms;
    e->cacheDerived();
    return e;
}

// Interleaved like a real tab: an LSRP and an ARP every second for `n` s.
void fill(LogModel &m, int n, qint64 t0 = 1700000000000LL)
{
    QVector<LogEntryPtr> v;
    for (int i = 0; i < n; ++i) {
        v << entry(kLsrp, t0 + i * 1000) << entry(kArp, t0 + i * 1000 + 500);
    }
    m.appendEntries(v);
}

QString valueOf(const QString &line, const QString &field)
{
    for (const FieldRow &r : CaptureDecoder::describe(CaptureDecoder::parseLine(line))) {
        if (r.field.trimmed() == field) return r.value;
    }
    return QString();
}

}  // namespace

TEST_SUITE(fieldplotzoom)
{
    const Schema::Decoder &d = kavachSchema();

    // ---- the capture tag's type, without decoding --------------------------
    CHECK(captureTypeOf(kLsrp) == QLatin1String("lsrp"), "the type is read off the tag");
    CHECK(captureTypeOf(QStringLiteral("@nms_hlth_7_1 x 1")) == QLatin1String("nms_hlth"),
          "a type with an underscore keeps it (the last two parts are loco and controller)");
    CHECK(captureTypeOf(QStringLiteral("plain text")).isEmpty(), "a line without a tag has no type");
    CHECK(captureTypeOf(kArp) == CaptureDecoder::parseLine(kArp).typeToken,
          "and agrees with the full parser");

    // ---- one packet type per series -------------------------------------------
    LogModel m(nullptr, 5000);
    fill(m, 30);
    const QString lsrpMode = valueOf(kLsrp, QStringLiteral("LOCO_MODE"));
    const QString arpMode  = valueOf(kArp, QStringLiteral("LOCO_MODE"));
    CHECK(!lsrpMode.isEmpty() && !arpMode.isEmpty(), "both packets carry LOCO_MODE");

    const FieldSeries mixed = extractFieldSeries(&m, QStringLiteral("LOCO_MODE"), d);
    const FieldSeries lsrp  = extractFieldSeries(&m, QStringLiteral("lsrp"), QStringLiteral("LOCO_MODE"), d);
    const FieldSeries arp   = extractFieldSeries(&m, QStringLiteral("arp"), QStringLiteral("LOCO_MODE"), d);
    CHECK(mixed.points.size() == 60, "untyped: both packets' LOCO_MODE interleaved (the old behaviour)");
    CHECK(lsrp.points.size() == 30 && arp.points.size() == 30, "typed: only that packet's rows");
    CHECK(lsrp.rowsScanned == 30 && lsrp.rowsOfType == 30, "and only those rows are decoded at all");
    bool onlyLsrp = true;
    for (const auto &pt : lsrp.points) onlyLsrp = onlyLsrp && captureTypeOf(m.entryAt(pt.row)->text) == QLatin1String("lsrp");
    CHECK(onlyLsrp, "every point's row is an LSRP");
    CHECK(lsrp.label() == QStringLiteral("lsrp \u25B8 LOCO_MODE"), "the series names its packet and field");

    // ---- enum names and units from the schema's own display ------------------------
    bool okNum = false;
    const double modeValue = parseFieldNumber(lsrpMode, &okNum);
    if (lsrpMode.contains(QLatin1Char('('))) {
        CHECK(lsrp.labels.contains(qint64(modeValue)), "an enum value keeps its name for the axis");
    } else {
        CHECK(lsrp.labels.isEmpty(), "no name printed, none invented");
    }
    const FieldSeries speed = extractFieldSeries(&m, QStringLiteral("lsrp"), QStringLiteral("TRAIN_SPEED"), d);
    const QString speedText = valueOf(kLsrp, QStringLiteral("TRAIN_SPEED"));
    if (speedText.contains(QLatin1Char(' ')) && !speedText.contains(QLatin1Char('('))) {
        CHECK(speed.unit == speedText.section(QLatin1Char(' '), 1).trimmed(), "the unit is taken from the display");
    } else {
        CHECK(speed.unit.isEmpty(), "no unit printed, none invented");
    }

    // ---- a time window --------------------------------------------------------------
    const qint64 t0 = 1700000000000LL;
    const FieldSeries window = extractFieldSeries(&m, QStringLiteral("lsrp"), QStringLiteral("LOCO_MODE"), d,
                                                  20000, t0 + 10000, t0 + 19999);
    CHECK(window.points.size() == 10 && window.points.first().epochMs == t0 + 10000
              && window.points.last().epochMs == t0 + 19000,
          "a time window takes exactly the rows inside it");
    const FieldSeries capped = extractFieldSeries(&m, QStringLiteral("lsrp"), QStringLiteral("LOCO_MODE"), d, 5);
    CHECK(capped.hitRowCap && capped.points.size() <= 6 && capped.points.last().epochMs > t0 + 20000,
          "the cap samples across the whole span, not its start");

    // ---- the catalogue behind the Packet ▸ Field menu ---------------------------------------
    const FieldCatalogue cat = discoverFieldCatalogue(&m, d);
    CHECK(cat.types == QStringList({ "arp", "lsrp" }), "both packet types are found");
    CHECK(cat.rowCounts.value(QStringLiteral("lsrp")) == 30, "with their row counts");
    CHECK(cat.fields.value(QStringLiteral("lsrp")).contains(QStringLiteral("LOCO_MODE"))
              && cat.fields.value(QStringLiteral("arp")).contains(QStringLiteral("LOCO_MODE")),
          "and their fields");
    CHECK(cat.typesWith(QStringLiteral("LOCO_MODE")).size() == 2, "a shared name lists every packet carrying it");

    // ---- time ticks ---------------------------------------------------------------------------
    qint64 step = 0;
    const QVector<qint64> ticks = niceTimeTicks(t0 + 3, t0 + 60000, 6, &step);
    CHECK(step == 10000 || step == 15000, "a minute gets 10 or 15 s ticks");
    bool aligned = true;
    for (qint64 t : ticks) {
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(t);
        aligned = aligned && dt.time().msec() == 0 && dt.time().second() % (step / 1000) == 0;
    }
    CHECK(!ticks.isEmpty() && aligned, "ticks land on round wall-clock times");
    niceTimeTicks(t0, t0 + 800, 6, &step);
    CHECK(step < 1000, "under a second the ticks go to milliseconds");

    // ---- the window: nested menu, zoom, pan ------------------------------------------------------
    {
        FieldPlotWindow w(&m, QStringLiteral("1_1"));
        w.resize(900, 500);
        w.show();
        QTest::qWait(20);
        QMenu *menu = w.fieldMenu();
        QMenu *lsrpMenu = nullptr;
        for (QAction *a : menu->actions()) {
            if (a->menu() && a->text().startsWith(QLatin1String("lsrp"))) lsrpMenu = a->menu();
        }
        CHECK(lsrpMenu != nullptr, "Field ▸ has a submenu per packet type");
        QAction *modeAct = nullptr;
        if (lsrpMenu) for (QAction *a : lsrpMenu->actions()) if (a->text() == QLatin1String("LOCO_MODE")) modeAct = a;
        CHECK(modeAct != nullptr, "with that packet's fields inside");
        CHECK(modeAct && modeAct->toolTip().contains(QLatin1String("arp")),
              "a shared field says which other packet carries it");
        if (modeAct) modeAct->trigger();
        CHECK(w.currentType() == QLatin1String("lsrp") && w.currentField() == QLatin1String("LOCO_MODE"),
              "choosing it plots that packet's field");
        auto *button = w.findChild<QToolButton *>(QStringLiteral("fieldPlotChooser"));
        CHECK(button && button->text() == QStringLiteral("lsrp \u25B8 LOCO_MODE"), "and the chooser says so");
        CHECK(w.canvas()->series().points.size() == 30, "30 LSRP samples, no ARP ones");

        w.plotField(QStringLiteral("LOCO_MODE"), QStringLiteral("arp"));
        CHECK(w.currentType() == QLatin1String("arp"), "a caller can name the packet");
        w.plotField(QStringLiteral("LOCO_MODE"));
        CHECK(!w.currentType().isEmpty(), "without one, a packet that carries it is chosen");

        FieldPlotCanvas *c = w.canvas();
        c->resetZoom();
        CHECK(!c->isZoomed(), "fit shows everything");
        const qint64 span0 = c->viewToMs() - c->viewFromMs();
        c->zoomIn();
        CHECK(c->isZoomed() && (c->viewToMs() - c->viewFromMs()) < span0 * 0.6, "zoom in halves the time span");
        c->zoomOut();
        c->zoomOut();
        CHECK((c->viewToMs() - c->viewFromMs()) <= qint64(span0 * 1.05) + 1000,
              "zoom out never goes past the data");
        c->setTimeView(t0 + 5000, t0 + 9000);
        CHECK(c->viewFromMs() >= t0 + 4999 && c->viewToMs() <= t0 + 9001, "a set time window is shown as asked");
        QTest::qWait(400);   // the re-extraction debounce
        CHECK(c->series().points.size() >= 4, "and the window still has its samples");

        // Mouse: wheel zooms about the cursor, double-click fits.
        c->resetZoom();
        const QPoint mid(c->width() / 2, c->height() / 2);
        QWheelEvent wheel(QPointF(mid), QPointF(c->mapToGlobal(mid)), QPoint(), QPoint(0, 240),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(c, &wheel);
        CHECK(c->isZoomed(), "the wheel zooms in");
        QTest::mouseDClick(c, Qt::LeftButton, Qt::NoModifier, mid);
        CHECK(!c->isZoomed(), "double-click fits again");

        // Drag a box across the middle third: time zooms to it.
        const int x1 = c->width() / 3, x2 = c->width() * 2 / 3, y = c->height() / 2;
        QTest::mousePress(c, Qt::LeftButton, Qt::NoModifier, QPoint(x1, y));
        QMouseEvent move(QEvent::MouseMove, QPointF(x2, y + 2), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(c, &move);
        QTest::mouseRelease(c, Qt::LeftButton, Qt::NoModifier, QPoint(x2, y + 2));
        CHECK(c->isZoomed() && (c->viewToMs() - c->viewFromMs()) < span0 / 2,
              "dragging a box zooms to it");
        const double valueSpan = c->viewMaxValue() - c->viewMinValue();
        CHECK(valueSpan > 0, "values stay fitted to what is in view");

        QTest::keyClick(c, Qt::Key_0);
        CHECK(!c->isZoomed(), "0 fits");

        const QByteArray dir = qgetenv("DL_SHOTS");
        if (!dir.isEmpty()) {
            w.plotField(QStringLiteral("LOCO_MODE"), QStringLiteral("lsrp"));
            QTest::qWait(20);
            w.grab().save(QDir(QString::fromLocal8Bit(dir)).filePath(QStringLiteral("plot_fit.png")));
            c->setTimeView(t0 + 8000, t0 + 16000);
            QTest::qWait(400);
            QTest::mouseMove(c, QPoint(c->width() / 2, c->height() / 2));
            QTest::qWait(20);
            w.grab().save(QDir(QString::fromLocal8Bit(dir)).filePath(QStringLiteral("plot_zoomed.png")));
        }
    }
}

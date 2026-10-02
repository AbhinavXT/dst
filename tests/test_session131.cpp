#include "testutil.h"
#include "layoutaudit.h"

#include "logmodel.h"
#include "statusline.h"
#include "trackdiagram.h"
#include "trackdiagramwindow.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QSlider>

// =============================================================================
//  Session 131 — UI revamp, tool windows 7: the Track diagram.
//
//  0 m is "location not known" (Abhinav, 2026-10-02): a loco that has not
//  localised on an RFID tag reports abs_loco_loc = 0. Those samples no
//  longer set the span, place a signal, pin an event or draw the loco.
//  The canvas: lanes, labels stacked so none overlaps, a km scale, a
//  legend, the loco's state at the cursor, tooltips on every mark.
//  Real captures: replay/loco_1_1_26062026_162418.cap (352 of its @dmi
//  frames at 0 m, six tags) and loco_1_1_29062026_134509.cap (signals).
// =============================================================================

namespace {

// A whole real capture, at its own timestamps.
void loadCapture(LogModel *m, const QString &name, int maxLines = 1 << 30)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/") + name);
    if (!f.open(QIODevice::ReadOnly)) return;
    QVector<LogEntryPtr> v;
    while (!f.atEnd() && v.size() < maxLines) {
        const QString l = QString::fromUtf8(f.readLine()).trimmed();
        if (!l.startsWith(QLatin1Char('@'))) continue;
        auto e = QSharedPointer<LogEntry>::create();
        e->text = l;
        e->epochMs = QDateTime::fromString(l.section(QLatin1Char(' '), 1, 1), Qt::ISODate).toMSecsSinceEpoch();
        e->cacheDerived();
        v << e;
    }
    m->appendEntries(v);
}

bool labelsClear(const TrackDiagramCanvas &c, QByteArray *why)
{
    const QVector<QRect> &r = c.labelRects();
    for (int i = 0; i < r.size(); ++i) {
        if (r.at(i).left() < 0 || r.at(i).right() > c.width()) { *why = "a label leaves the canvas"; return false; }
        for (int j = i + 1; j < r.size(); ++j)
            if (r.at(i).adjusted(1, 1, -1, -1).intersects(r.at(j).adjusted(1, 1, -1, -1))) {
                *why = "two labels overlap";
                return false;
            }
    }
    return true;
}

int indexWhere(const TrackDiagram::Diagram &d, bool known)
{
    for (int i = 0; i < d.trace.samples.size(); ++i)
        if (TrackDiagram::locationKnown(d.trace.samples.at(i)) == known) return i;
    return -1;
}

bool hasHit(const TrackDiagramCanvas &c, const QString &prefix)
{
    for (const TrackDiagramCanvas::Hit &h : c.hits()) if (h.text.startsWith(prefix)) return true;
    return false;
}

}  // namespace

TEST_SUITE(session131)
{
    // ---- 0 m: location not known ----------------------------------------------------
    LogModel run(nullptr, 200000);
    loadCapture(&run, QStringLiteral("loco_1_1_26062026_162418.cap"));
    const TrackDiagram::Diagram d = TrackDiagram::build(&run, QStringLiteral("1_1"), QStringLiteral("L1_V1"));
    CHECK(!d.isEmpty() && d.hasLocation(), "fixture: a real run with a location");
    CHECK(d.unknownSamples == 352, QByteArray("the 352 frames at 0 m are counted as not localised (")
                                       + QByteArray::number(d.unknownSamples) + ")");
    CHECK(d.minLocM > 152000.0 && d.maxLocM >= 161253.0 && d.maxLocM < 162000.0,
          QByteArray("the span is the run's, 152.7..161.3 km, not 0..161 km (")
              + QByteArray::number(d.minLocM, 'f', 0) + ".." + QByteArray::number(d.maxLocM, 'f', 0) + ")");
    CHECK(d.trace.samples.size() == 826, "the 0 m samples stay in the trace: the cursor still steps through them");
    CHECK(d.tags.size() == 6, "the six real tags");
    bool eventAtZero = false;
    for (const TrackDiagram::EventMark &e : d.events) if (e.locM <= 0.0) eventAtZero = true;
    CHECK(!eventAtZero, "no event is pinned at 0 m");
    CHECK(d.unpinnedEvents == 22, QByteArray("events raised while not localised are counted, not pinned (")
                                      + QByteArray::number(d.unpinnedEvents) + ")");

    // ---- the canvas -----------------------------------------------------------------
    {
        TrackDiagramCanvas canvas;
        canvas.resize(1000, 420);
        canvas.setDiagram(d);
        canvas.show();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

        const int known = indexWhere(d, true), unknown = indexWhere(d, false);
        CHECK(known >= 0 && unknown >= 0, "fixture: samples of both kinds");
        canvas.setCursorIndex(known);
        canvas.repaint();
        CHECK(hasHit(canvas, QStringLiteral("Loco at ")), "cursor on a known location: the loco is drawn");
        canvas.setCursorIndex(unknown);
        canvas.repaint();
        CHECK(!hasHit(canvas, QStringLiteral("Loco at ")), "cursor on a 0 m frame: no loco drawn at 0 m");

        int tagTips = 0;
        for (const TrackDiagramCanvas::Hit &h : canvas.hits())
            if (h.text.startsWith(QLatin1String("RFID tag "))) {
                ++tagTips;
                if (!canvas.rect().contains(h.rect.center())) tagTips = -100;
            }
        CHECK(tagTips == 6, "every tag has a tooltip, on the canvas");
        CHECK(canvas.labelRects().size() == 6, QByteArray("all six tag ids fit, stacked (")
                                                   + QByteArray::number(canvas.labelRects().size()) + " drawn)");
        QByteArray why;
        CHECK(labelsClear(canvas, &why), QByteArray("tag labels: ") + (why.isEmpty() ? "clear" : why));
    }

    // ---- signals and their labels (another capture) -----------------------------------
    {
        LogModel sig(nullptr, 200000);
        loadCapture(&sig, QStringLiteral("loco_1_1_29062026_134509.cap"));
        const TrackDiagram::Diagram ds = TrackDiagram::build(&sig, QStringLiteral("1_1"), QStringLiteral("L1_V1"));
        CHECK(ds.signalMarks.size() == 3, "fixture: three real signals");
        bool placed = true;
        for (const TrackDiagram::SignalMark &s : ds.signalMarks) placed = placed && s.locM > 100000.0;
        CHECK(placed, "no signal placed relative to a 0 m loco");
        TrackDiagramCanvas canvas;
        canvas.resize(1000, 420);
        canvas.setDiagram(ds);
        canvas.show();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        canvas.repaint();
        int sigTips = 0, maTips = 0;
        for (const TrackDiagramCanvas::Hit &h : canvas.hits()) {
            if (h.text.startsWith(QLatin1String("Signal "))) ++sigTips;
            if (h.text.startsWith(QLatin1String("Movement authority end"))) ++maTips;
        }
        CHECK(sigTips == 3 && maTips >= 1, QByteArray("signals and MA ends have tooltips (")
                                               + QByteArray::number(sigTips) + " signals, " + QByteArray::number(maTips) + " MA ends)");
        QByteArray why;
        CHECK(canvas.labelRects().size() >= 3 + 4, "the signal names and most tag ids are drawn");
        CHECK(labelsClear(canvas, &why), QByteArray("tag and signal labels, the last one at the right edge: ")
                                             + (why.isEmpty() ? "clear" : why));
    }

    // ---- nothing but 0 m: says so ----------------------------------------------------------
    {
        LogModel early(nullptr, 2000);
        loadCapture(&early, QStringLiteral("loco_1_1_26062026_162418.cap"), 300);   // before the first tag
        const TrackDiagram::Diagram de = TrackDiagram::build(&early, QStringLiteral("1_1"), QStringLiteral("L1_V1"));
        CHECK(!de.isEmpty() && !de.hasLocation(), "the first 300 lines: frames, but none localised");
        TrackDiagramWindow w(&early, QStringLiteral("1_1"), QStringLiteral("L1_V1"));
        auto *status = w.findChild<StatusLine *>();
        CHECK(status && status->text().contains(QLatin1String("No location")), "the status says there is no location");
    }

    // ---- the window ----------------------------------------------------------------------------
    {
        TrackDiagramWindow w(&run, QStringLiteral("1_1"), QStringLiteral("L1_V1"));
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.resize(1100, 560);
        w.show();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        auto *status = w.findChild<StatusLine *>();
        const QString st = status ? status->text() : QString();
        CHECK(st.startsWith(QStringLiteral("6 RFID tags \u00B7 0 signals \u00B7 1 event")),
              QByteArray("the counts read as English (") + st.toUtf8() + ")");
        CHECK(st.contains(QLatin1String("not localised (0 m): 352 of 826 frames, 22 events")),
              "the status says what was left off the rail");
        CHECK(status && !status->toolTip().isEmpty(), "and its tooltip says why");
        CHECK(status && status->fontMetrics().horizontalAdvance(st) < w.width() - 160,
              "the status fits one line next to Save image…");
        QSlider *slider = w.findChild<QSlider *>();
        CHECK(slider && slider->maximum() == 825, "the slider still spans every sample");
        CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366,
              QByteArray("fits a 1366 x 768 laptop (minimum ")
                  + QByteArray::number(w.minimumSizeHint().width()) + " x "
                  + QByteArray::number(w.minimumSizeHint().height()) + ")");
        const QStringList loose = LayoutAudit::orphans(&w);
        CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (")
                                   + loose.join(QLatin1String(", ")).toUtf8() + ")");
    }
}

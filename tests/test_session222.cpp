#include "testutil.h"

#include "rfidtag.h"
#include "settings.h"
#include "stationlayoutwindow.h"
#include "tagbuilderwindow.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPushButton>

// =============================================================================
//  Session 222 — Station Layout: new tag, signal, point, text and line from
//  the drawing (right-click). The built-in station.xlsx.
// =============================================================================

namespace {
int tagIndex(const StationLayout::Layout &l, const QString &name)
{
    for (int i = 0; i < l.tags.size(); ++i) if (l.tags.at(i).name == name) return i;
    return -1;
}
const StationLayout::Line *lineOf(const StationLayout::Layout &l, const QString &id)
{
    for (const StationLayout::Line &ln : l.lines) if (ln.line == id) return &ln;
    return nullptr;
}
QStringList actionTexts(QMenu *m)
{
    QStringList out;
    for (QAction *a : m->actions()) if (!a->isSeparator()) out << a->text();
    return out;
}
}  // namespace

TEST_SUITE(session222)
{
    Settings::setStationLayoutLastFile(QString());
    StationLayoutWindow w;
    CHECK(w.reopenLast(), "the built-in layout opens");
    w.resize(1400, 800);
    w.show();
    QApplication::processEvents();

    // A line with two decodable main tags far enough apart to put one between them.
    const QHash<QString, qint64> loc = StationLayout::tagLocations(w.station());
    QString lineId, a, b;
    for (const StationLayout::Line &ln : w.station().lines) {
        for (int i = 0; i + 1 < ln.tags.size() && lineId.isEmpty(); ++i)
            if (loc.contains(ln.tags.at(i)) && loc.contains(ln.tags.at(i + 1))
                && std::llabs(loc.value(ln.tags.at(i)) - loc.value(ln.tags.at(i + 1))) > 100) {
                lineId = ln.line; a = ln.tags.at(i); b = ln.tags.at(i + 1);
            }
        if (!lineId.isEmpty()) break;
    }
    CHECK(!lineId.isEmpty(), QByteArray("line ") + lineId.toUtf8() + ": tags " + a.toUtf8() + ", " + b.toUtf8());
    if (lineId.isEmpty()) return;
    const qint64 mid = (loc.value(a) + loc.value(b)) / 2;

    // ---- the drawing's right-click names the line and the place ----
    const QPointF pa = w.canvas()->tagRect(a).center();
    CHECK(w.canvas()->lineAt(pa.y()) == lineId, "the lane under tag " + a.toUtf8() + " is its line");
    QString gotTag, gotLine;
    qint64 gotLoc = 0;
    QObject::connect(w.canvas(), &StationLayoutCanvas::contextRequested, &w,
                     [&](const QPoint &, const QString &t, const QString &l, qint64 m, bool) { gotTag = t; gotLine = l; gotLoc = m; });
    QContextMenuEvent onTag(QContextMenuEvent::Mouse, pa.toPoint(), w.canvas()->mapToGlobal(pa.toPoint()));
    QApplication::sendEvent(w.canvas(), &onTag);
    CHECK(gotTag == a && gotLine == lineId && std::llabs(gotLoc - loc.value(a)) < 50,
          QByteArray("right-click on tag ") + a.toUtf8() + ": the tag, its line, its place (" + QByteArray::number(gotLoc) + ")");
    QApplication::processEvents();
    for (QMenu *m : w.findChildren<QMenu *>()) m->close();

    QMenu *m = w.contextMenuFor(a, lineId, mid, false);
    const QString all = actionTexts(m).join(QLatin1Char('|'));
    CHECK(all.contains(QStringLiteral("Edit tag ") + a) && all.contains(QStringLiteral("New signal at tag ") + a)
              && all.contains(QStringLiteral("New tag here (line ") + lineId) && all.contains(QStringLiteral("New point here"))
              && all.contains(QStringLiteral("New text here")) && all.contains(QStringLiteral("New line")),
          all.toUtf8());
    delete m;
    CHECK(!actionTexts(m = w.contextMenuFor(QString(), QString(), mid, true)).join(QLatin1Char('|')).contains(QStringLiteral("New tag here")),
          "off every lane: no \"New tag here\"");
    delete m;

    // ---- new tag at a place on the line ----
    const int n = w.station().tags.size();
    const int inLine = lineOf(w.station(), lineId)->tags.size();
    TagBuilderWindow *tb = w.openNewTagAt(lineId, mid);
    const RfidTag::Summary s = RfidTag::summary(tb->tag());
    CHECK(s.ok && s.crcOk && tagIndex(w.station(), RfidTag::nameOf(tb->tag())) < 0,
          QByteArray("the builder starts with an id the layout does not use (") + RfidTag::nameOf(tb->tag()).toUtf8() + ")");
    CHECK(tb->applyButton()->text().contains(lineId), "it offers \"Add this tag to line …\"");
    const QString name = RfidTag::nameOf(tb->tag());
    tb->applyButton()->click();
    const StationLayout::Line *ln = lineOf(w.station(), lineId);
    const int pos = ln->tags.indexOf(name);
    CHECK(w.station().tags.size() == n + 1 && ln->tags.size() == inLine + 1, "added to the tags and to the line");
    CHECK(pos > 0 && ln->tags.at(pos - 1) == a && ln->tags.value(pos + 1) == b,
          QByteArray("in the line between ") + a.toUtf8() + " and " + b.toUtf8() + ", by location");
    CHECK(StationLayout::tagLocations(w.station()).value(name) == mid, "at the place clicked");
    CHECK(w.undo() && w.station().tags.size() == n && lineOf(w.station(), lineId)->tags.size() == inLine, "one Undo removes it");

    // ---- signal, line, point, text ----
    const int sigs = w.station().signalList.size();
    CHECK(!w.addSignal({ QStringLiteral("no-such-tag"), QStringLiteral("S1"), 1, 1 }) && w.station().signalList.size() == sigs,
          "a signal on a tag the layout lacks is refused");
    CHECK(w.addSignal({ a, QStringLiteral("S99"), 99, 527 }) && w.station().signalList.size() == sigs + 1
              && w.station().signalList.last().footTag == a,
          "a signal at tag " + a.toUtf8());
    const int lines = w.station().lines.size();
    CHECK(!w.addLine({ QString(), lineId, {} }), "a line id already used is refused");
    CHECK(w.addLine({ QStringLiteral("Loop 9"), QStringLiteral("UL9"), {} }) && w.station().lines.size() == lines + 1,
          "a new line UL9");
    QApplication::processEvents();
    bool lane = false;
    for (int y = 0; y < w.canvas()->height() && !lane; y += 4) lane = w.canvas()->lineAt(y) == QLatin1String("UL9");
    CHECK(lane, "UL9 gets its own lane in the drawing");
    StationLayout::Point p;
    p.name = QStringLiteral("P99");
    p.line1 = lineId;
    p.line2 = lineId;
    CHECK(!w.addPoint(p), "a point from a line to itself is refused");
    p.line2 = QStringLiteral("UL9");
    p.loc1 = p.loc2 = mid;
    CHECK(w.addPoint(p) && w.station().points.last().name == QLatin1String("P99"), "point P99 to UL9");
    CHECK(!w.addPoint(p), "a second P99 is refused");
    CHECK(!w.addText({ QStringLiteral("  "), mid, 1 }) && w.addText({ QStringLiteral("Cabin"), mid, -1 }), "an empty text refused; Cabin added");
    int undos = 0;
    while (w.undo()) ++undos;
    CHECK(undos == 4 && w.station().signalList.size() == sigs && w.station().lines.size() == lines,
          QByteArray("four Undo steps back to the file (") + QByteArray::number(undos) + ")");
}

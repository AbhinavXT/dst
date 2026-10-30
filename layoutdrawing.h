#ifndef LAYOUTDRAWING_H
#define LAYOUTDRAWING_H

// =============================================================================
//  A station's tag-TIN layout drawing, as data (session 430)
//  -----------------------------------------------------------------------------
//  The RDSO tag-TIN layout (Annexure-H) of a station, read off the drawing by
//  hand: each tag's number, kilometre, kind (the drawing's N / S / T / X / L /
//  G / A), the way its triangle points, its line, and the TIN section either
//  side of it; the signals at their foot tags; the station. Station Layout
//  checks a layout against it (Open ▸ Compare with ▸ A tag-layout drawing):
//  what the drawing shows and the layout's tags do not say, one line each.
//
//  The file: JSON, "format": "dlconsole-layout-drawing", version 1:
//    { "title", "source", "station": { "id", "location" },
//      "tags": [ { "tag", "loc" (m), "kind", "dir" (">" nominal, "<" reverse,
//                  absent: none), "line", "tin": [reverse, nominal] (0: white,
//                  non-KAVACH), "dashed" (another station's), "border" } ],
//      "signals": [ { "name", "foot" } ] }
//  Built in: :/layout_drawings/*.json (layout_drawings.qrc).
// =============================================================================

#include "stationlayout.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace LayoutDrawing {

struct Tag {
    int     number = 0;
    qint64  loc = 0;
    QChar   kind;                  // N, S, T, X, L, G, A
    int     dir = 0;               // +1 nominal (the triangle points ▷), -1 reverse (◁), 0 not drawn
    QString line;
    int     tinRev = -1, tinNom = -1;   // -1: not given
    bool    dashed = false, border = false;
};
struct Signal { QString name; int foot = 0; };
struct Drawing {
    QString title, source;
    qint64  stationId = 0, stationLoc = 0;
    QVector<Tag> tags;
    QVector<Signal> signalList;
};

bool fromJson(const QByteArray &json, Drawing *out, QString *err = nullptr);
bool load(const QString &path, Drawing *out, QString *err = nullptr);
QStringList builtIn();             // resource paths, sorted

// What the drawing shows that the layout does not say. A tag of the drawing
// is matched with the layout's main tag of its number nearest its kilometre
// (a layout that repeats a stretch has several); the layout's other tags
// between the drawing's first and last kilometre are "not on the drawing".
struct Finding {
    enum Kind {
        Station,       // the station sheet
        Moved,         // another kilometre
        KindDiffers,   // type / placement against the drawing's letter and triangle
        TinDiffers,    // tin_rev / tin_nom against the colours either side
        Repeated,      // the number again in the drawing's stretch
        NotOnDrawing,  // a layout tag in the stretch whose number the drawing has not
        SignalDiffers, // the signals sheet against the drawing's signals
        NotInLayout    // the drawing's tags the layout has not, one line per line of the drawing
    };
    Kind    kind = Moved;
    QString key;                   // the layout's tag key (StationLayout::tagKeys), when there is one
    QString text;
};
QVector<Finding> check(const Drawing &d, const StationLayout::Layout &l);

}  // namespace LayoutDrawing

#endif // LAYOUTDRAWING_H

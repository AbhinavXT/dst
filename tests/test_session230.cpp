#include "testutil.h"

#include "stationlayoutwindow.h"

#include <QPixmap>

// =============================================================================
//  Session 230 — Station Layout: the station's texts no longer overprint.
//  Texts at nearby locations (the S2S_FREQ… notes above the lanes) were all
//  drawn on one row. Now each goes on the first row where it is clear, and
//  the space over (or under) the lanes grows to hold the rows. Checked on
//  every station file of the built-in library (the Python tool's 47), drawn
//  at 1200 px: no two texts' boxes overlap.
// =============================================================================

TEST_SUITE(session230)
{
    int files = 0, withTexts = 0, maxRows = 0;
    QString worst;
    for (const QString &path : StationLayout::library()) {
        StationLayout::Layout l;
        if (!StationLayout::load(path, &l) || l.texts.isEmpty()) continue;
        ++files;
        StationLayoutCanvas c;
        c.setStation(l);
        c.resize(1200, c.minimumSizeHint().height());
        c.resize(1200, c.minimumSizeHint().height());
        c.grab();                                         // paints: the hit boxes are made there
        QVector<QRectF> boxes;
        for (const StationLayoutCanvas::Hit &h : c.hits())
            if (h.tag.isEmpty() && h.text.startsWith(QStringLiteral("Text at"))) boxes << h.rect;
        bool clear = boxes.size() == l.texts.size();
        for (int i = 0; i < boxes.size() && clear; ++i)
            for (int j = i + 1; j < boxes.size() && clear; ++j)
                clear = !boxes.at(i).adjusted(1, 1, -1, -1).intersects(boxes.at(j).adjusted(1, 1, -1, -1));
        CHECK(clear, QByteArray("texts apart in ") + path.toUtf8());
        ++withTexts;
        int rows = 0;
        for (const QRectF &b : boxes) rows = qMax(rows, int(b.top()));
        Q_UNUSED(rows);
        // The texts stay clear of the title band (it ends near y 30).
        double top = 1e9;
        for (const QRectF &b : boxes) top = qMin(top, b.top());
        CHECK(boxes.isEmpty() || top > 30, QByteArray("below the title in ") + path.toUtf8());
        if (boxes.size() > maxRows) { maxRows = boxes.size(); worst = path; }
    }
    CHECK(files > 5, QByteArray("library files with texts: ") + QByteArray::number(files));
    std::printf("  NOTE session230: %d files with texts; most texts in %s (%d)\n", withTexts, qPrintable(worst), maxRows);
}

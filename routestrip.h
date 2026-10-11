#ifndef ROUTESTRIP_H
#define ROUTESTRIP_H

// =============================================================================
//  Route strip (session 196) — the RFID Tag Builder's picture of a route.
//  -----------------------------------------------------------------------------
//  One line by absolute location, lowest on the left: a tick per tag (main
//  tall, duplicate short), its name, the signals at their foot tags, an arrow
//  for the direction of travel. A tag whose CRC-30 fails is drawn in the
//  error colour, an adjustment tag in the accent colour, and the selected
//  tag is ringed. Click a tag to select it. Tags with no location ("N/A")
//  are not drawn; the count says how many.
//
//  Session 212: drawn as the RDSO Annexure-H layouts are (tinlayout.h): the
//  line in its TIN colours with "(N-…)" under it, one symbol per main +
//  duplicate set with its placement letter, "R-…" over it.
//
//  The Tools ▸ Monitor ▸ Track diagram draws what a loco REPORTED in a log;
//  this draws a route being written, with no log behind it.
// =============================================================================

#include "rfidtag.h"

#include <QWidget>

class RouteStrip : public QWidget
{
    Q_OBJECT
public:
    explicit RouteStrip(QWidget *parent = nullptr);

    void setRoute(const RfidTag::Route &route);
    void setSelectedRow(int row);
    int  selectedRow() const { return m_selected; }
    int  rowAt(const QPoint &p) const;     // nearest drawn tag within a few px, -1 if none
    int  drawnTags() const;                // tags with a location

    QSize sizeHint() const override { return QSize(600, 100); }
    QSize minimumSizeHint() const override { return QSize(200, 100); }

signals:
    void rowClicked(int row);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    double xOf(qint64 loc) const;

    struct Mark { int row; qint64 loc; bool dup, crcOk; QString name; int unique, tinNom; QChar letter; };
    QVector<Mark> m_marks;
    QVector<QPair<qint64, QString>> m_signals;     // location, name
    qint64 m_lo = 0, m_hi = 0;
    int m_dir = 0;
    int m_selected = -1;
};

#endif  // ROUTESTRIP_H

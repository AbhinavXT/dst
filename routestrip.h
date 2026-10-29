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
//  The Tools ▸ Monitor ▸ Running ▸ Track diagram draws what a loco REPORTED in a log;
//  this draws a route being written, with no log behind it.
//
//  Session 397: the Loco Simulator's RFID tab draws its route with it, twice
//  (the whole route, and a stretch that follows the loco), and adds under
//  the line: the loco where it is, where speed sensor 1's pulses put it, each
//  tag's state (sent on reader 1 / 2, skipped), and reader 2's 20-24 m
//  window. None of it is drawn until set, so the Tag Builder's strip is as
//  it was.
//
//  Session 401: a double-click on a tag and a right-click anywhere are passed
//  on (rowDoubleClicked, contextRequested), for the Tag Builder's edit window
//  and its New tag here….
// =============================================================================

#include "rfidtag.h"

#include <QHash>
#include <QWidget>

class QPainter;

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

    // ---- session 397: the Loco Simulator's overlay ----------------------------
    // Draw only [lo, hi] (the follow view); clearView() draws the whole route.
    void setView(double lo, double hi);
    void clearView();
    bool hasView() const { return m_viewSet; }
    double viewLow() const { return m_viewSet ? m_viewLo : double(m_lo); }
    double viewHigh() const { return m_viewSet ? m_viewHi : double(m_hi); }
    // The loco (a filled loco under the line) and where the pulses put it (a
    // hollow outline). NaN hides one; the overlay rows are added on first use.
    void setLoco(double loc, double pulsesLoc);
    double loco() const { return m_loco; }
    double pulsesLoco() const { return m_pulsesLoco; }
    enum TagState { SentOn1 = 1, SentOn2 = 2, Skipped = 4, Faulted = 8 };
    void setTagStates(const QHash<int, int> &states);   // route row -> TagState flags
    int  tagState(int row) const { return m_states.value(row); }
    // Session 405: a Faulted tag's word on its chip ("late", "×2", "crc").
    void setTagLabels(const QHash<int, QString> &labels);
    QString tagLabel(int row) const { return m_labels.value(row); }
    void setReader2Window(double from, double to);      // NaN: none
    double xAt(double loc) const;                        // the widget x of a location

    QSize sizeHint() const override { return QSize(600, m_overlay ? 150 : 100); }
    QSize minimumSizeHint() const override { return QSize(200, m_overlay ? 150 : 100); }

    double locAt(double x) const;                        // session 401: the location at a widget x

signals:
    void rowClicked(int row);
    // Session 401: a tag double-clicked; a right-click (`row` -1 off the tags)
    // at location `loc`, for the caller's menu at `globalPos`.
    void rowDoubleClicked(int row);
    void contextRequested(int row, double loc, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    double xOf(qint64 loc) const;
    void   useOverlay();
    void   paintOverlay(QPainter &p);

    struct Mark { int row; qint64 loc; bool dup, crcOk; QString name; int unique, tinNom; QChar letter; };
    QVector<Mark> m_marks;
    QVector<QPair<qint64, QString>> m_signals;     // location, name
    qint64 m_lo = 0, m_hi = 0;
    int m_dir = 0;
    int m_selected = -1;
    bool   m_overlay = false;
    bool   m_viewSet = false;
    double m_viewLo = 0, m_viewHi = 0;
    double m_loco, m_pulsesLoco, m_window2From, m_window2To;   // NaN until set (routestrip.cpp)
    QHash<int, int> m_states;
    QHash<int, QString> m_labels;
};

#endif  // ROUTESTRIP_H

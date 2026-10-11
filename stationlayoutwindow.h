#ifndef STATIONLAYOUTWINDOW_H
#define STATIONLAYOUTWINDOW_H

// =============================================================================
//  Tools ▸ Station Layout… (session 208) — stationlayout.h for the data side.
//  -----------------------------------------------------------------------------
//  A schematic of the station on top: every line of the lines sheet as a
//  lane, its tags (diamonds) at their own abs_loc, signals at their foot
//  tags, points joining two lanes, station markers and texts. Drag a tag
//  along its lane to move it (re-encoded, CRC-30 recomputed). Below, one
//  table per sheet, edited in place, and the Checks list. Open / Save the
//  own .json; Import / Export the old Python tool's .xlsx. Ctrl+Z undoes.
//  Session 212: drawn as the RDSO Annexure-H Tag-TIN layouts are (tinlayout.h):
//  TIN-coloured bands, one symbol per tag set with its letter, "R-…" and km
//  above, gap dimensions, title, centre line, legend; Export ▸ PDF / PNG.
//  Session 218: arranged as the RDSO sheets are: DN loops above DN main, UP
//  loops below UP main, turnout tags on the points, the yard drawn wider.
// =============================================================================

#include "stationlayout.h"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QWidget>

class QLabel;
class QPainter;
class QListWidget;
class QScrollArea;
class TagBuilderWindow;
class QMenu;
class QTabWidget;
class QTableWidget;
class StatusLine;
class UndoLog;

#include <functional>

class QMenu;

// Session 211: Library ▸ a submenu per group of built-in station files;
// `pick` gets the chosen resource path. Shared by Station Layout and the
// Track diagram.
QMenu *stationLibraryMenu(QWidget *parent, const std::function<void(const QString &)> &pick);

class StationLayoutCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit StationLayoutCanvas(QWidget *parent = nullptr);
    void setStation(const StationLayout::Layout &l);
    void setSelectedTag(const QString &name);
    // Session 220: 1 = the drawing fits the window; each step widens what is seen.
    // `anchorX` (canvas x) stays under the same place; -1 = the visible middle.
    void setZoom(double z, double anchorX = -1);
    double zoom() const { return m_zoom; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    double xOf(qint64 locM) const;
    qint64 locAt(double x) const;
    QRectF tagRect(const QString &name) const;   // where a tag is drawn (tests)

    // Session 222: the line whose lane is at canvas y (within half a lane), or "".
    QString lineAt(double y) const;

    struct Hit { QRectF rect; QString tag; QString text; };
    const QVector<Hit> &hits() const { return m_hits; }

signals:
    void tagClicked(const QString &name);
    void tagDoubleClicked(const QString &name);   // session 221
    // Session 222: a right-click: the tag under it (or ""), the line whose lane
    // it is on (or ""), the location under it, and whether it is above the lanes.
    void contextRequested(const QPoint &globalPos, const QString &tag, const QString &line, qint64 locM, bool aboveLanes);
    void tagDragged(const QString &name, qint64 newLocM);

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    QScrollArea *scrollArea() const;
    void layoutLanes();
    int laneY(int lane) const;
    // Session 218: where a tag is drawn; a point's tag rides its diagonal.
    QPointF tagPos(const QString &name, double x) const;
    QFont smallFont() const;
    int sheetHeight(int width) const;
    int legend(QPainter *p, int top, int width) const;

    StationLayout::Layout m_l;
    QHash<QString, qint64> m_loc;      // tag -> abs_loc
    QHash<QString, StationLayout::TagInfo> m_info;
    QHash<QString, int>    m_lane;     // tag -> lane
    QStringList m_laneNames;           // "DN  DM", ..., "(no line)"
    QHash<QString, int> m_laneOfLine;  // line id -> lane
    QSet<QString> m_flagged;           // tags checks() names
    qint64 m_min = 0, m_max = 1;
    // Session 218: x is piecewise linear, (location, fraction of the width)
    // knots: the yard between the points is drawn wider than the block sections.
    QVector<QPair<qint64, double>> m_knots;
    QHash<QString, int> m_pointOfTag;  // tag -> index into m_l.points (a turnout tag)
    double m_zoom = 1.0;
    QString m_selected;
    QVector<Hit> m_hits;
    QString m_dragTag;
    double  m_dragX = -1;
    bool    m_panning = false;         // session 220: the sheet is grabbed
    QPoint  m_panFrom;                 // where (global)
    int     m_panH = 0, m_panV = 0;    // the scroll bars when the grab began
};

class StationLayoutWindow : public QWidget
{
    Q_OBJECT
public:
    explicit StationLayoutWindow(QWidget *parent = nullptr);

    bool openFile(const QString &path);       // .json, or an .xlsx (imported)
    bool saveFile(const QString &path);       // .json
    bool exportXlsx(const QString &path);
    // Session 212: the drawing as a printable sheet, .pdf (A3 landscape) or
    // .png, by extension.
    bool exportDrawing(const QString &path);
    bool reopenLast();
    void setStation(const StationLayout::Layout &l);
    const StationLayout::Layout &station() const { return m_layout; }
    bool isModified() const { return m_modified; }
    StationLayoutCanvas *canvas() const { return m_canvas; }
    QTabWidget *tabs() const { return m_tabs; }
    QTableWidget *table(int sheet) const;     // 0 tags, 1 signals, 2 points, 3 lines, 4 station, 5 texts
    bool undo();
    // Move a tag (what a drag does).
    bool moveTag(const QString &name, qint64 locM);
    // Session 221: the RFID Tag Builder on one of the layout's tags (`name`),
    // or on a new one ("": started from the selected tag, if any). Its Apply
    // button comes back to applyTag().
    TagBuilderWindow *openInTagBuilder(const QString &name);
    TagBuilderWindow *tagBuilder() const;
    // `bytes` into the layout: replaces tag `target` ("" = adds it), named by
    // its bits; the lines and signals naming `target` follow a new name. One
    // Undo step. Refused (false, status says why) for bytes that are not a
    // tag, or a name another tag already has.
    bool applyTag(const QString &target, const QByteArray &bytes);

    // Session 222: adding from the drawing (right-click). A new tag at `locM` on
    // `line`: the Tag Builder starts from the nearest tag of that line, at that
    // location, with the lowest id the layout does not use; Apply adds it and
    // puts it in the line, in location order.
    TagBuilderWindow *openNewTagAt(const QString &line, qint64 locM);
    // Each is one Undo step; false (status says why) when refused.
    bool addSignal(const StationLayout::Signal &s);   // its foot tag must be in the layout
    bool addLine(const StationLayout::Line &ln);      // a line id not used yet
    bool addPoint(const StationLayout::Point &p);     // a new name; two different lines of the layout
    bool addText(const StationLayout::Text &t);       // some text
    // The menu a right-click on the drawing opens (built, not shown: tests).
    QMenu *contextMenuFor(const QString &tag, const QString &line, qint64 locM, bool aboveLanes);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void refreshAll();
    void fillTags();
    void fillSheet(int sheet);
    void onTagCell(int row, int col);
    void onSheetCell(int sheet);
    void addRow();
    void deleteRows();
    void changed(const QString &label, const StationLayout::Layout &before);
    void setModified(bool on);
    bool confirmDiscard();

    StationLayout::Layout m_layout;
    QPointer<TagBuilderWindow> m_builder;   // session 221
    QString m_builderTarget;           // the tag it edits; "" = a new tag
    QString m_builderLine;             // session 222: a new tag goes into this line
    QString m_path;                    // the .json being edited; empty after an import
    bool m_modified = false;
    bool m_updating = false;
    StationLayoutCanvas *m_canvas = nullptr;
    QScrollArea *m_scroll = nullptr;
    QTabWidget *m_tabs = nullptr;
    QVector<QTableWidget *> m_tables;
    QListWidget *m_checks = nullptr;
    QLabel *m_other = nullptr;
    StatusLine *m_status = nullptr;
    UndoLog *m_undo = nullptr;
};

#endif // STATIONLAYOUTWINDOW_H

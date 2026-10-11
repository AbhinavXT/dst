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

#include "planrun.h"
#include "rfidtag.h"
#include "stationlayout.h"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QWidget>

class QLabel;
class QPushButton;
class QPainter;
class QListWidget;
class QScrollArea;
class TagBuilderWindow;
class MessageDispatcher;
class LogModel;
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
    // Session 226: tags that differ from a compared file: ringed, by kind.
    void setDiffTags(const QHash<QString, int> &kinds);   // name -> StationLayout::TagChange::Kind
    // Session 227: a loco's run over the layout: name -> PlanRun::State, ringed.
    void setRunStates(const QHash<QString, int> &states);
    // Session 223: the tags selected together (highlighted like the selected one).
    void setMarkedTags(const QSet<QString> &names);
    // The tags whose marks fall inside `r` (canvas coordinates).
    QStringList tagsIn(const QRectF &r) const;
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

    struct Hit { QRectF rect; QString tag; QString text; int signal = -1; int point = -1; };

    // Session 233: selecting a signal or a point (an index into the layout's
    // lists; -1 for none), as a tag is selected.
    void setSelectedSignal(int i);
    void setSelectedPoint(int i);
    int selectedSignal() const { return m_selSignal; }
    int selectedPoint() const { return m_selPoint; }

    // Session 233: Measure. Each click (or measureAt) sets the start, then the
    // end; a third starts again. A click on a tag, signal or point takes its
    // location; anywhere else, the location under it.
    struct MeasureEnd {
        bool    set = false;
        qint64  loc = 0;
        QString what;            // "tag 910", "signal S1647", "point P30", "164.610 km"
        double  y = 0;
    };
    void setMeasuring(bool on);
    bool isMeasuring() const { return m_measuring; }
    void measureAt(const QPointF &pos);
    void clearMeasure();
    const MeasureEnd &measureStart() const { return m_measureA; }
    const MeasureEnd &measureEnd() const { return m_measureB; }
    // The lane names, top to bottom (tests).
    const QStringList &laneNames() const { return m_laneNames; }
    const QVector<Hit> &hits() const { return m_hits; }

signals:
    void tagClicked(const QString &name);
    void tagDoubleClicked(const QString &name);   // session 221
    // Session 223: Shift + drag drew a box round these tags; Ctrl + click toggled one.
    void tagsBoxed(const QStringList &names);
    void tagToggled(const QString &name);
    // Session 222: a right-click: the tag under it (or ""), the line whose lane
    // it is on (or ""), the location under it, and whether it is above the lanes.
    void contextRequested(const QPoint &globalPos, const QString &tag, const QString &line, qint64 locM, bool aboveLanes);
    void tagDragged(const QString &name, qint64 newLocM);
    void signalClicked(int index);            // session 233
    void pointClicked(int index);
    void measured(const QString &text);       // the measurement, in words
    void measureCancelled();                  // Esc while measuring

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
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
    // Session 230: the station's texts on rows of their own, so texts at nearby
    // locations do not overprint; the rows above grow the space over the lanes.
    void placeTexts();
    int  topMargin() const;
    QVector<int> m_textRow;            // per m_l.texts entry
    int  m_textRowsAbove = 1, m_textRowsBelow = 1;
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
    QSet<QString> m_marked;            // session 223
    QVector<StationLayout::Line> m_extraLines;   // session 233: lines only points name
    int m_selSignal = -1, m_selPoint = -1;       // session 233
    // Session 233: Measure.
    bool m_measuring = false;
    int  m_measureClicks = 0;
    MeasureEnd m_measureA, m_measureB;
    QPoint m_hover;
    QHash<QString, int> m_diffKinds;   // session 226
    QHash<QString, int> m_runStates;   // session 227
    QPoint  m_bandFrom;                // Shift + drag: the box's corner (canvas)
    QRect   m_band;                    // the box while it is drawn; null = none
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
    // Session 233: select a row of a sheet (1 signals, 2 points) and show it.
    void selectSheetRow(int sheet, int row);
    QPushButton *measureButton() const { return m_measureBtn; }

    explicit StationLayoutWindow(QWidget *parent = nullptr);

    bool openFile(const QString &path);       // .json, or an .xlsx (imported)
    bool saveFile(const QString &path);       // .json
    bool exportXlsx(const QString &path);
    // Session 212: the drawing as a printable sheet, .pdf (A3 landscape) or
    // .png, by extension.
    bool exportDrawing(const QString &path);
    // Session 229: the tag table, one row per tag by line and location: id,
    // line, location, kind (the RDSO notation), TIN nominal / reverse, the
    // main tag of a duplicate, signals at it, the point it belongs to, CRC-30.
    // .pdf (A4, for site teams) or .csv, by extension.
    struct TagRow { QString name, line, kind, mainTag, signalNames, point, crc; qint64 locM = 0; int tinNom = 0, tinRev = 0; bool decodes = false; };
    QVector<TagRow> tagTable() const;
    bool exportTagTable(const QString &path);
    bool reopenLast();
    void setStation(const StationLayout::Layout &l);
    const StationLayout::Layout &station() const { return m_layout; }
    bool isModified() const { return m_modified; }
    StationLayoutCanvas *canvas() const { return m_canvas; }
    QTabWidget *tabs() const { return m_tabs; }
    StatusLine *status() const { return m_status; }
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
    // Session 223: several tags at once. The selection is the Tags table's.
    QStringList selectedTags() const;
    void selectTags(const QStringList &names);
    // Every one moved by `metres` (CRC-30 recomputed); refused whole if one will not take it.
    bool shiftTags(const QStringList &names, qint64 metres);
    // One field set on every one (CRC recomputed); refused whole if one lacks the
    // field or will not take the value, or if a tag's name (its id) would change.
    bool setFieldOnTags(const QStringList &names, const QString &field, qint64 value);
    // The tags, and their places in the lines (signals naming them stay, and Checks says so).
    bool deleteTags(const QStringList &names);
    // Session 224: a line as a Tag Builder route and back. The route holds the
    // line's tags in its order, each main tag's duplicate after it, and the
    // signals at them; its direction is left unset (set it in the builder:
    // route.xml export asks for it). Empty when the line is not in the layout.
    RfidTag::Route lineAsRoute(const QString &line) const;
    TagBuilderWindow *openLineInTagBuilder(const QString &line);
    // A route as a new line `lineId`: its tags the layout lacks are added, its
    // main tags make the line, its signals the layout lacks are added. Refused
    // whole when the line id is used, or a tag of the same name differs.
    bool addLineFromRoute(const RfidTag::Route &r, const QString &lineId, const QString &name);
    // Session 226: compare with another station file (.json / .xlsx, a built-in
    // one): the Compare tab lists what differs, the drawing rings the tags.
    // It follows edits until cleared. False when the file cannot be read.
    bool compareWith(const QString &path);
    void compareWith(const StationLayout::Layout &other, const QString &label);
    void clearCompare();
    const StationLayout::Diff &comparison() const { return m_diff; }
    // Session 227: a loco's run over the layout. Every @rfid read of the log
    // against the layout's tags: read (the same 16 bytes), different (the same
    // id, other bytes: the fields named), not read. "Not read" is judged only
    // on the lines the run read a tag of, between the first and last such
    // tags: a tag the loco never went past is not marked. Reads of tags the
    // layout lacks are listed. The Run tab; the drawing rings them.
    void setDispatcher(MessageDispatcher *dispatcher) { m_dispatcher = dispatcher; }
    void showRun(const LogModel *model, const QString &key);
    void clearRun();
    struct RunTag { QString name, line; PlanRun::State state = PlanRun::State::NotRead; qint64 firstMs = 0; QString detail; };
    const QVector<RunTag> &runTags() const { return m_runTags; }
    const QStringList &runNotInLayout() const { return m_runExtra; }
    // Session 228: every tag whose CRC-30 does not match its contents gets the
    // one they give (only the CRC bits change). The number fixed (0: none to
    // fix); one Undo step. *notFixed: the tags whose page_x / page_y do not
    // make a tag, left as they are.
    int fixAllCrcs(QStringList *notFixed = nullptr);
    // The menu a right-click on the drawing opens (built, not shown: tests).
    QMenu *contextMenuFor(const QString &tag, const QString &line, qint64 locM, bool aboveLanes);

signals:
    void jumpRequested(const QString &tabKey, qint64 ms);   // session 227: a read, in its log

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    QPushButton *m_measureBtn = nullptr;
    void refreshAll();
    void fillRun();
    int  tagIndexOf(const QString &name) const;   // -1 when absent
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
    QListWidget *m_compare = nullptr;              // session 226
    QTableWidget *m_run = nullptr;                 // session 227
    MessageDispatcher *m_dispatcher = nullptr;
    QPointer<LogModel> m_runModel;      // gone when its tab closes: the run is then hidden
    QString m_runKey;
    QVector<RunTag> m_runTags;
    QStringList m_runExtra;
    bool m_comparing = false;
    StationLayout::Layout m_compareTo;
    QString m_compareLabel;
    StationLayout::Diff m_diff;
    void fillCompare();
    QLabel *m_other = nullptr;
    StatusLine *m_status = nullptr;
    UndoLog *m_undo = nullptr;
};

#endif // STATIONLAYOUTWINDOW_H

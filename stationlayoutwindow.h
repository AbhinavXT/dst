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
// =============================================================================

#include "stationlayout.h"

#include <QHash>
#include <QSet>
#include <QWidget>

class QLabel;
class QListWidget;
class QScrollArea;
class QTabWidget;
class QTableWidget;
class StatusLine;
class UndoLog;

class StationLayoutCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit StationLayoutCanvas(QWidget *parent = nullptr);
    void setStation(const StationLayout::Layout &l);
    void setSelectedTag(const QString &name);
    void setZoom(double z);
    double zoom() const { return m_zoom; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    double xOf(qint64 locM) const;
    qint64 locAt(double x) const;
    QRectF tagRect(const QString &name) const;   // where a tag is drawn (tests)

    struct Hit { QRectF rect; QString tag; QString text; };
    const QVector<Hit> &hits() const { return m_hits; }

signals:
    void tagClicked(const QString &name);
    void tagDragged(const QString &name, qint64 newLocM);

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    void layoutLanes();
    int laneY(int lane) const;

    StationLayout::Layout m_l;
    QHash<QString, qint64> m_loc;      // tag -> abs_loc
    QHash<QString, int>    m_lane;     // tag -> lane
    QStringList m_laneNames;           // "DN  DM", ..., "(no line)"
    QHash<QString, int> m_laneOfLine;  // line id -> lane
    QSet<QString> m_flagged;           // tags checks() names
    qint64 m_min = 0, m_max = 1;
    double m_zoom = 1.0;
    QString m_selected;
    QVector<Hit> m_hits;
    QString m_dragTag;
    double  m_dragX = -1;
};

class StationLayoutWindow : public QWidget
{
    Q_OBJECT
public:
    explicit StationLayoutWindow(QWidget *parent = nullptr);

    bool openFile(const QString &path);       // .json, or an .xlsx (imported)
    bool saveFile(const QString &path);       // .json
    bool exportXlsx(const QString &path);
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

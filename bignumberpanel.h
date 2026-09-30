#ifndef BIGNUMBERPANEL_H
#define BIGNUMBERPANEL_H
// =============================================================================
//  BigNumberPanel -- the Live Loco Console's big-number mode.
//
//  A row of tiles above the packet tabs, each one field in large type:
//  readable across a room, and with F11 (presentation) on a cab or wall
//  screen. Defaults: Speed, Mode, Frame clock, Brake (LiveFields).
//
//  CONFIGURABLE
//    - Right-click any field in the console's tables > "Show as big number".
//    - Right-click a tile > Rename, Move left / right, Remove, Reset to
//      defaults.
//    The tiles and whether the panel is shown are remembered.
//
//  The panel only draws. LocoConsoleWindow resolves each tile's value from
//  the selected loco's latest frames and hands them over with setValues().
// =============================================================================
#include <QVector>
#include <QWidget>

#include "livefields.h"

class QFrame;
class QGridLayout;
class QLabel;
class Sparkline;

class BigNumberPanel : public QWidget
{
    Q_OBJECT
public:
    struct TileValue {
        QString text;             // the big text ("50 km/h", "16:29:16")
        QString detail;           // the small line ("from lsrp", "FRAME_NUM 59357 · 5 s ago")
        bool    stale = false;    // older than the stale threshold: drawn muted
        bool    missing = true;   // no source has it yet: "—"
        // Session 82.
        bool    hasNumber = false;   // a measurement: drawn as a sparkline
        double  number = 0.0;
        bool    trackState = true;   // count "time in this state" (not for clocks)
        int     level = 0;           // 0 fine, 1 amber, 2 red (the tile's rule)
    };

    // Seconds of history the sparkline shows.
    static constexpr int kSparkSeconds = 60;

    explicit BigNumberPanel(QWidget *parent = nullptr);

    QVector<LiveFieldRef> tiles() const { return m_refs; }
    void setTiles(const QVector<LiveFieldRef> &refs);    // rebuilds and saves
    void addTile(const LiveFieldRef &ref);
    void setValues(const QVector<TileValue> &values);    // in tiles() order
    void setValues(const QVector<TileValue> &values, qint64 nowMs);   // tests

    // For the tests.
    QString valueTextAt(int index) const;
    QString detailTextAt(int index) const;
    bool    isStaleAt(int index) const;
    int     levelAt(int index) const;
    int     sparkPointsAt(int index) const;
    // "for 4 min 12 s" for a state tile, empty otherwise.
    QString stateTextAt(int index) const;

    // Saved tiles, or the defaults.
    static QVector<LiveFieldRef> loadSaved();

    // Remove and Reset push their undo here when set (session 79).
    void setUndoLog(class UndoLog *log) { m_undo = log; }
    // A tile's colour rule (undoable). The dialog uses this; so do the tests.
    void setRule(int index, const TileRule &rule);
    // Every open panel re-reads its tiles from the INI (settings import).
    static void reloadAll();

signals:
    void tilesChanged();
    // Double-click a tile, or its menu ▸ Plot over time (session 82).
    void plotRequested(int index);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void pushUndo(const QString &label, const QVector<LiveFieldRef> &before);
    struct Tile {
        QFrame *frame = nullptr;
        QLabel *label = nullptr;
        QLabel *value = nullptr;
        QLabel *detail = nullptr;
        class Sparkline *spark = nullptr;
        bool    stale = false;
        int     level = 0;
        // time in state: when the text last changed
        QString lastText;
        qint64  changedMs = -1;
        QString stateText;
        // sparkline history: (ms, value), last kSparkSeconds
        QVector<QPair<qint64, double>> history;
    };
    void editRule(int index);

    void rebuild();
    void save() const;
    void restyle();
    void showTileMenu(int index, const QPoint &globalPos);

    QVector<LiveFieldRef> m_refs;
    QVector<Tile>         m_tiles;
    QGridLayout          *m_grid = nullptr;
    QLabel               *m_hint = nullptr;
    class UndoLog *m_undo = nullptr;
};

#endif // BIGNUMBERPANEL_H

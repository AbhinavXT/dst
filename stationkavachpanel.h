#ifndef STATIONKAVACHPANEL_H
#define STATIONKAVACHPANEL_H
// =====================================================================
//  stationkavachpanel.{h,cpp} -- session 422: the station's live state,
//  set by hand while the Packet Maker answers as the station
//  (stationkavach.h). Packet Maker ▸ Send log ▸ Station….
//
//    Signals   each signal's aspect, as the interlocking would show it
//    Points    Normal / Reverse
//    Occupied  the track circuits and TINs the routes name
//    Routes    the authority each route gives (FS / OS / SR)
//    TSR       each TSR on or off
//    Answers   the last answer to each loco: what was sent, and what the
//              state does not agree with
//
//  It edits the State it is given; the next LSRP is answered with it.
//  Session 442: Interlocking: the station works its signals from the track
//  circuits (StationInterlock); a point a set route holds is not thrown.
//  Session 428: Show on the drawing (drawingRequested): the station live
//  on the Station Layout drawing, the locos and their MAs on it.
// =====================================================================
#include <QDialog>
#include <QPair>
#include <QVector>

#include "stationkavach.h"

class QCheckBox;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

class StationKavachPanel : public QDialog {
    Q_OBJECT
public:
    StationKavachPanel(const QVector<QPair<qint64, QString>> &aspects, QWidget *parent = nullptr);
    // Shows `s`, editing `st` (kept by the caller, outliving the panel's use of it).
    void setStation(const StationKavach::Station &s, StationKavach::State *st);
    void showAnswer(int locoId, const QString &text, const QStringList &checks);
    // Session 442: what the interlocking did (StationInterlock::work), newest last.
    void showInterlocking(const QStringList &events);
    // Session 435: the state changed elsewhere (the Station Layout drawing): shown again, answers kept.
    void refresh() { rebuild(); }

    QPushButton *drawingButton() const { return m_drawing; }

signals:
    void stateChanged();
    void drawingRequested();           // session 428
    void refused(const QString &why);  // session 442: an edit the interlocking does not allow

private:
    void rebuild();
    QVector<QPair<qint64, QString>> m_aspects;
    StationKavach::Station m_station;
    StationKavach::State  *m_state = nullptr;
    QTableWidget *m_signals = nullptr, *m_points = nullptr, *m_occupied = nullptr, *m_routes = nullptr, *m_tsr = nullptr;
    QPlainTextEdit *m_answers = nullptr;
    QPlainTextEdit *m_events = nullptr;     // session 442
    QCheckBox *m_interlocking = nullptr;
    QPushButton *m_drawing = nullptr;
    QHash<int, QString> m_lastAnswer;   // by loco
};

#endif  // STATIONKAVACHPANEL_H

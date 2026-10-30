#ifndef STATIONKAVACHEDITOR_H
#define STATIONKAVACHEDITOR_H
// =====================================================================
//  stationkavacheditor.{h,cpp} -- session 423: the Stationary KAVACH data
//  (stationkavach.h, session 422) edited in Tools ▸ Station Layout, tab
//  KAVACH, instead of by hand in the JSON.
//
//    top       station id, STN version, MA sections by aspect
//    Signals   name, foot tag, signal info, the aspect the file starts with
//    Routes    one Table-of-Control row each: entry -> exit, direction,
//              section type, authority, OS speed, points, track circuits,
//              TINs, duplicate-tag distance, adjacent TINs, TIN, location
//              reset; the length (from its tags) shown
//    the route selected: its tags (linking distances), SSP, gradient, LC
//              gates, turnouts, track conditions, TSR. The profile tables'
//              columns are the SLRP sub-packet's own fields
//              (schema/kavach.xml); a cell left empty is a field not given
//
//  Each edit becomes the layout's new "kavach" block (edited()): the
//  window makes it one Undo step and lists what checks() says under
//  Checks. What cannot be read (a word where a number goes, a location
//  reset that is not three numbers) is refused and said (refused()), the
//  table put back as it was.
// =====================================================================
#include <QJsonObject>
#include <QWidget>

#include <functional>

#include "packetbuilder.h"
#include "stationkavach.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTableWidget;

namespace StationLayout { struct Layout; }

class StationKavachEditor : public QWidget {
    Q_OBJECT
public:
    explicit StationKavachEditor(QWidget *parent = nullptr);

    // Shows `kavach` (empty: no KAVACH data, and a button to start it).
    // The route selected stays selected when it is still there.
    void setKavach(const QJsonObject &kavach);
    const StationKavach::Station &station() const { return m_station; }

    // A start for a layout without KAVACH data: its station id, and its
    // signals (name, foot tag) at Red. No routes: they are added here.
    static QJsonObject seedFrom(const StationLayout::Layout &l);
    void setSeedSource(const StationLayout::Layout *l) { m_seedFrom = l; }

    enum List { Tags, Ssp, Gradient, Lc, Turnouts, TrackConditions, Tsr, ListCount };
    QTableWidget *signalTable() const { return m_signals; }
    QTableWidget *routeTable() const { return m_routes; }
    QTableWidget *listTable(List l) const { return m_lists[l]; }
    QTabWidget *listTabs() const { return m_listTabs; }
    int currentRoute() const;
    void selectRoute(int i);
    QPushButton *startButton() const { return m_start; }

    // Row operations (the buttons'), on the table they name.
    void addSignal();
    void deleteSignals();
    void addRoute(bool copySelected);
    void deleteRoutes();
    void addListRow();
    void deleteListRows();

signals:
    // The new "kavach" block (empty: removed) and the Undo label.
    void edited(const QJsonObject &kavach, const QString &label);
    void refused(const QString &why);

private:
    void fill();
    void fillRoute();
    void commit(const StationKavach::Station &next, const QString &label);
    void refuse(const QString &why);
    void onSignalsEdited();
    void onRoutesEdited();
    void onListEdited(List l);
    void onTopEdited();
    QStringList listColumns(List l) const;   // the field names, in order
    QVector<StationKavach::Row> *listOf(StationKavach::Route &r, List l) const;
    QComboBox *enumCell(QTableWidget *t, int row, int col, const QString &enumName, qint64 value,
                        const std::function<void()> &onChange);

    PacketBuilder m_builder;           // the active schema: field widths, enum names
    StationKavach::Station m_station;
    bool m_has = false;                // the layout has KAVACH data
    bool m_filling = false;
    int  m_route = -1;
    const StationLayout::Layout *m_seedFrom = nullptr;
    QStackedWidget *m_stack = nullptr;
    QPushButton *m_start = nullptr;
    QSpinBox  *m_stationId = nullptr, *m_stnVersion = nullptr;
    QLineEdit *m_sections = nullptr;
    QTableWidget *m_signals = nullptr, *m_routes = nullptr;
    QLabel *m_routeLabel = nullptr;
    QTabWidget *m_listTabs = nullptr;
    QTableWidget *m_lists[ListCount] = {};
    QStringList m_listCols[ListCount];
};

#endif  // STATIONKAVACHEDITOR_H

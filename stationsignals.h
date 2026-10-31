#ifndef STATIONSIGNALS_H
#define STATIONSIGNALS_H
// =====================================================================
//  stationsignals.{h,cpp} -- session 461: a station's signals and the
//  movement authority each route would get.
//
//  CUR_SIG_INFO (17 bits, the SLRP's MA sub-packet; the decoder's sigInfo
//  formatter): bits 0-4 line number (0 none), 5-8 direction (UP, DN, UP
//  MAIN, ...), 9-14 signal type (Home, Adv-Str, Auto, ...), 15 override
//  (0 standstill, 1 running), 16 stop signal. Station 527 sent S43 66400
//  (DN MAIN, Adv-Str, stop) and A900 77664 (DN MAIN, Semi-Auto Lit, stop).
//
//  ONE SIGNAL LIST: the layout's station signals (Signals step) and the
//  KAVACH data's signals (what the station sends) are two lists. Where
//  they disagree is said: a signal in one and not the other, a different
//  foot tag. A layout with no station signals (KAVACH data alone) is not
//  compared.
//
//  MA BY ASPECT (maTable()): for a route, what the station would give a
//  loco on it with its exit signal at each aspect (Red, calling-on,
//  Yellow, Double Yellow, Green): the sections and MA_W_R_T_SIG, worked
//  out by StationKavach::sectionsFrom, the signals beyond at Green and the
//  points as the route sets them. The same numbers Packet Maker answers
//  with.
// =====================================================================
#include "stationkavach.h"
#include "stationlayout.h"

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace StationSignals {

struct SigInfo {
    int line = 0;          // 0-31, 0: not applicable
    int dir = 0;           // 0-15 (sigDirName)
    int kind = 0;          // 0-63 (sigKindName)
    bool running = false;  // override: 1 running, 0 standstill
    bool stop = false;     // a stop signal
};
SigInfo unpack(qint64 v);
qint64 pack(const SigInfo &s);
// "DN MAIN · Adv-Str · stop signal" (the parts that are set).
QString describe(qint64 v);
// The codes with a name, for pickers: (code, name).
QVector<QPair<int, QString>> kinds();
QVector<QPair<int, QString>> directions();

// Where the layout's station signals and the KAVACH signals disagree, one
// line each. Nothing when the station has no KAVACH data.
QStringList reconcile(const StationLayout::Layout &l, const StationKavach::Station &s);

struct MaRow {
    int aspect = 0;
    int wanted = 0, got = 0;   // sections the aspect gives, and the routes there are for them
    qint64 maEnd = 0;          // MA_W_R_T_SIG, m
    QStringList routes;        // the sections' route names
    QString note;              // ends short, or broken
};
// The aspects shown: Red, Red with calling-on, Yellow, Double Yellow, Green.
QVector<int> previewAspects();
QVector<MaRow> maTable(const StationKavach::Station &s, int route);

}  // namespace StationSignals

#endif // STATIONSIGNALS_H

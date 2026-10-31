#ifndef STATIONBUILD_H
#define STATIONBUILD_H
// =====================================================================
//  stationbuild.{h,cpp} -- session 459: building a station step by step
//  in Station Layout (the guided steps beside the drawing).
//
//  THE STEPS, in the order a station is best filled in: Station, Lines,
//  Tags, Points, Signals, Track circuits, Labels, Routes (the KAVACH
//  tab), Check. steps() says for each how many rows it has and what is
//  wrong in it, each problem with the row it is about. Its problems are
//  StationLayout::problems() (what Checks lists) by sheet, plus what
//  only a station being built needs said: no station row, no lines, a
//  tag on no line or point (drawn in "(no line)"), a signal whose foot
//  tag is not placed as a signal foot. Reported, never corrected.
//
//  A NEW STATION (newStation()): DN main and UP main with their
//  approach, home, starter and exit tags, the loops beside them with a
//  point at each end, a signal at each home and starter, every tag a
//  main and a duplicate. As the shipped stations have them
//  (Telapur 527, HBL's sheets):
//    - DN main and its loops are run Nominal (locations rising), UP main
//      and its loops Reverse (falling);
//    - a tag's TIN in a direction is the track it leads onto that way:
//      tin_nom the track on its rising side, tin_rev the falling side;
//    - a signal's foot tag is placed 1 (Signal foot, Nominal) on a line
//      run Nominal, 2 (Reverse) on one run Reverse;
//    - the duplicate 4 m after its main tag, the way the line is run;
//    - version 1, the station's id both ways, station section both ways.
//  Refused by name (nothing made) when an id, TIN or location would not
//  fit its field.
// =====================================================================
#include "stationlayout.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace StationBuild {

enum Step { StepStation = 0, StepLines, StepTags, StepPoints, StepSignals, StepCircuits, StepLabels,
            StepRoutes, StepCheck, StepCount };

QString stepTitle(int step);       // "Tags (RFID)"
QString stepHelp(int step);        // what goes in it, and how to fill it in
bool stepOptional(int step);       // a station can do without it: Track circuits, Labels
// The sheet a step's table shows (StationLayout::Sheet); SheetNone for Routes and Check.
int sheetOfStep(int step);
int stepOfSheet(int sheet);

struct StepState {
    int count = 0;                                  // its rows
    QVector<StationLayout::Problem> problems;       // each with its sheet and row (row -1: the step as a whole)
};
// Every step. `routeCount` and `kavachNotes` are the KAVACH tab's (its
// routes, and what its checks say): the Routes step's. The Check step holds
// every problem of the others.
QVector<StepState> steps(const StationLayout::Layout &l, int routeCount = 0, const QStringList &kavachNotes = {});

struct Plan {
    int stationId = 1;
    qint64 centre = 100000;        // m: the middle of the yard
    int downLoops = 1, upLoops = 1;
    int loopLength = 700;          // m: each loop between its points
    int firstTag = 1;              // the first unique id; the rest follow
    int firstTin = 1;              // the first TIN; the rest follow
};
// The tags (unique id), TINs and metres the plan takes up, for the dialog.
int tagsNeeded(const Plan &p);
int tinsNeeded(const Plan &p);
qint64 firstLocation(const Plan &p);
qint64 lastLocation(const Plan &p);
// The station. Empty and *err when refused.
StationLayout::Layout newStation(const Plan &p, QString *err = nullptr);

}  // namespace StationBuild

#endif // STATIONBUILD_H

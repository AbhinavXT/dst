#ifndef STATIONSTRETCH_H
#define STATIONSTRETCH_H
// =====================================================================
//  stationstretch.{h,cpp} -- session 458: several Stationary KAVACH
//  stations along one stretch, answering one loco as it goes.
//
//  THE FILE ("dlconsole-stretch", stretch.json, written by
//  scripts/kavach_sheets.py when sections are read together): the station
//  layout files in the order they lie. Each is a Station Layout file with
//  KAVACH data (docs/STATION_KAVACH.md).
//
//  WHO ANSWERS (which()):
//    ARP   the station it is for (APPROACHING_STN_ID); 0: the one the
//          loco is with.
//    LSRP  the station the loco is with: the last whose ARP was answered,
//          else the first whose routes hold its LAST_RFID_TAG.
//  A loco is handed over (session 447) when a station's MA carries
//  NEXT_STN_COMM 1 and APPR_STN_ILC_IBS_ID; it is with the next station
//  once that station has answered its ARP.
//
//  A DRY RUN (run()): a loco driven along a line of tags (the merged
//  stretch layout's Down or Up Main), an LSRP at each main tag, every
//  signal at one aspect. Each step says who answered, the MA, and where the
//  loco was handed over -- or where it was not: a tag on no route of the
//  station it is with, taken by the station that holds it. No radio, no
//  keys: the answers only.
// =====================================================================
#include "stationkavach.h"
#include "stationlayout.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace StationStretch {

constexpr const char *kFormat = "dlconsole-stretch";

struct Member {
    QString path;                      // its station file
    StationLayout::Layout layout;
    StationKavach::Station station;
    StationKavach::State state;
    QHash<int, StationKavach::Loco> locos;
};

struct Stretch {
    QVector<Member> members;           // in the order they lie
    QHash<int, int> with;              // loco id -> member it is with
    int indexOf(int stationId) const;
    int holding(int tag) const;        // the first member whose routes hold `tag`; -1: none
};

// The stretch file, each station file beside it read (its KAVACH data:
// a member without routes is named in *notes and left out). False and
// *err on a file that is not a stretch, or one whose stations none load.
bool load(const QString &path, Stretch *out, QString *err = nullptr, QStringList *notes = nullptr);
bool isStretchFile(const QString &path);

// The member to answer `pkt` (an ARP when `arp`), -1: none (said in *why).
int which(const Stretch &s, const QHash<QString, qint64> &pkt, bool arp, QString *why = nullptr);

// The stretch's Down or Up main line: every station's main line of that
// name (Down / DN ... Main, Up ... Main; not a loop), its main tags in the
// order a loco on it reads them (its routes' direction: Nominal, rising
// locations), each once; tags whose CRC-30 does not hold left out. The
// direction is *dir (1 Nominal, 2 Reverse).
QStringList mainLine(const Stretch &s, bool down, int *dir = nullptr);

struct Step {
    int     tag = 0;
    qint64  absLoc = 0;
    int     station = 0;               // who answered (its id); 0: nobody
    bool    ok = false;
    QString note;                      // the answer, or why none
    qint64  ma = -1;                   // MA_W_R_T_SIG
    int     handTo = 0;                // NEXT_STN_COMM's station, when the MA carries it
    QString event;                     // "handed over to 522", "not handed over: ...", ""
};

// A loco along the tags `line` (names, in the order it reads them; "…D"
// duplicates skipped), Nominal or Reverse (`dir` 1 / 2), every signal of
// every station at `aspect`.
QVector<Step> run(Stretch s, const QStringList &line, int dir, int aspect, const Schema::Encoder &enc);

}  // namespace StationStretch

#endif  // STATIONSTRETCH_H

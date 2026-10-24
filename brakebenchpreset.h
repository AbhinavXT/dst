#ifndef BRAKEBENCHPRESET_H
#define BRAKEBENCHPRESET_H
// =============================================================================
//  BenchPreset (session 362) -- the Brake Test Bench's fault setup, as data,
//  saved under a name and loaded on request.
//
//  docs/BRAKE_BENCH_NEXT.md item 5: "plant faults, rates, analog offsets,
//  forced bits, manual pressures, validity modes, queued codes, frames off,
//  silence". That and nothing else: the network (listen port, the VCCs),
//  the analog card's channels and the brake relays' pressures are the bench's
//  wiring, kept in the window's settings, and a preset loaded on another PC
//  must not move them.
//
//  Opening the window still starts clean (session 360, on purpose); a preset
//  is applied only when the operator picks one. Loading one sets every field
//  it holds, the defaults included, so what was set before does not leak in.
//
//  Stored in dlconsole.ini as one JSON document (brakebench/presets), so a
//  name may hold any character. A key the reader does not know is skipped,
//  a missing one is its default, so a preset from an older or newer
//  DLConsole still loads.
// =============================================================================
#include "biuprotocol.h"
#include "brakeplant.h"

#include <QJsonObject>
#include <QStringList>
#include <QVector>

struct BenchPreset {
    struct Air {
        double apply = 1.0, release = 1.0;    // kg/cm^2 per second
        int    fault = 0;                     // BrakePlant::Fault
        double param = 0;                     // slow x / leak per s
        double offset = 0;                    // the analog card reads off by this
    };
    struct Pressure {
        bool   manual = false;
        double kg = 0;
        int    valid = 0;                     // BiuSimulator::ValidMode
    };

    Air      air[BrakePlant::kChannels];
    Pressure pressure[Biu::kPressures];
    int      bits[3][8] = {};                 // BiuSimulator::BitMode, bytes 1-3
    QVector<quint16> faults, displays;        // queued codes, front first
    bool     frameOn[3] = { true, true, true };
    bool     silent = false;

    // What differs from `clean` (the bench as it opens), for the status line
    // and the menu: "BC stuck · BP manual 3.00 · fault 0x1004 queued · 0x420 off".
    // Empty when nothing does.
    QStringList differences(const BenchPreset &clean) const;

    QJsonObject toJson() const;
    static BenchPreset fromJson(const QJsonObject &o, const BenchPreset &defaults);

    // The store, in dlconsole.ini.
    static QStringList names();               // sorted, case-insensitive
    static bool        exists(const QString &name);
    static bool        load(const QString &name, const BenchPreset &defaults, BenchPreset *out);
    static void        save(const QString &name, const BenchPreset &p);
    static void        remove(const QString &name);
};

#endif  // BRAKEBENCHPRESET_H

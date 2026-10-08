#ifndef RADIOHEALTH_H
#define RADIOHEALTH_H

// =============================================================================
//  Radio health (session 173) — Tools ▸ Monitor ▸ Radio health…
//  -----------------------------------------------------------------------------
//  The loco's radio and GSM links over time, from what the tab already holds:
//    @dmi     signal_strength (0..5 bars); "Approaching Radio Hole" in
//             alarm_code (the radio holes the loco was told of)
//    @ccsys   both radios: health, temperature, PA temperature, forward
//             power; active_radio
//    @nmshlth RADIO_1/2_HEALTH (3 = Radio Fail)
//    @dlsys   GSM-1 / GSM-2 RSSI (99 = not known)
//
//  THE QUESTION IT ANSWERS: was a loss of radio a radio hole the loco had
//  been told of, or not? Each no-radio spell (signal_strength 0) is matched
//  against the radio-hole announcements: "announced" when one was on the DMI
//  in the kRadioHoleLeadMs before the spell began or during it. Observed,
//  not judged: an unannounced spell is listed, not called a fault.
// =============================================================================

#include <QString>
#include <QVector>

class LogModel;

namespace RadioHealth {

constexpr qint64 kSpellGapMs = 10000;        // a longer silence between @dmi ends a spell
constexpr qint64 kRadioHoleLeadMs = 120000;  // an announcement this long before still counts

struct Series {
    QString name, unit;
    QVector<qint64> ms;
    QVector<double> v;                       // NaN: not known (e.g. GSM RSSI 99)
    bool isEmpty() const { return ms.isEmpty(); }
};

struct Span {
    qint64  fromMs = 0, toMs = 0;
    QString what;
    bool    announced = false;               // no-radio spells: a radio hole was announced
};

struct Report {
    qint64 fromMs = 0, toMs = 0;
    Series signal;                           // DMI bars
    QVector<Span> noRadio;                   // signal_strength 0
    QVector<Span> radioHole;                 // "Approaching Radio Hole" on the DMI
    QVector<Span> radioFail;                 // NMS / CCSYS: a radio not OK, or no active radio
    QVector<Series> temperatures;            // radio 1, radio 2, PA 1, PA 2 (°C)
    QVector<Series> power;                   // forward power, radio 1 / 2 (W)
    QVector<Series> gsm;                     // GSM-1 / GSM-2 RSSI
    qint64 noRadioTotalMs = 0, longestNoRadioMs = 0;
    int    announcedSpells = 0;
    bool   any() const { return !signal.isEmpty() || !temperatures.isEmpty() || !gsm.isEmpty(); }
};

Report build(const LogModel *model, qint64 fromMs = 0, qint64 toMs = 0);

// Marks each no-radio spell announced or not, and totals them (build()
// calls it; separate so the rule can be tested on its own).
void matchAnnouncements(Report &r);

// One paragraph of plain words for the window's header and the tests.
QString summaryText(const Report &r);

}  // namespace RadioHealth

#endif // RADIOHEALTH_H

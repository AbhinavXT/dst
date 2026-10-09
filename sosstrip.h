#ifndef SOSSTRIP_H
#define SOSSTRIP_H

// =============================================================================
//  SoS track strip (session 185) — the SoS window's picture of one snapshot
//  -----------------------------------------------------------------------------
//  A straight line through the own loco, by absolute location (increasing to
//  the right, as the track diagram draws it), one lane per TIN: the own
//  loco's line in the middle, the others above and below it, named.
//
//    trains     each a bar of its length behind its front, with an arrow the
//               way it is going; the own loco in the accent colour, the others
//               by threat (collision: error, SoS: warning, none: muted)
//    target     the one SOS_RecomputeAggregates picked, outlined and labelled
//    ghost      where the ARP itself put a loco, dashed, when SOSWithAdjustment
//               moved it or turned it round
//    bands      around the own loco's front: sos_trigger_distance (warning)
//               and collision_trigger_distance (error), from the log's @linfo;
//               without one, no bands, and the legend says so
//    stations   a pin at the station's location
//
//  Hover a train, a ghost or a pin for what it is.
// =============================================================================

#include "soslog.h"

#include <QWidget>

class LogModel;

class SosStrip : public QWidget
{
    Q_OBJECT
public:
    // The trigger distances, from @linfo; -1 when the log has none.
    struct Config {
        int sosTriggerM = -1;
        int collisionTriggerM = -1;
        int cancelM = -1;
        bool known() const { return sosTriggerM >= 0 || collisionTriggerM >= 0; }
    };
    static Config configFromLog(const LogModel *model);

    explicit SosStrip(QWidget *parent = nullptr);
    void setConfig(const Config &c) { m_cfg = c; update(); }
    void setSnapshot(const SosLog::Snapshot *s);   // nullptr: nothing to draw

    QSize sizeHint() const override { return QSize(1000, 190); }
    QSize minimumSizeHint() const override { return QSize(480, 150); }

    // For the tests: what was drawn, and the text under a point.
    struct Hit { QRect rect; QString text; };
    const QVector<Hit> &hits() const { return m_hits; }
    QString legend() const;
    QStringList laneNames() const;     // "TIN 101 (own)", "TIN 102", ...
    double spanM() const { return m_halfSpanM * 2; }

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *e) override;

private:
    double xOf(double locM, const QRect &r) const;
    QVector<int> laneTins() const;

    Config m_cfg;
    SosLog::Snapshot m_s;
    bool m_has = false;
    double m_halfSpanM = 2500.0;
    QVector<Hit> m_hits;
};

#endif // SOSSTRIP_H

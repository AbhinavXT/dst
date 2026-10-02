#ifndef CABPANEL_H
#define CABPANEL_H

// =============================================================================
//  Cab and link widgets for the Live Loco Console (session 82)
//  -----------------------------------------------------------------------------
//  CabDisplay   a small copy of the driver's DMI, from @dmi (and @slrp for
//               the signal aspects): a speed dial with the permitted arc and
//               the target speed marked, a distance-to-target bar with time
//               to target, the mode and brake state, and the current and
//               next signal as lamps.
//  LinkLights   one light per packet type the loco sends: blinks on arrival,
//               shows its rate, goes amber when late and grey when silent,
//               judged against THAT type's own rhythm.
//  HealthLights the health bits the DMI carries (Kavach unit, pulse
//               generator, BIU) and the NMS fault reports, per module.
//
//  What is not in the capture is not drawn: radio_health_status and
//  warning_type have no mapping in the schema yet, so they get no light
//  rather than a guessed colour.
// =============================================================================

#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

// ---- what the cab shows -------------------------------------------------------
struct CabState {
    bool    hasDmi = false;
    bool    stale = false;            // DMI older than 3 s
    double  speed = 0.0;
    bool    hasPermitted = false;
    double  permitted = 0.0;
    bool    hasTarget = false;
    double  targetDistance = 0.0;     // m
    double  targetSpeed = 0.0;        // km/h
    QString mode;                     // display text
    QString brake;                    // display text
    int     brakeLevel = 0;           // 0 none, 1 overspeed warning, 2 service brake, 3 emergency
    // Signal aspects: SLRP's CUR/NEXT when present, else DMI current_sig_aspect.
    int     currentAspect = -1;       // raw sigAspect value, -1 unknown
    int     nextAspect = -1;
    QString currentAspectText, nextAspectText;
    QString aspectSource;             // "slrp" / "dmi"
};

// From one DMI frame's raw values and display text (and optionally SLRP's).
CabState cabStateFrom(const QHash<QString, qint64> &dmiRaw, const QHash<QString, QString> &dmiText,
                      const QHash<QString, qint64> &slrpRaw = {}, const QHash<QString, QString> &slrpText = {});

// A signal aspect as lamps: up to two lit lamps, top first.
struct AspectLamps {
    QVector<QString> colours;         // "red", "yellow", "green", "white"
    QString          extra;           // "route 1 left", "calling-on", "stop board", ...
    bool             known = false;
};
AspectLamps lampsFor(int aspect);

// Time to cover `distanceM` at `speedKmh`, as text ("42 s", "3 min 5 s"),
// empty when stationary.
QString timeToTarget(double distanceM, double speedKmh);

// The cab view's speed dial runs 0 .. this, the loco's top speed (session 92).
constexpr double kCabDialMaxKmh = 250.0;

class CabDisplay : public QWidget
{
    Q_OBJECT
public:
    explicit CabDisplay(QWidget *parent = nullptr);
    void setState(const CabState &state);
    const CabState &state() const { return m_state; }
    // Fields the operator added (session 83): label, value, stale.
    struct Extra { QString label; QString value; bool stale = false; };
    void setExtras(const QVector<Extra> &extras);
    const QVector<Extra> &extras() const { return m_extras; }
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *) override;
private:
    void paintExtras(class QPainter &p);
    CabState m_state;
    QVector<Extra> m_extras;
};

// ---- heartbeat lights ---------------------------------------------------------
struct Heartbeat {
    QString name;                     // "lsrp"
    double  rate = 0.0;               // per second
    qint64  ageMs = -1;               // since the last one
    qint64  intervalMs = 0;           // this type's usual gap (running average)
    enum class State { Live, Late, Silent } state = State::Live;
    bool    pulse = false;            // arrived within the last refresh
};
// Live up to 2x its usual interval (at least 2 s), late up to 5x (at least
// 5 s), then silent.
Heartbeat::State heartbeatState(qint64 ageMs, qint64 intervalMs);

class LinkLights : public QWidget
{
    Q_OBJECT
public:
    explicit LinkLights(QWidget *parent = nullptr);
    void setBeats(const QVector<Heartbeat> &beats);
    const QVector<Heartbeat> &beats() const { return m_beats; }
    QSize sizeHint() const override;
    // Session 124: the lights WRAP onto more lines rather than running off
    // the right edge ("dlsys silent" was cut in half).
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;
    QSize minimumSizeHint() const override { return QSize(120, lineHeight()); }
protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *e) override;   // tooltips per light
private:
    int lineHeight() const { return fontMetrics().height() + 10; }
    QString labelFor(const Heartbeat &b) const;
    // Where each light goes at width `w`; rows counted into *rows.
    QVector<QRect> layoutFor(int w, int *rows) const;
    QVector<Heartbeat> m_beats;
    QVector<QRect>     m_rects;
};

// ---- card health lights -------------------------------------------------------
struct HealthLight {
    QString name;
    enum class State { Unknown, Ok, Fault } state = State::Unknown;
    QString detail;
};
// DMI health bits, and NMS faults by module (faults: module -> fault names;
// nmsSeen: whether any fault report has arrived).
QVector<HealthLight> healthLightsFrom(const QHash<QString, qint64> &dmiRaw, bool dmiSeen,
                                      const QHash<QString, QStringList> &faultsByModule, bool nmsSeen);

class HealthLights : public QWidget
{
    Q_OBJECT
public:
    explicit HealthLights(QWidget *parent = nullptr);
    void setLights(const QVector<HealthLight> &lights);
    const QVector<HealthLight> &lights() const { return m_lights; }
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *e) override;
private:
    QVector<HealthLight> m_lights;
    QVector<QRect>       m_rects;
    QStringList          m_shownDetails;   // tooltips of what is drawn (after folding)
};

#endif // CABPANEL_H

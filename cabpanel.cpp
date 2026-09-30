#include "cabpanel.h"

#include "uicolors.h"

#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <cmath>

namespace {

const double kPi = 3.14159265358979323846;

QString textOr(const QHash<QString, QString> &t, const char *key)
{
    return t.value(QLatin1String(key)).trimmed();
}

}  // namespace

// =============================================================================
//  Pure helpers
// =============================================================================

CabState cabStateFrom(const QHash<QString, qint64> &dmi, const QHash<QString, QString> &dmiText,
                      const QHash<QString, qint64> &slrp, const QHash<QString, QString> &slrpText)
{
    CabState s;
    s.hasDmi = dmi.contains(QStringLiteral("train_speed"));
    if (s.hasDmi) {
        const qint64 v = dmi.value(QStringLiteral("train_speed"));
        s.speed = v >= 511 ? 0.0 : double(v);
        const qint64 p = dmi.value(QStringLiteral("speed_limit_permissible"), 0);
        s.hasPermitted = p > 0 && p < 511;
        s.permitted = double(p);
        const qint64 d = dmi.value(QStringLiteral("target_distance"), 0);
        s.hasTarget = d > 0 && dmi.contains(QStringLiteral("target_speed"));
        s.targetDistance = double(d);
        s.targetSpeed = double(dmi.value(QStringLiteral("target_speed"), 0));
        s.mode = textOr(dmiText, "loco_mode");
        s.brake = textOr(dmiText, "brake_type");
        // dmiBrakeType: 0 none, 1 overspeed no brakes, 2 normal, 3 FSB,
        // 4 EB, 5 light engine brake.
        switch (dmi.value(QStringLiteral("brake_type"), 0)) {
        case 1: s.brakeLevel = 1; break;
        case 2: case 3: case 5: s.brakeLevel = 2; break;
        case 4: s.brakeLevel = 3; break;
        default: s.brakeLevel = 0; break;
        }
    }
    // The aspects are inside the SLRP's movement-authority sub-packet, and
    // sub-packet fields do not reach the decoder's raw-value map (only the
    // header's do). So the number is read off the decoded text, "11 (Green)",
    // whose leading integer IS the wire value; the raw map is used when a
    // future decoder does export it.
    auto aspectOf = [&slrp, &slrpText](const char *field, int *out) {
        const QString key = QLatin1String(field);
        if (slrp.contains(key)) { *out = int(slrp.value(key)); return true; }
        const QString text = slrpText.value(key).trimmed();
        if (text.isEmpty()) return false;
        bool ok = false;
        const int v = text.section(QLatin1Char(' '), 0, 0).toInt(&ok);
        if (ok) *out = v;
        return ok;
    };
    int cur = -1;
    if (aspectOf("CUR_SIG_ASPECT", &cur)) {
        s.currentAspect = cur;
        s.currentAspectText = textOr(slrpText, "CUR_SIG_ASPECT");
        int next = -1;
        s.nextAspect = aspectOf("NEXT_SIG_ASPECT", &next) ? next : -1;
        s.nextAspectText = textOr(slrpText, "NEXT_SIG_ASPECT");
        s.aspectSource = QStringLiteral("slrp");
    } else if (dmi.contains(QStringLiteral("current_sig_aspect"))) {
        s.currentAspect = int(dmi.value(QStringLiteral("current_sig_aspect")));
        s.currentAspectText = textOr(dmiText, "current_sig_aspect");
        s.aspectSource = QStringLiteral("dmi");
    }
    return s;
}

AspectLamps lampsFor(int a)
{
    // sigAspect (kavach.xml). Lamps are what a driver sees on the post.
    AspectLamps l;
    l.known = true;
    if (a == 0) { l.colours = {}; }   // "Unidentified": a real state, no lamp lit
    else if (a == 1) { l.colours = { QStringLiteral("red") }; }
    else if (a == 2) { l.colours = { QStringLiteral("yellow") }; }
    else if (a >= 3 && a <= 8) {
        l.colours = { QStringLiteral("yellow") };
        l.extra = QObject::tr("route %1 %2").arg(a - 2).arg(a <= 5 ? QObject::tr("left") : QObject::tr("right"));
    }
    else if (a == 10) { l.colours = { QStringLiteral("yellow"), QStringLiteral("yellow") }; }
    else if (a == 11) { l.colours = { QStringLiteral("green") }; }
    else if (a == 12 || a == 13) {
        l.colours = { QStringLiteral("yellow"), QStringLiteral("yellow") };
        l.extra = a == 12 ? QObject::tr("route 1 left") : QObject::tr("route 4 right");
    }
    else if (a == 14) { l.colours = {}; l.extra = QObject::tr("AG marker off"); }
    else if (a == 15) { l.colours = { QStringLiteral("red"), QStringLiteral("white") }; l.extra = QObject::tr("calling-on"); }
    else if (a == 24) { l.colours = { QStringLiteral("red") }; l.extra = QObject::tr("stop board"); }
    else if (a >= 32 && a <= 63) { l.colours = { QStringLiteral("yellow") }; l.extra = QObject::tr("stencil route %1").arg(a - 31); }
    else { l.known = false; }
    return l;
}

QString timeToTarget(double distanceM, double speedKmh)
{
    if (speedKmh < 1.0 || distanceM <= 0.0) return QString();
    const qint64 s = qint64(std::llround(distanceM / (speedKmh / 3.6)));
    if (s < 60) return QObject::tr("%1 s").arg(s);
    if (s < 3600) return QObject::tr("%1 min %2 s").arg(s / 60).arg(s % 60);
    return QObject::tr("%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60);
}

Heartbeat::State heartbeatState(qint64 ageMs, qint64 intervalMs)
{
    if (ageMs < 0) return Heartbeat::State::Silent;
    const qint64 lateAfter = qMax<qint64>(2000, intervalMs * 2);
    const qint64 silentAfter = qMax<qint64>(5000, intervalMs * 5);
    if (ageMs <= lateAfter) return Heartbeat::State::Live;
    if (ageMs <= silentAfter) return Heartbeat::State::Late;
    return Heartbeat::State::Silent;
}

QVector<HealthLight> healthLightsFrom(const QHash<QString, qint64> &dmi, bool dmiSeen,
                                      const QHash<QString, QStringList> &faultsByModule, bool nmsSeen)
{
    QVector<HealthLight> out;
    // dmiHealth: 1 OK, 0 fault. Only the three bits the schema maps.
    const struct { const char *field; const char *name; } bits[] = {
        { "LKAVACH_health_status", "Kavach unit" },
        { "pg_health_status",      "Pulse generator" },
        { "biu_health_status",     "BIU" },
    };
    for (const auto &b : bits) {
        HealthLight l;
        l.name = QObject::tr(b.name);
        if (!dmiSeen || !dmi.contains(QLatin1String(b.field))) {
            l.state = HealthLight::State::Unknown;
            l.detail = QObject::tr("no @dmi yet");
        } else {
            const bool ok = dmi.value(QLatin1String(b.field)) == 1;
            l.state = ok ? HealthLight::State::Ok : HealthLight::State::Fault;
            l.detail = QObject::tr("DMI %1 = %2").arg(QLatin1String(b.field)).arg(ok ? QObject::tr("OK") : QObject::tr("fault"));
        }
        out << l;
    }
    if (!nmsSeen) {
        out << HealthLight{ QObject::tr("NMS faults"), HealthLight::State::Unknown, QObject::tr("no @nmsflt yet") };
    } else if (faultsByModule.isEmpty()) {
        out << HealthLight{ QObject::tr("NMS faults"), HealthLight::State::Ok, QObject::tr("the last fault report lists none") };
    } else {
        QStringList modules = faultsByModule.keys();
        modules.sort(Qt::CaseInsensitive);
        for (const QString &m : modules) {
            const QStringList faults = faultsByModule.value(m);
            out << HealthLight{ m, HealthLight::State::Fault,
                                QObject::tr("%1 active: %2").arg(faults.size()).arg(faults.join(QStringLiteral("; "))) };
        }
    }
    return out;
}

// =============================================================================
//  CabDisplay
// =============================================================================

CabDisplay::CabDisplay(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(200);
    UiColor::onThemeChange(this, [this]() { update(); });
}

void CabDisplay::setState(const CabState &state)
{
    m_state = state;
    update();
}

void CabDisplay::setExtras(const QVector<Extra> &extras)
{
    const bool resize = extras.size() != m_extras.size();
    m_extras = extras;
    if (resize) {
        setMinimumHeight(200 + ((m_extras.size() + 2) / 3) * (fontMetrics().height() + 6));
        updateGeometry();
    }
    update();
}

QSize CabDisplay::sizeHint() const
{
    return QSize(760, 240 + ((m_extras.size() + 2) / 3) * (fontMetrics().height() + 6));
}

void CabDisplay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette pal = palette();
    p.fillRect(rect(), pal.color(QPalette::Base));
    const QColor text = pal.color(QPalette::Text), ink = UiColor::muted(), frame = UiColor::frame();
    const CabState &s = m_state;

    if (!s.hasDmi) {
        p.setPen(ink);
        p.drawText(rect(), Qt::AlignCenter, tr("No @dmi from this loco yet: the cab view is drawn from the DMI frame"));
        return;
    }
    const bool dim = s.stale;
    auto live = [dim, ink](const QColor &c) { return dim ? ink : c; };

    // ---- speed dial -----------------------------------------------------------------
    const int band = m_extras.isEmpty() ? 0 : ((m_extras.size() + 2) / 3) * (fontMetrics().height() + 6) + 8;
    const int dialSize = qMin(height() - 16 - band, width() / 3);
    const QRectF dial(12, (height() - band - dialSize) / 2.0, dialSize, dialSize);
    const QPointF c = dial.center();
    const double radius = dialSize / 2.0;
    // Session 92: a fixed 0-250 km/h dial -- the loco's top speed -- so a
    // needle's angle always means the same speed. It used to start at 120
    // and grow with the speed, so the scale moved under the operator.
    const double maxSpeed = kCabDialMaxKmh;
    // 240° sweep from 210° (lower left) clockwise to -30° (lower right).
    auto angleOf = [maxSpeed](double v) { return (210.0 - qBound(0.0, v / maxSpeed, 1.0) * 240.0) * kPi / 180.0; };
    auto at = [&](double v, double r) { return QPointF(c.x() + r * std::cos(angleOf(v)), c.y() - r * std::sin(angleOf(v))); };

    p.setPen(QPen(frame, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(dial);
    // permitted arc (0 .. permitted) and overspeed arc (permitted .. max)
    const QRectF arcRect = dial.adjusted(radius * 0.12, radius * 0.12, -radius * 0.12, -radius * 0.12);
    auto arc = [&](double from, double to, const QColor &col, double w) {
        const double a0 = 210.0 - qBound(0.0, from / maxSpeed, 1.0) * 240.0;
        const double a1 = 210.0 - qBound(0.0, to / maxSpeed, 1.0) * 240.0;
        p.setPen(QPen(col, w, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(arcRect, int(a0 * 16), int((a1 - a0) * 16));
    };
    if (s.hasPermitted) {
        arc(0, s.permitted, live(UiColor::ok()), radius * 0.09);
        arc(s.permitted, maxSpeed, live(UiColor::error()), radius * 0.04);
    } else {
        arc(0, maxSpeed, ink, radius * 0.03);
    }
    // ticks
    QFont small = p.font();
    small.setPointSizeF(qMax(6.0, small.pointSizeF() - 1.0));
    p.setFont(small);
    // Minor tick every 10 km/h, a labelled major tick every 50.
    for (int v = 0; v <= int(maxSpeed); v += 10) {
        const bool major = v % 50 == 0;
        p.setPen(QPen(ink, major ? 1.4 : 0.8));
        p.drawLine(at(v, radius * 0.95), at(v, radius * (major ? 0.82 : 0.88)));
        if (!major) continue;
        const QPointF lp = at(v, radius * 0.68);
        p.drawText(QRectF(lp.x() - 16, lp.y() - 8, 32, 16), Qt::AlignCenter, QString::number(v));
    }
    // target speed marker on the rim
    if (s.hasTarget) {
        const QPointF tip = at(s.targetSpeed, radius * 0.97);
        const QPointF base1 = at(s.targetSpeed - maxSpeed * 0.02, radius * 1.08);
        const QPointF base2 = at(s.targetSpeed + maxSpeed * 0.02, radius * 1.08);
        QPolygonF tri; tri << tip << base1 << base2;
        p.setPen(Qt::NoPen);
        p.setBrush(live(UiColor::series(2)));
        p.drawPolygon(tri);
    }
    // needle
    const bool over = s.hasPermitted && s.speed > s.permitted;
    p.setPen(QPen(live(over ? UiColor::error() : text), 3.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c, at(s.speed, radius * 0.8));
    p.setBrush(live(text));
    p.drawEllipse(c, 4, 4);
    QFont big = font();
    big.setBold(true);
    big.setPointSizeF(font().pointSizeF() * 2.0);
    p.setFont(big);
    p.setPen(live(over ? UiColor::error() : text));
    p.drawText(QRectF(c.x() - radius, c.y() + radius * 0.25, 2 * radius, radius * 0.4), Qt::AlignCenter,
               QString::number(s.speed, 'f', 0));
    p.setFont(small);
    p.setPen(ink);
    p.drawText(QRectF(c.x() - radius, c.y() + radius * 0.62, 2 * radius, radius * 0.25), Qt::AlignCenter,
               s.hasPermitted ? tr("km/h · permitted %1").arg(s.permitted, 0, 'f', 0) : tr("km/h"));

    // ---- distance-to-target bar ---------------------------------------------------------
    const double barX = dial.right() + 28;
    const QRectF bar(barX, 20, 26, height() - 56 - band);
    p.setFont(font());
    p.setPen(QPen(frame, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(bar);
    // Scale: 0–1000 m, compressed above 200 m (a driver needs the last
    // 200 m most), as the DMI's own bar is.
    auto fracOf = [](double d) {
        d = qBound(0.0, d, 1000.0);
        return d <= 200.0 ? d / 200.0 * 0.5 : 0.5 + (d - 200.0) / 800.0 * 0.5;
    };
    for (double m : { 0.0, 100.0, 200.0, 500.0, 1000.0 }) {
        const double y = bar.bottom() - fracOf(m) * bar.height();
        p.setPen(ink);
        p.setFont(small);
        p.drawText(QRectF(bar.right() + 4, y - 8, 44, 16), Qt::AlignLeft | Qt::AlignVCenter, QString::number(m, 'f', 0));
    }
    if (s.hasTarget) {
        const double f = fracOf(s.targetDistance);
        const QRectF fill(bar.left() + 2, bar.bottom() - f * bar.height(), bar.width() - 4, f * bar.height());
        p.fillRect(fill, live(s.targetDistance < 200 ? UiColor::warning() : UiColor::series(0)));
    }
    p.setFont(font());
    p.setPen(text);
    const QString distText = s.hasTarget
        ? (s.targetDistance >= 1000 ? tr("%1 km").arg(s.targetDistance / 1000.0, 0, 'f', 2) : tr("%1 m").arg(s.targetDistance, 0, 'f', 0))
        : tr("no target");
    p.drawText(QRectF(bar.left() - 20, bar.bottom() + 4, 110, 18), Qt::AlignLeft, distText);

    // ---- right column: target, time, mode, brake, signals ----------------------------------------
    const double colX = bar.right() + 64;
    const double colW = width() - colX - 12;
    int y = 18;
    // The label column fits its longest label in the current font (text
    // size changes with View > Text size).
    const int labelW = fontMetrics().horizontalAdvance(tr("Time to target")) + 14;
    const int lineH = fontMetrics().height() + 6;
    auto line = [&](const QString &label, const QString &value, const QColor &col, bool bold = false) {
        QFont f = font();
        p.setFont(f);
        p.setPen(ink);
        p.drawText(QRectF(colX, y, labelW, lineH), Qt::AlignLeft | Qt::AlignVCenter, label);
        f.setBold(bold);
        p.setFont(f);
        p.setPen(col);
        p.drawText(QRectF(colX + labelW, y, colW - labelW, lineH), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(f).elidedText(value, Qt::ElideRight, int(colW - labelW)));
        y += lineH;
    };
    line(tr("Target"), s.hasTarget ? tr("%1 km/h in %2").arg(s.targetSpeed, 0, 'f', 0).arg(distText) : tr("none"), live(text), true);
    const QString ttt = s.hasTarget ? timeToTarget(s.targetDistance, s.speed) : QString();
    line(tr("Time to target"), ttt.isEmpty() ? tr("—") : ttt, live(text));
    line(tr("Mode"), s.mode.isEmpty() ? tr("—") : s.mode, live(text), true);
    const QColor brakeCol = s.brakeLevel >= 3 ? UiColor::error() : s.brakeLevel == 2 ? UiColor::warning()
                          : s.brakeLevel == 1 ? UiColor::warning() : text;
    line(tr("Brake"), s.brake.isEmpty() ? tr("—") : s.brake, live(brakeCol), s.brakeLevel > 0);

    // signal lamps: current and next, as signal heads
    auto head = [&](double x, const QString &title, int aspect, const QString &aspectText) {
        const AspectLamps lamps = lampsFor(aspect);
        const QRectF body(x, y + 4, 34, 74);
        p.setPen(QPen(frame, 1.2));
        p.setBrush(pal.color(QPalette::AlternateBase));
        p.drawRoundedRect(body, 6, 6);
        for (int i = 0; i < 2; ++i) {
            const QPointF lc(body.center().x(), body.top() + 18 + i * 36);
            QColor col = pal.color(QPalette::Window);
            if (i < lamps.colours.size()) {
                col = UiColor::signalLamp(lamps.colours.at(i));
                if (dim) col = col.darker(170);
            }
            p.setPen(QPen(frame, 1));
            p.setBrush(col);
            p.drawEllipse(lc, 12, 12);
        }
        QFont f = font();
        p.setFont(small);
        p.setPen(ink);
        p.drawText(QRectF(x + 40, y + 4, colW / 2 - 44, 16), Qt::AlignLeft, title);
        p.setFont(f);
        p.setPen(live(text));
        const QString what = aspect < 0 ? tr("unknown")
                                        : (lamps.known ? aspectText : tr("%1 (not drawn)").arg(aspectText));
        p.drawText(QRectF(x + 40, y + 22, colW / 2 - 44, 56), Qt::AlignLeft | Qt::TextWordWrap, what);
    };
    head(colX, tr("Signal ahead (%1)").arg(s.aspectSource.isEmpty() ? tr("none") : s.aspectSource),
         s.currentAspect, s.currentAspectText);
    if (s.aspectSource == QLatin1String("slrp")) {
        head(colX + colW / 2, tr("Next signal"), s.nextAspect, s.nextAspectText);
    }

    if (dim) {
        p.setFont(font());
        p.setPen(UiColor::warning());
        p.drawText(rect().adjusted(0, 0, -10, -6), Qt::AlignRight | Qt::AlignBottom, tr("DMI frame is old — values muted"));
    }
    paintExtras(p);
}

void CabDisplay::paintExtras(QPainter &p)
{
    // Added fields: a band along the bottom, three to a row (session 83).
    if (m_extras.isEmpty()) return;
    const int rowH = fontMetrics().height() + 6;
    const int rows = (m_extras.size() + 2) / 3;
    const int top = height() - rows * rowH - 4;
    const int colW = (width() - 24) / 3;
    p.setPen(QPen(UiColor::frame(), 1));
    p.drawLine(12, top - 3, width() - 12, top - 3);
    for (int i = 0; i < m_extras.size(); ++i) {
        const Extra &e = m_extras.at(i);
        const QRect r(12 + (i % 3) * colW, top + (i / 3) * rowH, colW - 8, rowH);
        QFont f = font();
        p.setFont(f);
        p.setPen(UiColor::muted());
        const int labelW = qMin(colW / 2, fontMetrics().horizontalAdvance(e.label) + 10);
        p.drawText(QRect(r.left(), r.top(), labelW, r.height()), Qt::AlignLeft | Qt::AlignVCenter, e.label);
        f.setBold(true);
        p.setFont(f);
        p.setPen(e.stale ? UiColor::muted() : palette().color(QPalette::Text));
        p.drawText(QRect(r.left() + labelW, r.top(), r.width() - labelW, r.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(f).elidedText(e.value, Qt::ElideRight, r.width() - labelW));
    }
}

// =============================================================================
//  LinkLights
// =============================================================================

LinkLights::LinkLights(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    UiColor::onThemeChange(this, [this]() { update(); });
}

void LinkLights::setBeats(const QVector<Heartbeat> &beats)
{
    m_beats = beats;
    updateGeometry();
    update();
}

QSize LinkLights::sizeHint() const { return QSize(400, fontMetrics().height() + 10); }

void LinkLights::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    m_rects.clear();
    int x = 2;
    const int h = height();
    for (const Heartbeat &b : m_beats) {
        const QColor col = b.state == Heartbeat::State::Live ? UiColor::ok()
                         : b.state == Heartbeat::State::Late ? UiColor::warning() : UiColor::muted();
        const QString label = b.state == Heartbeat::State::Silent
            ? tr("%1 silent %2 s").arg(b.name).arg(b.ageMs / 1000)
            : tr("%1 %2/s").arg(b.name).arg(b.rate, 0, 'f', b.rate < 10 ? 1 : 0);
        const int w = fontMetrics().horizontalAdvance(label) + 24;
        const QRect r(x, 0, w, h);
        m_rects << r;
        const QPointF dot(x + 8, h / 2.0);
        if (b.pulse) {
            QColor halo = col;
            halo.setAlpha(90);
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(dot, 7.5, 7.5);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawEllipse(dot, 4.5, 4.5);
        p.setPen(b.state == Heartbeat::State::Silent ? UiColor::muted() : palette().color(QPalette::Text));
        p.drawText(QRect(x + 16, 0, w - 16, h), Qt::AlignLeft | Qt::AlignVCenter, label);
        x += w + 6;
    }
}

bool LinkLights::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        for (int i = 0; i < m_rects.size() && i < m_beats.size(); ++i) {
            if (m_rects.at(i).contains(he->pos())) {
                const Heartbeat &b = m_beats.at(i);
                QToolTip::showText(he->globalPos(),
                    tr("%1: last %2 ms ago, usually every %3 ms.\nAmber when later than twice its usual gap, grey "
                       "when silent for five.").arg(b.name).arg(b.ageMs).arg(b.intervalMs), this);
                return true;
            }
        }
        QToolTip::hideText();
        e->ignore();
        return true;
    }
    return QWidget::event(e);
}

// =============================================================================
//  HealthLights
// =============================================================================

HealthLights::HealthLights(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    UiColor::onThemeChange(this, [this]() { update(); });
}

void HealthLights::setLights(const QVector<HealthLight> &lights)
{
    m_lights = lights;
    update();
}

QSize HealthLights::sizeHint() const { return QSize(400, fontMetrics().height() + 10); }

void HealthLights::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    m_rects.clear();
    int x = 2;
    const int h = height();
    // Too many to fit: the faulting modules fold into one light that names
    // them in its tooltip, rather than running off the edge unseen.
    QVector<HealthLight> shown = m_lights;
    auto widthOf = [this](const QVector<HealthLight> &ls) {
        int w = 2;
        for (const HealthLight &l : ls) w += fontMetrics().horizontalAdvance(QStringLiteral("? ") + l.name) + 30;
        return w;
    };
    if (widthOf(shown) > width() && shown.size() > 4) {
        QVector<HealthLight> folded = shown.mid(0, 3);
        HealthLight nms{ tr("NMS: %1 module(s) with faults").arg(shown.size() - 3), HealthLight::State::Fault, QString() };
        QStringList details;
        for (int i = 3; i < shown.size(); ++i) details << QStringLiteral("%1 — %2").arg(shown.at(i).name, shown.at(i).detail);
        nms.detail = details.join(QLatin1Char('\n'));
        folded << nms;
        shown = folded;
    }
    for (const HealthLight &l : shown) {
        const QColor col = l.state == HealthLight::State::Ok ? UiColor::ok()
                         : l.state == HealthLight::State::Fault ? UiColor::error() : UiColor::muted();
        // A glyph as well as a colour: ✓ ✗ ?
        const QString glyph = l.state == HealthLight::State::Ok ? QStringLiteral("\u2713")
                            : l.state == HealthLight::State::Fault ? QStringLiteral("\u2717") : QStringLiteral("?");
        const QString label = QStringLiteral("%1 %2").arg(glyph, l.name);
        const int w = fontMetrics().horizontalAdvance(label) + 22;
        const QRect r(x, 0, w, h);
        m_rects << r;
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawEllipse(QPointF(x + 7, h / 2.0), 4.5, 4.5);
        if (m_rects.size() == 1) m_shownDetails.clear();
        m_shownDetails << l.detail;
        p.setPen(l.state == HealthLight::State::Unknown ? UiColor::muted() : col);
        p.drawText(QRect(x + 15, 0, w - 15, h), Qt::AlignLeft | Qt::AlignVCenter, label);
        x += w + 8;
    }
}

bool HealthLights::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        for (int i = 0; i < m_rects.size() && i < m_shownDetails.size(); ++i) {
            if (m_rects.at(i).contains(he->pos())) {
                QToolTip::showText(he->globalPos(), m_shownDetails.at(i), this);
                return true;
            }
        }
        QToolTip::hideText();
        e->ignore();
        return true;
    }
    return QWidget::event(e);
}

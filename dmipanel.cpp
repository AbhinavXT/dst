#include "dmipanel.h"

#include "capturedecoder.h"
#include "messagedispatcher.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QHeaderView>
#include <QTableWidget>
#include <QVBoxLayout>

#include "settings.h"

#include <cmath>

namespace {

const double kPi = 3.14159265358979323846;

// Screen regions, Annexure-B B4.2.1 (units of the 800 x 600 screen).
const QRectF kA(0, 0, 92, 388);
const QRectF kB(92, 0, 365, 388);
const QRectF kC(457, 0, 112, 388);
const QRectF kD(569, 0, 231, 419);
const QRectF kE(0, 388, 156, 45);
const QRectF kF(156, 388, 152, 45);
const QRectF kG(308, 388, 261, 45);
const QRectF kH(0, 433, 569, 69);
const QRectF kI(0, 502, 569, 46);
const QRectF kJ(569, 419, 135, 67);
const QRectF kL(569, 486, 135, 62);
const QRectF kM(704, 419, 96, 129);
const QRectF kK(0, 550, 800, 50);

// Dial (B1-B3): centre, and the radii the annexure gives as diameters.
const QPointF kDial(274.5, 186.0);
const double kDialR   = 157.0;   // B2 division circle, diameter 314
const double kOuterR  = 178.0;   // B3 outer, 356
const double kMidR    = 168.0;   // B3 mid, 336
const double kInnerR  = 158.0;   // B3 inner, 316

QString rowText(const QVector<FieldRow> &rows, const QString &field)
{
    // The body's own field (indented), not a header row of the same name.
    for (const FieldRow &r : rows) {
        if (r.field.startsWith(QLatin1Char(' ')) && r.field.trimmed() == field) return r.value.trimmed();
    }
    for (const FieldRow &r : rows) {
        if (r.field.trimmed() == field) return r.value.trimmed();
    }
    return QString();
}

QFont pxFont(const QFont &base, double px, bool bold)
{
    QFont f = base;
    f.setFamily(QStringLiteral("Helvetica"));
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(qMax(1, int(std::lround(px))));
    f.setBold(bold);
    return f;
}

double piecewise(const QVector<QPointF> &pts, double x)
{
    if (x <= pts.first().x()) return pts.first().y();
    for (int i = 1; i < pts.size(); ++i) {
        if (x <= pts.at(i).x()) {
            const QPointF a = pts.at(i - 1), b = pts.at(i);
            return a.y() + (x - a.x()) / (b.x() - a.x()) * (b.y() - a.y());
        }
    }
    return pts.last().y();
}

}  // namespace

// =============================================================================
//  Pure mapping: @dmi frame -> what each region shows
// =============================================================================

QString dmiModeName(int mode)
{
    switch (mode) {
    case 1:  return QObject::tr("Stand By Mode");
    case 2:  return QObject::tr("Staff Responsible Mode");
    case 3:  return QObject::tr("Limited Supervision Mode");
    case 4:  return QObject::tr("Full Supervision Mode");
    case 5:  return QObject::tr("Override Mode");
    case 6:  return QObject::tr("On Sight Mode");
    case 7:  return QObject::tr("Trip Mode");
    case 8:  return QObject::tr("Post Trip Mode");
    case 9:  return QObject::tr("Reverse Mode");
    case 10: return QObject::tr("Shunt Mode");
    case 11: return QObject::tr("Non Leading Mode");
    case 12: return QObject::tr("System Failure Mode");
    case 13: return QObject::tr("Isolation Mode");
    default: return QString();
    }
}

QString dmiSystemMessage(const QString &flag, const QHash<QString, qint64> &raw, const QHash<QString, QString> &text)
{
    auto n = [&raw](const char *k) { return QString::number(raw.value(QLatin1String(k))); };
    auto d4 = [&raw](const char *k) { return QStringLiteral("%1").arg(raw.value(QLatin1String(k)), 4, 10, QLatin1Char('0')); };
    // Region H wording (B4.6.7 (d), "Message to be displayed in LP-OCIP"),
    // with each XX / YYYY from the field the frame carries for it.
    if (flag.startsWith(QLatin1String("Ack Block Stop")))
        return QObject::tr("Ack Block Stop, SOS Generates in %1 s").arg(n("ack_blockstop_sos_generate_time"));
    if (flag.startsWith(QLatin1String("Head-On Collision")))
        return QObject::tr("Head On Collision with Loco %1 in %2 m").arg(n("collision_loco_id"), d4("collision_loco_distance"));
    if (flag.startsWith(QLatin1String("Rear-End Collision")))
        return QObject::tr("Rear End Collision with Loco %1 in %2 m").arg(n("collision_loco_id"), d4("collision_loco_distance"));
    if (flag.startsWith(QLatin1String("Override Selected")))
        return QObject::tr("Override selected, Pass Signal in %1s").arg(n("override_timeout"));
    if (flag.startsWith(QLatin1String("Reverse Mode Expires")))
        return QObject::tr("Reverse Mode Expires in %1m or %2s").arg(d4("expiry_reverse_distance"), n("expiry_reverse_mode"));
    if (flag == QLatin1String("Manned LC Gate") || flag == QLatin1String("Unmanned LC Gate"))
        return QObject::tr("%1 %2 in %3m").arg(flag, text.value(QStringLiteral("lc.id")).section(QLatin1Char(' '), 0, 0),
                                               d4("lc.distance"));
    if (flag.startsWith(QLatin1String("Turnout with Speed")))
        return QObject::tr("TurnOut in %1m with Speed Limit %2kmph").arg(d4("target_distance"), n("to_speed"));
    if (flag.startsWith(QLatin1String("TSR with Speed")))
        return QObject::tr("TSR in %1m with Speed Limit %2kmph").arg(d4("tsr_distance"), n("target_speed"));
    if (flag.startsWith(QLatin1String("PSR with Speed")))
        return QObject::tr("PSR in %1m with Speed Limit %2kmph").arg(d4("target_distance"), n("target_speed"));
    if (flag == QLatin1String("End of Authority"))
        return QObject::tr("End of Authority in %1m").arg(n("target_distance"));
    if (flag.startsWith(QLatin1String("Approaching Radio Hole")))
        return QObject::tr("Approaching Radio Hole in %1 m").arg(d4("track_condition_start_distance"));
    if (flag.startsWith(QLatin1String("Neutral Section")))
        return QObject::tr("Neutral Section approaching in %1 m").arg(n("track_condition_start_distance"));
    if (flag.startsWith(QLatin1String("Train Length Computation Success")))
        return QObject::tr("Train Length Computation Success (%1m)").arg(d4("train_length"));
    if (flag.startsWith(QLatin1String("Train Length Computation Fail")))
        return QObject::tr("Train Length Computation Fail (%1m)").arg(d4("train_length"));
    return flag;
}

QString dmiContextMessage(const QString &flag, const QHash<QString, qint64> &raw, const QHash<QString, QString> &text)
{
    auto n = [&raw](const char *k) { return QString::number(raw.value(QLatin1String(k))); };
    // Region I wording (B4.7 (d)).
    if (flag.startsWith(QLatin1String("FSB will be")))
        return QObject::tr("FSB will be applied in %1S").arg(n("fsb_applied_time"));
    if (flag.startsWith(QLatin1String("EB will be")))
        return QObject::tr("EB will be applied in %1S").arg(n("eb_applied_time"));
    if (flag == QLatin1String("SOS - Self Loco Manual"))       return QObject::tr("SOS – Self Loco ( Manual )");
    if (flag == QLatin1String("SOS - Self Loco Stopped in Block Section")) return QObject::tr("SOS – Self Loco ( Stopped in Block section )");
    if (flag == QLatin1String("SOS - Self Loco Train Parted")) return QObject::tr("SOS – Self Loco ( Train Parted )");
    if (flag == QLatin1String("SOS - Station (All Locos)"))
        return QObject::tr("SOS – From Station %1 ( SOS to All Locos )").arg(n("sos_station_id"));
    if (flag == QLatin1String("SOS - Station (This Loco)"))
        return QObject::tr("SOS – From Station %1 ( SOS to This Loco )").arg(n("sos_station_id"));
    if (flag == QLatin1String("Over Speed - Reduce Speed"))    return QObject::tr("Over Speed, Please Reduce Speed");
    if (flag == QLatin1String("Brake Applied - Speed Limit Exceeded")) return QObject::tr("Brake Applied, Speed Limit Exceeded");
    if (flag == QLatin1String("Train Type Selected")) {
        QString type = text.value(QStringLiteral("train_type"));
        const int open = type.indexOf(QLatin1Char('('));
        if (open >= 0) type = type.mid(open + 1).chopped(1).replace(QLatin1Char('_'), QLatin1Char(' '));
        return QObject::tr("%1 Train Type selected").arg(type);
    }
    // "SOS - Other Loco …": the frame names no loco id for it; the wording
    // stays without one rather than borrowing the collision loco's.
    if (flag.startsWith(QLatin1String("SOS - Other Loco ")))
        return QObject::tr("SOS – From Loco ( %1 )").arg(flag.mid(17));
    return flag;
}

double dmiTargetScaleY(double m)
{
    // A1 figure: 0 at 337, 250 at 272, 500 at 207, 1000 at 157, 2000 at 107.
    static const QVector<QPointF> pts{ { 0, 337 }, { 250, 272 }, { 500, 207 }, { 1000, 157 }, { 2000, 107 } };
    return piecewise(pts, qBound(0.0, m, 2000.0));
}

double dmiMaScaleY(double m)
{
    // C1 figure: 0 342, 100 308, 200 274, 250 259, 500 198, 1000 147,
    // 2000 112, 3000 77, beyond 3000 ("+++") 45.
    if (m > 3000.0) return 45.0;
    static const QVector<QPointF> pts{ { 0, 342 }, { 100, 308 }, { 200, 274 }, { 250, 259 }, { 500, 198 },
                                       { 1000, 147 }, { 2000, 112 }, { 3000, 77 } };
    return piecewise(pts, qMax(0.0, m));
}

QVector<int> dmiLitLamps(int a)
{
    // Lamps top to bottom: 0 YLW, 1 GRN, 2 YLW, 3 RED (B4.6.4.1 (a)).
    if (a == 1 || a == 15 || a == 24) return { 3 };          // red (calling-on, stop board: red too)
    if ((a >= 2 && a <= 8) || (a >= 32 && a <= 63)) return { 2 };   // yellow
    if (a == 10 || a == 12 || a == 13) return { 0, 2 };      // double yellow
    if (a == 11) return { 1 };                                // green
    return {};                                                // unidentified, AG marker: none lit
}

QStringList dmiAssumptions()
{
    return {
        QObject::tr("Movement authority (C) is ma_w_r_t_sig."),
        QObject::tr("Loco id (B4) is train_id."),
        QObject::tr("Next lower speed (B7) is target_speed, shown while there is a target."),
        QObject::tr("Tag diagram (M): the current, last and last-but-one tag IDs (rc/rl/rll), each with its status "
                    "(rcs/rls/rlls): 1 read (dark green, \u2713), 2 missed (red, \u2717), 0 none (grey), 3 unnamed (?)."),
        QObject::tr("RF bars (J3): signal_strength, capped at 5."),
    };
}

DmiState dmiStateFrom(const QVector<FieldRow> &rows, const QHash<QString, qint64> &raw)
{
    DmiState s;
    if (!raw.contains(QStringLiteral("train_speed"))) return s;
    s.valid = true;
    QHash<QString, QString> text;
    for (const FieldRow &r : rows) {
        if (!r.field.startsWith(QLatin1Char(' '))) continue;
        const QString f = r.field.trimmed();
        if (!text.contains(f)) text.insert(f, r.value.trimmed());
    }
    auto v = [&raw](const char *k) { return int(raw.value(QLatin1String(k))); };

    s.speed = v("train_speed") >= 511 ? 0 : v("train_speed");
    s.permitted = v("speed_limit_permissible");
    s.hasPermitted = s.permitted > 0 && s.permitted < 511;
    s.targetDistance = v("target_distance");
    s.targetSpeed = v("target_speed");
    s.hasTargetSpeed = s.targetDistance > 0;
    s.sectionSpeed = v("section_speed_info");
    s.locoId = QString::number(v("train_id"));
    // B5/B6 from the body's DATE_TIME ("26-06-2026 16:24:19").
    const QDateTime dt = QDateTime::fromString(text.value(QStringLiteral("rtc")), QStringLiteral("dd-MM-yyyy HH:mm:ss"));
    if (dt.isValid()) {
        s.date = QLocale::c().toString(dt, QStringLiteral("dd-MMM-yyyy"));
        s.time = dt.toString(QStringLiteral("HH:mm:ss"));
    }
    s.locationKm = raw.value(QStringLiteral("abs_loco_loc")) / 1000.0;
    s.brakeType = v("brake_type");
    s.mode = v("loco_mode");
    s.modeText = dmiModeName(s.mode);

    switch (v("target_distance_type")) {
    case 1: s.targetType = QStringLiteral("EOA"); break;
    case 2: s.targetType = QStringLiteral("Turnout"); break;
    case 3: s.targetType = QStringLiteral("TSR"); break;
    case 4: s.targetType = QStringLiteral("PSR"); break;
    case 5: s.targetType = QStringLiteral("Collision"); break;
    case 6: s.targetType = QStringLiteral("SoS"); break;
    default: break;
    }
    s.movementAuthority = v("ma_w_r_t_sig");

    s.aspect = v("current_sig_aspect");
    if (s.aspect >= 3 && s.aspect <= 8) s.route = s.aspect - 2;
    if (s.aspect == 12) s.route = 1;
    if (s.aspect == 13) s.route = 4;
    if (s.aspect >= 32 && s.aspect <= 63) s.stencil = s.aspect - 31;
    s.signalDistance = v("appr_sig_dist");
    // D3: "DN MAIN Adv-Str" from current_sig_info's decoded parts.
    static const QRegularExpression re(QStringLiteral("type=(\\S+)\\s+dir=(.+?)\\s+line=(\\S+)"));
    const QRegularExpressionMatch m = re.match(text.value(QStringLiteral("current_sig_info")));
    if (m.hasMatch() && m.captured(1) != QLatin1String("undefined")) {
        QString kind = m.captured(1);
        if (m.captured(3) != QLatin1String("n/a")) kind.replace(QLatin1String("L-X"), QStringLiteral("L-") + m.captured(3));
        s.signalName = m.captured(2) + QLatin1Char(' ') + kind;
        // The disc under the post (B4.6.4.1 (b)).
        if (kind.startsWith(QLatin1String("Calling"))) s.disc = QStringLiteral("C");
        else if (kind == QLatin1String("Auto-Gate")) s.disc = QStringLiteral("AG");
        else if (kind.startsWith(QLatin1String("IB-Stop"))) s.disc = QStringLiteral("IB");
        else if (kind.startsWith(QLatin1String("Gate-Stop"))) s.disc = QStringLiteral("G");
        else if (kind == QLatin1String("Auto")) s.disc = QStringLiteral("A");
    }
    if (s.aspect == 15) s.disc = QStringLiteral("C");

    s.dc = text.value(QStringLiteral("deceleration_constant"));
    s.trainLength = v("train_length");

    for (const QString &flag : text.value(QStringLiteral("alarm_code")).split(QStringLiteral("; "), Qt::SkipEmptyParts)) {
        if (flag == QLatin1String("no_alarm") || flag == QLatin1String("(none)")) continue;
        s.systemMessages << dmiSystemMessage(flag, raw, text);
    }
    for (const QString &flag : text.value(QStringLiteral("context_values")).split(QStringLiteral("; "), Qt::SkipEmptyParts)) {
        if (flag == QLatin1String("(none)")) continue;
        s.contextMessages << dmiContextMessage(flag, raw, text);
    }

    s.rfBars = qBound(0, v("signal_strength"), 5);
    s.tagId = v("rc");
    const int dir = v("movement_dir");
    s.tagDir = dir == 1 ? QStringLiteral("N") : dir == 2 ? QStringLiteral("R") : QString();
    s.nextTagDistance = v("dist_next_rfid");
    s.tagIds[0] = v("rc");  s.tagStatus[0] = v("rcs");
    s.tagIds[1] = v("rl");  s.tagStatus[1] = v("rls");
    s.tagIds[2] = v("rll"); s.tagStatus[2] = v("rlls");
    return s;
}

DmiState dmiStateFromLine(const QString &line)
{
    return dmiStateFromCapture(CaptureDecoder::parseLine(line));
}

DmiState dmiStateFromCapture(const CaptureLine &cap)
{
    QHash<QString, qint64> raw;
    if (!cap.valid || cap.type != CapType::Dmi) return DmiState();
    const QVector<FieldRow> rows = CaptureDecoder::describe(cap, nullptr, 0, &raw);
    return dmiStateFrom(rows, raw);
}

// =============================================================================
//  Colours
// =============================================================================

DmiColours DmiColours::annexure()
{
    // Table B.2.
    DmiColours c;
    c.bg  = QColor(0, 0, 0);
    c.wht = QColor(255, 255, 255);
    c.lgy = QColor(128, 128, 128);
    c.mgy = QColor(150, 150, 150);
    c.gry = QColor(192, 192, 192);
    c.lbl = QColor(0, 139, 206);
    c.ylw = QColor(223, 223, 0);
    c.lor = QColor(255, 165, 0);
    c.org = QColor(255, 128, 64);
    c.brd = QColor(255, 0, 0);
    c.lgr = QColor(128, 255, 0);
    c.grn = QColor(0, 255, 0);
    c.dgr = QColor(0, 128, 0);
    return c;
}

DmiColours DmiColours::forTheme(const QPalette &p)
{
    // The theme's own background and ink; each Annexure-B hue moved just far
    // enough to be seen on that background (3:1, the graphics floor).
    const DmiColours a = annexure();
    DmiColours c;
    c.bg  = p.color(QPalette::Base);
    c.wht = p.color(QPalette::Text);
    c.gry = UiColor::frame();
    c.lgy = UiColor::muted();
    c.mgy = UiColor::withContrast(p.color(QPalette::Button), c.bg, 1.0);
    auto fit = [&c](const QColor &x) { return UiColor::withContrast(x, c.bg, 3.0); };
    c.lbl = fit(a.lbl);
    c.ylw = fit(a.ylw);
    c.lor = fit(a.lor);
    c.org = fit(a.org);
    c.brd = fit(a.brd);
    c.lgr = fit(a.lgr);
    c.grn = fit(a.grn);
    c.dgr = fit(a.dgr);
    return c;
}

// =============================================================================
//  The panel
// =============================================================================

DmiView::DmiView(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(400, 300);
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Expanding);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    UiColor::onThemeChange(this, [this]() { update(); });
}

void DmiView::setState(const DmiState &state)
{
    m_state = state;
    update();
}

void DmiView::setAnnexureColours(bool on)
{
    m_annexure = on;
    update();
}

void DmiView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    const DmiColours c = m_annexure ? DmiColours::annexure() : DmiColours::forTheme(palette());
    p.fillRect(rect(), m_annexure ? QColor(0, 0, 0) : palette().color(QPalette::Window));

    // 800 x 600 units, scaled to fit, centred.
    const double scale = qMin(width() / 800.0, height() / 600.0);
    p.translate((width() - 800 * scale) / 2.0, (height() - 600 * scale) / 2.0);
    p.scale(scale, scale);
    p.fillRect(QRectF(0, 0, 800, 600), c.bg);

    const DmiState &s = m_state;
    const QFont base = font();
    auto text = [&p, &base](const QRectF &r, const QString &t, double px, bool bold, const QColor &col,
                            int align = Qt::AlignLeft | Qt::AlignVCenter) {
        p.setFont(pxFont(base, px, bold));
        p.setPen(col);
        p.drawText(r, align, t);
    };
    const QColor ink = m_stale ? c.lgy : c.wht;

    // Region borders: GRY, 1 unit (B4.1.3).
    p.setPen(QPen(c.gry, 1));
    p.setBrush(Qt::NoBrush);
    for (const QRectF &r : { kA, kB, kC, kD, kE, kF, kG, kH, kI, kJ, kL, kM }) p.drawRect(r);

    if (!s.valid) {
        text(QRectF(0, 0, 800, 600), m_emptyText.isEmpty() ? tr("Waiting for @dmi from the selected loco") : m_emptyText,
             24, false, c.lgy, Qt::AlignCenter);
        return;
    }

    // ---- Region A: target distance -----------------------------------------------
    // Session 92: the scale, its labels and the distance are always drawn,
    // so the column reads "0000 m" rather than vanishing when the target
    // distance and type are 0. Only the bar and the type need a target.
    {
        if (!s.targetType.isEmpty())
            text(QRectF(kA.left(), 4, kA.width(), 22), s.targetType, 18.67, true, ink, Qt::AlignHCenter | Qt::AlignVCenter);
        text(QRectF(kA.left(), 28, kA.width(), 22), tr("Target"), 18.67, false, ink, Qt::AlignHCenter | Qt::AlignVCenter);
        text(QRectF(kA.left(), 50, kA.width(), 22), tr("Distance"), 18.67, false, ink, Qt::AlignHCenter | Qt::AlignVCenter);
        const double markX = kA.left() + 45, markW = 30;
        p.setPen(QPen(ink, 1));
        for (double y : { 107.0, 124.0, 141.0, 157.0, 182.0, 207.0, 272.0, 337.0 }) p.drawLine(QPointF(markX, y), QPointF(markX + markW, y));
        const struct { double y; const char *label; } labels[] = { { 107, "2000" }, { 157, "1000" }, { 207, "500" }, { 272, "250" }, { 337, "0" } };
        for (const auto &l : labels) text(QRectF(kA.left(), l.y - 10, 40, 20), QLatin1String(l.label), 14, false, ink, Qt::AlignRight | Qt::AlignVCenter);
        if (s.targetDistance > 0) {
            const double top = dmiTargetScaleY(s.targetDistance);
            p.fillRect(QRectF(markX + 3, top, 24, 337 - top), m_stale ? c.lgy : c.lor);
        }
        text(QRectF(kA.left(), 352, kA.width(), 26), QStringLiteral("%1 m").arg(qMin(s.targetDistance, 9999), 4, 10, QLatin1Char('0')),
             18.67, true, ink, Qt::AlignHCenter | Qt::AlignVCenter);
    }

    // ---- Region B: speedometer ------------------------------------------------------
    auto angleOf = [](double kmh) { return (149.0 - qBound(0.0, kmh, 250.0) * 298.0 / 250.0) * kPi / 180.0; };
    auto at = [&](double kmh, double r) { return QPointF(kDial.x() - r * std::sin(angleOf(kmh)), kDial.y() - r * std::cos(angleOf(kmh))); };
    auto arcPath = [&](double from, double to, double rOut, double rIn) {
        QPainterPath path;
        const int n = qMax(2, int(std::fabs(to - from) / 1.0));
        for (int i = 0; i <= n; ++i) {
            const QPointF pt = at(from + (to - from) * i / n, rOut);
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        for (int i = n; i >= 0; --i) path.lineTo(at(from + (to - from) * i / n, rIn));
        path.closeSubpath();
        return path;
    };
    // B3 rings
    const bool over = s.hasPermitted && s.speed > s.permitted;
    const bool braking = s.brakeType >= 2 && s.brakeType <= 4;
    p.setPen(Qt::NoPen);
    if (s.hasPermitted) {
        p.setBrush(m_stale ? c.lgy : c.lgr);
        p.drawPath(arcPath(0, s.permitted, kOuterR, kMidR));
        p.drawPath(arcPath(qMax(0, s.permitted - 2), s.permitted, kMidR, kInnerR));   // edge mark: one division
    }
    if (s.hasTargetSpeed && s.hasPermitted && s.targetSpeed < s.permitted) {
        p.setBrush(m_stale ? c.lgy : c.dgr);
        p.drawPath(arcPath(0, s.targetSpeed, kMidR, kInnerR));
    }
    if (over) {
        p.setBrush(m_stale ? c.lgy : (braking ? c.brd : c.lor));
        p.drawPath(arcPath(s.permitted, s.speed, kOuterR, kInnerR));
    }
    // B2 divisions: 125 of 2 km/h; every 10th labelled (17 x 2), every 5th 15 x 2, rest 6 x 1.
    for (int i = 0; i <= 125; ++i) {
        const double kmh = i * 2.0;
        const double len = (i % 10 == 0) ? 17 : (i % 5 == 0) ? 15 : 6;
        p.setPen(QPen(ink, (i % 5 == 0) ? 2 : 1));
        p.drawLine(at(kmh, kDialR), at(kmh, kDialR - len));
        if (i % 10 == 0) {
            const QPointF lp = at(kmh, kDialR - 17 - 16);
            text(QRectF(lp.x() - 20, lp.y() - 11, 40, 22), QString::number(int(kmh)), 20, false, ink, Qt::AlignCenter);
        }
    }
    // B1 pointer: white within the limit, yellow at it, light orange above,
    // red while the system brakes (B4.4.6, B4.10).
    QColor ptr = c.wht;
    if (s.hasPermitted && s.speed == s.permitted) ptr = c.ylw;
    if (over) ptr = braking ? c.brd : c.lor;
    if (m_stale) ptr = c.lgy;
    {
        const double a = angleOf(s.speed);
        const QPointF dir(-std::sin(a), -std::cos(a));
        const QPointF nrm(-dir.y(), dir.x());
        const QPointF tip = kDial + dir * 130.0, neck = kDial + dir * 112.0;
        QPolygonF arm;
        arm << kDial + nrm * 5.0 << neck + nrm * 5.0 << tip + nrm * 2.0 << tip - nrm * 2.0 << neck - nrm * 5.0 << kDial - nrm * 5.0;
        p.setPen(Qt::NoPen);
        p.setBrush(ptr);
        p.drawPolygon(arm);
        p.drawEllipse(kDial, 26, 26);
        text(QRectF(kDial.x() - 26, kDial.y() - 26, 52, 52), QString::number(s.speed), 20, true, QColor(0, 0, 0), Qt::AlignCenter);
    }
    // B4 loco id, B5/B6 date and time
    text(QRectF(100, 2, 150, 24), s.locoId, 18.67, true, ink);
    text(QRectF(kB.right() - 180, 2, 176, 20), s.date, 16, true, ink, Qt::AlignRight | Qt::AlignVCenter);
    text(QRectF(kB.right() - 180, 20, 176, 20), s.time, 16, true, ink, Qt::AlignRight | Qt::AlignVCenter);
    // B7 next lower speed, B11 section speed
    if (s.hasTargetSpeed) text(QRectF(kB.left() + 10, 312, 60, 20), QString::number(s.targetSpeed), 16, true, ink);
    if (s.sectionSpeed > 0) text(QRectF(kDial.x() - 40, 285, 80, 24), QString::number(s.sectionSpeed), 18.67, true, ink, Qt::AlignCenter);
    // B8 brake symbol
    if (s.brakeType >= 2 && s.brakeType <= 4) {
        const QRectF br(kB.left() + 4, 346, 55, 35);
        p.fillRect(br, s.brakeType == 4 ? c.brd : c.bg);
        p.setPen(QPen(s.brakeType == 4 ? QColor(0, 0, 0) : c.brd, 3));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(br.center(), 9, 9);
        p.drawArc(QRectF(br.center().x() - 15, br.center().y() - 13, 30, 26), 110 * 16, 140 * 16);
        p.drawArc(QRectF(br.center().x() - 15, br.center().y() - 13, 30, 26), -70 * 16, 140 * 16);
        if (s.brakeType != 4) {
            p.drawLine(QPointF(br.left() + 2, br.center().y()), QPointF(br.center().x() - 17, br.center().y()));
            if (s.brakeType == 3) p.drawLine(QPointF(br.center().x() + 17, br.center().y()), QPointF(br.right() - 2, br.center().y()));
        }
    }
    // B9 location
    text(QRectF(kB.left() + 60, 346, 240, 30), tr("LOC : %1 Km").arg(s.locationKm, 0, 'f', 3), 18.67, true, ink, Qt::AlignCenter);
    // B10 mode symbol (55 x 35 at x 398, y 346)
    {
        const QRectF mr(398, 346, 55, 35);
        const QPointF mc = mr.center();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(ink, 2));
        switch (s.mode) {
        case 1:   // stand by: power symbol
            p.drawArc(QRectF(mc.x() - 12, mc.y() - 12, 24, 24), 120 * 16, 300 * 16);
            p.drawLine(QPointF(mc.x(), mc.y() - 15), QPointF(mc.x(), mc.y()));
            break;
        case 2:   // SR: box with a cross
            p.drawRect(mr.adjusted(12, 4, -12, -4));
            p.drawLine(mr.adjusted(12, 4, -12, -4).topLeft(), mr.adjusted(12, 4, -12, -4).bottomRight());
            p.drawLine(mr.adjusted(12, 4, -12, -4).topRight(), mr.adjusted(12, 4, -12, -4).bottomLeft());
            break;
        case 3:   // LS: box with a diamond
            p.drawRect(mr.adjusted(12, 4, -12, -4));
            p.drawPolygon(QPolygonF({ QPointF(mc.x(), mr.top() + 7), QPointF(mc.x() + 9, mc.y()), QPointF(mc.x(), mr.bottom() - 7), QPointF(mc.x() - 9, mc.y()) }));
            break;
        case 4:   // FS: box with an oval
            p.drawRect(mr.adjusted(6, 5, -6, -5));
            p.drawEllipse(mr.adjusted(12, 10, -12, -10));
            break;
        case 6: { // OS: the eye
            QPolygonF eye({ QPointF(mr.right() - 8, mr.top() + 4), QPointF(mr.left() + 10, mc.y()), QPointF(mr.right() - 8, mr.bottom() - 4) });
            p.drawPolyline(eye);
            p.drawArc(QRectF(mc.x() - 2, mr.top() + 7, 14, 21), -90 * 16, 180 * 16);
            break;
        }
        case 7: case 8: {   // trip / post trip: a signal head
            const QRectF head(mc.x() - 7, mr.top() + 2, 14, 31);
            if (s.mode == 7) { p.setBrush(c.brd); }
            p.drawRoundedRect(head, 4, 4);
            p.setBrush(s.mode == 7 ? c.wht : Qt::NoBrush);
            for (int k = 0; k < 3; ++k) p.drawEllipse(QPointF(mc.x(), head.top() + 6 + k * 9.5), 3, 3);
            break;
        }
        case 9:   // reverse: curved arrow
            p.drawArc(QRectF(mc.x() - 10, mr.top() + 5, 20, 22), 90 * 16, -250 * 16);
            p.drawLine(QPointF(mc.x() - 12, mc.y() - 3), QPointF(mc.x() - 18, mc.y() - 3));
            break;
        case 10:  // shunt: a small wagon
            p.drawRect(QRectF(mc.x() - 14, mc.y() - 6, 28, 10));
            p.drawEllipse(QPointF(mc.x() - 8, mc.y() + 8), 3, 3);
            p.drawEllipse(QPointF(mc.x() + 8, mc.y() + 8), 3, 3);
            break;
        case 11:  // non leading: crossing arrows
            p.drawLine(QPointF(mr.left() + 12, mr.top() + 6), QPointF(mr.right() - 12, mr.bottom() - 6));
            p.drawLine(QPointF(mr.right() - 12, mr.top() + 6), QPointF(mr.left() + 12, mr.bottom() - 6));
            break;
        case 12: { // system failure: red triangle
            p.setBrush(c.brd);
            p.setPen(Qt::NoPen);
            p.drawPolygon(QPolygonF({ QPointF(mc.x(), mr.top() + 2), QPointF(mr.right() - 12, mr.bottom() - 2), QPointF(mr.left() + 12, mr.bottom() - 2) }));
            text(mr, QStringLiteral("!"), 20, true, QColor(255, 255, 255), Qt::AlignCenter);
            break;
        }
        default:
            break;
        }
    }

    // ---- Region C: movement authority --------------------------------------------------
    text(QRectF(kC.left() + 4, 2, kC.width() - 4, 22), tr("Mov. Authority"), 16, false, ink);
    {
        const double markX = kC.left() + 45;
        p.setPen(QPen(ink, 1));
        for (double y : { 45.0, 77.0, 112.0, 147.0, 198.0, 259.0, 274.0, 308.0, 342.0 }) p.drawLine(QPointF(markX, y), QPointF(markX + 35, y));
        const struct { double y; const char *label; } labels[] = { { 45, "+++" }, { 77, "3000" }, { 112, "2000" }, { 147, "1000" },
                                                                   { 198, "500" }, { 259, "250" }, { 342, "0" } };
        for (const auto &l : labels) text(QRectF(kC.left(), l.y - 10, 40, 20), QLatin1String(l.label), 14, false, ink, Qt::AlignRight | Qt::AlignVCenter);
        if (s.movementAuthority > 0) {
            const double top = dmiMaScaleY(s.movementAuthority);
            p.fillRect(QRectF(markX + 3, top, 29, 342 - top), m_stale ? c.lgy : c.lbl);
        }
        text(QRectF(kC.left(), 352, kC.width(), 26), QStringLiteral("%1 m").arg(qMin(s.movementAuthority, 99999), 5, 10, QLatin1Char('0')),
             18.67, true, ink, Qt::AlignHCenter | Qt::AlignVCenter);
    }

    // ---- Region D: signal -------------------------------------------------------------------
    // Session 89: the signal post, its lamps, stem and disc are always
    // drawn. With no aspect (0, "Unidentified") every lamp is simply unlit;
    // what depends on a signal being known -- the route indicator, the
    // stencil and the distance -- still waits for one.
    {
        const double postX = 651, postY = 45;
        QPolygonF post({ QPointF(postX + 12, postY), QPointF(postX + 58, postY), QPointF(postX + 70, postY + 12),
                         QPointF(postX + 70, postY + 208), QPointF(postX + 58, postY + 220), QPointF(postX + 12, postY + 220),
                         QPointF(postX, postY + 208), QPointF(postX, postY + 12) });
        p.setPen(QPen(ink, 2));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(post);
        const QVector<int> lit = dmiLitLamps(s.aspect);
        const QString lampName[4] = { QStringLiteral("yellow"), QStringLiteral("green"), QStringLiteral("yellow"), QStringLiteral("red") };
        for (int k = 0; k < 4; ++k) {
            const QPointF lc(postX + 35, postY + 12 + 22 + k * 52);
            p.setPen(QPen(ink, 2));
            p.setBrush(lit.contains(k) ? (m_stale ? UiColor::signalLamp(lampName[k]).darker(170) : UiColor::signalLamp(lampName[k]))
                                       : QBrush(Qt::NoBrush));
            p.drawEllipse(lc, 20, 20);
        }
        // stem and disc
        p.setPen(QPen(ink, 2));
        p.drawLine(QPointF(postX + 35, postY + 220), QPointF(postX + 35, postY + 300));
        const QPointF discC(postX + 35, postY + 237);
        // The disc is white on the post in every theme (B4.6.4.1 (b)), like a lamp.
        p.setBrush(s.disc.isEmpty() ? QBrush(c.bg) : QBrush(UiColor::signalLamp(QStringLiteral("white"))));
        p.drawEllipse(discC, 15, 15);
        if (!s.disc.isEmpty()) text(QRectF(discC.x() - 15, discC.y() - 15, 30, 30), s.disc, 16, false, QColor(0, 0, 0), Qt::AlignCenter);
        // junction route indicators (Table B.4), and the stencil
        if (s.route > 0) {
            static const QPointF pos[6] = { { 587, 24 }, { 580, 59 }, { 585, 85 }, { 706, 24 }, { 715, 59 }, { 710, 85 } };
            const QPointF o = pos[s.route - 1];
            QPointF a0, a1;
            switch (s.route) {
            case 1: case 6: a0 = o + QPointF(6, 4); a1 = o + QPointF(40, 30); break;     // "\"
            case 2: case 5: a0 = o + QPointF(4, 12); a1 = o + QPointF(56, 12); break;    // "-"
            default:        a0 = o + QPointF(6, 30); a1 = o + QPointF(40, 4); break;     // "/"
            }
            p.setPen(QPen(ink, 9, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(a0, a1);
        }
        if (s.stencil > 0) text(QRectF(660, 6, 60, 36), QString::number(s.stencil), 28, true, ink, Qt::AlignCenter);
        // Session 92: the distance too, even with no aspect ("0000 m").
        text(QRectF(kD.left(), 360, kD.width(), 26), QStringLiteral("%1 m").arg(qMin(s.signalDistance, 9999), 4, 10, QLatin1Char('0')),
             18.67, true, ink, Qt::AlignHCenter | Qt::AlignVCenter);
    }
    text(QRectF(kD.left() + 2, 392, kD.width() - 4, 24), s.signalName, 17.33, false, m_stale ? c.lgy : c.org);

    // ---- E F G ------------------------------------------------------------------------------------
    text(kE.adjusted(10, 0, -4, 0), s.dc, 18.67, false, ink, Qt::AlignHCenter | Qt::AlignVCenter);
    text(kF.adjusted(10, 0, -4, 0), tr("TL %1 m").arg(s.trainLength), 18.67, false, ink, Qt::AlignHCenter | Qt::AlignVCenter);
    text(kG.adjusted(13, 0, -4, 0), s.modeText, 18.67, false, ink);

    // ---- H I: messages, alternating every 2 s when several ----------------------------------------
    if (!s.systemMessages.isEmpty()) {
        const QString msg = s.systemMessages.at(m_msgTick % s.systemMessages.size());
        text(kH.adjusted(11, 0, -8, 0), msg, 24, true, ink);
    }
    if (!s.contextMessages.isEmpty()) {
        const QString msg = s.contextMessages.at(m_msgTick % s.contextMessages.size());
        text(kI.adjusted(11, 0, -8, 0), msg, 22.67, true, ink);
    }

    // ---- J: radio ------------------------------------------------------------------------------------
    {
        const double x0 = kJ.left() + 22, base = kJ.top() + 48;
        p.setPen(QPen(ink, 2));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(QPolygonF({ QPointF(x0 + 8, base - 38), QPointF(x0 + 44, base - 38), QPointF(x0 + 26, base - 20) }));
        p.setPen(QPen(ink, 4));
        p.drawLine(QPointF(x0 + 26, base - 38), QPointF(x0 + 26, base));
        text(QRectF(kJ.left() + 2, base - 22, 24, 20), QStringLiteral("RF"), 14, false, ink);
        for (int b = 0; b < 5; ++b) {
            const double h = 15 + b * 5;
            p.setPen(QPen(b < s.rfBars ? ink : c.lgy, 4));
            if (b >= s.rfBars) p.setPen(QPen(c.lgy, 1));
            p.drawLine(QPointF(x0 + 52 + b * 8, base), QPointF(x0 + 52 + b * 8, base - h));
        }
    }
    // ---- L: last tag ----------------------------------------------------------------------------------
    text(QRectF(kL.left() + 6, kL.top() + 6, kL.width() - 8, 22),
         tr("Tid-%1, Dir-%2").arg(s.tagId).arg(s.tagDir.isEmpty() ? QStringLiteral("-") : s.tagDir), 16, false, ink);
    text(QRectF(kL.left() + 6, kL.top() + 30, kL.width() - 8, 22), tr("T Dist-%1").arg(s.nextTagDistance), 16, false, ink);
    // ---- M: the last three tags on a track -------------------------------------------------------------
    // Session 89: each tag's ID is written beside its sleeper, in the colour
    // of its status, with a mark that does not rely on colour: ✓ read (1),
    // ✗ missed (2), nothing for none (0), ? for a status the spec does not
    // name (3). An empty slot (ID 0, status 0) shows a dash.
    {
        text(QRectF(kM.left(), kM.top() + 2, kM.width(), 18), tr("Tag"), 14, false, ink, Qt::AlignHCenter | Qt::AlignVCenter);
        const double top = kM.top() + 20, bottom = top + 104, cx = kM.left() + 27;
        p.setPen(QPen(ink, 1));
        p.drawLine(QPointF(cx - 20, top), QPointF(cx - 6, bottom));
        p.drawLine(QPointF(cx + 20, top), QPointF(cx + 6, bottom));
        for (int k = 0; k < 6; ++k) {
            const double y = top + 8 + k * 16, half = 22 - k * 2.6;
            p.drawLine(QPointF(cx - half, y), QPointF(cx + half, y));
        }
        const double widths[3] = { 26, 17, 12 };
        for (int k = 0; k < 3; ++k) {
            const double y = top + 22 + k * 30;
            const int st = s.tagStatus[k];
            const QColor fill = st == 1 ? c.dgr : st == 2 ? c.brd : c.gry;
            p.fillRect(QRectF(cx - widths[k] / 2, y, widths[k], 8), m_stale ? c.lgy : fill);
            const bool empty = s.tagIds[k] == 0 && st == 0;
            const QString mark = st == 1 ? QStringLiteral("\u2713") : st == 2 ? QStringLiteral("\u2717")
                               : st == 3 ? QStringLiteral("?") : QString();
            const QColor idInk = m_stale ? c.lgy : st == 1 ? c.dgr : st == 2 ? c.brd : ink;
            text(QRectF(kM.left() + 50, y - 7, kM.width() - 51, 22),
                 empty ? QStringLiteral("\u2014") : QString::number(s.tagIds[k]) + mark, 13, st != 0, idInk);
        }
    }
    // ---- K: soft keys (labels only) ----------------------------------------------------------------------
    {
        const char *keys[10] = { "P_TRP", "REV", "OVRD", "SHNT", "MBT", "SR", "CONFIG", "", "CNFM", "INFO" };
        for (int k = 0; k < 10; ++k) {
            const QRectF kr(4 + k * 79.6, kK.top() + 6, 75, 38);
            p.setPen(QPen(c.gry, 1));
            p.setBrush(c.mgy);
            p.drawRect(kr);
            text(kr, QLatin1String(keys[k]), 18.67, true, m_annexure ? QColor(255, 255, 255) : palette().color(QPalette::ButtonText),
                 Qt::AlignCenter);
        }
    }
    if (m_stale) {
        text(QRectF(0, 0, 800, 22), tr("frame is old"), 14, true, c.lor, Qt::AlignHCenter | Qt::AlignTop);
    }
}

// =============================================================================
//  Window
// =============================================================================

DmiWindow::DmiWindow(MessageDispatcher *dispatcher, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_dispatcher(dispatcher)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("DMI (LP-OCIP)"));
    WindowGeometry::makeResizableWindow(this);
    resize(880, 720);
    WindowGeometry::restore(this, QStringLiteral("dmiWindow"));

    m_view = new DmiView(this);
    m_source = new QComboBox(this);
    m_source->setObjectName(QStringLiteral("dmiSource"));
    m_source->setMinimumWidth(120);
    auto *annexure = new QCheckBox(tr("Annexure-B colours"), this);
    annexure->setObjectName(QStringLiteral("dmiAnnexureColours"));
    annexure->setToolTip(tr("Table B.2's exact colours on black, as the real panel. Off: follow the console's theme."));
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    annexure->setChecked(settings.value(QStringLiteral("dmi/annexureColours"), false).toBool());
    m_view->setAnnexureColours(annexure->isChecked());
    // Session 84: time travel.
    m_follow = new QCheckBox(tr("\u23F1 Follow cursor"), this);
    m_follow->setObjectName(QStringLiteral("dmiFollowCursor"));
    m_follow->setToolTip(tr("Show the panel as it was at the row selected in any tab, or at a replay window's "
                            "cursor: each loco's latest @dmi at or before that moment. Off: the live panel."));
    auto *save = new QPushButton(tr("Save image…"), this);
    // Session 92: "Fields" opens a side panel with the displayed frame's
    // decoded @dmi fields. It replaces "Field sources…", whose notes on
    // which field feeds which region now sit under that table.
    m_fieldsBtn = new QPushButton(tr("Fields \u25B8"), this);
    m_fieldsBtn->setObjectName(QStringLiteral("dmiFieldsButton"));
    m_fieldsBtn->setCheckable(true);
    m_fieldsBtn->setToolTip(tr("Show the decoded fields of the @dmi frame the panel is drawing, beside it"));
    m_fields = new QTableWidget(0, 2, this);
    m_fields->setObjectName(QStringLiteral("dmiFieldsTable"));
    m_fields->setHorizontalHeaderLabels({ tr("Field"), tr("Value") });
    m_fields->verticalHeader()->hide();
    m_fields->horizontalHeader()->setStretchLastSection(true);
    m_fields->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fields->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fields->setAlternatingRowColors(true);
    m_fields->setWordWrap(false);
    auto *sources = new QLabel(tr("<b>Where each region comes from</b><br>") + dmiAssumptions().join(QStringLiteral("<br>")), this);
    sources->setWordWrap(true);
    sources->setStyleSheet(UiColor::mutedStyle());
    m_fieldsPane = new QWidget(this);
    m_fieldsPane->setObjectName(QStringLiteral("dmiFieldsPane"));
    auto *paneLay = new QVBoxLayout(m_fieldsPane);
    paneLay->setContentsMargins(0, 0, 0, 0);
    m_fieldsTitle = new QLabel(m_fieldsPane);
    paneLay->addWidget(m_fieldsTitle);
    paneLay->addWidget(m_fields, 1);
    paneLay->addWidget(sources);
    m_fieldsPane->setMinimumWidth(300);
    m_fieldsPane->hide();
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("dmiStatus"));
    m_status->setStyleSheet(UiColor::mutedStyle());

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Loco / Ctrl:"), this));
    top->addWidget(m_source);
    top->addWidget(m_follow);
    top->addWidget(annexure);
    top->addStretch(1);
    top->addWidget(m_fieldsBtn);
    top->addWidget(save);
    auto *root = new QVBoxLayout(this);
    root->addLayout(top);
    auto *body = new QHBoxLayout;
    body->addWidget(m_view, 1);
    body->addWidget(m_fieldsPane);
    root->addLayout(body, 1);
    root->addWidget(m_status);

    connect(annexure, &QCheckBox::toggled, this, [this](bool on) {
        m_view->setAnnexureColours(on);
        QSettings s(Settings::iniPath(), QSettings::IniFormat);
        s.setValue(QStringLiteral("dmi/annexureColours"), on);
    });
    connect(m_follow, &QCheckBox::toggled, this, [this](bool on) { setFollowCursor(on); });
    connect(m_source, &QComboBox::currentTextChanged, this, [this](const QString &) {
        render();
        refreshStatus();
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save DMI image"), QStringLiteral("dmi.png"), tr("PNG (*.png)"));
        if (!path.isEmpty()) saveImage(path);
    });
    connect(m_fieldsBtn, &QPushButton::toggled, this, [this](bool on) { setFieldsVisible(on); });
    // H / I alternate every 2 s (B4.6.7 (e)); staleness is judged each tick.
    auto *tick = new QTimer(this);
    tick->setInterval(2000);
    connect(tick, &QTimer::timeout, this, [this]() {
        m_view->advanceMessages();
        refreshStatus();
    });
    tick->start();

    if (m_dispatcher) {
        connect(m_dispatcher, &MessageDispatcher::entryAppended, this, [this](const QString &key, const LogEntryPtr &e) {
            if (!e.isNull() && e->text.startsWith(QLatin1String("@dmi_"))) observeLine(key, e->text);
        });
    }
    connect(DmiTimeTravel::instance(), &DmiTimeTravel::momentChanged, this, [this](const DmiMoment &m) {
        if (m_following) showMoment(m);
    });
    // Remembered, like the colours: an operator reviewing an incident keeps
    // reopening the window in the same mode.
    if (settings.value(QStringLiteral("dmi/followCursor"), false).toBool()) m_follow->setChecked(true);
    else refreshStatus();
}

DmiWindow *DmiWindow::showFollowing(QWidget *owner, MessageDispatcher *dispatcher)
{
    // One panel per owner: a second window for every right-click would bury
    // the first. A replay's own DMI windows are its children, not the owner's.
    DmiWindow *w = nullptr;
    if (owner) {
        for (DmiWindow *c : owner->findChildren<DmiWindow *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (c->isVisible()) { w = c; break; }
        }
    }
    if (!w) w = new DmiWindow(dispatcher, owner);
    w->setFollowCursor(true);
    w->show();
    w->raise();
    w->activateWindow();
    return w;
}

void DmiWindow::addSource(const QString &key)
{
    if (!key.isEmpty() && m_source->findText(key) < 0) m_source->addItem(key);
}

void DmiWindow::observeLine(const QString &sourceKey, const QString &line)
{
    const CaptureLine cap = CaptureDecoder::parseLine(line);
    if (!cap.valid || cap.type != CapType::Dmi) return;
    const QString key = cap.key().isEmpty() ? sourceKey : cap.key();
    m_lastLine.insert(key, line);
    m_lastMs.insert(key, QDateTime::currentMSecsSinceEpoch());
    addSource(key);
    // Following, a live frame is remembered for when following stops but
    // does not replace the moment on screen.
    if (!m_following && m_source->currentText() == key) {
        render();
        refreshStatus();
    }
}

QString DmiWindow::selectedSource() const { return m_source->currentText(); }

void DmiWindow::setSelectedSource(const QString &key)
{
    addSource(key);
    m_source->setCurrentText(key);
}

bool DmiWindow::saveImage(const QString &path) const { return m_view->grab().save(path, "PNG"); }

QString DmiWindow::statusText() const { return m_status->text(); }

void DmiWindow::setFollowCursor(bool on)
{
    if (m_follow->isChecked() != on) {
        m_follow->setChecked(on);          // comes back here through toggled
        return;
    }
    if (m_following == on) return;
    m_following = on;
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("dmi/followCursor"), on);
    auto *tt = DmiTimeTravel::instance();
    if (on) {
        m_moment = tt->last();
        tt->follow(this);                  // may deliver the last pointed-at moment
        showMoment(tt->last());
    } else {
        tt->unfollow(this);
        setWindowTitle(tr("DMI (LP-OCIP)"));
        render();
        refreshStatus();
    }
}

void DmiWindow::showMoment(const DmiMoment &moment)
{
    m_moment = moment;
    for (const QString &k : moment.keys()) addSource(k);
    // Which loco: the one the operator pointed at if it has a frame; else
    // stay on the chosen one if it has; else the first that has.
    QString want = m_source->currentText();
    if (!moment.preferredKey.isEmpty() && moment.frameFor(moment.preferredKey)) want = moment.preferredKey;
    else if (!moment.frameFor(want) && !moment.frames.isEmpty()) want = moment.frames.first().key;
    if (!want.isEmpty() && want != m_source->currentText()) {
        m_source->setCurrentText(want);    // renders through currentTextChanged
    } else {
        render();
        refreshStatus();
    }
}

void DmiWindow::render()
{
    const QString key = m_source->currentText();
    if (!m_following) {
        m_view->setEmptyText(QString());
        m_shown = CaptureDecoder::parseLine(m_lastLine.value(key));
        m_view->setState(dmiStateFromCapture(m_shown));
        refreshFields();
        return;
    }
    const DmiFrameAt *f = m_moment.valid ? m_moment.frameFor(key) : nullptr;
    if (!m_moment.valid) m_view->setEmptyText(tr("Pick a row in any tab, or move a replay cursor"));
    else if (!f)         m_view->setEmptyText(tr("No @dmi at or before this moment"));
    m_shown = f ? f->cap : CaptureLine();
    m_view->setState(f ? dmiStateFromCapture(f->cap) : DmiState());
    refreshFields();
    m_view->setStale(f && m_moment.atMs - f->frameMs > kDmiStaleMs);
    setWindowTitle(m_moment.valid
                       ? tr("DMI (LP-OCIP) \u2014 at %1").arg(QDateTime::fromMSecsSinceEpoch(m_moment.atMs).toString(QStringLiteral("HH:mm:ss")))
                       : tr("DMI (LP-OCIP) \u2014 following cursor"));
}

void DmiWindow::refreshStatus()
{
    const QString key = m_source->currentText();
    if (m_following) {
        m_status->setStyleSheet(UiColor::accentStyle());
        if (!m_moment.valid) {
            m_status->setText(tr("\u23F1 Following the cursor: select a row in any tab, or move a replay window's cursor."));
            return;
        }
        const auto clock = [](qint64 ms) {
            return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss.zzz"));
        };
        const QString where = tr("\u23F1 At %1 \u00B7 %2").arg(clock(m_moment.atMs), m_moment.origin);
        const DmiFrameAt *f = m_moment.frameFor(key);
        if (!f) {
            m_status->setText(tr("%1 \u00B7 no @dmi from %2 in the %3 min before this moment")
                                  .arg(where, key.isEmpty() ? tr("any loco") : key)
                                  .arg(kDmiLookbackMs / 60000));
            return;
        }
        const qint64 gap = m_moment.atMs - f->frameMs;
        QString text = tr("%1 \u00B7 %2's @dmi of %3, %4 s before")
                           .arg(where, key, clock(f->frameMs), QString::number(gap / 1000.0, 'f', 1));
        if (gap > kDmiStaleMs) text += tr(" \u2014 stale, muted");
        m_status->setText(text);
        return;
    }
    m_status->setStyleSheet(UiColor::mutedStyle());
    if (key.isEmpty() || !m_lastMs.contains(key)) {
        m_view->setStale(false);
        m_status->setText(tr("No @dmi received yet. The panel is drawn from the loco's DMI frames."));
        return;
    }
    const qint64 age = QDateTime::currentMSecsSinceEpoch() - m_lastMs.value(key);
    m_view->setStale(age > 3000);
    m_status->setText(tr("%1 · last @dmi %2 s ago · RDSO/SPN/196/2020 Annexure-B Amdt-3 layout")
                          .arg(key).arg(age / 1000));
}

// ---- Session 92: the decoded fields beside the panel ---------------------------------------------

void DmiWindow::setFieldsVisible(bool on)
{
    if (m_fieldsBtn->isChecked() != on) { m_fieldsBtn->setChecked(on); return; }   // back via toggled
    m_fieldsBtn->setText(on ? tr("Fields \u25C2") : tr("Fields \u25B8"));
    m_fieldsPane->setVisible(on);
    // Widen the window by the pane rather than squeezing the panel.
    if (on && !isMaximized() && !isFullScreen()) resize(width() + m_fieldsPane->minimumWidth(), height());
    refreshFields();
}

bool DmiWindow::fieldsVisible() const { return m_fieldsPane->isVisible(); }

void DmiWindow::refreshFields()
{
    if (!m_fieldsPane->isVisible()) return;             // cost nothing while closed
    m_fields->setRowCount(0);
    if (!m_shown.valid || m_shown.type != CapType::Dmi) {
        m_fieldsTitle->setText(tr("No @dmi frame on the panel."));
        return;
    }
    m_fieldsTitle->setText(tr("<b>@dmi %1</b> \u00B7 %2").arg(m_shown.key(),
                                                               m_shown.rtc.isValid() ? m_shown.rtc.toString(QStringLiteral("dd-MMM-yyyy HH:mm:ss"))
                                                                                     : tr("no RTC")));
    const QVector<FieldRow> rows = CaptureDecoder::describe(m_shown, nullptr, 0, nullptr);
    m_fields->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        m_fields->setItem(i, 0, new QTableWidgetItem(rows[i].field));
        m_fields->setItem(i, 1, new QTableWidgetItem(rows[i].value));
    }
    m_fields->resizeColumnToContents(0);
}

QStringList DmiWindow::fieldNames() const
{
    QStringList out;
    for (int i = 0; i < m_fields->rowCount(); ++i) out << m_fields->item(i, 0)->text();
    return out;
}

QString DmiWindow::fieldValue(const QString &name) const
{
    for (int i = 0; i < m_fields->rowCount(); ++i)
        if (m_fields->item(i, 0)->text() == name) return m_fields->item(i, 1)->text();
    return QString();
}

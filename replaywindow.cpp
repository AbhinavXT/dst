#include "replaywindow.h"
#include "dmipanel.h"

#include <QStatusBar>
#include "uistyle.h"
#include "windowgeometry.h"

#include <QCloseEvent>
#include <algorithm>

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPainter>
#include <QMenu>
#include <QAction>
#include <QPushButton>
#include <QSet>
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

namespace {
const CapType kTabOrder[] = {
    CapType::Rfid, CapType::SLRP, CapType::AAP, CapType::ARP, CapType::ArpRecv,
    CapType::LSRP,
    CapType::Dmi,
    CapType::NmsHlth, CapType::NmsFault, CapType::NmsRssi,
    CapType::Dip1, CapType::Dip2, CapType::Dop1, CapType::Dop2,
    CapType::CcSys, CapType::DlSys, CapType::Aep, CapType::Linfo,
    CapType::AuthKeys,                      // key-set lifecycle (from LCU)
    CapType::Random,                        // session nonce pair
    CapType::UBA,                           // target + braking curve
    CapType::Speed,                         // tachometer pulses
    CapType::AnalogTop, CapType::AnalogBottom   // analog (pressure) inputs
};
const int kTabOrderCount = int(sizeof(kTabOrder) / sizeof(kTabOrder[0]));

QColor okColor()   { return QColor(0x46, 0xC0, 0x7A); }
QColor failColor() { return QColor(0xE0, 0x5A, 0x52); }
QColor rfidColor() { return QColor(0x4E, 0x9A, 0xE6); }
QColor axisColor() { return QColor(0x88, 0x90, 0x9A); }
QColor curColor()  { return QColor(0xD9, 0xA0, 0x40); }

// MSB-first bit read for arp/lsrp loco-location extraction.
quint32 getbits(const QByteArray &b, int start, int n) {
    quint32 v = 0;
    const int lim = b.size() * 8;
    for (int i = start; i < start + n; ++i) {
        const int bit = (i < lim) ? ((quint8(b[i >> 3]) >> (7 - (i & 7))) & 1) : 0;
        v = (v << 1) | quint32(bit);
    }
    return v;
}
// ABS_LOCO_LOC for arp/lsrp: bit 80 + 4+7+17+20+3 = bit 131, width 23.
// 0 (uninitialised / pre-localisation) and 2^23-1 (0x7FFFFF, the 23-bit
// "location unknown" sentinel) are NOT real positions: treat them as no fix
// so such packets never place/move the loco or affect the track range.
static const quint32 kLocoLocUnknown = (1u << 23) - 1u;   // 8388607
// Deliberately not ArpRecv. That packet carries a position too, but it is
// ANOTHER train's, and the loco box on the track view is this key's own loco.
// Plotting neighbouring trains is a feature worth having and is not this one:
// done here by accident it would make one loco appear to jump between two
// positions. The bit offset would also be wrong — an arprecv body starts at
// bit 64, not 80, so the constant below does not apply to it.
bool locoLocation(const CaptureLine &c, double &out) {
    if ((c.type == CapType::ARP || c.type == CapType::LSRP) && c.bytes.size() >= 20) {
        const quint32 v = getbits(c.bytes, 131, 23);
        if (v == 0u || v == kLocoLocUnknown) { return false; }
        out = double(v);
        return true;
    }
    return false;
}

// An RFID read only carries a usable TRACK POSITION when its 23-bit absolute
// location is a real fix. The same two values that are "no fix" for the loco's
// own location are also "no fix" here: 0 (uninitialised / null balise read,
// e.g. an all-zero "01 00 00 .." keep-alive frame) and 2^23-1 = 0x7FFFFF (the
// "location unknown" sentinel). Such reads decode as a valid frame (so they
// still show in the inspector / event log) but they must NOT establish or move
// a key's spatial origin, otherwise a single null balise sets lastPos = 0 and
// poisons the per-key carry-forward, dragging the whole track axis back to 0.
static const qint64 kRfidLocUnknown = (1LL << 23) - 1;   // 8388607
inline bool rfidHasFix(const RfidInfo &t) {
    return t.valid && t.absLoc > 0 && t.absLoc != kRfidLocUnknown;
}

QString modeName(int v) {
    static const char *m[] = {"?","Stand_By","Staff_Responsible","Limited_Supervision",
        "Full_Supervision","Override","On_Sight","Trip","Post_Trip","Reverse",
        "Shunting","Non_Leading","System_Failure","Isolation"};
    return (v >= 0 && v < (int)(sizeof(m)/sizeof(m[0]))) ? QString::fromLatin1(m[v]) : QString::number(v);
}
QString emergName(int v) {
    static const char *m[] = {"No Emergency","Unusual Stoppage","SoS","Roll Back",
        "Head-On","Rear-End","Parting SoS","Spare"};
    return (v >= 0 && v < 8) ? QString::fromLatin1(m[v]) : QString::number(v);
}
QString frameToClock(int s) {
    if (s < 0) return QString();
    return QStringLiteral("%1:%2:%3").arg(s / 3600, 2, 10, QChar('0'))
            .arg((s % 3600) / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}
} // namespace

// ======================= TimelineWidget =======================
TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(210);
    setMouseTracking(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
}

void TimelineWidget::setData(const QVector<ReplayRec> *recs) { m_recs = recs; recomputeRange(); update(); }
void TimelineWidget::setEvents(const QVector<ReplayEvent> *ev) { m_events = ev; update(); }
void TimelineWidget::setLocoStates(const QVector<LocoVizState> &states) { m_locos = states; update(); }
void TimelineWidget::setProfile(const SlrpProfile &p) { m_profile = p; updateMinHeight(); update(); }

void TimelineWidget::updateMinHeight()
{
    int h = 210;
    if (m_profile.valid) { h = 312; }      // header + look-ahead lane + track + axis
    else if (m_hasPlot)  { h = 260; }
    setMinimumHeight(h);
    updateGeometry();
}

// Parse a leading signed decimal out of a decoded field value ("42 °C" -> 42,
// "0.1 (0.1 W)" -> 0.1, "1 (Red)" -> 1). Rejects hex ("0x..") and non-numeric
// strings ("PASS", "a=1 b=0"). Returns false if there is no number to plot.
static bool leadingNumber(const QString &s, double &out)
{
    const QString t = s.trimmed();
    int i = 0; const int n = t.size();
    if (i < n && (t[i] == '-' || t[i] == '+')) { ++i; }
    const int ds = i;
    while (i < n && t[i].isDigit()) { ++i; }
    if (i < n && t[i] == '.') { ++i; while (i < n && t[i].isDigit()) { ++i; } }
    if (i == ds) { return false; }                 // no digits
    if (i < n && (t[i] == 'x' || t[i] == 'X')) { return false; }  // hex
    bool ok = false; out = t.left(i).toDouble(&ok); return ok;
}

// "4 (Full_Supervision)" -> "Full_Supervision";  "45 km/h" -> "45 km/h".
static QString prettyParen(const QString &s)
{
    const int a = s.indexOf('(');
    const int b = s.lastIndexOf(')');
    if (a >= 0 && b > a) { return s.mid(a + 1, b - a - 1).trimmed(); }
    return s.trimmed();
}

void TimelineWidget::setPlotSeries(const QVector<QPair<int,double>> &pts, const QString &label)
{
    m_plot = pts; m_plotLabel = label; m_hasPlot = !pts.isEmpty();
    if (m_hasPlot) {
        double lo = 1e300, hi = -1e300;
        for (const auto &pt : pts) { lo = qMin(lo, pt.second); hi = qMax(hi, pt.second); }
        if (hi <= lo) { hi = lo + 1.0; }
        m_plotLo = lo; m_plotHi = hi;
    }
    updateMinHeight(); update();
}

void TimelineWidget::clearPlot()
{
    m_plot.clear(); m_plotLabel.clear(); m_hasPlot = false;
    updateMinHeight(); update();
}
void TimelineWidget::setMode(Mode m) { m_mode = m; recomputeRange(); update(); }

void TimelineWidget::setCursorIndex(int idx)
{
    if (!m_recs || m_recs->isEmpty()) { return; }
    m_cursor = qBound(0, idx, m_recs->size() - 1);
    update();
}

double TimelineWidget::valueOf(int i) const
{
    if (!m_recs || i < 0 || i >= m_recs->size()) { return 0.0; }
    return (m_mode == Spatial) ? m_recs->at(i).pos : double(m_recs->at(i).tMs);
}

void TimelineWidget::recomputeRange()
{
    m_min = 0.0; m_max = 1.0;
    if (!m_recs || m_recs->isEmpty()) { return; }
    double lo =  1e18, hi = -1e18;
    for (int i = 0; i < m_recs->size(); ++i) {
        const double v = valueOf(i);
        // In Spatial mode a track location is always > 0; a 0 means "no fix"
        // (provisional / pre-localisation) and must never define the axis floor.
        // This is a safety net: even if a stray zero-position record slips
        // through, it can no longer drag the range to a negative start.
        if (m_mode == Spatial && v <= 0.0) { continue; }
        lo = qMin(lo, v); hi = qMax(hi, v);
    }
    if (lo > hi) { lo = 0.0; hi = 1.0; }      // nothing positioned yet
    if (hi <= lo) { hi = lo + 1.0; }
    const double pad = (hi - lo) * 0.04;
    m_min = lo - pad; m_max = hi + pad;
}

int TimelineWidget::valueToX(double v) const
{
    const int w = width() - 2 * m_margin;
    return m_margin + int((v - m_min) / (m_max - m_min) * w);
}
double TimelineWidget::xToValue(int px) const
{
    const int w = width() - 2 * m_margin;
    if (w <= 0) { return m_min; }
    return m_min + double(px - m_margin) / double(w) * (m_max - m_min);
}
int TimelineWidget::nearestIndex(double v) const
{
    if (!m_recs || m_recs->isEmpty()) { return 0; }
    int best = 0; double bd = 1e18;
    for (int i = 0; i < m_recs->size(); ++i) {
        const double d = qAbs(valueOf(i) - v);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(0x1E, 0x22, 0x28));
    if (!m_recs || m_recs->isEmpty()) {
        p.setPen(axisColor());
        p.drawText(rect(), Qt::AlignCenter, tr("no records"));
        return;
    }

    const int W      = width();
    const int axisY  = height() - 26;
    const int trackY = axisY - 52;             // track centre-line
    const int headerB = 30;

    QFont base = p.font();
    QFont small = base; small.setPointSizeF(base.pointSizeF() - 1.0);

    // ---- header state strip (active loco identity + state) -------------
    {
        LocoVizState L;                       // the active source drives the strip
        for (const LocoVizState &s : m_locos) { if (s.active) { L = s; break; } }
        p.setFont(small);
        int x = m_margin - 4;
        const int y1 = 14, y2 = 27;
        auto seg = [&](int y, const QString &t, const QColor &c, int &cx) {
            p.setPen(c); p.drawText(cx, y, t);
            cx += p.fontMetrics().horizontalAdvance(t) + 16;
        };
        const QColor lab(0x88, 0x90, 0x9A), val(0xC8, 0xD0, 0xD8);
        const double km = L.absLocM / 1000.0;
        int cx = x;
        seg(y1, L.keyLabel.isEmpty() ? tr("LOCO --") : tr("SRC %1").arg(L.keyLabel),
            L.valid ? QColor(0xE0, 0xE6, 0xEC) : lab, cx);
        if (L.valid && !L.locoId.isEmpty()) { seg(y1, tr("id %1").arg(L.locoId), lab, cx); }
        seg(y1, tr("%1 m  (%2 km)").arg(qint64(L.absLocM)).arg(km, 0, 'f', 3), val, cx);
        if (L.valid) {
            seg(y1, tr("%1").arg(L.speed.isEmpty() ? QStringLiteral("-- km/h") : L.speed),
                QColor(0x6F, 0xC2, 0x76), cx);
            seg(y1, tr("[%1]").arg(L.srcLabel), lab, cx);
        }
        cx = x;
        if (L.valid) {
            const QColor emCol = (L.emSev >= 2) ? failColor()
                               : (L.emSev == 1) ? QColor(0xD9, 0xA0, 0x40) : okColor();
            seg(y2, tr("mode %1").arg(L.mode.isEmpty() ? QStringLiteral("--") : L.mode), val, cx);
            seg(y2, L.emergency.isEmpty() ? tr("emg --") : L.emergency, emCol, cx);
            if (!L.trainLen.isEmpty()) seg(y2, tr("len %1").arg(L.trainLen), lab, cx);
            if (!L.lastTag.isEmpty())  seg(y2, tr("tag %1").arg(L.lastTag), lab, cx);
            seg(y2, (L.dir == 1) ? tr("\u25B6 nominal")
                  : (L.dir == 2) ? tr("\u25C0 reverse") : tr("dir ?"), lab, cx);
        }
        p.setFont(base);
    }

    // axis title (mode)
    p.setPen(axisColor());
    p.drawText(W - m_margin - 90, 14, m_mode == Spatial ? tr("Track position") : tr("Time"));

    // ---- field-over-time plot lane (shown when no SLRP profile lane) ---
    if (m_hasPlot && !m_plot.isEmpty() && !m_profile.valid) {
        const int padTop = headerB + 6;
        const int padBot = trackY - 42;
        if (padBot > padTop + 16) {
            auto vy = [&](double val) -> int {
                return padBot - int((val - m_plotLo) / (m_plotHi - m_plotLo) * (padBot - padTop));
            };
            p.setPen(QColor(0x33, 0x3B, 0x44));
            p.drawLine(m_margin, padBot, W - m_margin, padBot);
            const QColor plotCol(0x6F, 0xC2, 0x76);
            p.setPen(QPen(plotCol, 2));
            QPoint prev; bool first = true;
            for (const auto &pt : m_plot) {
                if (pt.first < 0 || pt.first >= m_recs->size()) { continue; }
                const QPoint cur(valueToX(valueOf(pt.first)), vy(pt.second));
                if (!first) { p.drawLine(prev, cur); }
                p.setBrush(plotCol); p.setPen(Qt::NoPen);
                p.drawEllipse(cur, 2, 2);
                p.setPen(QPen(plotCol, 2));
                prev = cur; first = false;
            }
            p.setFont(small);
            p.setPen(plotCol);
            p.drawText(m_margin, padTop - 2, m_plotLabel);
            p.drawText(W - m_margin - 70, padTop + 8, QString::number(m_plotHi, 'g', 5));
            p.drawText(W - m_margin - 70, padBot,     QString::number(m_plotLo, 'g', 5));
            p.setFont(base);
        }
    }

    // ---- SLRP look-ahead profile: stacked colour lanes -----------------
    //  Rendered in the Kavach-4.0 "performance" style: one horizontal band
    //  per overlay, laid along the profile's relative-distance axis and
    //  colour-coded so each overlay is glanceable at a glance:
    //     MA   green  |  SSP  red    |  GRAD  amber
    //     TAGS blue   |  TSR  orange
    //  (TO has no field in SlrpProfile, so it is not drawn here.)
    if (m_profile.valid) {
        const SlrpProfile &P = m_profile;
        const int lx0 = m_margin, lx1 = W - m_margin;
        const int laneTop = headerB + 16;
        const int laneBot = trackY - 46;
        if (laneBot > laneTop + 30 && lx1 > lx0 + 10) {
            // --- compose ref-relative positions per Annexure-C --------------
            //  All overlays are placed on ONE axis: d = "look-ahead distance
            //  from the ref RFID", positive in the direction of travel. This is
            //  exactly the convention the decoder/inspector use for abs:
            //    tag_abs = refAbs + travel*tagacc      (travel = +1 nom / -1 rev)
            //    seg_abs = blockAbs + travel*relpos
            //  Inverting that to the d axis (d = travel*(abs - refAbs)):
            //    * TAGS  d = tagacc                       (per-hop running sum)
            //    * SSP/GRAD .d = per-segment LENGTH    -> chain from blockOrigin
            //    * TSR/COND .d = start dist, .len = length (from blockOrigin)
            //  Block origin in d-space is travel*(blockAbs - refAbs). With
            //  blockAbs = refAbs - dps (nominal) / refAbs + dps (reverse) and
            //  travel = +1 / -1, BOTH cases reduce to the same value: -dps.
            //  (The earlier reverse branch used +dps, which mirrored MA/SSP/
            //  GRAD/TSR/COND across the ref whenever DIST_PKT_START != 0.)
            //
            //  CORRECTED MODEL (single origin): the whole look-ahead profile is
            //  anchored on ONE point -- the START SIGNAL the MA/profile is issued
            //  from. In d-space (look-ahead from that signal, +ve ahead) the
            //  block overlays (MA/SSP/GRAD/TSR/COND) AND the tag-linking list all
            //  start at d = 0. DIST_PKT_START is the travel-signed gap from
            //  LAST_REF_RFID to the start signal, so the ref RFID itself sits at
            //  d = -DIST_PKT_START. (When DIST_PKT_START == 0 the ref and the
            //  start signal coincide, which is why earlier frames looked right.)
            const bool reverse = (P.pktDir == 2);
            const int blockOrigin = 0;                  // start signal = origin
            const int refMarkerD  = -P.distPktStart;    // where LAST_REF_RFID sits
            struct Seg { int a; int b; QString lbl; };
            QVector<Seg> sspSeg, gradSeg, tsrSeg;
            struct TagP { int pos; QString lbl; };
            QVector<TagP> tagPos;
            { int acc = blockOrigin;
              for (const auto &s : P.ssp) {
                  sspSeg.push_back({ acc, acc + s.d, QString::number(s.a) });
                  acc += s.d; } }
            { int acc = blockOrigin;
              for (const auto &g : P.grad) {
                  gradSeg.push_back({ acc, acc + g.d,
                      QStringLiteral("g%1%2").arg(g.value)
                          .arg(g.uphill ? QChar(0x2191) : QChar(0x2193)) });
                  acc += g.d; } }
            { int acc = blockOrigin;  // tags accumulate ahead of the start signal
              for (const auto &t : P.tags) {
                  acc += t.d;
                  tagPos.push_back({ acc, QStringLiteral("\u25CF%1").arg(t.tag) }); } }
            for (const auto &z : P.tsr) {
                tsrSeg.push_back({ blockOrigin + z.d, blockOrigin + z.d + z.len,
                    tr("TSR%1 %2").arg(z.id).arg(z.a) }); }
            QVector<Seg> condSeg;
            for (const auto &z : P.cond) {
                condSeg.push_back({ blockOrigin + z.sd, blockOrigin + z.sd + z.len,
                    CaptureDecoder::tcTypeName(z.type) }); }
            const int maA = blockOrigin;
            const int maB = P.haveMA ? blockOrigin + P.maWrtSig : blockOrigin;   // SSP ends here too

            // distance extent across every composed position
            int dMin = 0, dMax = 1;
            auto ext = [&](int d){ if (d < dMin) { dMin = d; } if (d > dMax) { dMax = d; } };
            ext(blockOrigin);
            ext(refMarkerD);                          // keep the ref RFID tick in view
            if (P.haveMA) { ext(maB); if (P.reqShorten) { ext(P.newMA); } }
            for (const auto &s : sspSeg)  { ext(s.a); ext(s.b); }
            for (const auto &g : gradSeg) { ext(g.a); ext(g.b); }
            for (const auto &z : tsrSeg)  { ext(z.a); ext(z.b); }
            for (const auto &z : condSeg) { ext(z.a); ext(z.b); }
            for (const auto &t : tagPos)  { ext(t.pos); }
            if (dMax <= dMin) { dMax = dMin + 1; }
            const int dpad = qMax(1, (dMax - dMin) / 25);
            dMin -= dpad; dMax += dpad;
            // Orient the look-ahead axis to match the spatial track below:
            // the composed positions are "distance in the travel/look-ahead
            // direction from the ref" (always increasing ahead). On the track
            // axis higher absolute km is rightward, so for a reverse run the
            // look-ahead runs toward *lower* km and must be drawn right->left.
            // Flipping here keeps MA/SSP/GRAD/TAGS/TSR/COND all consistent with
            // the RFID balises on the rail, instead of mirror-imaged.
            auto dx = [&](int d) -> int {
                double f = double(d - dMin) / double(dMax - dMin);
                if (reverse) { f = 1.0 - f; }
                const int x = lx0 + int(f * (lx1 - lx0));
                return qBound(lx0, x, lx1); };

            // canonical Kavach-4.0 lane palette
            const QColor cMA  (127, 255,   0);   // green  - movement authority
            const QColor cSSP (255,  99, 132);   // red    - static speed profile
            const QColor cGRAD(255, 206,  86);   // amber  - gradient
            const QColor cTLI ( 54, 162, 235);   // blue   - tag linking
            const QColor cTSR (245, 124,   0);   // orange - temporary speed restriction
            const QColor cCOND(171, 110, 230);   // violet - track condition
            const QColor txt  (0xEC, 0xEF, 0xF2);

            p.setFont(small);
            p.setPen(QColor(0x90, 0x98, 0xA2));
            p.drawText(lx0, laneTop + 2,
                       tr("Look-ahead profile  (m ahead of start signal \u00B7 SLRP ref %1)")
                           .arg(P.refRfid));

            const bool hMA  = P.haveMA;
            const bool hSSP = !sspSeg.isEmpty();
            const bool hGRD = !gradSeg.isEmpty();
            const bool hTLI = !tagPos.isEmpty();
            const bool hTSR = !tsrSeg.isEmpty();
            const bool hCND = !condSeg.isEmpty();
            int nb = (hMA?1:0) + (hSSP?1:0) + (hGRD?1:0) + (hTLI?1:0) + (hTSR?1:0) + (hCND?1:0);
            if (nb < 1) { nb = 1; }

            const int bandsTop = laneTop + 12;
            const int gap = 3;
            const int bandH = qBound(13, (laneBot - bandsTop - (nb - 1) * gap) / nb, 22);

            // origin marker = the START SIGNAL (d = 0): where MA / SSP / GRAD /
            // TSR / COND and the tag-linking list all begin. The LAST_REF_RFID
            // sits DIST_PKT_START metres back from it, at d = refMarkerD, and is
            // drawn as a separate tick (it usually coincides with one tag dot).
            {
                const int xs = dx(blockOrigin);          // start signal
                p.setPen(QPen(QColor(0xC8, 0xD0, 0xD8), 1, Qt::DashLine));
                p.drawLine(xs, bandsTop - 2, xs, laneBot);
                p.setPen(QColor(0xB8, 0xC0, 0xC8));
                // the signal sits on the right edge once the axis is flipped
                // for a reverse run, so keep its label inside the lane
                const QString sigLbl = tr("sig");
                const int sw = p.fontMetrics().horizontalAdvance(sigLbl);
                p.drawText(reverse ? (xs - sw - 2) : (xs + 2), laneBot + 2, sigLbl);

                // LAST_REF_RFID tick, only when it doesn't land on the signal
                if (refMarkerD != blockOrigin) {
                    const int xr = dx(refMarkerD);
                    p.setPen(QPen(QColor(0x86, 0x90, 0x9A), 1, Qt::DotLine));
                    p.drawLine(xr, bandsTop - 2, xr, laneBot);
                    p.setPen(QColor(0x98, 0xA2, 0xAC));
                    const QString refLbl = tr("ref %1").arg(P.refRfid);
                    const int rw = p.fontMetrics().horizontalAdvance(refLbl);
                    p.drawText(reverse ? (xr + 2) : (xr - rw - 2), laneBot + 2, refLbl);
                }
            }

            auto laneName = [&](int by, const QString &nm, const QColor &c){
                p.setPen(c);
                p.drawText(2, by + (bandH + 8) / 2, nm);
            };
            auto seg = [&](int da, int db, int by, const QColor &col, const QString &label){
                int xa = dx(da), xb = dx(db);
                if (xb < xa) { const int t = xa; xa = xb; xb = t; }
                const QRect r(xa, by, qMax(1, xb - xa), bandH);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(col.red(), col.green(), col.blue(), 70));
                p.drawRect(r);
                p.setPen(QPen(col, 1)); p.setBrush(Qt::NoBrush);
                p.drawRect(r);
                if (!label.isEmpty() && (xb - xa) > 18) {
                    p.setPen(txt);
                    p.drawText(xa + 3, by + bandH - 4, label);
                }
            };

            int y = bandsTop;

            if (hMA) {                               // green authority bar
                laneName(y, tr("MA"), cMA);
                QString lbl = tr("MA %1 m").arg(P.maWrtSig);
                if (P.authType == 1 && P.authSpeed >= 0) { lbl += tr("  %1 km/h").arg(P.authSpeed); }
                seg(maA, maB, y, cMA, lbl);
                if (P.reqShorten) {
                    const int xn = dx(P.newMA);
                    p.setPen(QPen(cMA, 1, Qt::DotLine));
                    p.drawLine(xn, y, xn, y + bandH);
                }
                y += bandH + gap;
            }
            if (hSSP) {                              // red speed segments
                laneName(y, tr("SSP"), cSSP);
                for (const auto &s : sspSeg) { seg(s.a, s.b, y, cSSP, s.lbl); }
                y += bandH + gap;
            }
            if (hGRD) {                              // amber gradient segments
                laneName(y, tr("GRAD"), cGRAD);
                for (const auto &g : gradSeg) { seg(g.a, g.b, y, cGRAD, g.lbl); }
                y += bandH + gap;
            }
            if (hTLI) {                              // blue tag-linking segments
                laneName(y, tr("TAGS"), cTLI);
                for (int i = 0; i < tagPos.size(); ++i) {
                    const int da = tagPos[i].pos;
                    const int db = (i + 1 < tagPos.size()) ? tagPos[i + 1].pos : dMax;
                    seg(da, db, y, cTLI, QString());     // band only; label point-anchored below
                }
                // a tag label marks a *point* (the tag), so anchor it at the
                // tag's own position and place the text on the in-band side so
                // the flip for reverse runs doesn't push it onto the neighbour.
                p.setPen(txt);
                for (int i = 0; i < tagPos.size(); ++i) {
                    const int xt = dx(tagPos[i].pos);
                    const QString &lab = tagPos[i].lbl;
                    const int tw = p.fontMetrics().horizontalAdvance(lab);
                    p.drawText(reverse ? (xt - tw - 3) : (xt + 3), y + bandH - 4, lab);
                }
                y += bandH + gap;
            }
            if (hTSR) {                              // orange TSR zones
                laneName(y, tr("TSR"), cTSR);
                for (const auto &z : tsrSeg) { seg(z.a, z.b, y, cTSR, z.lbl); }
                y += bandH + gap;
            }
            if (hCND) {                              // violet track-condition zones
                laneName(y, tr("COND"), cCOND);
                for (const auto &z : condSeg) { seg(z.a, z.b, y, cCOND, z.lbl); }
                y += bandH + gap;
            }
            p.setFont(base);
        }
    }

    // ---- track band ----------------------------------------------------
    const int x0 = m_margin, x1 = W - m_margin;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x2A, 0x31, 0x3A));
    p.drawRect(QRect(x0, trackY - 5, x1 - x0, 10));         // rail bed
    p.setPen(QPen(QColor(0x6A, 0x74, 0x80), 2));
    p.drawLine(x0, trackY - 3, x1, trackY - 3);             // two rails
    p.drawLine(x0, trackY + 3, x1, trackY + 3);
    // sleepers (faint, one per record tick clamped to a sane spacing)
    p.setPen(QColor(0x3A, 0x42, 0x4C));
    {
        int lastX = -100;
        for (int i = 0; i < m_recs->size(); ++i) {
            const int x = valueToX(valueOf(i));
            if (x - lastX >= 6) { p.drawLine(x, trackY - 5, x, trackY + 5); lastX = x; }
        }
    }

    // ---- RFID balises (above the track) --------------------------------
    {
        // find the last RFID passed at/before the active loco position
        double locoPos = valueOf(m_cursor);
        for (const LocoVizState &s : m_locos) { if (s.active && s.valid) { locoPos = s.absLocM; break; } }
        int lastPassed = -1; double bestDelta = 1e18;
        for (int i = 0; i < m_recs->size(); ++i) {
            const ReplayRec &r = m_recs->at(i);
            if (!r.isRfid || !rfidHasFix(r.rfid)) { continue; }
            const double d = locoPos - r.pos;
            if (d >= 0 && d < bestDelta) { bestDelta = d; lastPassed = i; }
        }
        p.setFont(small);
        int lastLabelX = -1000;
        for (int i = 0; i < m_recs->size(); ++i) {
            const ReplayRec &r = m_recs->at(i);
            if (!r.isRfid || !rfidHasFix(r.rfid)) { continue; }   // no phantom marker for null balise
            const int x = valueToX(valueOf(i));
            const bool hot = (i == lastPassed);
            const QColor c = hot ? QColor(0x8F, 0xC2, 0xF0) : rfidColor();
            p.setPen(QPen(c, hot ? 2 : 1));
            p.drawLine(x, trackY - 6, x, trackY - 26);          // stem
            QPolygon dia;                                       // balise diamond
            dia << QPoint(x, trackY - 34) << QPoint(x - 5, trackY - 28)
                << QPoint(x, trackY - 22) << QPoint(x + 5, trackY - 28);
            p.setBrush(c); p.setPen(Qt::NoPen); p.drawPolygon(dia);
            if (x - lastLabelX >= 26) {                          // de-clutter labels
                p.setPen(c);
                p.drawText(x + 7, trackY - 28, QString::number(r.rfid.unique));
                lastLabelX = x;
            }
        }
        p.setFont(base);
    }

    // ---- event ribbon (short colour verticals straddling the rail) -----
    if (m_events) {
        auto sevColor = [](ReplayEvent::Sev s) -> QColor {
            switch (s) {
            case ReplayEvent::Error: return failColor();
            case ReplayEvent::Warn:  return QColor(0xE8, 0x9A, 0x3C);
            default:                 return QColor(0x6A, 0x90, 0xB0);
            }
        };
        for (int sev = 0; sev <= 2; ++sev) {
            for (const ReplayEvent &e : *m_events) {
                if (int(e.sev) != sev) { continue; }
                if (e.recIndex < 0 || e.recIndex >= m_recs->size()) { continue; }
                const int x = valueToX(valueOf(e.recIndex));
                p.setPen(QPen(sevColor(e.sev), 2));
                p.drawLine(x, trackY + 8, x, trackY + 18);
            }
        }
    }

    // ---- cursor guide --------------------------------------------------
    const int cx = qBound(x0, valueToX(valueOf(m_cursor)), x1);
    p.setPen(QPen(curColor(), 1, Qt::DashLine));
    p.drawLine(cx, headerB, cx, axisY);

    // ---- loco boxes (one per source; each rides the rail at its own ----
    //      abs location in spatial mode / its own time in temporal mode) --
    auto drawLoco = [&](const LocoVizState &s, bool active) {
        const int boxW = active ? 80 : 58;
        const int boxH = active ? 30 : 22;
        const double val = (m_mode == Spatial) ? s.absLocM : double(s.tMs);
        const int lx = qBound(x0 + boxW / 2, valueToX(val), x1 - boxW / 2);
        const QRect box(lx - boxW / 2, trackY - boxH / 2, boxW, boxH);

        QColor fill, border;
        if (!s.valid)         { fill = QColor(0x30, 0x36, 0x3E); border = QColor(0x55, 0x5E, 0x69); }
        else if (s.emSev >= 2){ fill = QColor(0x5A, 0x26, 0x24); border = failColor(); }
        else if (s.emSev == 1){ fill = QColor(0x5A, 0x49, 0x1E); border = QColor(0xD9, 0xA0, 0x40); }
        else                  { fill = QColor(0x27, 0x53, 0x44); border = okColor(); }
        if (!active) { fill = fill.darker(135); border = border.darker(120); }

        if (s.valid && (s.dir == 1 || s.dir == 2)) {       // direction nose
            const int nz = active ? 11 : 8;
            QPolygon nose;
            if (s.dir == 1) nose << QPoint(box.right(), box.top())
                                 << QPoint(box.right() + nz, box.center().y())
                                 << QPoint(box.right(), box.bottom());
            else            nose << QPoint(box.left(), box.top())
                                 << QPoint(box.left() - nz, box.center().y())
                                 << QPoint(box.left(), box.bottom());
            p.setBrush(fill); p.setPen(QPen(border, active ? 2 : 1)); p.drawPolygon(nose);
        }
        p.setBrush(fill); p.setPen(QPen(border, active ? 2 : 1));
        p.drawRoundedRect(box, 4, 4);

        QString spTxt = QStringLiteral("--");
        if (s.valid && !s.speed.isEmpty()) {
            double v; spTxt = leadingNumber(s.speed, v) ? QString::number(qint64(v)) : s.speed.left(4);
        }
        QFont sp = base; sp.setBold(active);
        if (!active) { sp.setPointSizeF(base.pointSizeF() - 1.0); }
        p.setFont(sp);
        p.setPen(QColor(0xF0, 0xF4, 0xF8));
        p.drawText(box, Qt::AlignCenter, spTxt);

        p.setFont(small);                                   // key label above
        p.setPen(active ? QColor(0xE0, 0xE6, 0xEC) : QColor(0x9A, 0xA2, 0xAA));
        p.drawText(QRect(box.left() - 24, box.top() - 15, boxW + 48, 13),
                   Qt::AlignHCenter, s.keyLabel);

        if (active) {                                       // km/h + mode under the box
            p.setPen(QColor(0xB8, 0xC0, 0xC8));
            p.drawText(box, Qt::AlignHCenter | Qt::AlignBottom, s.valid ? tr("km/h") : QString());
            if (s.valid && !s.mode.isEmpty()) {
                p.setPen(border);
                p.drawText(QRect(box.left() - 24, box.bottom() + 2, boxW + 48, 14),
                           Qt::AlignHCenter, s.mode);
            }
        }
        p.setFont(base);
    };
    for (const LocoVizState &s : m_locos) { if (!s.active) { drawLoco(s, false); } }
    for (const LocoVizState &s : m_locos) { if (s.active)  { drawLoco(s, true);  } }

    // ---- axis + scale labels -------------------------------------------
    p.setPen(QPen(axisColor(), 1));
    p.drawLine(m_margin, axisY, W - m_margin, axisY);
    p.setFont(small);
    p.setPen(axisColor());
    auto axisLabel = [&](double v) -> QString {
        if (m_mode == Spatial) return QString::number(v / 1000.0, 'f', 2) + " km";
        return QDateTime::fromMSecsSinceEpoch(qint64(v)).toString("HH:mm:ss");
    };
    p.drawText(m_margin - 6, axisY + 16, axisLabel(m_min));
    p.drawText(W - m_margin - 44, axisY + 16, axisLabel(m_max));
    p.setFont(base);
}

void TimelineWidget::mousePressEvent(QMouseEvent *e) { mouseMoveEvent(e); }
void TimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_recs || m_recs->isEmpty()) { return; }
    const int idx = nearestIndex(xToValue(e->pos().x()));
    if (idx != m_cursor) { m_cursor = idx; update(); emit cursorMoved(idx); }
}

// ======================= event extraction =======================
// One ordered pass over the records, producing the merged timeline that drives
// both the Event Log and the colour ribbon. Edge-triggered everywhere a level
// would otherwise spam (emergency, mode, MA-shorten, fault sets).
//
// Cost note: most detectors read only the CaptureLine / CaptureIndex / RfidInfo
// metadata already decoded at load. Two re-decode the frame — profileOf() on
// SLRP (MA shortening) and faultsOf() on NMS-fault (fault onset/clear) — but
// each is gated to its own packet type and this whole pass runs once per load,
// so the extra work is bounded to those two periodic streams.
//
// Severity policy (deliberate, easy to retune):
//   Error : CRC fail, emergency onset            — must never be missed
//   Warn  : dropped frames, MA shortened, fault set, bad-CRC RFID read
//   Info  : RFID read, mode change, emergency clear, fault clear, baselines
// Faults are Warn rather than Error on purpose: NMS carries many routine and
// transient faults, and painting the ribbon red for all of them would drown
// the genuine alarms. Flip the two add()s below if your captures say otherwise.
QVector<ReplayEvent> extractEvents(const QVector<ReplayRec> &recs)
{
    QVector<ReplayEvent> ev;
    QMap<QString, quint32> lastSeq;
    QMap<QString, int>     lastMode;
    QMap<QString, int>     lastEmerg;
    QMap<QString, bool>    lastShorten;                 // MA-shorten edge, per key
    QMap<QString, QHash<quint64, QString>> activeFaults;// key -> (faultKey -> "module / fault")
    QSet<QString>          faultSeeded;                 // first NMS-fault packet seen, per key

    // Stable identity for a fault across packets: (subsystem, module, codeType, faultId).
    auto packFault = [](const ActiveFaultInfo &a) -> quint64 {
        return (quint64(quint32(a.subsystem) & 0xFFFF) << 48)
             | (quint64(quint32(a.moduleId)  & 0xFFFF) << 32)
             | (quint64(quint32(a.codeType)  & 0xFFFF) << 16)
             |  quint64(quint32(a.faultId)   & 0xFFFF);
    };

    for (int i = 0; i < recs.size(); ++i) {
        const ReplayRec  &r = recs.at(i);
        const CaptureLine &c = r.line;
        const QString key = c.key();
        auto add = [&](ReplayEvent::Sev s, const QString &sum) {
            ReplayEvent e;
            e.recIndex = i; e.tMs = r.tMs; e.pos = r.pos; e.key = key;
            e.type = QString::fromLatin1(CaptureDecoder::typeLabel(c.type));
            e.sev = s; e.summary = sum;
            ev.push_back(e);
        };

        if (c.crcChecked && !c.crcOk) {
            add(ReplayEvent::Error, QStringLiteral("CRC FAIL"));
        }
        // Sequence is a per-controller global counter, so a gap = dropped capture(s).
        // Ignore resets/wraps (cur < prev) to avoid spurious huge gaps.
        if (lastSeq.contains(key)) {
            const quint32 prev = lastSeq.value(key);
            if (c.seq > prev + 1U) {
                add(ReplayEvent::Warn,
                    QStringLiteral("dropped %1 (seq %2\u2192%3)").arg(c.seq - prev - 1U).arg(prev).arg(c.seq));
            }
        }
        lastSeq[key] = c.seq;

        if (r.isRfid) {
            const bool crcBad = !r.rfid.crcOk;
            add(crcBad ? ReplayEvent::Warn : ReplayEvent::Info,
                QStringLiteral("RFID tag=%1  abs=%2 m%3")
                    .arg(r.rfid.unique).arg(r.rfid.absLoc)
                    .arg(crcBad ? QStringLiteral("  (CRC FAIL)") : QString()));
        }
        if (r.idx.emergency >= 0) {
            const int prev = lastEmerg.value(key, 0);
            const int cur  = r.idx.emergency;
            if (cur > 0 && prev <= 0) {
                add(ReplayEvent::Error, QStringLiteral("EMERGENCY: %1").arg(emergName(cur)));
            } else if (cur == 0 && prev > 0) {
                add(ReplayEvent::Info, QStringLiteral("emergency cleared (was %1)").arg(emergName(prev)));
            }
            lastEmerg[key] = cur;
        }
        if (r.idx.locoMode >= 0) {
            if (lastMode.contains(key) && lastMode.value(key) != r.idx.locoMode) {
                add(ReplayEvent::Info,
                    QStringLiteral("mode %1 \u2192 %2")
                        .arg(modeName(lastMode.value(key)), modeName(r.idx.locoMode)));
            }
            lastMode[key] = r.idx.locoMode;
        }

        // ---- MA shortening (SLRP) — edge-triggered on false->true ----
        if (c.type == CapType::SLRP) {
            const SlrpProfile P = CaptureDecoder::profileOf(c);
            if (P.valid && P.haveMA) {
                const bool was = lastShorten.value(key, false);
                if (P.reqShorten && !was) {
                    add(ReplayEvent::Warn,
                        QStringLiteral("MA shortened \u2192 new MA %1 m").arg(P.newMA));
                }
                lastShorten[key] = P.reqShorten;          // re-arms when it clears
            }
        }

        // ---- NMS fault onset / clear (set diff vs previous packet) ----
        if (c.type == CapType::NmsFault) {
            QHash<quint64, QString> now;
            for (const ActiveFaultInfo &a : CaptureDecoder::faultsOf(c)) {
                now.insert(packFault(a),
                           QStringLiteral("%1 / %2").arg(a.moduleName, a.faultName));
            }
            QHash<quint64, QString> &prev = activeFaults[key];

            if (!faultSeeded.contains(key)) {
                // First fault packet for this source: these faults predate the
                // capture, so don't report them as fresh onsets — note the
                // standing state once and use it as the baseline.
                if (!now.isEmpty()) {
                    add(ReplayEvent::Info,
                        QStringLiteral("%1 fault(s) active at capture start").arg(now.size()));
                }
                faultSeeded.insert(key);
            } else {
                for (auto it = now.constBegin(); it != now.constEnd(); ++it) {
                    if (!prev.contains(it.key())) {
                        add(ReplayEvent::Warn, QStringLiteral("FAULT set: %1").arg(it.value()));
                    }
                }
                for (auto it = prev.constBegin(); it != prev.constEnd(); ++it) {
                    if (!now.contains(it.key())) {
                        add(ReplayEvent::Info, QStringLiteral("fault cleared: %1").arg(it.value()));
                    }
                }
            }
            prev = now;
        }
    }
    return ev;
}

// ======================= ReplayWindow =======================
ReplayWindow::ReplayWindow(const QString &capPath, QWidget *parent)
    : ReplayWindow(QStringList{capPath}, parent) {}

ReplayWindow::ReplayWindow(const QStringList &capPaths, QWidget *parent)
    : QMainWindow(parent)
{
    const QString title = (capPaths.size() == 1)
        ? capPaths.first().section('/', -1)
        : tr("%1 captures").arg(capPaths.size());
    setWindowTitle(tr("Replay \u2014 %1").arg(title));
    m_title = title;
    resize(1040, 760);
    // Default above; a remembered size/position wins over it.
    WindowGeometry::restore(this, QStringLiteral("replayWindow"));

    QWidget *central = new QWidget(this);
    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(10, 8, 10, 10);
    root->setSpacing(8);

    // controls (single compact row)
    QHBoxLayout *ctl = new QHBoxLayout();
    ctl->setSpacing(6);
    m_modeBox = new QComboBox(central);
    m_modeBox->addItem(tr("\u2194 Spatial"));
    m_modeBox->addItem(tr("\u23F1 Temporal"));
    m_modeBox->setFixedWidth(118);
    ctl->addWidget(m_modeBox);

    // active-source selector (only meaningful with >1 key; populated after load)
    m_keyBox = new QComboBox(central);
    m_keyBox->setFixedWidth(110);
    m_keyBox->setToolTip(tr("Source the inspector & state strip follow; all sources draw on the track"));
    ctl->addWidget(m_keyBox);

    m_play = new QPushButton(tr("\u25B6"), central);
    m_play->setObjectName(QStringLiteral("replayPlay"));
    // Wide enough for its glyph under the app's padding (session 136: at a
    // fixed 36 px the triangle was a sliver; the jump arrows at 30 px were
    // blank).
    m_play->setMinimumWidth(m_play->fontMetrics().horizontalAdvance(QStringLiteral("\u25B6")) + 32);
    m_play->setToolTip(tr("Play / pause (Space)"));
    ctl->addWidget(m_play);

    // Session 82: how fast. "Step" is the old behaviour (one record per
    // 200 ms, whatever the gap between them); the rest play in RECORDED time,
    // scaled, so a quiet minute takes a minute at 1x and six seconds at 10x.
    m_speed = new QComboBox(central);
    m_speed->setObjectName(QStringLiteral("replaySpeed"));
    m_speed->addItem(tr("Step"), 0.0);
    for (double f : { 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 50.0 }) {
        m_speed->addItem(QStringLiteral("%1\u00D7").arg(f), f);
    }
    m_speed->setCurrentIndex(m_speed->findData(1.0));
    m_speed->setToolTip(tr("Playback speed against the recorded time. Step: one record every 200 ms."));
    ctl->addWidget(m_speed);

    // Jump between events (the Event Log's list), filtered by severity.
    m_eventLevel = new QComboBox(central);
    m_eventLevel->setObjectName(QStringLiteral("replayEventLevel"));
    m_eventLevel->addItem(tr("any event"), int(ReplayEvent::Info));
    m_eventLevel->addItem(tr("warnings+"), int(ReplayEvent::Warn));
    m_eventLevel->addItem(tr("errors"), int(ReplayEvent::Error));
    m_eventLevel->setToolTip(tr("Which events ◀ / ▶ stop at"));
    auto *evPrev = new QPushButton(tr("\u25C0 event"), central);
    auto *evNext = new QPushButton(tr("event \u25B6"), central);
    evPrev->setObjectName(QStringLiteral("replayEventPrev"));
    evNext->setObjectName(QStringLiteral("replayEventNext"));
    evPrev->setToolTip(tr("Previous event ( [ )"));
    evNext->setToolTip(tr("Next event ( ] )"));
    ctl->addWidget(evPrev);
    ctl->addWidget(m_eventLevel);
    ctl->addWidget(evNext);
    connect(evPrev, &QPushButton::clicked, this, [this]() { stepEvent(-1); });
    connect(evNext, &QPushButton::clicked, this, [this]() { stepEvent(+1); });

    QFrame *sep = new QFrame(central);
    sep->setFrameShape(QFrame::VLine); sep->setFrameShadow(QFrame::Sunken);
    ctl->addWidget(sep);

    ctl->addWidget(new QLabel(tr("Jump"), central));
    m_jumpField = new QComboBox(central);
    m_jumpField->addItem(tr("Frame # (s/midnight)"), QStringLiteral("frame"));
    m_jumpField->addItem(tr("Mode"),                 QStringLiteral("mode"));
    m_jumpField->addItem(tr("Emergency"),            QStringLiteral("emerg"));
    m_jumpField->addItem(tr("RFID id"),              QStringLiteral("rfid"));
    m_jumpField->setSizeAdjustPolicy(QComboBox::AdjustToContents);   // "Frame # (s/midnight)" whole
    ctl->addWidget(m_jumpField);
    m_jumpValue = new QComboBox(central);
    m_jumpValue->setEditable(true);
    m_jumpValue->setInsertPolicy(QComboBox::NoInsert);
    m_jumpValue->setMinimumWidth(m_jumpValue->fontMetrics().horizontalAdvance(QStringLiteral("Frame or HH:MM:SS")) + 40);
    m_jumpValue->setToolTip(tr("Pick or type a value, then \u25C0 / \u25B6 to seek"));
    ctl->addWidget(m_jumpValue);
    QPushButton *jprev = new QPushButton(tr("\u25C0"), central);
    QPushButton *jnext = new QPushButton(tr("\u25B6"), central);
    jprev->setObjectName(QStringLiteral("replayJumpPrev"));
    jnext->setObjectName(QStringLiteral("replayJumpNext"));
    for (QPushButton *b : { jprev, jnext })
        b->setMinimumWidth(b->fontMetrics().horizontalAdvance(QStringLiteral("\u25B6")) + 32);
    ctl->addWidget(jprev); ctl->addWidget(jnext);

    ctl->addStretch(1);
    root->addLayout(ctl);

    m_timeline = new TimelineWidget(central);
    root->addWidget(m_timeline);

    m_scroll = new QSlider(Qt::Horizontal, central);
    m_scroll->setSingleStep(1);
    m_scroll->setPageStep(10);
    m_scroll->setToolTip(tr("Scroll through the capture (or drag the timeline / use \u2190 \u2192)"));
    // The readout sits at the end of the scroll row, next to what it reads
    // (session 136): at the end of the controls it made the window at least
    // 1483 px wide, more than a 1366-px laptop, and was cut off even then.
    auto *scrollRow = new QHBoxLayout();
    scrollRow->setSpacing(12);
    scrollRow->addWidget(m_scroll, 1);
    m_readout = new QLabel(QString(), central);
    m_readout->setObjectName(QStringLiteral("replayReadout"));
    m_readout->setFont(UiStyle::monoFont());
    m_readout->setTextInteractionFlags(Qt::TextSelectableByMouse);
    scrollRow->addWidget(m_readout);
    root->addLayout(scrollRow);

    m_tabs = new QTabWidget(central);
    UiStyle::useTabTooltips(m_tabs);
    for (int i = 0; i < kTabOrderCount; ++i) { addTypeTab(kTabOrder[i]); }

    buildEventLog();   // creates m_eventPanel

    // segmented view toggle: Inspector | Event Log
    QHBoxLayout *viewRow = new QHBoxLayout();
    viewRow->setSpacing(0);
    QToolButton *bInspector = new QToolButton(central);
    bInspector->setText(tr("Inspector")); bInspector->setCheckable(true);
    bInspector->setChecked(true); bInspector->setAutoExclusive(true);
    QToolButton *bEvents = new QToolButton(central);
    bEvents->setText(tr("Event Log")); bEvents->setCheckable(true);
    bEvents->setAutoExclusive(true);
    viewRow->addWidget(bInspector);
    viewRow->addWidget(bEvents);
    viewRow->addStretch(1);

    // Export menu: events / records, with an active-source-only scope toggle.
    QToolButton *bExport = new QToolButton(central);
    bExport->setText(tr("Export \u25BE"));
    bExport->setToolTip(tr("Export the event log or the decoded records"));
    bExport->setPopupMode(QToolButton::InstantPopup);
    QMenu *exMenu = new QMenu(bExport);
    QAction *aScope = exMenu->addAction(tr("Active source only"));
    aScope->setCheckable(true);
    aScope->setChecked(m_exportActiveOnly);
    connect(aScope, &QAction::toggled, this, [this](bool on){ m_exportActiveOnly = on; });
    exMenu->addSeparator();
    connect(exMenu->addAction(tr("Events \u2192 CSV\u2026")),
            &QAction::triggered, this, [this]{ exportEvents(); });
    connect(exMenu->addAction(tr("Records \u2192 CSV\u2026")),
            &QAction::triggered, this, [this]{ exportRecords(false); });
    connect(exMenu->addAction(tr("Records + decode \u2192 JSON\u2026")),
            &QAction::triggered, this, [this]{ exportRecords(true); });
    bExport->setMenu(exMenu);
    viewRow->addWidget(bExport);

    // Session 84: the loco pilot's panel at the cursor.
    QToolButton *bDmi = new QToolButton(central);
    bDmi->setObjectName(QStringLiteral("replayDmi"));
    bDmi->setText(tr("\u23F1 DMI at cursor\u2026"));
    bDmi->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+D")));
    bDmi->setToolTip(tr("Open the DMI (LP-OCIP) window following this cursor: the panel as the loco pilot "
                        "saw it at the selected record (Ctrl+Alt+D)"));
    connect(bDmi, &QToolButton::clicked, this, [this]{ openDmi(); });
    viewRow->addWidget(bDmi);
    root->addLayout(viewRow);

    m_stack = new QStackedWidget(central);
    m_stack->addWidget(m_tabs);          // page 0 = inspector
    m_stack->addWidget(m_eventPanel);    // page 1 = event log
    root->addWidget(m_stack, 1);

    connect(bInspector, &QToolButton::clicked, this, [this]{ m_stack->setCurrentIndex(0); });
    connect(bEvents,    &QToolButton::clicked, this, [this]{ m_stack->setCurrentIndex(1); });

    setCentralWidget(central);

    connect(m_timeline, &TimelineWidget::cursorMoved, this, &ReplayWindow::onCursorMoved);
    connect(m_modeBox,  qOverload<int>(&QComboBox::currentIndexChanged), this, &ReplayWindow::onModeChanged);
    connect(m_keyBox,   qOverload<int>(&QComboBox::currentIndexChanged), this, &ReplayWindow::onKeyChanged);
    connect(m_play,     &QPushButton::clicked, this, &ReplayWindow::onPlayPause);
    connect(m_jumpField, qOverload<int>(&QComboBox::currentIndexChanged), this, &ReplayWindow::onJumpFieldChanged);
    connect(jprev, &QPushButton::clicked, this, [this]{ onJumpStep(-1); });
    connect(jnext, &QPushButton::clicked, this, [this]{ onJumpStep(+1); });
    connect(m_jumpValue->lineEdit(), &QLineEdit::returnPressed, this, &ReplayWindow::onJumpGo);
    connect(m_scroll, &QSlider::valueChanged, this, &ReplayWindow::onScroll);

    m_timer = new QTimer(this);
    m_timer->setInterval(50);   // the step mode keeps its 200 ms by counting ticks
    connect(m_timer, &QTimer::timeout, this, &ReplayWindow::onTick);

    loadFiles(capPaths);

    // Populate the source selector from the keys discovered during load.
    m_keyBox->blockSignals(true);
    for (const QString &k : m_keys) { m_keyBox->addItem(k); }
    m_keyBox->blockSignals(false);
    m_keyBox->setVisible(m_keys.size() > 1);   // hide when there's nothing to pick
    m_activeKey = 0;

    // Per source, where its @dmi frames are: the DMI at any cursor is then
    // a binary search per source rather than a walk back through the capture.
    m_dmiIdx = QVector<QVector<int>>(m_keys.size());
    for (int i = 0; i < m_recs.size(); ++i) {
        const ReplayRec &r = m_recs.at(i);
        if (r.line.type == CapType::Dmi && r.keyIndex >= 0 && r.keyIndex < m_dmiIdx.size()) {
            m_dmiIdx[r.keyIndex].append(i);
        }
    }

    m_events = extractEvents(m_recs);
    m_timeline->setData(&m_recs);
    m_timeline->setEvents(&m_events);
    populateEventLog();
    m_scroll->setRange(0, qMax(0, m_recs.size() - 1));
    onJumpFieldChanged(0);
    if (!m_recs.isEmpty()) { refreshState(0); }
}

void ReplayWindow::addTypeTab(CapType t)
{
    QTableWidget *tbl = new QTableWidget(0, 2, m_tabs);
    tbl->setHorizontalHeaderLabels({tr("Field"), tr("Value")});
    tbl->verticalHeader()->setVisible(false);
    tbl->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tbl->setFont(UiStyle::monoFont());
    tbl->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tbl->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tabs->addTab(tbl, QString::fromLatin1(CaptureDecoder::typeLabel(t)));
    m_typeTables.insert(int(t), tbl);

    // Right-click a row -> plot that field over the whole capture.
    const CapType ct = t;
    tbl->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tbl, &QTableWidget::customContextMenuRequested, this,
            [this, tbl, ct](const QPoint &pos) {
        QTableWidgetItem *it = tbl->itemAt(pos);
        QMenu menu(this);
        QAction *plot = nullptr;
        QString field;
        if (it) {
            QTableWidgetItem *nameItem = tbl->item(it->row(), 0);
            if (nameItem) {
                field = nameItem->text().trimmed();
                plot = menu.addAction(tr("Plot \u201C%1\u201D over time").arg(field));
            }
        }
        QAction *clr = menu.addAction(tr("Clear plot"));
        clr->setEnabled(m_timeline->hasPlot());
        QAction *chosen = menu.exec(tbl->viewport()->mapToGlobal(pos));
        if (chosen && chosen == plot) { plotField(ct, field); }
        else if (chosen == clr)        { clearPlot(); }
    });
}

static QColor sevColour(ReplayEvent::Sev s)
{
    switch (s) {
    case ReplayEvent::Error: return failColor();
    case ReplayEvent::Warn:  return QColor(0xE8, 0x9A, 0x3C);
    default:                 return QColor(0x6A, 0x90, 0xB0);
    }
}

void ReplayWindow::buildEventLog()
{
    m_eventPanel = new QWidget(this);
    QVBoxLayout *v = new QVBoxLayout(m_eventPanel);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    // filter row: free-text + severity toggles + count
    QHBoxLayout *f = new QHBoxLayout();
    m_eventFilter = new QLineEdit(m_eventPanel);
    m_eventFilter->setPlaceholderText(tr("filter\u2026  e.g. crc, rfid, ccsys, dropped, mode"));
    m_eventFilter->setClearButtonEnabled(true);
    f->addWidget(m_eventFilter, 1);

    auto mkSev = [&](const QString &txt, int bit, QColor col) {
        QToolButton *b = new QToolButton(m_eventPanel);
        b->setText(txt); b->setCheckable(true); b->setChecked(true);
        b->setToolTip(tr("show / hide %1 events").arg(txt));
        b->setStyleSheet(QStringLiteral(
            "QToolButton{padding:2px 8px;} QToolButton:checked{color:%1;font-weight:bold;}").arg(col.name()));
        connect(b, &QToolButton::toggled, this, [this, bit](bool on) {
            if (on) { m_sevMask |= bit; } else { m_sevMask &= ~bit; }
            applyEventFilter();
        });
        f->addWidget(b);
    };
    mkSev(tr("Error"), 0x4, sevColour(ReplayEvent::Error));
    mkSev(tr("Warn"),  0x2, sevColour(ReplayEvent::Warn));
    mkSev(tr("Info"),  0x1, sevColour(ReplayEvent::Info));

    m_eventCount = new QLabel(QString(), m_eventPanel);
    m_eventCount->setFont(UiStyle::monoFont());
    f->addWidget(m_eventCount);
    v->addLayout(f);

    // table
    m_eventLog = new QTableWidget(0, 4, m_eventPanel);
    m_eventLog->setHorizontalHeaderLabels({tr("Time"), tr("Type"), tr("Sev"), tr("Event")});
    m_eventLog->verticalHeader()->setVisible(false);
    m_eventLog->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_eventLog->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_eventLog->setSelectionMode(QAbstractItemView::SingleSelection);
    m_eventLog->setFont(UiStyle::monoFont());
    m_eventLog->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_eventLog->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_eventLog->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_eventLog->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    v->addWidget(m_eventLog, 1);

    connect(m_eventFilter, &QLineEdit::textChanged, this, [this]{ applyEventFilter(); });
    connect(m_eventLog, &QTableWidget::cellClicked, this, [this](int row, int) {
        QTableWidgetItem *it = m_eventLog->item(row, 0);
        if (it) { setCursor(it->data(Qt::UserRole).toInt()); }   // jump shared cursor
    });
}

void ReplayWindow::populateEventLog()
{
    static const char *sevTxt[] = { "INFO", "WARN", "ERR" };
    m_eventLog->setRowCount(m_events.size());
    for (int i = 0; i < m_events.size(); ++i) {
        const ReplayEvent &e = m_events.at(i);
        QTableWidgetItem *t = new QTableWidgetItem(
            QDateTime::fromMSecsSinceEpoch(e.tMs).toString("HH:mm:ss"));
        t->setData(Qt::UserRole, e.recIndex);
        m_eventLog->setItem(i, 0, t);
        m_eventLog->setItem(i, 1, new QTableWidgetItem(e.type));
        QTableWidgetItem *sv = new QTableWidgetItem(QString::fromLatin1(sevTxt[e.sev]));
        sv->setForeground(sevColour(e.sev));
        m_eventLog->setItem(i, 2, sv);
        m_eventLog->setItem(i, 3, new QTableWidgetItem(e.summary));
    }
    applyEventFilter();
}

void ReplayWindow::applyEventFilter()
{
    if (!m_eventLog) { return; }
    const QString needle = m_eventFilter ? m_eventFilter->text().trimmed().toLower() : QString();
    int shown = 0;
    for (int i = 0; i < m_events.size() && i < m_eventLog->rowCount(); ++i) {
        const ReplayEvent &e = m_events.at(i);
        const bool sevOk = ((m_sevMask >> int(e.sev)) & 1) != 0;
        bool textOk = needle.isEmpty();
        if (!textOk) {
            textOk = (e.type + ' ' + e.key + ' ' + e.summary).toLower().contains(needle);
        }
        const bool vis = sevOk && textOk;
        m_eventLog->setRowHidden(i, !vis);
        if (vis) { ++shown; }
    }
    if (m_eventCount) { m_eventCount->setText(tr("%1 / %2").arg(shown).arg(m_events.size())); }
}

void ReplayWindow::loadFiles(const QStringList &paths)
{
    m_recs.clear();
    m_keys.clear();
    m_tagLoc.clear();

    // ---- pass 1: parse every line from every file (no position yet) ----
    QMap<QString, int> keyIndexOf;
    for (const QString &path : paths) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { continue; }
        QTextStream in(&f);
        qint64 prevMs = 0;
        while (!in.atEnd()) {
            const QString ln = in.readLine();
            const CaptureLine c = CaptureDecoder::parseLine(ln);
            if (!c.valid) { continue; }

            ReplayRec rec;
            rec.line = c;
            rec.idx  = CaptureDecoder::indexOf(c);
            rec.tMs  = c.rtc.isValid() ? c.rtc.toMSecsSinceEpoch() : (prevMs + 1000);
            prevMs   = rec.tMs;
            if (c.type == CapType::Rfid) {
                rec.rfid   = CaptureDecoder::decodeRfid(c.bytes);
                rec.isRfid = rec.rfid.valid;
                if (rfidHasFix(rec.rfid)) {           // remember each tag's abs_loc
                    m_tagLoc.insert(rec.rfid.unique, qint64(rec.rfid.absLoc));
                }
            }
            const QString key = c.key();
            if (!keyIndexOf.contains(key)) { keyIndexOf.insert(key, -1); }  // placeholder
            rec.keyIndex = 0;                                                // fixed up below
            // stash key on the record via line.key(); resolve index after we
            // know the sorted key set.
            m_recs.push_back(rec);
        }
    }

    // Stable key ordering (loco_ctrl ascending) -> index map.
    m_keys = keyIndexOf.keys();              // QMap keys come out sorted
    for (int i = 0; i < m_keys.size(); ++i) { keyIndexOf[m_keys.at(i)] = i; }
    for (ReplayRec &r : m_recs) { r.keyIndex = keyIndexOf.value(r.line.key(), 0); }

    // ---- merge: sort the combined stream by time (stable) --------------
    std::stable_sort(m_recs.begin(), m_recs.end(),
                     [](const ReplayRec &a, const ReplayRec &b){ return a.tMs < b.tMs; });

    // ---- pass 2: per-key position carry-forward, anchored to first RFID -
    // The spatial origin for each key is its FIRST reported RFID abs location
    // (the loco's own ABS_LOCO_LOC reads ~0 until it localises off a tag, so
    // it must not establish the origin). Records before a key's first RFID are
    // pinned to that origin; keys that never report a position (e.g. non-loco
    // controllers) are pinned to the earliest RFID seen anywhere, so they do
    // not drag the spatial axis back to 0.
    const int K = qMax(1, m_keys.size());
    QVector<double> lastPos(K, 0.0);          // carry-forward position
    QVector<double> originPos(K, 0.0);        // this key's first RFID abs loc
    QVector<bool>   haveRfid(K, false);       // has this key seen its first RFID?
    QVector<QVector<int>> preIdx(K);          // records before this key's first RFID

    double globalOrigin = 0.0; bool haveGlobal = false;

    for (int i = 0; i < m_recs.size(); ++i) {
        ReplayRec &r = m_recs[i];
        const int k = r.keyIndex;

        if (r.isRfid) {
            // A null / sentinel balise read carries no position: it must not
            // establish or move this key's spatial origin. Without this guard a
            // single all-zero RFID frame sets lastPos = 0 and every following
            // record of the key inherits 0, collapsing the track axis floor to
            // ~0 (e.g. -6.65 km) and squashing all real data to the right edge.
            if (rfidHasFix(r.rfid)) {
                lastPos[k] = double(r.rfid.absLoc);
                if (!haveRfid[k]) {                      // first real RFID = spatial origin
                    haveRfid[k] = true; originPos[k] = lastPos[k];
                    for (int j : preIdx[k]) { m_recs[j].pos = originPos[k]; }   // back-fill
                    preIdx[k].clear();
                    if (!haveGlobal) { haveGlobal = true; globalOrigin = originPos[k]; }
                }
            }
        } else {
            double lp;
            // Trust the loco's own location only once it has localised (after
            // its first RFID); before that it is unreliable / zero.
            if (haveRfid[k] && locoLocation(r.line, lp)) { lastPos[k] = lp; }
        }

        if (haveRfid[k]) { r.pos = lastPos[k]; }
        else             { r.pos = 0.0; preIdx[k].push_back(i); }   // provisional
    }

    // Keys that never reported an RFID carry no real track position: pin their
    // records to the earliest origin seen anywhere (or 0 if no RFID at all).
    for (int k = 0; k < K; ++k) {
        if (haveRfid[k]) { continue; }
        const double fill = haveGlobal ? globalOrigin : 0.0;
        for (int j : preIdx[k]) { m_recs[j].pos = fill; }
    }
}

void ReplayWindow::onModeChanged(int index)
{
    m_timeline->setMode(index == 0 ? TimelineWidget::Spatial : TimelineWidget::Temporal);
    m_timeline->setCursorIndex(m_cursor);
}

void ReplayWindow::onKeyChanged(int index)
{
    if (index < 0 || index >= m_keys.size()) { return; }
    m_activeKey = index;
    refreshState(m_cursor);          // re-point inspector + strip at the new source
}

void ReplayWindow::onCursorMoved(int index) { setCursor(index, /*fromTimeline*/ true); }

void ReplayWindow::onScroll(int value) { setCursor(value); }

void ReplayWindow::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Left:  setCursor(m_cursor - 1); break;
    case Qt::Key_Right: setCursor(m_cursor + 1); break;
    case Qt::Key_Home:  setCursor(0); break;
    case Qt::Key_End:   setCursor(m_recs.size() - 1); break;
    case Qt::Key_Space: onPlayPause(); break;
    case Qt::Key_BracketLeft:  stepEvent(-1); break;
    case Qt::Key_BracketRight: stepEvent(+1); break;
    default: QMainWindow::keyPressEvent(e); return;
    }
    e->accept();
}

void ReplayWindow::onPlayPause()
{
    if (m_timer->isActive()) { m_timer->stop(); m_play->setText(tr("\u25B6")); }
    else {
        // The play clock starts at the record under the cursor.
        m_playClockMs = (m_cursor >= 0 && m_cursor < m_recs.size()) ? double(m_recs.at(m_cursor).tMs) : 0.0;
        m_stepTicks = 0;
        m_timer->start();
        m_play->setText(tr("\u275A\u275A"));
    }
}

void ReplayWindow::onTick()
{
    if (m_recs.isEmpty()) { return; }
    if (m_cursor + 1 >= m_recs.size()) { m_timer->stop(); m_play->setText(tr("\u25B6")); return; }
    const double speed = m_speed ? m_speed->currentData().toDouble() : 0.0;
    if (speed <= 0.0) {
        // Step: one record every 200 ms (four 50 ms ticks).
        if (++m_stepTicks >= 4) { m_stepTicks = 0; setCursor(m_cursor + 1); }
        return;
    }
    // Recorded time, scaled. Advance to the last record not after the play
    // clock; a long silence is crossed at the chosen speed, not skipped.
    m_playClockMs += m_timer->interval() * speed;
    int next = m_cursor;
    while (next + 1 < m_recs.size() && double(m_recs.at(next + 1).tMs) <= m_playClockMs) ++next;
    if (next != m_cursor) setCursor(next);
}

int ReplayWindow::nextEventIndex(int fromRec, int dir, int minSeverity) const
{
    if (dir > 0) {
        for (const ReplayEvent &e : m_events) {
            if (e.recIndex > fromRec && int(e.sev) >= minSeverity) return e.recIndex;
        }
    } else {
        for (int i = m_events.size() - 1; i >= 0; --i) {
            const ReplayEvent &e = m_events.at(i);
            if (e.recIndex < fromRec && int(e.sev) >= minSeverity) return e.recIndex;
        }
    }
    return -1;
}

void ReplayWindow::stepEvent(int dir)
{
    const int level = m_eventLevel ? m_eventLevel->currentData().toInt() : 0;
    const int target = nextEventIndex(m_cursor, dir, level);
    if (target < 0) {
        m_readout->setToolTip(QString());
        statusBar()->showMessage(dir > 0 ? tr("No later event at this level") : tr("No earlier event at this level"), 3000);
        return;
    }
    setCursor(target);
    for (const ReplayEvent &e : m_events) {
        if (e.recIndex == target) {
            statusBar()->showMessage(tr("%1 · %2 · %3").arg(QDateTime::fromMSecsSinceEpoch(e.tMs).toString(QStringLiteral("HH:mm:ss.zzz")),
                                                            e.type, e.summary), 6000);
            break;
        }
    }
}

void ReplayWindow::refreshState(int index)
{
    if (index < 0 || index >= m_recs.size()) { return; }
    const ReplayRec &cur = m_recs.at(index);
    offerDmi(index);

    const QString activeKeyStr = (m_activeKey >= 0 && m_activeKey < m_keys.size())
                                 ? m_keys.at(m_activeKey) : QString();
    m_readout->setText(tr("t %1   pos %2 km   [%3/%4]   @%5")
        .arg(QDateTime::fromMSecsSinceEpoch(cur.tMs).toString("HH:mm:ss"))
        .arg(cur.pos / 1000.0, 0, 'f', 3)
        .arg(index + 1).arg(m_recs.size())
        .arg(cur.line.key()));

    // ---- one loco box per key: latest arp/lsrp with a VALID location ---
    {
        QVector<LocoVizState> locos;
        for (int k = 0; k < m_keys.size(); ++k) {
            int li = -1; double locM = 0.0;
            for (int j = index; j >= 0; --j) {
                const ReplayRec &rj = m_recs.at(j);
                if (rj.keyIndex != k) { continue; }
                if (rj.line.type != CapType::ARP && rj.line.type != CapType::LSRP) { continue; }
                double lp;
                if (locoLocation(rj.line, lp)) { li = j; locM = lp; break; }  // skip 0 / unknown
            }
            if (li < 0) { continue; }   // no loco frame with a real location yet -> no box
            const ReplayRec &lr = m_recs.at(li);
            const QVector<FieldRow> rows = CaptureDecoder::describe(lr.line, &m_tagLoc);
            auto field = [&](const char *name) -> QString {
                for (const FieldRow &fr : rows) {
                    if (fr.field.trimmed() == QLatin1String(name)) { return fr.value; }
                }
                return QString();
            };
            double v;
            LocoVizState ls;
            ls.valid     = true;
            ls.active    = (k == m_activeKey);
            ls.keyLabel  = m_keys.at(k);
            ls.tMs       = lr.tMs;
            ls.srcLabel  = QString::fromLatin1(CaptureDecoder::typeLabel(lr.line.type));
            ls.locoId    = field("SOURCE_LOCO_ID");
            ls.speed     = field("TRAIN_SPEED");
            ls.trainLen  = field("TRAIN_LENGTH");
            ls.lastTag   = field("LAST_RFID_TAG");
            ls.mode      = prettyParen(field("LOCO_MODE"));
            ls.emergency = prettyParen(field("EMERGENCY_STATUS"));
            // Box sits at the validated location from this frame (0 / unknown
            // frames were already skipped above); raw field stays in the tab.
            ls.absLocM   = locM;
            ls.dir       = leadingNumber(field("MOVEMENT_DIR"), v) ? int(v) : 0;
            const int ec = leadingNumber(field("EMERGENCY_STATUS"), v) ? int(v) : 0;
            ls.emSev     = (ec == 0) ? 0 : (ec == 1 ? 1 : 2);
            locos.push_back(ls);
        }
        // Ensure the active key always has a strip entry, even before its first
        // loco frame, so the header reads "LOCO --" rather than another source.
        bool haveActive = false;
        for (const LocoVizState &s : locos) { if (s.active) { haveActive = true; break; } }
        if (!haveActive) {
            LocoVizState ph;
            ph.active = true; ph.valid = false;
            ph.keyLabel = activeKeyStr; ph.absLocM = cur.pos;
            locos.push_back(ph);
        }
        m_timeline->setLocoStates(locos);
    }

    // ---- SLRP look-ahead profile for the active key ------------------------
    //
    // NOT just the latest SLRP frame. The station issues a profile once and
    // then sends movement authorities against it: 3322 of the 3340 SLRP frames
    // in replay/ carry nothing but the MA sub-packet. Taking one frame meant
    // the look-ahead lanes were blank for 99% of the timeline and flickered
    // into view only on the eighteen frames that happened to carry them.
    //
    // A loco holds the profile it was issued until REF_PROF_ID changes, so the
    // walk goes back to the frame that issued the one in force now, and stops
    // the moment it crosses into a different profile. Bounded by the same
    // per-key scan the rest of this function does.
    {
        SlrpProfile latest;
        int latestAt = -1;
        for (int j = index; j >= 0; --j) {
            const ReplayRec &rj = m_recs.at(j);
            if (rj.keyIndex == m_activeKey && rj.line.type == CapType::SLRP) {
                latest = CaptureDecoder::profileOf(rj.line);
                latestAt = j;
                break;
            }
        }

        SlrpProfile held;
        if (latest.valid && !latest.carriesLanes()) {
            // The id in force now. A zero-id frame inherits it from earlier,
            // which is why this is not simply latest.refProfId.
            int epoch = latest.refProfId;
            for (int j = latestAt - 1; j >= 0; --j) {
                const ReplayRec &rj = m_recs.at(j);
                if (rj.keyIndex != m_activeKey || rj.line.type != CapType::SLRP) { continue; }
                const SlrpProfile q = CaptureDecoder::profileOf(rj.line);
                if (!q.valid) { continue; }
                if (epoch == 0) { epoch = q.refProfId; }
                if (q.refProfId != 0 && q.refProfId != epoch) {
                    break;              // an older profile: it was discarded, do not show it
                }
                if (q.carriesLanes()) { held = q; break; }
            }
        }

        m_timeline->setProfile(CaptureDecoder::carryProfile(held, latest));
    }

    // for each type tab, find the latest frame of that type at or before `index`
    for (auto it = m_typeTables.begin(); it != m_typeTables.end(); ++it) {
        const int t = it.key();
        QTableWidget *tbl = it.value();
        int found = -1;
        for (int j = index; j >= 0; --j) {
            const ReplayRec &rj = m_recs.at(j);
            if (rj.keyIndex == m_activeKey && int(rj.line.type) == t) { found = j; break; }
        }
        if (found < 0) {
            tbl->setRowCount(1);
            tbl->setItem(0, 0, new QTableWidgetItem(tr("(none yet)")));
            tbl->setItem(0, 1, new QTableWidgetItem(QString()));
            continue;
        }
        const QVector<FieldRow> rows = CaptureDecoder::describe(m_recs.at(found).line, &m_tagLoc);
        tbl->setRowCount(rows.size());
        for (int r = 0; r < rows.size(); ++r) {
            tbl->setItem(r, 0, new QTableWidgetItem(rows.at(r).field));
            QTableWidgetItem *v = new QTableWidgetItem(rows.at(r).value);
            const QString &fv = rows.at(r).value;
            if (rows.at(r).field == QLatin1String("CRC")) {
                if (fv == QLatin1String("FAIL")) v->setForeground(failColor());
                else if (fv == QLatin1String("PASS")) v->setForeground(okColor());
            } else if (fv.endsWith(QLatin1String("FAIL"))) {
                v->setForeground(failColor());
            } else if (fv.endsWith(QLatin1String("PASS"))) {
                v->setForeground(okColor());
            }
            tbl->setItem(r, 1, v);
        }
        // mark the tab whose frame is the one just landed on (== current record)
        const bool isCurrent = (found == index);
        m_tabs->setTabText(m_tabs->indexOf(tbl),
            QString::fromLatin1(CaptureDecoder::typeLabel(CapType(t))) + (isCurrent ? " \u25CF" : ""));
    }
}

// ======================= jump / seek =======================
int ReplayWindow::fieldValue(const ReplayRec &r, int field) const
{
    switch (field) {
    case 0: return r.idx.frameNum;     // Frame #
    case 1: return r.idx.locoMode;     // Mode
    case 2: return r.idx.emergency;    // Emergency
    case 3: return r.idx.rfidUid;      // RFID id
    }
    return -1;
}

void ReplayWindow::plotField(CapType type, const QString &fieldName)
{
    if (fieldName.isEmpty()) { return; }
    QVector<QPair<int,double>> pts;
    for (int i = 0; i < m_recs.size(); ++i) {
        const CaptureLine &c = m_recs.at(i).line;
        if (c.type != type) { continue; }
        const QVector<FieldRow> rows = CaptureDecoder::describe(c, &m_tagLoc);
        for (const FieldRow &fr : rows) {
            if (fr.field.trimmed() == fieldName) {
                double v = 0.0;
                if (leadingNumber(fr.value, v)) { pts.push_back(qMakePair(i, v)); }
                break;
            }
        }
    }
    m_plotType = type; m_plotFieldName = fieldName;
    m_timeline->setPlotSeries(
        pts, QStringLiteral("%1 . %2")
                 .arg(QString::fromLatin1(CaptureDecoder::typeLabel(type)), fieldName));
}

void ReplayWindow::clearPlot()
{
    m_plotType = CapType::Unknown; m_plotFieldName.clear();
    m_timeline->setPlotSeries({}, QString());
}

void ReplayWindow::setCursor(int index, bool fromTimeline)
{
    if (m_recs.isEmpty()) { return; }
    m_cursor = qBound(0, index, m_recs.size() - 1);
    if (!fromTimeline) { m_timeline->setCursorIndex(m_cursor); }
    if (m_scroll && m_scroll->value() != m_cursor) {
        m_scroll->blockSignals(true);
        m_scroll->setValue(m_cursor);
        m_scroll->blockSignals(false);
    }
    refreshState(m_cursor);
}

void ReplayWindow::onJumpFieldChanged(int)
{
    const int field = m_jumpField->currentIndex();
    m_jumpValue->blockSignals(true);
    m_jumpValue->clear();

    // distinct values present in the capture, sorted
    QList<int> vals;
    for (const ReplayRec &r : m_recs) {
        const int v = fieldValue(r, field);
        if (v >= 0 && !vals.contains(v)) { vals.append(v); }
    }
    std::sort(vals.begin(), vals.end());

    for (int v : vals) {
        QString label;
        switch (field) {
        case 0:  label = QStringLiteral("%1  (%2)").arg(v).arg(frameToClock(v)); break;
        case 1:  label = QStringLiteral("%1  %2").arg(v).arg(modeName(v));        break;
        case 2:  label = QStringLiteral("%1  %2").arg(v).arg(emergName(v));       break;
        default: label = QString::number(v);                                      break;
        }
        m_jumpValue->addItem(label, v);
    }
    m_jumpValue->setCurrentIndex(-1);
    m_jumpValue->setEditText(field == 0 ? tr("frame or HH:MM:SS") : QString());
    m_jumpValue->blockSignals(false);
}

// resolve the target value the user picked or typed (-1 if unparseable)
static int parseJumpValue(QComboBox *box, int field)
{
    if (box->currentIndex() >= 0) { return box->currentData().toInt(); }
    QString s = box->currentText().trimmed();
    if (s.isEmpty()) { return -1; }
    if (field == 0 && s.contains(':')) {                  // HH:MM:SS -> seconds
        const QStringList p = s.split(':');
        int sec = 0; for (const QString &x : p) { sec = sec * 60 + x.toInt(); }
        return sec;
    }
    bool ok = false; const int v = s.toInt(&ok);
    return ok ? v : -1;
}

void ReplayWindow::onJumpGo()
{
    if (m_recs.isEmpty()) { return; }
    const int field = m_jumpField->currentIndex();
    const int target = parseJumpValue(m_jumpValue, field);
    if (target < 0) { return; }

    if (field == 0) {                                     // Frame#: nearest by time
        int best = -1; int bd = 1 << 30;
        for (int i = 0; i < m_recs.size(); ++i) {
            const int v = fieldValue(m_recs.at(i), 0);
            if (v < 0) { continue; }
            const int d = qAbs(v - target);
            if (d < bd) { bd = d; best = i; }
        }
        if (best >= 0) { setCursor(best); }
    } else {                                              // categorical: first match
        for (int i = 0; i < m_recs.size(); ++i) {
            if (fieldValue(m_recs.at(i), field) == target) { setCursor(i); return; }
        }
    }
}

void ReplayWindow::onJumpStep(int dir)
{
    if (m_recs.isEmpty()) { return; }
    const int field  = m_jumpField->currentIndex();
    const int target = parseJumpValue(m_jumpValue, field);

    if (field == 0) {
        // Frame#: step to the next/prev record with a different (existing) frame
        const int curFrame = fieldValue(m_recs.at(m_cursor), 0);
        for (int i = m_cursor + dir; i >= 0 && i < m_recs.size(); i += dir) {
            const int v = fieldValue(m_recs.at(i), 0);
            if (v >= 0 && v != curFrame) { setCursor(i); return; }
        }
        return;
    }
    if (target < 0) { return; }
    for (int i = m_cursor + dir; i >= 0 && i < m_recs.size(); i += dir) {
        if (fieldValue(m_recs.at(i), field) == target) { setCursor(i); return; }
    }
    // wrap around
    const int start = (dir > 0) ? 0 : m_recs.size() - 1;
    for (int i = start; i != m_cursor; i += dir) {
        if (i < 0 || i >= m_recs.size()) break;
        if (fieldValue(m_recs.at(i), field) == target) { setCursor(i); return; }
    }
}

// ======================= export (offline, synchronous) =======================
// Replay is offline with the whole capture already in memory, so export is a
// one-shot write under a wait cursor — no worker thread (that exists for the
// live window, and is hard-wired to LogEntry, not the decode-rich ReplayRec).
namespace {

// RFC 4180: quote a field if it holds a comma, quote, CR or LF; double any
// embedded quote. Same rule as Exporter::csvEscape.
QString csvQuote(const QString &f)
{
    bool need = false;
    for (QChar c : f) { if (c == ',' || c == '"' || c == '\n' || c == '\r') { need = true; break; } }
    if (!need) { return f; }
    QString o; o.reserve(f.size() + 4); o += '"';
    for (QChar c : f) { if (c == '"') { o += '"'; } o += c; }
    o += '"';
    return o;
}

QString hexBytes(const QByteArray &b)
{
    QString o; o.reserve(b.size() * 3);
    for (int i = 0; i < b.size(); ++i) {
        if (i > 0) { o += ' '; }
        o += QString(QStringLiteral("%1")).arg(quint8(b[i]), 2, 16, QChar('0'));
    }
    return o;
}

QString isoMs(qint64 epochMs)
{
    return QDateTime::fromMSecsSinceEpoch(epochMs).toString(Qt::ISODateWithMs);
}

const char *sevKey(ReplayEvent::Sev s)
{
    switch (s) {
    case ReplayEvent::Warn:  return "warn";
    case ReplayEvent::Error: return "error";
    default:                 return "info";
    }
}

const char *dirKey(CapType t)
{
    switch (CaptureDecoder::directionFor(t)) {
    case CapDir::In:  return "in";
    case CapDir::Out: return "out";
    default:          return "";
    }
}

QString crcKey(const CaptureLine &c)
{
    return c.crcChecked ? (c.crcOk ? QStringLiteral("pass") : QStringLiteral("fail"))
                        : QStringLiteral("na");
}

} // namespace

void ReplayWindow::exportEvents()
{
    if (m_events.isEmpty()) {
        QMessageBox::information(this, tr("Export events"), tr("No events to export."));
        return;
    }
    const QString def = QStringLiteral("replay_events_%1.csv")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export events to CSV"), def, tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) { return; }

    // Mirror the on-screen filter exactly: severity mask + free-text needle,
    // plus the optional active-source scope. What you see is what you get.
    const QString needle = m_eventFilter ? m_eventFilter->text().trimmed().toLower() : QString();
    const QString activeKey = (m_exportActiveOnly && m_activeKey >= 0 && m_activeKey < m_keys.size())
                              ? m_keys.at(m_activeKey) : QString();

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export events"),
                             tr("Cannot write %1:\n%2").arg(path, f.errorString()));
        return;
    }
    f.write("\xEF\xBB\xBF", 3);                       // UTF-8 BOM for Excel
    QTextStream out(&f);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    QApplication::setOverrideCursor(Qt::WaitCursor);

    out << "time_iso,time_ms,key,type,severity,pos_m,event\n";
    int rows = 0;
    for (const ReplayEvent &e : m_events) {
        if (!activeKey.isEmpty() && e.key != activeKey) { continue; }
        if (((m_sevMask >> int(e.sev)) & 1) == 0) { continue; }
        if (!needle.isEmpty() &&
            !(e.type + ' ' + e.key + ' ' + e.summary).toLower().contains(needle)) { continue; }
        out << csvQuote(isoMs(e.tMs)) << ','
            << e.tMs << ','
            << csvQuote(e.key) << ','
            << csvQuote(e.type) << ','
            << sevKey(e.sev) << ','
            << QString::number(e.pos, 'f', 1) << ','
            << csvQuote(e.summary) << '\n';
        ++rows;
    }
    out.flush();
    f.close();
    QApplication::restoreOverrideCursor();
    QMessageBox::information(this, tr("Export events"),
        tr("Wrote %1 event(s) to\n%2").arg(rows).arg(path));
}

void ReplayWindow::exportRecords(bool asJson)
{
    if (m_recs.isEmpty()) {
        QMessageBox::information(this, tr("Export records"), tr("No records to export."));
        return;
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString def    = asJson ? QStringLiteral("replay_records_%1.json").arg(stamp)
                                  : QStringLiteral("replay_records_%1.csv").arg(stamp);
    const QString filter = asJson ? tr("JSON files (*.json);;All files (*)")
                                  : tr("CSV files (*.csv);;All files (*)");
    const QString path = QFileDialog::getSaveFileName(this, tr("Export records"), def, filter);
    if (path.isEmpty()) { return; }

    const QString activeKey = (m_exportActiveOnly && m_activeKey >= 0 && m_activeKey < m_keys.size())
                              ? m_keys.at(m_activeKey) : QString();

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export records"),
                             tr("Cannot write %1:\n%2").arg(path, f.errorString()));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int rows = 0;

    if (!asJson) {
        // Flat wire record per row: round-trips the capture into a spreadsheet.
        f.write("\xEF\xBB\xBF", 3);
        QTextStream out(&f);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#endif
        out << "time_iso,time_ms,key,type,seq,direction,crc,pos_m,raw_bytes_hex\n";
        for (const ReplayRec &r : m_recs) {
            const CaptureLine &c = r.line;
            if (!activeKey.isEmpty() && c.key() != activeKey) { continue; }
            out << csvQuote(isoMs(r.tMs)) << ','
                << r.tMs << ','
                << csvQuote(c.key()) << ','
                << csvQuote(QString::fromLatin1(CaptureDecoder::typeLabel(c.type))) << ','
                << c.seq << ','
                << dirKey(c.type) << ','
                << crcKey(c) << ','
                << QString::number(r.pos, 'f', 1) << ','
                << csvQuote(hexBytes(c.bytes)) << '\n';
            ++rows;
        }
        out.flush();
    } else {
        // Full decode per record: the fields array is the same Field|Value the
        // inspector shows (abs-loc annotations included via m_tagLoc).
        QJsonArray arr;
        for (const ReplayRec &r : m_recs) {
            const CaptureLine &c = r.line;
            if (!activeKey.isEmpty() && c.key() != activeKey) { continue; }
            QJsonObject o;
            o["time_ms"]       = double(r.tMs);
            o["time_iso"]      = isoMs(r.tMs);
            o["key"]           = c.key();
            o["type"]          = QString::fromLatin1(CaptureDecoder::typeLabel(c.type));
            o["seq"]           = double(c.seq);
            o["direction"]     = QString::fromLatin1(dirKey(c.type));
            o["crc"]           = crcKey(c);
            o["pos_m"]         = r.pos;
            o["raw_bytes_hex"] = hexBytes(c.bytes);
            QJsonArray fields;
            for (const FieldRow &fr : CaptureDecoder::describe(c, &m_tagLoc)) {
                QJsonObject jf; jf["field"] = fr.field; jf["value"] = fr.value;
                fields.append(jf);
            }
            o["fields"] = fields;
            arr.append(o);
            ++rows;
        }
        const QByteArray bytes = QJsonDocument(arr).toJson(QJsonDocument::Indented);
        f.write(bytes);
    }

    f.close();
    QApplication::restoreOverrideCursor();
    QMessageBox::information(this, tr("Export records"),
        tr("Wrote %1 record(s) to\n%2").arg(rows).arg(path));
}

void ReplayWindow::closeEvent(QCloseEvent *event)
{
    WindowGeometry::save(this, QStringLiteral("replayWindow"));
    QMainWindow::closeEvent(event);
}

// =============================================================================
//  Session 84: DMI time travel
// =============================================================================

DmiMoment ReplayWindow::dmiMomentAt(int index) const
{
    DmiMoment m;
    if (index < 0 || index >= m_recs.size()) return m;
    m.valid = true;
    m.atMs = m_recs.at(index).tMs;
    m.origin = tr("replay %1, record %2").arg(m_title).arg(index + 1);
    if (m_activeKey >= 0 && m_activeKey < m_keys.size()) m.preferredKey = m_keys.at(m_activeKey);
    for (int k = 0; k < m_dmiIdx.size() && k < m_keys.size(); ++k) {
        const QVector<int> &idx = m_dmiIdx.at(k);
        const auto it = std::upper_bound(idx.begin(), idx.end(), index);   // first after the cursor
        if (it == idx.begin()) continue;
        const ReplayRec &r = m_recs.at(*(it - 1));
        if (r.tMs < m.atMs - kDmiLookbackMs) continue;
        m.frames.append(DmiFrameAt{ m_keys.at(k), r.line, r.tMs });
    }
    // Session 169: every type, each source's latest at or before the cursor
    // (the Live Loco Console follows too). Newest first, back to the lookback.
    QSet<QPair<int, int>> seen;
    for (int i = index; i >= 0; --i) {
        const ReplayRec &r = m_recs.at(i);
        if (r.tMs < m.atMs - kDmiLookbackMs) break;
        if (!r.line.valid || r.keyIndex < 0 || r.keyIndex >= m_keys.size()) continue;
        const QPair<int, int> id(int(r.line.type), r.keyIndex);
        if (seen.contains(id)) continue;
        seen.insert(id);
        m.latest.append(DmiFrameAt{ m_keys.at(r.keyIndex), r.line, r.tMs });
    }
    return m;
}

void ReplayWindow::offerDmi(int index)
{
    DmiTimeTravel::instance()->offer(this, [this, index]() { return dmiMomentAt(index); });
}

void ReplayWindow::openDmi()
{
    auto *w = new DmiWindow(nullptr, this);
    w->setFollowCursor(true);
    offerDmi(m_cursor);          // this cursor, whoever pointed last
    w->show();
    w->raise();
}

void ReplayWindow::setActiveKey(int index) { m_keyBox->setCurrentIndex(index); }

#include "radiochart.h"

#include "capturedecoder.h"
#include "fieldplot.h"
#include "frameclock.h"
#include "locoidentity.h"
#include "logmodel.h"
#include "radiolinks.h"
#include "rejectrules.h"

#include <QDateTime>
#include <QFile>
#include <QFontMetricsF>
#include <QMap>
#include <QObject>
#include <QPainter>
#include <algorithm>

namespace RadioChart {
namespace {

// FRAME_NUM b after a, across midnight (as RadioLinks counts it).
int step(qint64 a, qint64 b)
{
    qint64 d = b - a;
    if (d < -FrameClock::kSecondsPerDay / 2) d += FrameClock::kSecondsPerDay;
    if (d >  FrameClock::kSecondsPerDay / 2) d -= FrameClock::kSecondsPerDay;
    return int(d);
}

QString hms(qint64 ms, const char *fmt = "HH:mm:ss.zzz")
{
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QLatin1String(fmt));
}

// An exported image on white, for paper: every ink is 4.5:1 or more on it.
const QColor kInk(0x1a, 0x1a, 0x1a);       // 17:1
const QColor kMuted(0x55, 0x55, 0x55);     // 7.5:1
const QColor kRule(0xb8, 0xb8, 0xb8);      // lines only
const QColor kSlrp(0x0b, 0x4f, 0x9c);      // 8:1
const QColor kLsrp(0x1d, 0x6b, 0x2f);      // 6.6:1
const QColor kArp(0x6a, 0x2c, 0x91);       // 8.6:1
const QColor kDrop(0xb0, 0x00, 0x20);      // 6.9:1
const QColor kQuiet(0x8a, 0x53, 0x00);     // 6.2:1

constexpr int kWidth = 1140, kXTime = 14, kXLoco = 200, kXStn = 620, kXNote = 700;
constexpr int kTop = 112, kRowH = 24;

}  // namespace

int Chart::dropped() const
{
    return int(std::count_if(messages.cbegin(), messages.cend(), [](const Message &m) { return !m.drops.isEmpty(); }));
}

QString Chart::summary() const
{
    QMap<QString, int> n, d;
    for (const Message &m : messages) {
        n[m.type] += 1;
        d[m.type] += m.drops.isEmpty() ? 0 : 1;
    }
    QStringList parts;
    for (const QString &t : { QStringLiteral("slrp"), QStringLiteral("lsrp"), QStringLiteral("arp") }) {
        if (!n.value(t)) continue;
        QString s = QObject::tr("%1 %2 %3").arg(t.toUpper()).arg(n.value(t))
                        .arg(t == QLatin1String("slrp") ? QObject::tr("received") : QObject::tr("sent"));
        if (d.value(t)) s += QObject::tr(" (%1 would not be processed)").arg(d.value(t));
        parts << s;
    }
    if (!silences.isEmpty()) parts << QObject::tr("%1 silence%2").arg(silences.size()).arg(silences.size() == 1 ? "" : "s");
    return parts.join(QStringLiteral(" · "));
}

Chart build(const LogModel *model, const QString &tabName, qint64 fromMs, qint64 toMs)
{
    Chart c;
    c.fromMs = fromMs;
    c.toMs = toMs;
    c.title = QObject::tr("Radio conversation — %1 · %2 – %3")
                  .arg(tabName, hms(fromMs, "yyyy-MM-dd  HH:mm:ss"), hms(toMs, "HH:mm:ss"));
    if (!model || toMs <= fromMs) { c.error = QObject::tr("The end is not after the start."); return c; }
    if (toMs - fromMs > kMaxSpanMs) {
        c.error = QObject::tr("A radio chart covers at most %1 minutes of log.").arg(kMaxSpanMs / 60000);
        return c;
    }

    LocoIdentity identity;
    qint64 ownFn = -1, ownMs = 0;
    const QString kFn = QStringLiteral("FRAME_NUM");
    // From 10 s before: the loco's clock and identity, as they stood at the start.
    for (int i = 0; i < model->count(); ++i) {
        const LogEntry *e = model->entryPtrAt(i);
        if (!e || e->epochMs < fromMs - RadioLinks::kOwnClockMs || e->epochMs > toMs) continue;
        const QString type = captureTypeOf(e->text);
        if (type != QLatin1String("slrp") && type != QLatin1String("lsrp") && type != QLatin1String("arp")) continue;
        const CaptureLine cap = CaptureDecoder::parseLine(e->text);
        if (!cap.valid || cap.bytes.isEmpty()) continue;
        QHash<QString, qint64> raw;
        CaptureDecoder::describe(cap, nullptr, 0, &raw);
        const QString key = e->tabKey();
        const bool own = type != QLatin1String("slrp");
        if (own) identity.observe(key, type, raw);
        if (e->epochMs < fromMs) {
            if (own && raw.contains(kFn)) { ownFn = raw.value(kFn); ownMs = e->epochMs; }
            continue;
        }
        if (c.messages.size() >= kMaxMessages) {
            c.error = QObject::tr("More than %1 radio frames in that window: choose a shorter one.").arg(kMaxMessages);
            return c;
        }
        Message m;
        m.ms = e->epochMs;
        m.row = i;
        m.type = type;
        m.fromLoco = own;
        m.frameNum = raw.value(kFn, -1);
        if (!own) {
            m.station = int(raw.value(QStringLiteral("SOURCE_STN_ID"), -1));
            if (m.frameNum >= 0 && ownFn >= 0 && m.ms - ownMs <= RadioLinks::kOwnClockMs) {
                m.hasAge = true;
                m.ageS = step(m.frameNum, ownFn + qRound64((m.ms - ownMs) / 1000.0));
                const FrameClock::Accept a = FrameClock::classify(m.ageS);
                if (a == FrameClock::Accept::Stale) m.drops << QObject::tr("%1 s old (over 4 s)").arg(m.ageS);
                if (a == FrameClock::Accept::Ahead) m.drops << QObject::tr("%1 s early (2 s or more)").arg(-m.ageS);
            }
        } else if (m.frameNum >= 0) {
            ownFn = m.frameNum;
            ownMs = m.ms;
        }
        QHash<QString, qint64> judged = raw;
        const QHash<QString, qint64> ctx = identity.contextFor(key);
        for (auto it = ctx.cbegin(); it != ctx.cend(); ++it) judged.insert(it.key(), it.value());
        for (const RejectRules::Finding &f : kavachRejectRules().evaluate(judged, type))
            m.drops << QStringLiteral("%1 %2 = %3").arg(f.rule.clause, f.rule.field).arg(f.actual);
        c.messages << m;
    }
    if (c.messages.isEmpty()) { c.error = QObject::tr("No SLRP, LSRP or ARP in that window."); return c; }

    // Silences, link by link.
    QMap<QString, QVector<int>> links;
    for (int i = 0; i < c.messages.size(); ++i) {
        const Message &m = c.messages.at(i);
        if (m.frameNum < 0) continue;
        links[m.fromLoco ? m.type.toUpper() : QObject::tr("SLRP from station %1").arg(m.station)] << i;
    }
    for (auto it = links.cbegin(); it != links.cend(); ++it) {
        const QVector<int> &ix = it.value();
        QVector<int> steps;
        for (int k = 1; k < ix.size(); ++k) {
            const int d = step(c.messages.at(ix.at(k - 1)).frameNum, c.messages.at(ix.at(k)).frameNum);
            if (d > 0) steps << d;
        }
        if (steps.isEmpty()) continue;
        std::sort(steps.begin(), steps.end());
        const int cycle = qMax(1, steps.at((steps.size() - 1) / 2));
        for (int k = 1; k < ix.size(); ++k) {
            const Message &a = c.messages.at(ix.at(k - 1)), &b = c.messages.at(ix.at(k));
            const int d = step(a.frameNum, b.frameNum);
            const bool outage = d < 0 || d > RadioLinks::kOutageS;
            if (!outage && d < 2 * cycle) continue;
            c.silences << Silence{ a.ms, b.ms, ix.at(k - 1), it.key(), d, outage ? 0 : d / cycle - 1, outage };
        }
    }
    std::sort(c.silences.begin(), c.silences.end(),
              [](const Silence &a, const Silence &b) { return a.afterIndex < b.afterIndex; });
    return c;
}

Drawing draw(const Chart &c)
{
    Drawing d;
    auto line = [&d](qreal x1, qreal y1, qreal x2, qreal y2, const QColor &col, qreal w = 1, bool dashed = false) {
        Step s;
        s.kind = Step::Line;
        s.line = QLineF(x1, y1, x2, y2);
        s.color = col;
        s.width = w;
        s.dashed = dashed;
        d.steps << s;
    };
    auto text = [&d](qreal x, qreal y, const QString &t, const QColor &col, int align = 0, bool bold = false) {
        Step s;
        s.kind = Step::Text;
        s.line = QLineF(x, y, x, y);
        s.text = t;
        s.color = col;
        s.align = align;
        s.bold = bold;
        d.steps << s;
    };
    const int rows = c.messages.size() + c.silences.size();
    const int bottom = kTop + rows * kRowH + 16;
    d.size = QSize(kWidth, bottom + 34);
    Step bg;
    bg.kind = Step::Rect;
    bg.rect = QRectF(0, 0, kWidth, d.size.height());
    bg.color = Qt::white;
    d.steps << bg;

    text(kXTime, 26, c.title, kInk, 0, true);
    text(kXTime, 48, c.error.isEmpty() ? c.summary() : c.error, c.error.isEmpty() ? kMuted : kDrop);
    for (const auto &head : { qMakePair(kXLoco, QObject::tr("Loco")), qMakePair(kXStn, QObject::tr("Stations")) }) {
        line(head.first - 60, 64, head.first + 60, 64, kInk);
        line(head.first - 60, 90, head.first + 60, 90, kInk);
        line(head.first - 60, 64, head.first - 60, 90, kInk);
        line(head.first + 60, 64, head.first + 60, 90, kInk);
        text(head.first, 82, head.second, kInk, 1, true);
        line(head.first, 90, head.first, bottom, kRule, 2);
    }
    text(kXTime, 82, QObject::tr("Logged at"), kMuted);
    text(kXNote, 82, QObject::tr("Would not be processed"), kMuted);

    int y = kTop, s = 0;
    for (int i = 0; i < c.messages.size(); ++i) {
        const Message &m = c.messages.at(i);
        const QColor col = m.type == QLatin1String("slrp") ? kSlrp : m.type == QLatin1String("lsrp") ? kLsrp : kArp;
        const qreal x1 = m.fromLoco ? kXLoco : kXStn, x2 = m.fromLoco ? kXStn : kXLoco;
        const qreal dir = x2 > x1 ? 1 : -1, ay = y + 14;
        text(kXTime, y + 18, hms(m.ms), kInk);
        line(x1, ay, x2, ay, col, 1.5);
        line(x2, ay, x2 - dir * 9, ay - 4, col, 1.5);
        line(x2, ay, x2 - dir * 9, ay + 4, col, 1.5);
        QString label = m.type.toUpper();
        if (m.frameNum >= 0) label += QObject::tr("  %1 (%2)").arg(m.frameNum).arg(FrameClock::timeText(m.frameNum));
        if (!m.fromLoco) label += QObject::tr("  stn %1").arg(m.station);
        if (m.hasAge) label += QObject::tr("  %1 s old").arg(m.ageS);
        text((kXLoco + kXStn) / 2.0, ay - 4, label, col, 1);
        if (!m.drops.isEmpty()) {
            const qreal cx = x2 - dir * 22;
            line(cx - 5, ay - 5, cx + 5, ay + 5, kDrop, 2.5);
            line(cx - 5, ay + 5, cx + 5, ay - 5, kDrop, 2.5);
            text(kXNote, y + 18, QStringLiteral("✕ ") + m.drops.join(QStringLiteral("; ")), kDrop);
        }
        y += kRowH;
        for (; s < c.silences.size() && c.silences.at(s).afterIndex == i; ++s) {
            const Silence &q = c.silences.at(s);
            line(kXLoco + 20, y + 4, kXStn - 20, y + 4, kQuiet, 1, true);
            line(kXLoco + 20, y + 22, kXStn - 20, y + 22, kQuiet, 1, true);
            const QString what = q.outage
                ? QObject::tr("no %1 for %2 s: no contact").arg(q.link).arg(q.seconds)
                : QObject::tr("no %1 for %2 s: %3 frame%4 expected").arg(q.link).arg(q.seconds).arg(q.missing).arg(q.missing == 1 ? "" : "s");
            text((kXLoco + kXStn) / 2.0, y + 17, what, kQuiet, 1);
            y += kRowH;
        }
    }
    text(kXTime, bottom + 22,
         QObject::tr("Times are the log's. FRAME_NUM is the sender's clock (seconds since midnight + 1). "
                     "✕: what the receiver would do, by the age window (over 4 s old, 2 s early) and the reject rules."),
         kMuted);
    return d;
}

void paint(QPainter &p, const Drawing &d)
{
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = p.font();
    f.setPixelSize(12);
    for (const Step &s : d.steps) {
        switch (s.kind) {
        case Step::Rect:
            p.fillRect(s.rect, s.color);
            break;
        case Step::Line: {
            QPen pen(s.color, s.width);
            if (s.dashed) pen.setDashPattern({ 4, 3 });
            p.setPen(pen);
            p.drawLine(s.line);
            break;
        }
        case Step::Text: {
            f.setBold(s.bold);
            p.setFont(f);
            p.setPen(s.color);
            const qreal w = QFontMetricsF(f).horizontalAdvance(s.text);
            const qreal x = s.align < 0 ? s.line.x1() - w : s.align > 0 ? s.line.x1() - w / 2 : s.line.x1();
            p.drawText(QPointF(x, s.line.y1()), s.text);
            break;
        }
        }
    }
}

QImage toImage(const Drawing &d)
{
    QImage img(d.size, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    paint(p, d);
    return img;
}

QByteArray toSvg(const Drawing &d)
{
    QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                 "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\" viewBox=\"0 0 %1 %2\" "
                                 "font-family=\"sans-serif\" font-size=\"12\">\n")
                      .arg(d.size.width()).arg(d.size.height());
    for (const Step &s : d.steps) {
        const QString col = s.color.name();
        switch (s.kind) {
        case Step::Rect:
            out += QStringLiteral("<rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" fill=\"%5\"/>\n")
                       .arg(s.rect.x()).arg(s.rect.y()).arg(s.rect.width()).arg(s.rect.height()).arg(col);
            break;
        case Step::Line:
            out += QStringLiteral("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%4\" stroke=\"%5\" stroke-width=\"%6\"%7/>\n")
                       .arg(s.line.x1()).arg(s.line.y1()).arg(s.line.x2()).arg(s.line.y2()).arg(col).arg(s.width)
                       .arg(s.dashed ? QStringLiteral(" stroke-dasharray=\"4 3\"") : QString());
            break;
        case Step::Text:
            out += QStringLiteral("<text x=\"%1\" y=\"%2\" fill=\"%3\" text-anchor=\"%4\"%5>%6</text>\n")
                       .arg(s.line.x1()).arg(s.line.y1()).arg(col)
                       .arg(s.align < 0 ? "end" : s.align > 0 ? "middle" : "start")
                       .arg(s.bold ? QStringLiteral(" font-weight=\"bold\"") : QString())
                       .arg(s.text.toHtmlEscaped());
            break;
        }
    }
    out += QStringLiteral("</svg>\n");
    return out.toUtf8();
}

bool save(const Chart &c, const QString &path, QString *error)
{
    const Drawing d = draw(c);
    if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(toSvg(d)) < 0) {
            if (error) *error = f.errorString();
            return false;
        }
        return true;
    }
    if (!toImage(d).save(path, "PNG")) {
        if (error) *error = QObject::tr("could not write %1").arg(path);
        return false;
    }
    return true;
}

}  // namespace RadioChart

#include "fieldplot.h"
#include <QCoreApplication>
#include "uicolors.h"
#include "windowgeometry.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "schema/schemadecoder.h"

#include <QCloseEvent>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFileDialog>
#include <QKeyEvent>
#include <QSaveFile>
#include <QMenu>
#include <QPainterPath>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// ============================== extraction =================================

double parseFieldNumber(const QString &display, bool *ok)
{
    if (ok) *ok = false;
    const QString s = display.trimmed();
    if (s.isEmpty()) return 0.0;

    // Whole string is a number: the common case, and the only one where a
    // trailing-garbage check is worth skipping.
    bool conv = false;
    const double whole = s.toDouble(&conv);
    if (conv) { if (ok) *ok = true; return whole; }

    // Hex, as the schema renders raw and address-like fields.
    if (s.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        const qint64 v = s.mid(2).toLongLong(&conv, 16);
        if (conv) { if (ok) *ok = true; return double(v); }
    }

    // Leading number with a unit or annotation after it ("42 km/h",
    // "-3.5 m", "7 (RESERVED)"). Scanned by hand rather than with a regex
    // because this runs once per row per replot.
    int i = 0;
    const int n = s.size();
    if (i < n && (s.at(i) == QLatin1Char('-') || s.at(i) == QLatin1Char('+'))) ++i;
    int digits = 0;
    while (i < n && s.at(i).isDigit()) { ++i; ++digits; }
    if (i < n && s.at(i) == QLatin1Char('.')) {
        ++i;
        while (i < n && s.at(i).isDigit()) { ++i; ++digits; }
    }
    if (digits == 0) return 0.0;

    // Require a real separator after the digits. A letter glued straight on
    // means an identifier or a token ("12ABC", and the "0x"/"0xZZ" cases
    // that fall through from the hex branch above), not a measurement — and
    // treating those as numbers would put fabricated points on the plot,
    // which is worse than reporting them as non-numeric. Units therefore
    // have to be separated by a space, as the schema renders them.
    if (i < n && !s.at(i).isSpace()
        && s.at(i) != QLatin1Char('(') && s.at(i) != QLatin1Char('%')) {
        return 0.0;
    }
    const double lead = s.left(i).toDouble(&conv);
    if (!conv) return 0.0;
    if (ok) *ok = true;
    return lead;
}

namespace {

// Decode one entry the same way the field inspector does.
//
// Going through parseLine()/describe() rather than handing payload bytes to
// the schema is not a stylistic choice: the schema selects a packet by
// `captype`, which comes from the @<type>_<loco>_<ctrl> token in the
// message TEXT. kavach.xml defines no `pkt_type==` packets, so decoding
// bytes with an empty captype matches nothing, ever. Anything that decodes
// has to start from the text.
inline QVector<FieldRow> decodeEntry(const LogEntryPtr &e)
{
    if (!e) return {};
    const CaptureLine cap = CaptureDecoder::parseLine(e->text);
    if (!cap.valid || cap.bytes.isEmpty()) return {};
    return CaptureDecoder::describe(cap);
}

}  // namespace

QStringList discoverFieldNames(const LogModel *model,
                               const Schema::Decoder &decoder,
                               int sampleRows)
{
    QStringList out;
    if (!model || !decoder.isLoaded() || sampleRows <= 0) return out;

    Q_UNUSED(decoder);
    QSet<QString> seen;
    const int n = model->count();
    int looked = 0;

    // Sample from across the model, not just the head: a tab often starts
    // with one message type and only later carries the one you care about.
    const int stride = qMax(1, n / qMax(1, sampleRows));
    for (int i = 0; i < n && looked < sampleRows; i += stride) {
        LogEntryPtr e = model->entryAt(i);
        const QVector<FieldRow> rows = decodeEntry(e);
        if (rows.isEmpty()) continue;
        ++looked;
        for (const FieldRow &r : rows) {
            const QString name = r.field.trimmed();
            if (name.isEmpty()) continue;
            if (!seen.contains(name)) { seen.insert(name); out << name; }
        }
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

QString FieldSeries::label() const
{
    if (typeToken.isEmpty()) return fieldName;
    return QStringLiteral("%1 \u25B8 %2").arg(typeToken, fieldName);
}

QString captureTypeOf(const QString &text)
{
    // "@lsrp_1_1 2026-…" -> "lsrp"; "@nms_hlth_1_1" -> "nms_hlth". The
    // same rule as CaptureDecoder::parseLine (the last two '_' parts are
    // loco and controller), without splitting the whole line.
    int start = 0;
    const int n = text.size();
    while (start < n && text.at(start).isSpace()) ++start;
    if (start >= n || text.at(start) != QLatin1Char('@')) return QString();
    int end = start + 1;
    while (end < n && !text.at(end).isSpace()) ++end;
    const QStringView tag = QStringView(text).mid(start + 1, end - start - 1);
    const int last = tag.lastIndexOf(QLatin1Char('_'));
    if (last <= 0) return QString();
    const int prev = tag.left(last).lastIndexOf(QLatin1Char('_'));
    if (prev <= 0) return QString();
    return tag.left(prev).toString();
}

namespace {

// "42 km/h" -> "km/h"; "2 (Staff_Responsible)" -> label "Staff_Responsible".
void noteUnitAndLabel(const QString &display, double value, FieldSeries &out,
                      bool &unitSeen, bool &unitConflict)
{
    const QString s = display.trimmed();
    int i = 0;
    const int n = s.size();
    if (i < n && (s.at(i) == QLatin1Char('-') || s.at(i) == QLatin1Char('+'))) ++i;
    while (i < n && (s.at(i).isDigit() || s.at(i) == QLatin1Char('.'))) ++i;
    if (s.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) return;
    const QString rest = s.mid(i).trimmed();
    if (rest.isEmpty()) return;
    if (rest.startsWith(QLatin1Char('(')) && rest.endsWith(QLatin1Char(')'))) {
        const qint64 key = qint64(std::llround(value));
        if (std::fabs(value - double(key)) < 1e-9 && !out.labels.contains(key)) {
            out.labels.insert(key, rest.mid(1, rest.size() - 2).trimmed());
        }
        return;
    }
    if (rest.contains(QLatin1Char(' ')) || rest.size() > 12) return;   // prose, not a unit
    if (!unitSeen) { out.unit = rest; unitSeen = true; }
    else if (out.unit != rest) { unitConflict = true; }
}

}  // namespace

FieldSeries extractFieldSeries(const LogModel *model,
                               const QString &fieldName,
                               const Schema::Decoder &decoder,
                               int maxRows)
{
    return extractFieldSeries(model, QString(), fieldName, decoder, maxRows);
}

FieldSeries extractFieldSeries(const LogModel *model,
                               const QString &typeToken,
                               const QString &fieldName,
                               const Schema::Decoder &decoder,
                               int maxRows,
                               qint64 fromMs, qint64 toMs)
{
    FieldSeries out;
    out.typeToken = typeToken;
    out.fieldName = fieldName.trimmed();
    if (!model || fieldName.isEmpty() || !decoder.isLoaded() || maxRows <= 0) {
        return out;
    }

    Q_UNUSED(decoder);
    const int n = model->count();
    if (n <= 0) return out;

    // The row range: the whole tab, or a time window (rows arrive in time
    // order, so the ends are found by binary search).
    int lo = 0, hi = n;
    if (fromMs > 0 && toMs > fromMs) {
        auto firstAtOrAfter = [model, n](qint64 ms) {
            int a = 0, b = n;
            while (a < b) {
                const int mid = (a + b) / 2;
                const LogEntryPtr e = model->entryAt(mid);
                if (e && e->epochMs < ms) a = mid + 1; else b = mid;
            }
            return a;
        };
        lo = firstAtOrAfter(fromMs);
        hi = firstAtOrAfter(toMs + 1);
    }

    // Rows of the wanted type. Selecting them first means the row cap
    // applies to THIS packet's rows: a busy tab's 200,000 rows may hold
    // only 4,000 LSRPs, and those should all be decoded.
    QVector<int> rows;
    if (typeToken.isEmpty()) {
        rows.reserve(hi - lo);
        for (int i = lo; i < hi; ++i) rows.append(i);
    } else {
        for (int i = lo; i < hi; ++i) {
            const LogEntryPtr e = model->entryAt(i);
            if (e && captureTypeOf(e->text) == typeToken) rows.append(i);
        }
    }
    out.rowsOfType = rows.size();

    // Even sampling rather than truncation. Plotting the first maxRows of a
    // long session and labelling it with the session's name would misstate
    // the shape entirely — the interesting part is usually at the end.
    const int m = rows.size();
    const int stride = (m > maxRows) ? (m / maxRows) : 1;
    out.hitRowCap = (stride > 1);

    const QString wanted = fieldName.trimmed();
    bool first = true;
    bool unitSeen = false, unitConflict = false;

    for (int k = 0; k < m; k += stride) {
        const int i = rows.at(k);
        LogEntryPtr e = model->entryAt(i);
        if (!e) continue;
        ++out.rowsScanned;

        const QVector<FieldRow> decoded = decodeEntry(e);
        if (decoded.isEmpty()) continue;
        ++out.rowsDecoded;

        for (const FieldRow &r : decoded) {
            if (r.field.trimmed() != wanted) continue;
            ++out.rowsMatched;

            bool ok = false;
            const double v = parseFieldNumber(r.value, &ok);
            if (!ok) { ++out.rowsNonNumeric; break; }
            noteUnitAndLabel(r.value, v, out, unitSeen, unitConflict);

            FieldSeries::Point p;
            p.epochMs = e->epochMs;
            p.value   = v;
            p.row     = i;
            out.points.append(p);

            if (first) {
                out.minValue = out.maxValue = v;
                out.minMs    = out.maxMs    = e->epochMs;
                first = false;
            } else {
                out.minValue = qMin(out.minValue, v);
                out.maxValue = qMax(out.maxValue, v);
                out.minMs    = qMin(out.minMs, e->epochMs);
                out.maxMs    = qMax(out.maxMs, e->epochMs);
            }
            break;      // first occurrence per frame
        }
    }
    if (unitConflict) out.unit.clear();   // a unit that changes is no unit
    return out;
}

QVector<RowFields> collectRowFields(const LogModel *model, const QString &typeToken,
                                    const QStringList &fields, int maxRows,
                                    qint64 fromMs, qint64 toMs, bool *hitCap)
{
    QVector<RowFields> out;
    if (hitCap) *hitCap = false;
    if (!model || typeToken.isEmpty() || fields.isEmpty() || maxRows <= 0) return out;
    const int n = model->count();
    int lo = 0, hi = n;
    if (fromMs > 0 && toMs > fromMs) {
        auto firstAtOrAfter = [model, n](qint64 ms) {
            int a = 0, b = n;
            while (a < b) {
                const int mid = (a + b) / 2;
                const LogEntryPtr e = model->entryAt(mid);
                if (e && e->epochMs < ms) a = mid + 1; else b = mid;
            }
            return a;
        };
        lo = firstAtOrAfter(fromMs);
        hi = firstAtOrAfter(toMs + 1);
    }
    QVector<int> rows;
    for (int i = lo; i < hi; ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (e && captureTypeOf(e->text) == typeToken) rows.append(i);
    }
    const int stride = rows.size() > maxRows ? rows.size() / maxRows : 1;
    if (hitCap) *hitCap = stride > 1;
    QSet<QString> wanted;
    for (const QString &f : fields) wanted.insert(f.trimmed());
    for (int k = 0; k < rows.size(); k += stride) {
        const LogEntryPtr e = model->entryAt(rows.at(k));
        if (!e) continue;
        const CaptureLine cap = CaptureDecoder::parseLine(e->text);
        if (!cap.valid || cap.bytes.isEmpty()) continue;
        QHash<QString, qint64> raw;
        const QVector<FieldRow> decoded = CaptureDecoder::describe(cap, nullptr, 0, &raw);
        RowFields rf;
        rf.row = rows.at(k);
        rf.epochMs = e->epochMs;
        for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
            if (wanted.contains(it.key())) rf.raw.insert(it.key(), it.value());
        }
        for (const FieldRow &r : decoded) {
            const QString name = r.field.trimmed();
            if (wanted.contains(name) && !rf.display.contains(name)) rf.display.insert(name, r.value.trimmed());
        }
        if (!rf.raw.isEmpty()) out.append(rf);
    }
    return out;
}

QStringList FieldCatalogue::typesWith(const QString &field) const
{
    QStringList out;
    for (const QString &t : types) {
        if (fields.value(t).contains(field)) out << t;
    }
    std::stable_sort(out.begin(), out.end(), [this](const QString &a, const QString &b) {
        return rowCounts.value(a) > rowCounts.value(b);
    });
    return out;
}

FieldCatalogue discoverFieldCatalogue(const LogModel *model,
                                      const Schema::Decoder &decoder,
                                      int decodesPerType, int maxScan)
{
    FieldCatalogue cat;
    if (!model || !decoder.isLoaded() || decodesPerType <= 0) return cat;
    const int n = model->count();
    if (n <= 0) return cat;

    // Counting is cheap (the tag only) and done on every row up to maxScan,
    // spread across the tab. Decoding is the cost, so each type is decoded a
    // few times, at rows spread across its occurrences rather than its first
    // few (a field that is only filled later would otherwise be missed).
    const int stride = qMax(1, n / qMax(1, maxScan));
    QHash<QString, QVector<int>> rowsOf;
    for (int i = 0; i < n; i += stride) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e) continue;
        const QString t = captureTypeOf(e->text);
        if (t.isEmpty()) continue;
        rowsOf[t].append(i);
    }
    for (auto it = rowsOf.constBegin(); it != rowsOf.constEnd(); ++it) {
        const QVector<int> &rows = it.value();
        QStringList names;
        QSet<QString> seen;
        const int tries = qMin(rows.size(), decodesPerType * 3);
        int decodedOk = 0;
        for (int k = 0; k < tries && decodedOk < decodesPerType; ++k) {
            const int idx = rows.at(int(qint64(k) * rows.size() / qMax(1, tries)));
            const QVector<FieldRow> decoded = decodeEntry(model->entryAt(idx));
            if (decoded.isEmpty()) continue;
            ++decodedOk;
            for (const FieldRow &r : decoded) {
                const QString name = r.field.trimmed();
                if (name.isEmpty() || seen.contains(name)) continue;
                seen.insert(name);
                names << name;
            }
        }
        if (names.isEmpty()) continue;      // a type the schema cannot decode
        names.sort(Qt::CaseInsensitive);
        cat.types << it.key();
        cat.fields.insert(it.key(), names);
        cat.rowCounts.insert(it.key(), rows.size() * stride);
    }
    cat.types.sort(Qt::CaseInsensitive);
    return cat;
}

QVector<qint64> niceTimeTicks(qint64 loMs, qint64 hiMs, int target, qint64 *stepMs)
{
    QVector<qint64> out;
    if (hiMs <= loMs || target < 2) { if (stepMs) *stepMs = 1000; return out; }
    static const qint64 steps[] = {
        1, 2, 5, 10, 20, 50, 100, 200, 500,
        1000, 2000, 5000, 10000, 15000, 30000,
        60000, 120000, 300000, 600000, 900000, 1800000,
        3600000, 7200000, 10800000, 21600000, 43200000, 86400000 };
    const qint64 span = hiMs - loMs;
    qint64 step = steps[sizeof(steps) / sizeof(steps[0]) - 1];
    for (qint64 cand : steps) {
        if (span / cand <= target) { step = cand; break; }
    }
    if (stepMs) *stepMs = step;
    // Aligned to local wall-clock multiples, so 10:00:30 not 10:00:27.
    const qint64 offset = qint64(QDateTime::fromMSecsSinceEpoch(loMs).offsetFromUtc()) * 1000;
    qint64 t = ((loMs + offset + step - 1) / step) * step - offset;
    for (; t <= hiMs && out.size() < 64; t += step) out << t;
    return out;
}

// ============================== canvas =====================================


QVector<double> niceTicks(double lo, double hi, int target, bool integerOnly)
{
    QVector<double> out;
    if (target < 2) { target = 2; }
    if (!(hi >= lo)) { return out; }

    if (hi - lo <= 0.0) {
        // A flat series still deserves its one value labelled — an axis
        // with no numbers on it says nothing about what is being drawn.
        out << lo;
        return out;
    }

    const double raw  = (hi - lo) / double(target - 1);
    const double mag  = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    double step = (norm <= 1.0 ? 1.0 : norm <= 2.0 ? 2.0 : norm <= 5.0 ? 5.0 : 10.0) * mag;
    const double niceStep = step;

    // An integer field must not be labelled in fractions of a value it
    // cannot take — and where the whole range fits in a handful of steps of
    // 1, that is the labelling the operator wants: LOCO_MODE running 1..6
    // should read 1,2,3,4,5,6, not 2,4,6 with the value 1 unlabelled.
    if (integerOnly) {
        step = 0.0;
        for (double cand : { 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0 }) {
            if (std::floor((hi - lo) / cand) + 1 <= double(target) + 1.0) { step = cand; break; }
        }
        // Past 500, the same 1-2-5 step as a continuous value (session 147):
        // range / (target - 1) labelled ABS_LOCO_LOC 36121, 72242, 108363.
        if (step <= 0.0) { step = qMax(1.0, std::ceil(niceStep)); }
    }

    for (;;) {
        out.clear();
        const double first = std::ceil(lo / step - 1e-9) * step;
        for (double v = first; v <= hi + step * 1e-9; v += step) {
            // Snap away the accumulated error, or a 0.1 step produces
            // 0.30000000000000004 in the label.
            out << (integerOnly ? std::floor(v + 0.5) : std::round(v / step) * step);
            if (out.size() > 64) { break; }          // runaway guard
        }
        // Two labels at least (session 147): rounded up to 200000 over a
        // 0..190000 lane, the step left one, "0", on the axis. Down the
        // 1-2-5 ladder until two land.
        if (out.size() >= 2) { break; }
        const double m = std::pow(10.0, std::floor(std::log10(step) + 1e-9));
        const double n = step / m;
        const double smaller = (n > 4.5 ? 2.0 * m : n > 1.5 ? 1.0 * m : 0.5 * m);
        if (integerOnly && smaller < 1.0) { break; }
        step = smaller;
    }
    return out;
}

int tickDecimals(double step)
{
    if (step <= 0.0) { return 0; }
    if (std::fabs(step - std::floor(step)) < 1e-9) { return 0; }
    const int d = int(std::ceil(-std::log10(step))) + 1;
    return qBound(1, d, 6);
}

// =============================================================================
//  Shared cursor and CSV (session 81)
// =============================================================================

CursorValue valueAtOrBefore(const FieldSeries &s, qint64 ms, qint64 maxAgeMs)
{
    CursorValue out;
    const QVector<FieldSeries::Point> &pts = s.points;
    int lo = 0, hi = pts.size();
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (pts.at(mid).epochMs <= ms) lo = mid + 1; else hi = mid;
    }
    const int i = lo - 1;
    if (i < 0) return out;
    if (maxAgeMs >= 0 && ms - pts.at(i).epochMs > maxAgeMs) return out;
    out.has = true;
    out.value = pts.at(i).value;
    out.atMs = pts.at(i).epochMs;
    out.row = pts.at(i).row;
    return out;
}

QString seriesToCsv(const QVector<FieldSeries> &list, qint64 fromMs, qint64 toMs)
{
    // One row per distinct sample time. A cell is filled only where that
    // series HAS a sample at that time: carrying values forward would put
    // numbers in the file that no frame contained.
    QMap<qint64, QVector<QString>> rows;
    for (int k = 0; k < list.size(); ++k) {
        for (const auto &pt : list.at(k).points) {
            if (pt.epochMs < fromMs || pt.epochMs > toMs) continue;
            QVector<QString> &cells = rows[pt.epochMs];
            if (cells.size() < list.size()) cells.resize(list.size());
            cells[k] = QString::number(pt.value, 'g', 12);
        }
    }
    auto quote = [](const QString &t) {
        if (!t.contains(QLatin1Char(',')) && !t.contains(QLatin1Char('"'))) return t;
        QString q = t;
        q.replace(QLatin1String("\""), QLatin1String("\"\""));
        return QStringLiteral("\"%1\"").arg(q);
    };
    QStringList header{ QStringLiteral("time_local"), QStringLiteral("epoch_ms") };
    for (const FieldSeries &s : list) {
        QString name = s.label();
        name.replace(QStringLiteral(" \u25B8 "), QStringLiteral("."));
        if (!s.unit.isEmpty()) name += QStringLiteral(" (%1)").arg(s.unit);
        header << quote(name);
    }
    QString out = header.join(QLatin1Char(',')) + QLatin1Char('\n');
    for (auto it = rows.constBegin(); it != rows.constEnd(); ++it) {
        QStringList line{ QDateTime::fromMSecsSinceEpoch(it.key()).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                          QString::number(it.key()) };
        QVector<QString> cells = it.value();
        cells.resize(list.size());
        for (const QString &c : cells) line << c;
        out += line.join(QLatin1Char(',')) + QLatin1Char('\n');
    }
    return out;
}

// =============================================================================
//  Canvas: several series, in lanes or overlaid, on one time axis
//  (session 80 single series; session 81 several)
// =============================================================================

namespace {

// "1 point" / "588 points" (the status read "588 point(s) from 588 decoded row(s)").
QString countOf(int n, const char *one, const char *many)
{
    return QStringLiteral("%1 %2").arg(n).arg(QCoreApplication::translate("FieldPlotWindow", n == 1 ? one : many));
}

const double kValuePad = 0.06;
const int    kDragThreshold = 6;     // px before a press becomes a drag
const double kWheelFactor = 1.25;    // per notch
const qint64 kMinTimeSpanMs = 50;    // closest zoom
const int    kLaneGap = 10;          // px between lanes

}  // namespace

FieldPlotCanvas::FieldPlotCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(220);
    setFocusPolicy(Qt::WheelFocus);
    UiColor::onThemeChange(this, [this]() { update(); });
}

const FieldSeries &FieldPlotCanvas::series() const
{
    static const FieldSeries empty;
    return m_list.isEmpty() ? empty : m_list.first();
}

void FieldPlotCanvas::setSeries(const FieldSeries &s, bool keepView)
{
    setSeriesList(QVector<FieldSeries>{ s }, keepView);
}

bool FieldPlotCanvas::isWholeSeries(int i) const
{
    return i >= 0 && i < m_whole.size() && m_whole.at(i);
}

void FieldPlotCanvas::setSeriesList(const QVector<FieldSeries> &list, bool keepView)
{
    const double x0 = m_x0, x1 = m_x1;
    QVector<Lane> oldLanes = m_lanes;
    m_list = list;
    m_whole.clear();
    for (const FieldSeries &s : m_list) {
        bool whole = true;
        for (const auto &pt : s.points) {
            if (std::fabs(pt.value - std::floor(pt.value)) > 1e-9) { whole = false; break; }
        }
        m_whole << whole;
    }
    m_hoverSeries = m_hoverPoint = -1;
    rebuildLanes();
    if (keepView && x1 > x0) {
        m_x0 = x0; m_x1 = x1;
        for (int i = 0; i < m_lanes.size() && i < oldLanes.size(); ++i) {
            if (oldLanes.at(i).yManual) {
                m_lanes[i].y0 = oldLanes.at(i).y0;
                m_lanes[i].y1 = oldLanes.at(i).y1;
                m_lanes[i].yManual = true;
            }
        }
        fitValues();
    } else {
        fullView();
    }
    update();
}

void FieldPlotCanvas::setOverlay(bool on)
{
    if (m_overlay == on) return;
    m_overlay = on;
    rebuildLanes();
    fitValues();
    update();
}

int FieldPlotCanvas::laneCount() const { return m_lanes.size(); }

void FieldPlotCanvas::rebuildLanes()
{
    m_lanes.clear();
    auto titleOf = [this](int i) {
        const FieldSeries &s = m_list.at(i);
        return s.unit.isEmpty() ? s.fieldName : QStringLiteral("%1 (%2)").arg(s.fieldName, s.unit);
    };
    if (m_list.isEmpty()) return;
    if (m_overlay || m_list.size() == 1) {
        Lane lane;
        for (int i = 0; i < m_list.size(); ++i) {
            lane.members << i;
            lane.allWhole = lane.allWhole && isWholeSeries(i);
        }
        if (m_list.size() == 1) {
            lane.title = titleOf(0);
            lane.labels = m_list.first().labels;
        } else {
            // One axis for several fields: titled by the unit they share,
            // or said plainly that they do not share one.
            QSet<QString> units;
            for (const FieldSeries &s : m_list) units.insert(s.unit);
            lane.title = units.size() == 1 && !m_list.first().unit.isEmpty()
                             ? m_list.first().unit
                             : QObject::tr("value (mixed units)");
        }
        m_lanes << lane;
        return;
    }
    for (int i = 0; i < m_list.size(); ++i) {
        Lane lane;
        lane.members << i;
        lane.allWhole = isWholeSeries(i);
        lane.title = titleOf(i);
        lane.labels = m_list.at(i).labels;
        m_lanes << lane;
    }
}

qint64 FieldPlotCanvas::dataMinMs() const
{
    qint64 lo = 0; bool any = false;
    for (const FieldSeries &s : m_list) {
        if (s.isEmpty()) continue;
        lo = any ? qMin(lo, s.minMs) : s.minMs; any = true;
    }
    return lo;
}

qint64 FieldPlotCanvas::dataMaxMs() const
{
    qint64 hi = 0; bool any = false;
    for (const FieldSeries &s : m_list) {
        if (s.isEmpty()) continue;
        hi = any ? qMax(hi, s.maxMs) : s.maxMs; any = true;
    }
    return hi;
}

void FieldPlotCanvas::fullView()
{
    for (Lane &lane : m_lanes) lane.yManual = false;
    bool any = false;
    for (const FieldSeries &s : m_list) any = any || !s.isEmpty();
    if (!any) { m_x0 = 0; m_x1 = 1; return; }
    m_x0 = double(dataMinMs());
    m_x1 = double(dataMaxMs());
    if (m_x1 - m_x0 < 1000.0) { m_x0 -= 500.0; m_x1 += 500.0; }
    fitValues();
}

void FieldPlotCanvas::fitLane(Lane &lane)
{
    if (lane.yManual) return;
    bool any = false;
    double lo = 0, hi = 0;
    for (int idx : lane.members) {
        for (const auto &pt : m_list.at(idx).points) {
            if (pt.epochMs < m_x0 || pt.epochMs > m_x1) continue;
            if (!any) { lo = hi = pt.value; any = true; }
            else { lo = qMin(lo, pt.value); hi = qMax(hi, pt.value); }
        }
    }
    if (!any) {
        for (int idx : lane.members) {
            const FieldSeries &s = m_list.at(idx);
            if (s.isEmpty()) continue;
            if (!any) { lo = s.minValue; hi = s.maxValue; any = true; }
            else { lo = qMin(lo, s.minValue); hi = qMax(hi, s.maxValue); }
        }
    }
    if (!any) { lane.y0 = 0; lane.y1 = 1; return; }
    if (hi - lo <= 0.0) {
        const double half = lane.allWhole ? 1.0 : qMax(1e-6, std::fabs(lo) * 0.1 + 0.5);
        lo -= half; hi += half;
    } else {
        const double pad = (hi - lo) * kValuePad;
        lo -= lane.allWhole ? qMax(0.5, pad) : pad;
        hi += lane.allWhole ? qMax(0.5, pad) : pad;
    }
    lane.y0 = lo; lane.y1 = hi;
}

void FieldPlotCanvas::fitValues()
{
    for (Lane &lane : m_lanes) fitLane(lane);
}

double FieldPlotCanvas::viewMinValue(int lane) const
{
    return lane >= 0 && lane < m_lanes.size() ? m_lanes.at(lane).y0 : 0.0;
}

double FieldPlotCanvas::viewMaxValue(int lane) const
{
    return lane >= 0 && lane < m_lanes.size() ? m_lanes.at(lane).y1 : 0.0;
}

bool FieldPlotCanvas::isZoomed() const
{
    bool any = false;
    for (const FieldSeries &s : m_list) any = any || !s.isEmpty();
    if (!any) return false;
    for (const Lane &lane : m_lanes) if (lane.yManual) return true;
    return m_x0 > double(dataMinMs()) + 0.5 || m_x1 < double(dataMaxMs()) - 0.5;
}

void FieldPlotCanvas::setView(double x0, double x1)
{
    bool any = false;
    for (const FieldSeries &s : m_list) any = any || !s.isEmpty();
    if (!any) return;
    const double dataLo = double(dataMinMs());
    const double dataHi = double(qMax(dataMaxMs(), dataMinMs() + 1000));
    double span = qMax(double(kMinTimeSpanMs), x1 - x0);
    span = qMin(span, (dataHi - dataLo) * 1.02 + 1000.0);
    double c = (x0 + x1) / 2.0;
    c = qBound(dataLo - span * 0.01, c, dataHi + span * 0.01);
    m_x0 = c - span / 2.0;
    m_x1 = c + span / 2.0;
    if (m_x0 < dataLo - span * 0.01) { m_x1 += (dataLo - m_x0); m_x0 = dataLo; }
    if (m_x1 > dataHi + span * 0.01) { m_x0 -= (m_x1 - dataHi); m_x1 = dataHi; }
    fitValues();
    m_hoverSeries = m_hoverPoint = -1;
    update();
    announceView();
}

void FieldPlotCanvas::announceView() { emit viewChanged(qint64(m_x0), qint64(m_x1)); }

void FieldPlotCanvas::setTimeView(qint64 fromMs, qint64 toMs)
{
    for (Lane &lane : m_lanes) lane.yManual = false;
    setView(double(fromMs), double(toMs));
}

void FieldPlotCanvas::zoomIn()
{
    const double c = (m_x0 + m_x1) / 2.0, h = (m_x1 - m_x0) / 4.0;
    setView(c - h, c + h);
}

void FieldPlotCanvas::zoomOut()
{
    for (Lane &lane : m_lanes) lane.yManual = false;
    const double c = (m_x0 + m_x1) / 2.0, h = (m_x1 - m_x0);
    setView(c - h, c + h);
}

void FieldPlotCanvas::resetZoom()
{
    fullView();
    m_hoverSeries = m_hoverPoint = -1;
    update();
    announceView();
}

QString FieldPlotCanvas::valueText(const Lane &lane, double v, int decimals) const
{
    QString text = QString::number(v, 'f', decimals);
    if (lane.allWhole && !lane.labels.isEmpty()) {
        const QString name = lane.labels.value(qint64(std::llround(v)));
        if (!name.isEmpty()) text += QStringLiteral(" ") + name;
    }
    return text;
}

int FieldPlotCanvas::leftMargin() const
{
    QFont small = font();
    small.setPointSizeF(qMax(6.0, small.pointSizeF() - 1.0));
    const QFontMetrics fm(small);
    int widest = fm.horizontalAdvance(QStringLiteral("-0000"));
    for (const Lane &lane : m_lanes) {
        const QVector<double> ticks = niceTicks(lane.y0, lane.y1, 6, lane.allWhole);
        const double step = ticks.size() > 1 ? ticks.at(1) - ticks.at(0) : 1.0;
        const int dec = lane.allWhole ? 0 : tickDecimals(step);
        for (double v : ticks) widest = qMax(widest, fm.horizontalAdvance(valueText(lane, v, dec)));
    }
    return qMin(widest, 170) + 12 + fontMetrics().height() + 6;
}

QRect FieldPlotCanvas::plotRect() const
{
    const int line = fontMetrics().height();
    // The legend: one line per two series above the plot.
    const int legendLines = qMax(1, (m_list.size() + 1) / 2);
    return rect().adjusted(leftMargin(), legendLines * (line + 4) + 8, -14, -(line * 2 + 12));
}

QRect FieldPlotCanvas::laneRect(int lane) const
{
    const QRect r = plotRect();
    const int n = qMax(1, m_lanes.size());
    const int h = (r.height() - kLaneGap * (n - 1)) / n;
    return QRect(r.left(), r.top() + lane * (h + kLaneGap), r.width(), h);
}

int FieldPlotCanvas::laneAt(int py) const
{
    for (int i = 0; i < m_lanes.size(); ++i) {
        const QRect lr = laneRect(i);
        if (py >= lr.top() - kLaneGap / 2 && py <= lr.bottom() + kLaneGap / 2) return i;
    }
    return m_lanes.isEmpty() ? -1 : (py < plotRect().top() ? 0 : m_lanes.size() - 1);
}

double FieldPlotCanvas::xOf(qint64 ms) const
{
    const QRect r = plotRect();
    return r.left() + (double(ms) - m_x0) / qMax(1e-9, m_x1 - m_x0) * r.width();
}

double FieldPlotCanvas::yOf(const Lane &lane, int laneIndex, double v) const
{
    const QRect r = laneRect(laneIndex);
    return r.bottom() - (v - lane.y0) / qMax(1e-12, lane.y1 - lane.y0) * r.height();
}

qint64 FieldPlotCanvas::msAt(int px) const
{
    const QRect r = plotRect();
    return qint64(m_x0 + double(px - r.left()) / qMax(1, r.width()) * (m_x1 - m_x0));
}

double FieldPlotCanvas::valueAt(int laneIndex, int py) const
{
    if (laneIndex < 0 || laneIndex >= m_lanes.size()) return 0.0;
    const Lane &lane = m_lanes.at(laneIndex);
    const QRect r = laneRect(laneIndex);
    return lane.y0 + double(r.bottom() - py) / qMax(1, r.height()) * (lane.y1 - lane.y0);
}

void FieldPlotCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().base());

    const QColor ink   = UiColor::muted();
    const QColor text  = palette().color(QPalette::Text);
    const QColor frame = UiColor::frame();
    const QColor grid  = UiColor::grid();
    auto colourOf = [](int i) { return UiColor::series(i); };

    const QRect r = plotRect();
    bool any = false;
    for (const FieldSeries &s : m_list) any = any || !s.isEmpty();
    if (!any) {
        p.setPen(frame);
        p.drawRect(r);
        p.setPen(ink);
        p.drawText(rect(), Qt::AlignCenter, tr("No numeric samples for this field"));
        return;
    }

    QFont small = p.font();
    small.setPointSizeF(qMax(6.0, small.pointSizeF() - 1.0));
    QFont titleFont = p.font();
    titleFont.setBold(true);

    // ---- time ticks (shared) ----------------------------------------------------
    qint64 tStep = 1000;
    const QVector<qint64> tTicks = niceTimeTicks(qint64(m_x0), qint64(m_x1),
                                                 qMax(2, r.width() / 110), &tStep);
    const QString tFormat = tStep < 1000 ? QStringLiteral("HH:mm:ss.zzz") : QStringLiteral("HH:mm:ss");

    int visibleTotal = 0;
    for (int li = 0; li < m_lanes.size(); ++li) {
        const Lane &lane = m_lanes.at(li);
        const QRect lr = laneRect(li);

        // value axis
        p.setFont(small);
        const QVector<double> vTicks = niceTicks(lane.y0, lane.y1, qMax(2, qMin(6, lr.height() / 28)), lane.allWhole);
        const double vStep = vTicks.size() > 1 ? (vTicks.at(1) - vTicks.at(0)) : 1.0;
        const int dec = lane.allWhole ? 0 : tickDecimals(vStep);
        const int labelRight = lr.left() - 8;
        const int labelLeft  = fontMetrics().height() + 8;
        for (double v : vTicks) {
            if (v < lane.y0 || v > lane.y1) continue;
            const int y = int(std::lround(yOf(lane, li, v)));
            p.setPen(grid);
            p.drawLine(lr.left() + 1, y, lr.right() - 1, y);
            // Stacked lanes: a label on a lane's very edge would run into the
            // neighbouring lane's; the grid line is enough there.
            if (m_lanes.size() > 1 && (y - lr.top() < 7 || lr.bottom() - y < 7)) continue;
            p.setPen(ink);
            p.drawText(QRect(labelLeft, y - 9, labelRight - labelLeft, 18), Qt::AlignRight | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(valueText(lane, v, dec), Qt::ElideRight, labelRight - labelLeft));
        }
        for (qint64 t : tTicks) {
            const int x = int(std::lround(xOf(t)));
            if (x < lr.left() || x > lr.right()) continue;
            p.setPen(grid);
            p.drawLine(x, lr.top() + 1, x, lr.bottom() - 1);
        }
        p.setPen(frame);
        p.setBrush(Qt::NoBrush);
        p.drawRect(lr);

        // lane title, rotated, in the lane's own colour when it is one series
        p.setFont(titleFont);
        p.setPen(lane.members.size() == 1 && m_lanes.size() > 1 ? colourOf(lane.members.first()) : text);
        p.save();
        p.translate(4, lr.center().y());
        p.rotate(-90);
        const int titleLen = qMax(40, lr.height() + (m_lanes.size() == 1 ? 40 : 0));
        p.drawText(QRect(-titleLen / 2, 0, titleLen, fontMetrics().height() + 2), Qt::AlignCenter,
                   p.fontMetrics().elidedText(lane.title, Qt::ElideMiddle, titleLen));
        p.restore();

        // traces: steps for states, lines for measurements, no line over gaps
        p.setClipRect(lr.adjusted(1, 1, -1, -1));
        for (int idx : lane.members) {
            const QVector<FieldSeries::Point> &pts = m_list.at(idx).points;
            if (pts.isEmpty()) continue;
            const bool step = isWholeSeries(idx);
            const QColor c = colourOf(idx);
            p.setRenderHint(QPainter::Antialiasing, !step);
            p.setPen(QPen(c, 1.6));
            QVector<qint64> gaps;
            for (int i = 1; i < pts.size(); ++i) gaps << (pts.at(i).epochMs - pts.at(i - 1).epochMs);
            qint64 typical = 1000;
            if (!gaps.isEmpty()) {
                std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
                typical = qMax<qint64>(1, gaps.at(gaps.size() / 2));
            }
            const qint64 gapMs = qMax<qint64>(3000, typical * 10);
            int first = 0, last = pts.size() - 1;
            while (first < pts.size() - 1 && pts.at(first + 1).epochMs < m_x0) ++first;
            while (last > 0 && pts.at(last - 1).epochMs > m_x1) --last;
            QPainterPath path;
            bool open = false;
            int visible = 0;
            for (int i = first; i <= last; ++i) {
                const auto &pt = pts.at(i);
                const QPointF at(xOf(pt.epochMs), yOf(lane, li, pt.value));
                if (pt.epochMs >= m_x0 && pt.epochMs <= m_x1) ++visible;
                if (!open || (i > 0 && pt.epochMs - pts.at(i - 1).epochMs > gapMs)) {
                    path.moveTo(at); open = true; continue;
                }
                if (step) path.lineTo(QPointF(at.x(), path.currentPosition().y()));
                path.lineTo(at);
            }
            p.drawPath(path);
            visibleTotal += visible;
            if (visible > 0 && double(lr.width()) / visible >= 6.0) {
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setBrush(c);
                p.setPen(Qt::NoPen);
                for (int i = first; i <= last; ++i) {
                    const auto &pt = pts.at(i);
                    p.drawEllipse(QPointF(xOf(pt.epochMs), yOf(lane, li, pt.value)), 2.3, 2.3);
                }
                p.setBrush(Qt::NoBrush);
            }
        }
        p.setClipping(false);
    }

    // ---- time tick labels and title, under the last lane --------------------------
    p.setFont(small);
    const int tickTextY = r.bottom() + 4 + p.fontMetrics().ascent();
    for (qint64 t : tTicks) {
        const int x = int(std::lround(xOf(t)));
        if (x < r.left() || x > r.right()) continue;
        p.setPen(frame);
        p.drawLine(x, r.bottom(), x, r.bottom() + 3);
        const QString label = QDateTime::fromMSecsSinceEpoch(t).toString(tFormat);
        const int w = p.fontMetrics().horizontalAdvance(label);
        p.setPen(ink);
        p.drawText(qBound(r.left() - w / 2, x - w / 2, width() - w - 2), tickTextY, label);
    }
    p.setFont(titleFont);
    p.setPen(text);
    const QString date = QDateTime::fromMSecsSinceEpoch(qint64(m_x0)).toString(QStringLiteral("yyyy-MM-dd"));
    p.drawText(QRect(r.left(), height() - fontMetrics().height() - 6, r.width(), fontMetrics().height() + 4),
               Qt::AlignHCenter | Qt::AlignVCenter, tr("Time (local, %1)").arg(date));

    // ---- rubber band ------------------------------------------------------------------
    if (m_drag == Drag::Box) {
        const QRect lr = laneRect(m_dragLane);
        QRect band = QRect(m_dragStart, m_dragNow).normalized().intersected(lr);
        if (std::abs(m_dragNow.y() - m_dragStart.y()) < 20) {
            band = QRect(QPoint(band.left(), r.top()), QPoint(band.right(), r.bottom()));
        }
        QColor fill = UiColor::series(0);
        fill.setAlpha(40);
        p.fillRect(band, fill);
        p.setPen(QPen(UiColor::series(0), 1, Qt::DashLine));
        p.drawRect(band);
    }

    // ---- legend: two entries per line ----------------------------------------------------
    p.setFont(font());
    const QFontMetrics lfm = p.fontMetrics();
    const int lh = lfm.height() + 4;
    int zoomWidth = 0;
    if (isZoomed()) {
        const QString z = tr("zoomed · double-click to fit");
        zoomWidth = lfm.horizontalAdvance(z) + 16;
        p.setPen(ink);
        p.drawText(QRect(r.right() - zoomWidth, 4, zoomWidth, lh), Qt::AlignRight | Qt::AlignVCenter, z);
    }
    const int colWidth = (r.width() - zoomWidth) / (m_list.size() > 1 ? 2 : 1);
    for (int i = 0; i < m_list.size(); ++i) {
        const FieldSeries &s = m_list.at(i);
        const int lx = r.left() + (i % 2) * colWidth;
        const int ly = 4 + (i / 2) * lh;
        p.setPen(QPen(colourOf(i), 2.5));
        p.drawLine(lx, ly + lh / 2, lx + 20, ly + lh / 2);
        p.setPen(text);
        QString legend = s.label();
        if (!s.unit.isEmpty()) legend += QStringLiteral(" [%1]").arg(s.unit);
        legend += tr("  ·  %1 samples").arg(s.points.size());
        if (isWholeSeries(i) && m_list.size() == 1) legend += tr("  ·  step: held until the next sample");
        p.drawText(QRect(lx + 26, ly, colWidth - 32, lh), Qt::AlignLeft | Qt::AlignVCenter,
                   lfm.elidedText(legend, Qt::ElideRight, colWidth - 32));
    }
    Q_UNUSED(visibleTotal);

    // ---- shared cursor: a line through every lane and each series' value -----------------------
    if (m_cursorMs >= 0 && m_cursorMs >= m_x0 && m_cursorMs <= m_x1) {
        const int cx = int(std::lround(xOf(m_cursorMs)));
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(grid.darker(140), 1, Qt::DotLine));
        p.drawLine(cx, r.top() + 1, cx, r.bottom() - 1);
        QStringList lines;
        lines << QDateTime::fromMSecsSinceEpoch(m_cursorMs).toString(QStringLiteral("HH:mm:ss.zzz"));
        const qint64 window = qint64((m_x1 - m_x0) / qMax(1, r.width()) * 40.0) + 3000;
        for (int li = 0; li < m_lanes.size(); ++li) {
            const Lane &lane = m_lanes.at(li);
            for (int idx : lane.members) {
                const CursorValue cv = valueAtOrBefore(m_list.at(idx), m_cursorMs, window);
                const FieldSeries &s = m_list.at(idx);
                // With several fields, each is named by packet too: two
                // LOCO_MODEs from two packets must not read as one.
                const QString name = m_list.size() > 1 ? s.label() : s.fieldName;
                if (!cv.has) { lines << tr("%1: —").arg(name); continue; }
                const QPointF dot(xOf(cv.atMs), yOf(lane, li, cv.value));
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(idx == m_hoverSeries ? UiColor::error() : colourOf(idx), 2));
                p.drawEllipse(dot, 4, 4);
                QString v = valueText(isWholeSeries(idx) ? lane : Lane(), cv.value, isWholeSeries(idx) ? 0 : 3);
                if (isWholeSeries(idx) && lane.labels.isEmpty() && !s.labels.isEmpty()) {
                    const QString nm = s.labels.value(qint64(std::llround(cv.value)));
                    if (!nm.isEmpty()) v += QStringLiteral(" ") + nm;
                }
                if (!s.unit.isEmpty()) v += QStringLiteral(" ") + s.unit;
                lines << QStringLiteral("%1: %2").arg(name, v);
            }
        }
        const QString txt = lines.join(QLatin1Char('\n'));
        const QRect tb = p.fontMetrics().boundingRect(QRect(0, 0, 600, 600), Qt::AlignLeft, txt);
        QRect box(0, 0, tb.width() + 14, tb.height() + 8);
        box.moveTo(cx + 12, r.top() + 6);
        if (box.right() > r.right()) box.moveRight(cx - 12);
        QColor fill = palette().color(QPalette::Base);
        fill.setAlpha(235);
        p.setBrush(fill);
        p.setPen(frame);
        p.drawRect(box);
        p.setPen(text);
        p.drawText(box.adjusted(7, 4, -7, -4), Qt::AlignLeft | Qt::AlignTop, txt);
    }
}

int FieldPlotCanvas::nearestPoint(int px, int py, int *seriesOut) const
{
    if (seriesOut) *seriesOut = -1;
    const int li = laneAt(py);
    if (li < 0 || !plotRect().adjusted(-6, -6, 6, 6).contains(px, py)) return -1;
    const Lane &lane = m_lanes.at(li);
    const qint64 at = msAt(px);
    int best = -1, bestSeries = -1;
    double bestD = 1e18;
    for (int idx : lane.members) {
        const QVector<FieldSeries::Point> &pts = m_list.at(idx).points;
        int lo = 0, hi = pts.size();
        while (lo < hi) { const int mid = (lo + hi) / 2; if (pts.at(mid).epochMs < at) lo = mid + 1; else hi = mid; }
        for (int i = qMax(0, lo - 40); i < qMin(pts.size(), lo + 40); ++i) {
            const double dx = xOf(pts.at(i).epochMs) - px;
            const double dy = yOf(lane, li, pts.at(i).value) - py;
            const double d = dx * dx * 4.0 + dy * dy;
            if (d < bestD) { bestD = d; best = i; bestSeries = idx; }
        }
    }
    if (bestD > 30.0 * 30.0 * 4.0) return -1;
    if (seriesOut) *seriesOut = bestSeries;
    return best;
}

void FieldPlotCanvas::mouseMoveEvent(QMouseEvent *ev)
{
    const QRect r = plotRect();
    if (m_drag == Drag::Box) { m_dragNow = ev->pos(); update(); return; }
    if (m_drag == Drag::Pan) {
        const double dxMs = double(ev->pos().x() - m_dragStart.x()) / qMax(1, r.width()) * (m_panX1 - m_panX0);
        if (m_dragLane >= 0 && m_dragLane < m_lanes.size() && m_lanes.at(m_dragLane).yManual) {
            const QRect lr = laneRect(m_dragLane);
            const double dyV = double(ev->pos().y() - m_dragStart.y()) / qMax(1, lr.height()) * (m_panY1 - m_panY0);
            m_lanes[m_dragLane].y0 = m_panY0 + dyV;
            m_lanes[m_dragLane].y1 = m_panY1 + dyV;
        }
        setView(m_panX0 - dxMs, m_panX1 - dxMs);
        return;
    }
    if ((ev->buttons() & (Qt::LeftButton | Qt::RightButton)) && !m_dragStart.isNull()
        && (ev->pos() - m_dragStart).manhattanLength() > kDragThreshold) {
        m_dragLane = qMax(0, laneAt(m_dragStart.y()));
        if (ev->buttons() & Qt::LeftButton) {
            m_drag = Drag::Box;
            m_dragNow = ev->pos();
        } else {
            m_drag = Drag::Pan;
            m_panX0 = m_x0; m_panX1 = m_x1;
            if (m_dragLane < m_lanes.size()) { m_panY0 = m_lanes.at(m_dragLane).y0; m_panY1 = m_lanes.at(m_dragLane).y1; }
            setCursor(Qt::ClosedHandCursor);
        }
        m_cursorMs = -1;
        update();
        return;
    }
    int s = -1;
    const int idx = nearestPoint(ev->pos().x(), ev->pos().y(), &s);
    m_hoverSeries = s;
    m_hoverPoint = idx;
    m_cursorMs = r.contains(ev->pos()) ? (idx >= 0 ? m_list.at(s).points.at(idx).epochMs : msAt(ev->pos().x())) : -1;
    update();
}

void FieldPlotCanvas::leaveEvent(QEvent *)
{
    if (m_cursorMs >= 0 || m_hoverPoint >= 0) { m_cursorMs = -1; m_hoverPoint = m_hoverSeries = -1; update(); }
}

void FieldPlotCanvas::mousePressEvent(QMouseEvent *ev)
{
    setFocus(Qt::MouseFocusReason);
    if (ev->button() == Qt::LeftButton || ev->button() == Qt::RightButton) m_dragStart = ev->pos();
}

void FieldPlotCanvas::mouseReleaseEvent(QMouseEvent *ev)
{
    const Drag was = m_drag;
    m_drag = Drag::None;
    unsetCursor();
    if (was == Drag::Box) {
        const QRect lr = laneRect(m_dragLane);
        const QRect band = QRect(m_dragStart, ev->pos()).normalized().intersected(lr);
        const bool yToo = std::abs(ev->pos().y() - m_dragStart.y()) >= 20 && band.height() >= 20;
        m_dragStart = QPoint();
        if (band.width() >= kDragThreshold) {
            if (yToo && m_dragLane < m_lanes.size()) {
                const double top = valueAt(m_dragLane, band.top());
                const double bottom = valueAt(m_dragLane, band.bottom());
                m_lanes[m_dragLane].y0 = bottom;
                m_lanes[m_dragLane].y1 = top;
                m_lanes[m_dragLane].yManual = true;
            }
            setView(double(msAt(band.left())), double(msAt(band.right())));
        }
        update();
        return;
    }
    if (was == Drag::Pan) { m_dragStart = QPoint(); return; }
    m_dragStart = QPoint();

    if (ev->button() == Qt::LeftButton) {
        int s = -1;
        const int idx = nearestPoint(ev->pos().x(), ev->pos().y(), &s);
        if (idx < 0) return;
        const auto &pt = m_list.at(s).points.at(idx);
        emit pointActivated(pt.row, pt.epochMs);
    } else if (ev->button() == Qt::RightButton) {
        QMenu menu(this);
        QAction *in  = menu.addAction(tr("Zoom in"));
        QAction *out = menu.addAction(tr("Zoom out"));
        QAction *fit = menu.addAction(tr("Fit all (double-click)"));
        fit->setEnabled(isZoomed());
        QAction *chosen = menu.exec(mapToGlobal(ev->pos()));
        if (chosen == in) zoomIn();
        else if (chosen == out) zoomOut();
        else if (chosen == fit) resetZoom();
    }
}

void FieldPlotCanvas::mouseDoubleClickEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton) resetZoom();
}

void FieldPlotCanvas::wheelEvent(QWheelEvent *ev)
{
    if (m_list.isEmpty()) return;
    const int delta = ev->angleDelta().y();
    if (delta == 0) return;
    const double factor = std::pow(kWheelFactor, -double(delta) / 120.0);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QPoint pos = ev->position().toPoint();
#else
    const QPoint pos = ev->pos();
#endif
    if (ev->modifiers() & Qt::ShiftModifier) {
        const int li = laneAt(pos.y());
        if (li < 0) return;
        Lane &lane = m_lanes[li];
        const double at = valueAt(li, pos.y());
        lane.y0 = at - (at - lane.y0) * factor;
        lane.y1 = at + (lane.y1 - at) * factor;
        lane.yManual = true;
        update();
        announceView();
    } else {
        const double at = double(msAt(pos.x()));
        setView(at - (at - m_x0) * factor, at + (m_x1 - at) * factor);
    }
    ev->accept();
}

void FieldPlotCanvas::keyPressEvent(QKeyEvent *ev)
{
    const double span = m_x1 - m_x0;
    switch (ev->key()) {
    case Qt::Key_Plus: case Qt::Key_Equal: zoomIn(); break;
    case Qt::Key_Minus: zoomOut(); break;
    case Qt::Key_0: case Qt::Key_Home: resetZoom(); break;
    case Qt::Key_Left:  setView(m_x0 - span * 0.2, m_x1 - span * 0.2); break;
    case Qt::Key_Right: setView(m_x0 + span * 0.2, m_x1 + span * 0.2); break;
    default: QWidget::keyPressEvent(ev); return;
    }
    ev->accept();
}

// ============================== window =====================================

FieldPlotWindow::FieldPlotWindow(LogModel *model, const QString &tabKey,
                                 QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_model(model)
    , m_tabKey(tabKey)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Field over time — %1").arg(tabKey));
    WindowGeometry::makeResizableWindow(this);
    resize(1040, 600);
    WindowGeometry::restore(this, QStringLiteral("fieldPlot"));

    // Packet ▸ Field. A flat list of names cannot say WHICH packet's
    // LOCO_MODE is meant, and 35 names are in more than one.
    m_fieldButton = new QToolButton(this);
    m_fieldButton->setObjectName(QStringLiteral("fieldPlotChooser"));
    m_fieldButton->setPopupMode(QToolButton::InstantPopup);
    m_fieldButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_fieldButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_fieldButton->setMinimumWidth(240);
    m_fieldButton->setToolTip(tr("Choose a packet type, then one of its fields. The same field "
                                 "name in another packet is a different signal."));
    m_fieldMenu = new QMenu(m_fieldButton);
    m_fieldButton->setMenu(m_fieldMenu);

    m_addButton = new QToolButton(this);
    m_addButton->setObjectName(QStringLiteral("fieldPlotAdd"));
    m_addButton->setText(tr("+ Add field"));
    m_addButton->setPopupMode(QToolButton::InstantPopup);
    m_addButton->setToolTip(tr("Plot another field on the same time axis (up to %1)").arg(kMaxSeries));
    m_addMenu = new QMenu(m_addButton);
    m_addButton->setMenu(m_addMenu);

    m_overlayBox = new QCheckBox(tr("One axis"), this);
    m_overlayBox->setToolTip(tr("Draw every field against one value axis (for fields in the same unit, "
                                "e.g. speed against permitted speed). Off: one lane per field."));

    m_rescan = new QPushButton(tr("Rescan fields"));
    m_rescan->setToolTip(tr("Re-sample the tab for packet types and fields. Useful after "
                            "a schema reload, or once more traffic has arrived."));

    auto makeZoomButton = [this](const QString &text, const QString &tip) {
        auto *b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setMinimumWidth(30);
        return b;
    };
    m_zoomIn  = makeZoomButton(QStringLiteral("+"), tr("Zoom in on time (+, or the mouse wheel)"));
    m_zoomOut = makeZoomButton(QStringLiteral("\u2212"), tr("Zoom out (\u2212)"));
    m_zoomFit = makeZoomButton(tr("Fit"), tr("Show everything (0, or double-click the plot)"));

    m_exportButton = new QToolButton(this);
    m_exportButton->setObjectName(QStringLiteral("fieldPlotExport"));
    m_exportButton->setText(tr("Export"));
    m_exportButton->setPopupMode(QToolButton::InstantPopup);
    auto *exportMenu = new QMenu(m_exportButton);
    QAction *png = exportMenu->addAction(tr("Save image (PNG)…"));
    QAction *csv = exportMenu->addAction(tr("Save data in view (CSV)…"));
    QAction *copy = exportMenu->addAction(tr("Copy image"));
    m_exportButton->setMenu(exportMenu);
    connect(png, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save plot image"),
            QStringLiteral("%1_%2.png").arg(m_tabKey, currentField()), tr("PNG image (*.png)"));
        if (!path.isEmpty() && !saveImage(path)) m_status->fail(tr("Could not write %1").arg(path));
        else if (!path.isEmpty()) m_status->ok(tr("Saved %1").arg(path));
    });
    connect(csv, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save plot data"),
            QStringLiteral("%1_%2.csv").arg(m_tabKey, currentField()), tr("CSV (*.csv)"));
        if (!path.isEmpty() && !saveCsv(path)) m_status->fail(tr("Could not write %1").arg(path));
        else if (!path.isEmpty()) m_status->ok(tr("Saved %1").arg(path));
    });
    connect(copy, &QAction::triggered, this, [this]() {
        QApplication::clipboard()->setPixmap(m_canvas->grab());
        m_status->ok(tr("Plot image copied"));
    });

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Field:")));
    top->addWidget(m_fieldButton, 1);
    top->addWidget(m_addButton);
    top->addWidget(m_overlayBox);
    top->addWidget(m_rescan);
    top->addSpacing(8);
    top->addWidget(m_zoomOut);
    top->addWidget(m_zoomIn);
    top->addWidget(m_zoomFit);
    top->addWidget(m_exportButton);

    m_chipHost = new QWidget(this);
    m_chipRow = new QHBoxLayout(m_chipHost);
    m_chipRow->setContentsMargins(0, 0, 0, 0);
    m_chipHost->hide();

    m_canvas = new FieldPlotCanvas;
    // One line at 1100 px (it wrapped to two).
    auto *hint = new QLabel(tr("Wheel: zoom time  ·  Shift+wheel: zoom values  ·  Drag: zoom to a box  ·  "
                               "Right-drag: pan  ·  Double-click: fit  ·  Click a point: open its message"));
    hint->setStyleSheet(UiColor::mutedStyle());
    hint->setWordWrap(true);
    m_status = new StatusLine;
    m_status->setWordWrap(true);

    auto *root = new QVBoxLayout(this);
    root->addLayout(top);
    root->addWidget(m_chipHost);
    root->addWidget(m_canvas, 1);
    root->addWidget(hint);
    root->addWidget(m_status);

    m_reextract = new QTimer(this);
    m_reextract->setSingleShot(true);
    m_reextract->setInterval(250);
    connect(m_reextract, &QTimer::timeout, this, &FieldPlotWindow::reextractView);

    connect(m_rescan, &QPushButton::clicked, this, &FieldPlotWindow::refreshFields);
    connect(m_overlayBox, &QCheckBox::toggled, this, &FieldPlotWindow::setOverlay);
    connect(m_zoomIn,  &QToolButton::clicked, m_canvas, &FieldPlotCanvas::zoomIn);
    connect(m_zoomOut, &QToolButton::clicked, m_canvas, &FieldPlotCanvas::zoomOut);
    connect(m_zoomFit, &QToolButton::clicked, m_canvas, &FieldPlotCanvas::resetZoom);
    connect(m_canvas, &FieldPlotCanvas::viewChanged, this, &FieldPlotWindow::onViewChanged);
    connect(m_canvas, &FieldPlotCanvas::pointActivated,
            this, [this](int, qint64 ms) { emit jumpRequested(m_tabKey, ms); });

    refreshFields();
}

void FieldPlotWindow::setOverlay(bool on)
{
    if (m_overlayBox->isChecked() != on) {
        const QSignalBlocker block(m_overlayBox);
        m_overlayBox->setChecked(on);
    }
    m_canvas->setOverlay(on);
}

void FieldPlotWindow::updateButtonText()
{
    const QString field = currentField(), type = currentType();
    if (field.isEmpty()) { m_fieldButton->setText(tr("Choose a field…")); }
    else {
        m_fieldButton->setText(type.isEmpty() ? tr("%1  (any packet)").arg(field)
                                              : QStringLiteral("%1 \u25B8 %2").arg(type, field));
    }
    m_addButton->setEnabled(!m_catalogue.isEmpty() && m_selected.size() < kMaxSeries);
    m_overlayBox->setEnabled(m_selected.size() > 1);
}

void FieldPlotWindow::fillPacketMenu(QMenu *menu, bool adding)
{
    // Session 157: this runs from a field's own triggered() (choose(), and
    // addField()), while the submenu that holds that action is still
    // delivering the click. Deleting them here -- clear(), qDeleteAll -- freed
    // the menu under QMenu's feet and crashed the console on every pick.
    // They are taken off now and freed once the click has finished.
    for (QAction *a : menu->actions()) {
        menu->removeAction(a);
        if (a->parent() == menu) a->deleteLater();
    }
    for (QMenu *old : menu->findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly))
        old->deleteLater();
    if (m_catalogue.isEmpty()) {
        QAction *none = menu->addAction(tr("(no decodable packets in this tab)"));
        none->setEnabled(false);
        return;
    }
    for (const QString &type : m_catalogue.types) {
        QMenu *sub = menu->addMenu(tr("%1   (%2 rows)").arg(type).arg(m_catalogue.rowCounts.value(type)));
        sub->setToolTipsVisible(true);
        for (const QString &field : m_catalogue.fields.value(type)) {
            QAction *a = sub->addAction(field);
            const bool plotted = m_selected.contains(qMakePair(type, field));
            a->setCheckable(true);
            a->setChecked(adding ? plotted : (type == currentType() && field == currentField()));
            if (adding && plotted) a->setEnabled(false);
            const QStringList others = m_catalogue.typesWith(field);
            if (others.size() > 1) {
                QStringList rest = others;
                rest.removeAll(type);
                a->setToolTip(tr("Also in: %1 — a different signal there").arg(rest.join(QStringLiteral(", "))));
            }
            if (adding) connect(a, &QAction::triggered, this, [this, type, field]() { addField(field, type); });
            else        connect(a, &QAction::triggered, this, [this, type, field]() { choose(type, field); });
        }
    }
}

void FieldPlotWindow::rebuildMenus()
{
    fillPacketMenu(m_fieldMenu, false);
    fillPacketMenu(m_addMenu, true);
}

void FieldPlotWindow::rebuildChips()
{
    while (QLayoutItem *item = m_chipRow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (m_selected.size() < 2) { m_chipHost->hide(); return; }
    for (int i = 0; i < m_selected.size(); ++i) {
        auto *chip = new QToolButton(m_chipHost);
        chip->setText(QStringLiteral("\u25CF %1 \u25B8 %2   \u2715").arg(m_selected.at(i).first, m_selected.at(i).second));
        chip->setToolTip(tr("Remove this field from the plot"));
        chip->setAutoRaise(true);
        chip->setStyleSheet(QStringLiteral("QToolButton { color: %1; }").arg(UiColor::series(i).name()));
        connect(chip, &QToolButton::clicked, this, [this, i]() { removeSeries(i); });
        m_chipRow->addWidget(chip);
    }
    m_chipRow->addStretch(1);
    m_chipHost->show();
}

QString FieldPlotWindow::typeFor(const QString &field, const QString &typeToken) const
{
    if (!typeToken.isEmpty()) return typeToken;
    const QStringList carrying = m_catalogue.typesWith(field);
    return carrying.isEmpty() ? QString() : carrying.first();
}

void FieldPlotWindow::choose(const QString &typeToken, const QString &fieldName)
{
    const QPair<QString, QString> sel(typeToken, fieldName.trimmed());
    if (m_selected.isEmpty()) m_selected << sel;
    else m_selected[0] = sel;
    // A field chosen as primary that was also added further down: once.
    for (int i = m_selected.size() - 1; i > 0; --i) if (m_selected.at(i) == sel) m_selected.removeAt(i);
    updateButtonText();
    rebuildMenus();
    rebuildChips();
    replot();
}

void FieldPlotWindow::plotField(const QString &fieldName, const QString &typeToken)
{
    const QString want = fieldName.trimmed();
    if (want.isEmpty()) return;
    choose(typeFor(want, typeToken), want);
}

bool FieldPlotWindow::addField(const QString &fieldName, const QString &typeToken)
{
    const QString want = fieldName.trimmed();
    if (want.isEmpty() || m_selected.size() >= kMaxSeries) return false;
    const QPair<QString, QString> sel(typeFor(want, typeToken), want);
    if (m_selected.contains(sel)) return false;
    m_selected << sel;
    updateButtonText();
    rebuildMenus();
    rebuildChips();
    replot();
    return true;
}

bool FieldPlotWindow::removeSeries(int index)
{
    if (index < 0 || index >= m_selected.size() || m_selected.size() <= 1) return false;
    m_selected.removeAt(index);
    updateButtonText();
    rebuildMenus();
    rebuildChips();
    replot();
    return true;
}

void FieldPlotWindow::refreshFields()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_catalogue = discoverFieldCatalogue(m_model, kavachSchema());
    QApplication::restoreOverrideCursor();

    if (m_catalogue.isEmpty()) {
        rebuildMenus();
        m_status->state(tr("No decodable fields found in this tab. Either "
                           "no schema packet matches its traffic, or no "
                           "message carries raw bytes."));
        m_canvas->setSeries(FieldSeries());
        updateButtonText();
        return;
    }
    if (m_selected.isEmpty()) {
        QString busiest = m_catalogue.types.first();
        for (const QString &t : m_catalogue.types)
            if (m_catalogue.rowCounts.value(t) > m_catalogue.rowCounts.value(busiest)) busiest = t;
        m_selected << qMakePair(busiest, m_catalogue.fields.value(busiest).value(0));
    }
    rebuildMenus();
    rebuildChips();
    updateButtonText();
    replot();
}

void FieldPlotWindow::showStatus(const QVector<FieldSeries> &list, bool inView)
{
    QStringList parts;
    bool nonNumeric = false;
    for (const FieldSeries &s : list) {
        QString part = tr("%1: %2 from %3")
                           .arg(s.label(), countOf(s.points.size(), "point", "points"),
                                countOf(s.rowsDecoded, "decoded row", "decoded rows"));
        if (s.rowsNonNumeric > 0) {
            part += tr(", %1 non-numeric NOT plotted").arg(s.rowsNonNumeric);
            nonNumeric = true;
        }
        if (s.rowsMatched == 0 && s.rowsDecoded > 0) part += tr(", no decoded row contained it");
        if (s.hitRowCap) part += inView ? tr(", still sampled in view") : tr(", sampled (zoom in for every row)");
        else if (inView) part += tr(", every row in view");
        parts << part;
    }
    if (list.size() == 1) {
        const QStringList others = m_catalogue.typesWith(currentField());
        if (others.size() > 1) {
            QStringList rest = others;
            rest.removeAll(currentType());
            parts << tr("%1 is also in %2 (Field menu)").arg(currentField(), rest.join(QStringLiteral(", ")));
        }
    }
    const QString msg = parts.join(QStringLiteral("  ·  "));
    if (nonNumeric) m_status->warn(msg); else m_status->state(msg);
}

void FieldPlotWindow::replot()
{
    if (m_selected.isEmpty()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QVector<FieldSeries> list;
    for (const auto &sel : m_selected) {
        list << extractFieldSeries(m_model, sel.first, sel.second, kavachSchema());
    }
    QApplication::restoreOverrideCursor();
    m_anyCapped = false;
    for (const FieldSeries &s : list) m_anyCapped = m_anyCapped || s.hitRowCap;
    m_canvas->setSeriesList(list);
    showStatus(list, false);
}

void FieldPlotWindow::onViewChanged(qint64, qint64)
{
    if (m_anyCapped) m_reextract->start();
}

void FieldPlotWindow::reextractView()
{
    if (m_selected.isEmpty()) return;
    if (!m_canvas->isZoomed()) { replot(); return; }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QVector<FieldSeries> list;
    for (const auto &sel : m_selected) {
        list << extractFieldSeries(m_model, sel.first, sel.second, kavachSchema(), 20000,
                                   m_canvas->viewFromMs(), m_canvas->viewToMs());
    }
    QApplication::restoreOverrideCursor();
    m_canvas->setSeriesList(list, true);
    showStatus(list, true);
}

bool FieldPlotWindow::saveImage(const QString &path) const
{
    // The canvas only: the legend and both axis titles are part of it, so the
    // image stands on its own in a report.
    return m_canvas->grab().save(path, "PNG");
}

bool FieldPlotWindow::saveCsv(const QString &path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(seriesToCsv(m_canvas->seriesList(), m_canvas->viewFromMs(), m_canvas->viewToMs()).toUtf8());
    return file.commit();
}

void FieldPlotWindow::closeEvent(QCloseEvent *event)
{
    WindowGeometry::save(this, QStringLiteral("fieldPlot"));
    QWidget::closeEvent(event);
}

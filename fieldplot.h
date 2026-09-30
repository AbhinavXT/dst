#ifndef FIELDPLOT_H
#define FIELDPLOT_H

// =============================================================================
//  FieldPlot
//  -----------------------------------------------------------------------------
//  Track one decoded field's value across a whole tab or session and draw it
//  against time.
//
//  WHY
//    The field inspector answers "what does this frame say?". The question
//    that follows is almost always "and what was it doing before?" — did
//    the authorised speed step down or collapse, did the link quality decay
//    or drop out, was the counter already wrong three minutes earlier. That
//    is a shape over time, and reading it out of a table one row at a time
//    is exactly the work a plot removes.
//
//  THE HONEST-VALUE PROBLEM
//    Decoded values are display strings, not numbers: "42", "42 km/h",
//    "0x2A", "ON", "RESERVED (7)". Plotting requires a number, and the
//    tempting shortcut — QString::toDouble() and drop whatever fails — is
//    wrong in a way that matters here. A field that is numeric in 90% of
//    frames and an enum label in the other 10% would silently plot as if
//    those frames did not exist, and a gap in a safety trace reads as "no
//    data" when it actually means "a value I chose not to show you".
//
//    So extraction counts every outcome — matched, non-numeric, undecodable
//    — and the window reports them. A series is only worth trusting
//    alongside the number of rows it had to discard.
//
//  COST
//    Schema-decoding every row is far more expensive than anything else in
//    the app: a full 200,000-row tab would mean 200,000 decodes. Extraction
//    takes a row cap and reports whether it hit it, and the window samples
//    from the whole span rather than truncating at the cap — a plot of the
//    first 5% of a session, presented as the session, would be worse than
//    no plot.
// =============================================================================

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "statusline.h"

#include "logentry.h"

class LogModel;
namespace Schema { class Decoder; }

// ---- extraction (no UI; tested directly) ---------------------------------

// Pull a number out of a decoded field's display string.
//
// Accepts: "42", "-3.5", "42 km/h", "0x2A", "  7  ". Rejects anything with
// no leading numeric part, which is the caller's signal to count it as
// non-numeric rather than as zero.
double parseFieldNumber(const QString &display, bool *ok);

struct FieldSeries {
    struct Point {
        qint64 epochMs = 0;
        double value   = 0.0;
        int    row     = -1;      // row in the source model, for jumping
    };

    QVector<Point> points;

    // What was plotted (session 80): the packet type ("lsrp") and field.
    // An empty type means "any packet", the pre-80 behaviour.
    QString typeToken;
    QString fieldName;
    // The unit the schema prints after the number ("km/h"), when every
    // sample that had one agreed on it; empty otherwise.
    QString unit;
    // Enum names the schema prints in brackets: 2 -> "Staff_Responsible".
    QMap<qint64, QString> labels;

    // Provenance of the sample. All four are reported to the operator.
    int rowsScanned    = 0;   // rows we attempted
    int rowsDecoded    = 0;   // rows the schema could decode
    int rowsMatched    = 0;   // rows that contained the field
    int rowsNonNumeric = 0;   // field present but not a number
    bool hitRowCap     = false;
    int  rowsOfType    = 0;   // rows of typeToken in the range (all rows if no type)

    double minValue = 0.0, maxValue = 0.0;
    qint64 minMs    = 0,   maxMs    = 0;

    bool isEmpty() const { return points.isEmpty(); }
    // "lsrp ▸ LOCO_MODE", or just the field for an untyped series.
    QString label() const;
};

// Walk `model` and build a series for `fieldName` (matched on the trimmed
// field label). Samples evenly across the model when it is larger than
// `maxRows`, so the result spans the whole session rather than its start.
// Any packet type: kept for callers that do not know it.
FieldSeries extractFieldSeries(const LogModel *model,
                               const QString &fieldName,
                               const Schema::Decoder &decoder,
                               int maxRows = 20000);

// The same, for ONE packet type (session 80). A field name is not an
// address: 35 names occur in more than one packet, and LOCO_MODE from the
// loco's own LSRP interleaved with LOCO_MODE from every ARP it received
// draws a barcode, not a trace. Only rows whose capture tag is `typeToken`
// are decoded, which is also far cheaper. `fromMs`/`toMs` (both > 0)
// restrict it to a time window: the plot re-extracts there when zoomed in,
// so a zoomed view is at full resolution, not a magnified sample.
FieldSeries extractFieldSeries(const LogModel *model,
                               const QString &typeToken,
                               const QString &fieldName,
                               const Schema::Decoder &decoder,
                               int maxRows = 20000,
                               qint64 fromMs = 0, qint64 toMs = 0);

// Field labels present in the first `sampleRows` decodable rows, trimmed
// and de-duplicated, for populating the field chooser.
QStringList discoverFieldNames(const LogModel *model,
                               const Schema::Decoder &decoder,
                               int sampleRows = 200);

// Every packet type in the tab and the fields each carries, for the
// Packet ▸ Field menu. Every row's type is counted (cheap: the tag only);
// only a few rows of each type are decoded.
struct FieldCatalogue {
    QStringList                  types;       // sorted
    QHash<QString, QStringList>  fields;      // type -> fields, sorted
    QHash<QString, int>          rowCounts;   // type -> rows of it
    bool isEmpty() const { return types.isEmpty(); }
    QStringList typesWith(const QString &field) const;   // most rows first
};
FieldCatalogue discoverFieldCatalogue(const LogModel *model,
                                      const Schema::Decoder &decoder,
                                      int decodesPerType = 4,
                                      int maxScan = 50000);

// Several fields of one packet type, row by row (session 81): the speed–
// distance view needs speed, permitted speed and location FROM THE SAME
// FRAME, which separate series cannot promise. `raw` holds the decoder's
// integer values (no display-string parsing); `display` the rendered text.
// Only the requested fields are kept. Rows without any of them are skipped.
struct RowFields {
    int    row = -1;
    qint64 epochMs = 0;
    QHash<QString, qint64>  raw;
    QHash<QString, QString> display;
    bool   has(const QString &field) const { return raw.contains(field); }
};
QVector<RowFields> collectRowFields(const LogModel *model, const QString &typeToken,
                                    const QStringList &fields, int maxRows = 200000,
                                    qint64 fromMs = 0, qint64 toMs = 0, bool *hitCap = nullptr);

// The capture tag's type ("@lsrp_1_1 …" -> "lsrp"), without decoding.
QString captureTypeOf(const QString &text);

// Nice time-axis ticks for [loMs, hiMs] (1-2-5 steps in s/min/h). Returns
// the tick times; `stepMs` gets the step.
QVector<qint64> niceTimeTicks(qint64 loMs, qint64 hiMs, int target, qint64 *stepMs = nullptr);

// Tick values for an axis spanning [lo, hi], about `target` of them, on a
// 1-2-5 step.
//
// The reason this is not just lo + span*i/4: LOCO_MODE takes the values 1,
// 2, 4 and 6, and dividing that range into quarters labelled the axis
// 2.25, 3.5, 4.75 — numbers the field cannot hold. An enum axis has to be
// labelled in the units the field is actually in, so when every sample is
// a whole number the step is forced to a whole number too.
QVector<double> niceTicks(double lo, double hi, int target, bool integerOnly);

// How many decimals a label needs for this step. 0 for an integer axis.
int tickDecimals(double step);

// ---- widgets --------------------------------------------------------------

// Values of every series at one time, for the shared cursor: for each series
// the sample at or before `ms` (a state holds until the next sample), or none
// when the series has no sample within `maxAgeMs` before it.
struct CursorValue { bool has = false; double value = 0.0; qint64 atMs = 0; int row = -1; };
CursorValue valueAtOrBefore(const FieldSeries &s, qint64 ms, qint64 maxAgeMs);

// Samples of several series, one row per distinct sample time in
// [fromMs, toMs], as CSV (time_local, epoch_ms, one column per series; a cell
// is empty where that series has no sample at that time). Session 81, Export.
QString seriesToCsv(const QVector<FieldSeries> &list, qint64 fromMs, qint64 toMs);

class FieldPlotCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit FieldPlotCanvas(QWidget *parent = nullptr);

    // keepView: a re-extraction of the window already on screen (zoomed
    // in), which must not throw the operator back to the whole session.
    void setSeries(const FieldSeries &s, bool keepView = false);
    void setSeriesList(const QVector<FieldSeries> &list, bool keepView = false);
    const FieldSeries &series() const;                 // the first
    const QVector<FieldSeries> &seriesList() const { return m_list; }

    // One lane per series (its own value axis), or every series on one axis.
    void setOverlay(bool on);
    bool overlay() const { return m_overlay; }
    int  laneCount() const;

    // ---- zoom -----------------------------------------------------------------
    bool   isZoomed() const;
    qint64 viewFromMs() const { return qint64(m_x0); }
    qint64 viewToMs()   const { return qint64(m_x1); }
    double viewMinValue(int lane = 0) const;
    double viewMaxValue(int lane = 0) const;
    void   setTimeView(qint64 fromMs, qint64 toMs);   // values auto-fit
    void   zoomIn();
    void   zoomOut();
    void   resetZoom();

    // The shared cursor (hover), -1 when none. For tests and the readout.
    qint64 cursorMs() const { return m_cursorMs; }
    void   setCursorMs(qint64 ms) { m_cursorMs = ms; update(); }

signals:
    void pointActivated(int modelRow, qint64 epochMs);
    void viewChanged(qint64 fromMs, qint64 toMs);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void leaveEvent(QEvent *) override;
    QSize sizeHint() const override { return QSize(700, 320); }

private:
    struct Lane {
        QVector<int> members;         // indices into m_list
        double y0 = 0, y1 = 1;
        bool   yManual = false;
        bool   allWhole = true;
        QString title;                // "LOCO_MODE", or "km/h" on a shared axis
        QMap<qint64, QString> labels; // enum names, when one series
    };
    void   rebuildLanes();
    void   fitLane(Lane &lane);
    void   fitValues();
    void   fullView();
    void   setView(double x0, double x1);
    void   announceView();
    QRect  plotRect() const;           // all lanes together
    QRect  laneRect(int lane) const;
    int    laneAt(int py) const;
    int    leftMargin() const;
    QString valueText(const Lane &lane, double v, int decimals) const;
    double xOf(qint64 ms) const;
    double yOf(const Lane &lane, int laneIndex, double v) const;
    qint64 msAt(int px) const;
    double valueAt(int laneIndex, int py) const;
    int    nearestPoint(int px, int py, int *seriesOut) const;
    qint64 dataMinMs() const;
    qint64 dataMaxMs() const;
    bool   isWholeSeries(int i) const;

    QVector<FieldSeries> m_list;
    QVector<bool>        m_whole;       // per series: every sample whole
    QVector<Lane>        m_lanes;
    bool   m_overlay = false;

    double m_x0 = 0, m_x1 = 1;
    qint64 m_cursorMs = -1;
    int    m_hoverSeries = -1, m_hoverPoint = -1;

    enum class Drag { None, Box, Pan };
    Drag   m_drag = Drag::None;
    QPoint m_dragStart, m_dragNow;
    int    m_dragLane = 0;
    double m_panX0 = 0, m_panX1 = 0, m_panY0 = 0, m_panY1 = 0;
};

class QCheckBox;
class QHBoxLayout;
class QLabel;
class QMenu;
class QPushButton;
class QTimer;
class QToolButton;

class FieldPlotWindow : public QWidget
{
    Q_OBJECT
public:
    FieldPlotWindow(LogModel *model, const QString &tabKey,
                    QWidget *parent = nullptr);

    // Open straight onto a named field (replacing the first series). Without
    // a packet type, the one carrying the field in the most rows is taken.
    // A field the sample never saw is still selected (an empty type then
    // means "any packet"): the catalogue is a sample, not the session.
    void plotField(const QString &fieldName, const QString &typeToken = QString());

    // Several fields on one time axis (session 81). Returns false if it is
    // already plotted or the limit is reached.
    static constexpr int kMaxSeries = 6;
    bool addField(const QString &fieldName, const QString &typeToken = QString());
    bool removeSeries(int index);
    int  seriesCount() const { return m_selected.size(); }

    QString currentType()  const { return m_selected.isEmpty() ? QString() : m_selected.first().first; }
    QString currentField() const { return m_selected.isEmpty() ? QString() : m_selected.first().second; }
    FieldPlotCanvas *canvas() const { return m_canvas; }
    QMenu *fieldMenu() const { return m_fieldMenu; }
    QMenu *addMenu() const { return m_addMenu; }
    void setOverlay(bool on);

    // Export (session 81): the plot as an image, the samples in view as CSV.
    bool saveImage(const QString &path) const;
    bool saveCsv(const QString &path) const;

protected:
    void closeEvent(QCloseEvent *event) override;

signals:
    void jumpRequested(const QString &tabKey, qint64 epochMs);

private slots:
    void refreshFields();
    void replot();

private:
    void rebuildMenus();
    void fillPacketMenu(QMenu *menu, bool adding);
    void choose(const QString &typeToken, const QString &fieldName);
    void onViewChanged(qint64 fromMs, qint64 toMs);
    void reextractView();
    void updateButtonText();
    void rebuildChips();
    void showStatus(const QVector<FieldSeries> &list, bool inView);
    QString typeFor(const QString &field, const QString &typeToken) const;

    LogModel *m_model = nullptr;
    QString   m_tabKey;
    QVector<QPair<QString, QString>> m_selected;   // (type, field), first = primary
    FieldCatalogue m_catalogue;

    QToolButton     *m_fieldButton = nullptr;
    QMenu           *m_fieldMenu = nullptr;
    QToolButton     *m_addButton = nullptr;
    QMenu           *m_addMenu = nullptr;
    QCheckBox       *m_overlayBox = nullptr;
    QToolButton     *m_exportButton = nullptr;
    QHBoxLayout     *m_chipRow = nullptr;
    QWidget         *m_chipHost = nullptr;
    StatusLine      *m_status   = nullptr;
    QPushButton     *m_rescan   = nullptr;
    QToolButton     *m_zoomIn = nullptr, *m_zoomOut = nullptr, *m_zoomFit = nullptr;
    FieldPlotCanvas *m_canvas   = nullptr;
    QTimer          *m_reextract = nullptr;
    bool             m_anyCapped = false;
};

#endif // FIELDPLOT_H

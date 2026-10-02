#include "faultpanelwindow.h"
#include "uistyle.h"
#include "uicolors.h"
#include "windowgeometry.h"
#include "messagedispatcher.h"

#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QToolBar>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSet>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QApplication>
#include <QPalette>
#include <algorithm>

namespace {
const int    kRefreshMs   = 500;
const qint64 kClearKeepMs = 5 * 60 * 1000;   // drop cleared faults after 5 min
const qint64 kStaleMs     = 8000;            // source considered stale (no update)

// Linear blend a->b by t in [0,1].
QColor blend(const QColor &a, const QColor &b, qreal t) {
    return QColor(int(a.red()   * (1 - t) + b.red()   * t),
                  int(a.green() * (1 - t) + b.green() * t),
                  int(a.blue()  * (1 - t) + b.blue()  * t));
}

// All panel colors are derived from the *active application palette* so the
// window automatically matches whatever theme (Light/Dark) is applied, and
// re-derives on a theme toggle. Only the three semantic accents (active red /
// cleared grey / ok green) are explicit, tuned per mode for legible contrast.
struct FaultPalette {
    bool   dark;
    QColor text;        // primary value text
    QColor label;       // secondary text (source / since)
    QColor dim;         // cleared / inactive text
    QColor base;        // table / card background
    QColor altBase;     // alternating row background
    QColor border;      // card + grid border
    QColor headerBg;    // table header background
    QColor active;      // red  (active fault)
    QColor cleared;     // grey (cleared fault dot)
    QColor ok;          // green ("no active faults")
};

FaultPalette faultPalette() {
    const QPalette p = qApp->palette();
    FaultPalette c;
    c.dark     = p.color(QPalette::Window).lightness() < 128;
    c.text     = p.color(QPalette::Text);
    c.label    = blend(c.text, p.color(QPalette::Window), 0.40);
    c.dim      = p.color(QPalette::Disabled, QPalette::Text);
    c.base     = p.color(QPalette::Base);
    c.altBase  = p.color(QPalette::AlternateBase);
    c.border   = blend(c.text, p.color(QPalette::Window), 0.80);
    c.headerBg = blend(p.color(QPalette::Window), c.base, 0.5);
    // This window solved the dark/light problem first, with a hand-picked
    // pair per meaning. UiColor is that idea extracted so the rest of the
    // program shares it — including the contrast floor, which two of the old
    // values here did not clear.
    c.active   = UiColor::error();
    c.cleared  = UiColor::muted();
    c.ok       = UiColor::ok();
    return c;
}
}

FaultPanelWindow::FaultPanelWindow(MessageDispatcher *dispatcher, QWidget *parent)
    : QMainWindow(parent)
    , m_dispatcher(dispatcher)
{
    setWindowTitle(tr("Active Faults"));
    WindowGeometry::makeResizableWindow(this);
    setWindowFlag(Qt::Window);
    resize(900, 560);
    // Default above; a remembered size/position wins over it.
    WindowGeometry::restore(this, QStringLiteral("faultPanel"));
    buildUi();

    if (m_dispatcher) {
        connect(m_dispatcher, &MessageDispatcher::entryAppended,
                this,         &FaultPanelWindow::onEntryAppended);
    }
    m_refresh = new QTimer(this);
    m_refresh->setInterval(kRefreshMs);
    connect(m_refresh, &QTimer::timeout, this, &FaultPanelWindow::onRefreshTick);
    m_refresh->start();
}

void FaultPanelWindow::buildUi()
{
    QWidget *central = new QWidget(this);
    central->setAutoFillBackground(true);   // paints QPalette::Window, follows theme
    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(12, 10, 12, 12);
    root->setSpacing(10);

    // ---- header band ---------------------------------------------------
    QHBoxLayout *top = new QHBoxLayout();
    QLabel *title = new QLabel(tr("ACTIVE FAULTS"), central);
    QFont tf = title->font(); tf.setBold(true); tf.setPointSizeF(tf.pointSizeF() + 1.5);
    title->setFont(tf);
    top->addWidget(title);
    top->addSpacing(18);

    top->addWidget(new QLabel(tr("Source:"), central));
    m_selector = new QComboBox(central);
    m_selector->addItem(tr("All sources"));
    m_selector->setMinimumWidth(140);
    top->addWidget(m_selector);

    m_clearedBox = new QCheckBox(tr("Show recently cleared"), central);
    top->addWidget(m_clearedBox);

    top->addStretch(1);
    m_summary = new QLabel(central);
    QFont sf = m_summary->font(); sf.setBold(true); m_summary->setFont(sf);
    top->addWidget(m_summary);
    top->addSpacing(12);
    m_clock = new QLabel(central);
    top->addWidget(m_clock);
    root->addLayout(top);

    // Controls (combo / checkbox / labels) intentionally carry NO explicit
    // colors: they inherit the Fusion palette and therefore match the theme.

    connect(m_selector, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &FaultPanelWindow::onSelectionChanged);
    connect(m_clearedBox, &QCheckBox::toggled, this, &FaultPanelWindow::onShowClearedToggled);

    // ---- table + empty state (stacked) ---------------------------------
    QStackedWidget *stack = new QStackedWidget(central);
    m_stack = stack;
    // Export lives on the panel itself: the report is of what is on screen
    // right now, and routing it through the main window's File menu would
    // make that relationship unclear.
    //
    // The window had no toolbar, so one is created here. (An earlier
    // findChild<QToolBar*>() would have silently attached to nothing —
    // exactly the orphaned-action failure this codebase has already hit
    // once.)
    // A button at the end of the header row (session 141): as a toolbar of
    // one action it cost a whole row above the panel's own header.
    auto *exp = new QPushButton(tr("Save report…"), central);
    exp->setObjectName(QStringLiteral("faultSaveReport"));
    exp->setToolTip(tr("Save the current fault picture as an HTML report "
                       "for an incident record or handover."));
    connect(exp, &QPushButton::clicked,
            this, &FaultPanelWindow::onExportReport);
    top->addSpacing(12);
    top->addWidget(exp);


    m_table = new QTableWidget(0, 8, stack);
    m_table->setHorizontalHeaderLabels(
        { QString(), tr("Source"), tr("Module"), tr("Fault"),
          tr("Code"), tr("Reported by"), tr("Active since"), tr("Last seen") });
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    QHeaderView *hh = m_table->horizontalHeader();
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);   // as the other tool tables
    m_table->setObjectName(QStringLiteral("faultTable"));
    hh->setSectionResizeMode(0, QHeaderView::Fixed);  m_table->setColumnWidth(0, 26);
    hh->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(2, QHeaderView::Stretch);
    hh->setSectionResizeMode(3, QHeaderView::Stretch);
    hh->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    stack->addWidget(m_table);

    m_empty = new QLabel(tr("\u2713  No active faults"), stack);
    m_empty->setAlignment(Qt::AlignCenter);
    stack->addWidget(m_empty);

    root->addWidget(stack, 1);
    setCentralWidget(central);

    applyTheme();   // colour every surface from the active palette
}

// Build/refresh all palette-derived styling. Called once from buildUi() and
// again whenever the application palette or style changes (theme toggle).
void FaultPanelWindow::applyTheme()
{
    const FaultPalette c = faultPalette();

    if (m_clock) {
        m_clock->setStyleSheet(QStringLiteral("color:%1;").arg(c.label.name()));
    }
    if (m_table) {
        m_table->setStyleSheet(QStringLiteral(
            "QTableWidget{background:%1;alternate-background-color:%2;color:%3;"
            "  border:1px solid %4;gridline-color:%4;}"
            "QHeaderView::section{background:%5;color:%6;border:0px;"
            "  border-bottom:1px solid %4;padding:5px 6px;font-weight:bold;}")
            .arg(c.base.name(), c.altBase.name(), c.text.name(),
                 c.border.name(), c.headerBg.name(), c.label.name()));
    }
    if (m_empty) {
        // Background/border track the theme; the text colour (ok vs dim) is set
        // per-state in rebuildTable().
        m_empty->setStyleSheet(QStringLiteral(
            "QLabel{font-size:18px;background:%1;border:1px solid %2;border-radius:6px;color:%3;}")
            .arg(c.base.name(), c.border.name(), c.ok.name()));
    }
    // Re-fill the table so cell foreground colours pick up the new palette.
    if (m_table) { rebuildTable(); }
}

void FaultPanelWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    if (e && (e->type() == QEvent::ApplicationPaletteChange ||
              e->type() == QEvent::PaletteChange ||
              e->type() == QEvent::StyleChange)) {
        applyTheme();
    }
}

void FaultPanelWindow::onEntryAppended(QString tabKey, LogEntryPtr entry)
{
    Q_UNUSED(tabKey);
    if (entry.isNull()) { return; }
    const CaptureLine c = CaptureDecoder::parseLine(entry->text);
    if (!c.valid || c.type != CapType::NmsFault) { return; }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    ingestFault(c, nowMs);

    const QString key = c.key();
    if (m_selector->findText(key) < 0) { m_selector->addItem(key); }
    m_dirty = true;
}

void FaultPanelWindow::ingestFault(const CaptureLine &c, qint64 nowMs)
{
    if (c.bytes.size() < 29) { return; }
    const int sub = (quint8)c.bytes[27];

    KeyState &ks = m_keys[c.key()];
    ks.lastUpdateMs = nowMs;

    QSet<QString> present;
    const QVector<ActiveFaultInfo> list = CaptureDecoder::faultsOf(c);
    for (const ActiveFaultInfo &fi : list) {
        const QString fk = QStringLiteral("%1:%2:%3")
                               .arg(fi.subsystem).arg(fi.moduleId).arg(fi.faultId);
        present.insert(fk);
        auto it = ks.faults.find(fk);
        if (it == ks.faults.end()) {
            Fault f;
            f.subsystem = fi.subsystem; f.subsystemName = fi.subsystemName;
            f.moduleId  = fi.moduleId;  f.moduleName    = fi.moduleName;
            f.codeType  = fi.codeType;
            f.faultId   = fi.faultId;   f.faultName     = fi.faultName;
            f.firstMs   = nowMs; f.lastMs = nowMs; f.active = true;
            ks.faults.insert(fk, f);
        } else {
            it->lastMs = nowMs; it->codeType = fi.codeType; it->active = true;
        }
    }
    // This packet is a full snapshot for `sub`: faults of that subsystem not
    // present any more have cleared.
    for (auto it = ks.faults.begin(); it != ks.faults.end(); ++it) {
        if (it->subsystem == sub && !present.contains(it.key()) && it->active) {
            it->active = false;
            it->lastMs = nowMs;            // time it cleared
        }
    }
}

QString FaultPanelWindow::durationText(qint64 ms)
{
    if (ms < 0) { return QStringLiteral("--"); }
    const qint64 s = ms / 1000;
    if (s < 60)    { return QStringLiteral("%1s").arg(s); }
    if (s < 3600)  { return QStringLiteral("%1m %2s").arg(s / 60).arg(s % 60); }
    return QStringLiteral("%1h %2m").arg(s / 3600).arg((s % 3600) / 60);
}

QString FaultPanelWindow::ageText(qint64 ms)
{
    if (ms < 0)      { return QStringLiteral("--"); }
    if (ms < 1500)   { return QStringLiteral("now"); }
    return durationText(ms) + tr(" ago");
}

void FaultPanelWindow::onSelectionChanged(int index)
{
    m_selectedKey = (index <= 0) ? QString() : m_selector->itemText(index);
    m_dirty = true;
}

void FaultPanelWindow::onShowClearedToggled(bool on)
{
    m_showCleared = on;
    m_dirty = true;
}

void FaultPanelWindow::onRefreshTick()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    // prune long-cleared faults
    for (KeyState &ks : m_keys) {
        for (auto it = ks.faults.begin(); it != ks.faults.end(); ) {
            if (!it->active && nowMs - it->lastMs > kClearKeepMs) { it = ks.faults.erase(it); }
            else { ++it; }
        }
    }
    rebuildTable();
}

QVector<FaultPanelWindow::Fault> FaultPanelWindow::currentFaults() const
{
    QVector<Fault> out;
    for (auto it = m_keys.constBegin(); it != m_keys.constEnd(); ++it) {
        for (const Fault &f : it.value().faults) out.append(f);
    }
    // Active first, then most recently seen. An incident report is read
    // top-down and the things still wrong are what matter; burying them
    // under resolved history would invert the priority.
    std::sort(out.begin(), out.end(), [](const Fault &a, const Fault &b) {
        if (a.active != b.active) return a.active;
        return a.lastMs > b.lastMs;
    });
    return out;
}

QString FaultPanelWindow::buildReportHtml(const QVector<Fault> &faults,
                                          const QString &title,
                                          qint64 nowMs)
{
    auto esc = [](const QString &s) { return s.toHtmlEscaped(); };
    auto stamp = [](qint64 ms) {
        // UTC, matching the log files. A fault report is correlated against
        // archives, and a local timestamp with no zone is exactly the
        // ambiguity the Time column had to fix.
        return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC)
                            .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                            + QStringLiteral("Z")
                      : QStringLiteral("—");
    };

    int active = 0;
    for (const Fault &f : faults) if (f.active) ++active;

    QString h;
    h += QStringLiteral("<!DOCTYPE html><html><head><meta charset='utf-8'>");
    h += QStringLiteral("<title>%1</title>").arg(esc(title));
    // Inline styles: the report has to survive being emailed or archived on
    // its own, and a linked stylesheet would not.
    h += QStringLiteral(
        "<style>"
        "body{font-family:sans-serif;font-size:12px;margin:24px;}"
        "h1{font-size:18px;margin-bottom:2px;}"
        ".meta{color:#666;margin-bottom:16px;}"
        "table{border-collapse:collapse;width:100%;}"
        "th,td{border:1px solid #ccc;padding:4px 8px;text-align:left;"
        "vertical-align:top;}"
        "th{background:#f0f0f0;}"
        "tr.active td{background:#fff4f4;}"
        ".act{color:#cc3300;font-weight:bold;}"
        ".res{color:#666;}"
        "</style></head><body>");

    h += QStringLiteral("<h1>%1</h1>").arg(esc(title));
    h += QStringLiteral("<div class='meta'>Generated %1 &middot; "
                        "%2 fault(s), %3 currently active</div>")
             .arg(stamp(nowMs)).arg(faults.size()).arg(active);

    if (faults.isEmpty()) {
        h += QStringLiteral("<p>No faults have been reported.</p>");
        h += QStringLiteral("</body></html>");
        return h;
    }

    h += QStringLiteral(
        "<table><tr><th>State</th><th>Subsystem</th><th>Module</th>"
        "<th>Fault</th><th>Code</th><th>First seen</th><th>Last seen</th></tr>");

    for (const Fault &f : faults) {
        h += QStringLiteral("<tr class='%1'>").arg(f.active ? "active" : "");
        h += QStringLiteral("<td class='%1'>%2</td>")
                 .arg(f.active ? "act" : "res",
                      f.active ? QStringLiteral("ACTIVE")
                               : QStringLiteral("resolved"));
        h += QStringLiteral("<td>%1</td>").arg(esc(f.subsystemName));
        h += QStringLiteral("<td>%1</td>").arg(esc(f.moduleName));
        h += QStringLiteral("<td>%1</td>").arg(esc(f.faultName));
        h += QStringLiteral("<td>%1:%2:%3</td>")
                 .arg(f.subsystem).arg(f.moduleId).arg(f.faultId);
        h += QStringLiteral("<td>%1</td>").arg(stamp(f.firstMs));
        h += QStringLiteral("<td>%1</td>").arg(stamp(f.lastMs));
        h += QStringLiteral("</tr>");
    }
    h += QStringLiteral("</table></body></html>");
    return h;
}

void FaultPanelWindow::onExportReport()
{
    const QVector<Fault> faults = currentFaults();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QString title = tr("Kavach fault report");

    const QString suggested =
        QStringLiteral("fault-report-%1.html")
            .arg(QDateTime::fromMSecsSinceEpoch(nowMs, Qt::UTC)
                     .toString(QStringLiteral("yyyyMMdd-HHmmss")));

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save fault report"), suggested,
        tr("HTML report (*.html);;All files (*)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Save failed"),
                             tr("Could not write %1:\n%2")
                                 .arg(path, f.errorString()));
        return;
    }
    // Explicit UTF-8: subsystem and fault names come from the schema and
    // are not guaranteed ASCII, and the <meta charset> above promises it.
    f.write(buildReportHtml(faults, title, nowMs).toUtf8());
    f.close();

    QMessageBox::information(
        this, tr("Fault report"),
        tr("Saved %1 fault(s) to:\n%2").arg(faults.size()).arg(path));
}

void FaultPanelWindow::rebuildTable()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool allSources = m_selectedKey.isEmpty();
    const FaultPalette pc = faultPalette();

    // collect rows
    struct Row { QString key; Fault f; };
    QVector<Row> rows;
    int activeCount = 0, sourcesWithFaults = 0;
    qint64 newestUpdate = 0;

    for (auto kit = m_keys.constBegin(); kit != m_keys.constEnd(); ++kit) {
        const QString &key = kit.key();
        if (!allSources && key != m_selectedKey) { continue; }
        newestUpdate = qMax(newestUpdate, kit.value().lastUpdateMs);
        bool any = false;
        for (const Fault &f : kit.value().faults) {
            if (f.active) { ++activeCount; any = true; }
            if (!f.active && !m_showCleared) { continue; }
            rows.push_back({ key, f });
        }
        if (any) { ++sourcesWithFaults; }
    }

    // sort: active first, then most-recent first
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b){
        if (a.f.active != b.f.active) { return a.f.active && !b.f.active; }
        return a.f.lastMs > b.f.lastMs;
    });

    // summary
    if (activeCount == 0) {
        m_summary->setText(tr("\u2713 No active faults"));
        m_summary->setStyleSheet(QStringLiteral("color:%1;").arg(pc.ok.name()));
    } else {
        m_summary->setText(tr("\u25CF %1 active  \u00B7  %2 source%3")
                           .arg(activeCount).arg(sourcesWithFaults)
                           .arg(sourcesWithFaults == 1 ? QString() : QStringLiteral("s")));
        m_summary->setStyleSheet(QStringLiteral("color:%1;").arg(pc.active.name()));
    }
    const qint64 upAge = (newestUpdate > 0) ? (nowMs - newestUpdate) : -1;
    m_clock->setText(newestUpdate > 0
        ? tr("updated %1%2").arg(ageText(upAge), upAge > kStaleMs ? tr("  \u26A0 stale") : QString())
        : tr("waiting for fault data\u2026"));

    // empty-state switch
    if (rows.isEmpty()) {
        m_empty->setText(activeCount == 0 && !m_showCleared
                         ? tr("\u2713  No active faults")
                         : tr("No faults to show"));
        m_empty->setStyleSheet(QStringLiteral(
            "QLabel{font-size:18px;background:%1;border:1px solid %2;border-radius:6px;color:%3;}")
            .arg(pc.base.name(), pc.border.name(),
                 (activeCount == 0 ? pc.ok : pc.label).name()));
        if (m_stack) { m_stack->setCurrentWidget(m_empty); }
        m_table->setRowCount(0);
        return;
    }
    if (m_stack) { m_stack->setCurrentWidget(m_table); }

    const QFont mono = UiStyle::monoFont();
    m_table->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const Fault &f = rows[r].f;
        const bool act = f.active;
        const QColor fg = act ? pc.text : pc.dim;

        auto cell = [&](int col, const QString &txt, const QColor &c) {
            QTableWidgetItem *it = new QTableWidgetItem(txt);
            it->setForeground(c);
            m_table->setItem(r, col, it);
            return it;
        };
        // status dot
        QTableWidgetItem *dot = new QTableWidgetItem(QStringLiteral("\u25CF"));
        dot->setForeground(act ? pc.active : pc.cleared);
        dot->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(r, 0, dot);

        cell(1, rows[r].key, act ? pc.label : pc.dim);
        cell(2, QStringLiteral("%1  (%2)").arg(f.moduleId).arg(f.moduleName), fg);
        QTableWidgetItem *fault = cell(3, f.faultName, act ? pc.active : pc.dim);
        QFont ff = fault->font(); ff.setBold(act); fault->setFont(ff);
        // "0x02", not "0X02": only the digits are upper case (session 141).
        QTableWidgetItem *code = cell(4, QStringLiteral("0x") + QStringLiteral("%1")
                                      .arg(f.codeType, 2, 16, QChar('0')).toUpper(), fg);
        code->setFont(mono);
        cell(5, f.subsystemName, act ? pc.label : pc.dim);
        cell(6, durationText(act ? (nowMs - f.firstMs) : (f.lastMs - f.firstMs)), fg);
        cell(7, act ? ageText(nowMs - f.lastMs) : tr("cleared %1").arg(ageText(nowMs - f.lastMs)),
             act ? pc.label : pc.cleared);
    }
    m_dirty = false;
}

void FaultPanelWindow::closeEvent(QCloseEvent *event)
{
    // These windows are WA_DeleteOnClose, so this is the last
    // point at which the geometry still exists to be read.
    WindowGeometry::save(this, QStringLiteral("faultPanel"));
    QMainWindow::closeEvent(event);
}

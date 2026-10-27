#include "runcomparewindow.h"

#include "dmitimetravel.h"
#include "messagedispatcher.h"
#include "reviewpack.h"
#include "sessionreader.h"
#include "statusline.h"
#include "uicolors.h"

#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr qint64 kStaleMs = 3000;   // as the DMI window mutes a frame (kDmiStaleMs)
constexpr int    kTickMs  = 100;

QString clockText(qint64 ms)
{
    const qint64 t = qMax<qint64>(0, ms);
    return QStringLiteral("%1:%2.%3").arg(t / 60000, 2, 10, QLatin1Char('0'))
                                     .arg((t / 1000) % 60, 2, 10, QLatin1Char('0')).arg((t / 100) % 10);
}
}  // namespace

RunCompareWindow::RunCompareWindow(const ColorRules *rules, QWidget *parent)
    : QWidget(parent, Qt::Window), m_rules(rules)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("runCompareWindow"));
    setWindowTitle(tr("Compare two runs"));
    resize(1280, 860);

    auto *grid = new QGridLayout;
    buildSide(A, grid, 0);
    buildSide(B, grid, 1);

    m_alignBox = new QComboBox(this);
    m_alignBox->setObjectName(QStringLiteral("runCompareAlign"));
    m_alignBox->addItem(tr("the first DMI frame"), int(AlignAt::FirstDmi));
    m_alignBox->addItem(tr("the first FSB or EB"), int(AlignAt::FirstBrake));
    m_alignBox->addItem(tr("the first change of mode"), int(AlignAt::FirstModeChange));
    auto *alignBtn = new QPushButton(tr("Align"), this);
    connect(alignBtn, &QPushButton::clicked, this, [this]() {
        QString why;
        if (align(AlignAt(m_alignBox->currentData().toInt()), &why))
            m_status->ok(tr("Both runs start at %1.").arg(m_alignBox->currentText()));
        else
            m_status->warn(why);
    });

    m_play = new QPushButton(tr("▶ Play"), this);
    m_play->setObjectName(QStringLiteral("runComparePlay"));
    m_play->setShortcut(QKeySequence(QStringLiteral("F5")));
    connect(m_play, &QPushButton::clicked, this, [this]() { m_playing ? pause() : play(); });
    m_speed = new QComboBox(this);
    m_speed->setObjectName(QStringLiteral("runCompareSpeed"));
    for (int x : { 1, 2, 4, 8, 16 }) m_speed->addItem(tr("%1×").arg(x), x);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setObjectName(QStringLiteral("runCompareSlider"));
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        if (!m_updating) setT(qint64(v) * 100);
    });
    m_clock = new QLabel(this);
    m_clock->setObjectName(QStringLiteral("runCompareClock"));

    auto *bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("Align at"), this));
    bar->addWidget(m_alignBox);
    bar->addWidget(alignBtn);
    bar->addSpacing(16);
    bar->addWidget(m_play);
    bar->addWidget(m_speed);
    bar->addWidget(m_slider, 1);
    bar->addWidget(m_clock);

    m_diffs = new QTableWidget(0, 3, this);
    m_diffs->setObjectName(QStringLiteral("runCompareDiffs"));
    m_diffs->setHorizontalHeaderLabels({ tr("On the DMI"), tr("Run A"), tr("Run B") });
    // The two runs' values get equal room; the field names what they need.
    m_diffs->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_diffs->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_diffs->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_diffs->verticalHeader()->hide();
    m_diffs->verticalHeader()->setDefaultSectionSize(m_diffs->fontMetrics().lineSpacing() + 6);
    m_diffs->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_diffs->setWordWrap(false);
    m_diffs->setMinimumHeight(m_diffs->fontMetrics().lineSpacing() * 8);
    m_same = new QLabel(this);
    m_same->setStyleSheet(UiColor::mutedStyle());
    m_status = new StatusLine(this);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(grid, 3);
    lay->addLayout(bar);
    lay->addWidget(new QLabel(tr("What differs between the two DMIs at this moment:"), this));
    lay->addWidget(m_diffs, 1);
    lay->addWidget(m_same);
    lay->addWidget(m_status);

    m_timer = new QTimer(this);
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        advance(now - m_lastTick);
        m_lastTick = now;
    });
    refresh();
}

void RunCompareWindow::buildSide(Side side, QGridLayout *grid, int column)
{
    Run &r = m_run[side];
    const QString name = side == A ? tr("Run A") : tr("Run B");
    r.title = new QLabel(tr("%1: not loaded").arg(name), this);
    r.title->setObjectName(side == A ? QStringLiteral("runCompareTitleA") : QStringLiteral("runCompareTitleB"));
    auto *open = new QPushButton(tr("Open…"), this);
    open->setToolTip(tr("One or more .dlr files of one run, or a review pack (.zip)"));
    connect(open, &QPushButton::clicked, this, [this, side]() { openFiles(side); });
    r.locoBox = new QComboBox(this);
    connect(r.locoBox, QOverload<int>::of(&QComboBox::activated), this,
            [this, side](int) { setLoco(side, m_run[side].locoBox->currentData().toString()); });
    r.startEdit = new QDateTimeEdit(this);
    r.startEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd  HH:mm:ss.zzz"));
    r.startEdit->setToolTip(tr("The moment of this run that the clock calls T+0"));
    connect(r.startEdit, &QDateTimeEdit::dateTimeChanged, this, [this, side](const QDateTime &dt) {
        if (!m_updating) setStart(side, dt.toMSecsSinceEpoch());
    });
    r.view = new DmiView(this);
    r.view->setObjectName(side == A ? QStringLiteral("runCompareDmiA") : QStringLiteral("runCompareDmiB"));
    r.view->setEmptyText(tr("Open a run"));
    r.caption = new QLabel(this);
    r.caption->setAlignment(Qt::AlignCenter);

    auto *head = new QHBoxLayout;
    head->addWidget(r.title, 1);
    head->addWidget(open);
    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Loco"), this));
    row->addWidget(r.locoBox);
    row->addWidget(new QLabel(tr("T+0 at"), this));
    row->addWidget(r.startEdit, 1);
    grid->addLayout(head, 0, column);
    grid->addLayout(row, 1, column);
    grid->addWidget(r.view, 2, column);
    grid->addWidget(r.caption, 3, column);
    grid->setRowStretch(2, 1);
}

void RunCompareWindow::openFiles(Side side)
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, side == A ? tr("Run A") : tr("Run B"), QDir::homePath(),
                                                            tr("Recordings and review packs (*.dlr *.zip);;All files (*)"));
    if (paths.isEmpty()) return;
    QString err;
    if (!loadRun(side, paths, &err)) m_status->fail(err);
}

bool RunCompareWindow::loadRun(Side side, const QStringList &paths, QString *err)
{
    QStringList dlrs;
    for (const QString &p : paths) {
        if (p.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) {
            const ReviewPack::Opened pack = ReviewPack::open(p, ReviewPack::defaultDir(p));
            if (!pack.ok) {
                if (err) *err = pack.error;
                return false;
            }
            dlrs << pack.dlrs;
        } else {
            dlrs << p;
        }
    }
    QHash<QString, QVector<Frame>> frames;
    for (const QString &path : qAsConst(dlrs)) {
        SessionReader reader;
        if (!reader.open(path)) continue;
        while (reader.next()) {
            const LogEntryPtr e = MessageDispatcher::buildEntry(reader.wire(), reader.arrivalMs(), m_rules);
            if (!e) continue;
            const QString key = dmiKeyOfText(e->text);
            if (!key.isEmpty()) frames[key].append({ e->epochMs, e->text });
        }
    }
    if (frames.isEmpty()) {
        if (err) *err = tr("No @dmi frames in %1: nothing to show on a DMI.")
                            .arg(QFileInfo(paths.value(0)).fileName());
        return false;
    }
    for (auto it = frames.begin(); it != frames.end(); ++it)
        std::stable_sort(it->begin(), it->end(), [](const Frame &x, const Frame &y) { return x.ms < y.ms; });

    Run &r = m_run[side];
    r.files = paths;
    r.frames = frames;
    m_updating = true;
    r.locoBox->clear();
    QStringList keys = frames.keys();
    keys.sort();
    for (const QString &k : keys) r.locoBox->addItem(tr("Loco %1").arg(k), k);
    m_updating = false;
    r.title->setText(tr("%1: %2%3")
                         .arg(side == A ? tr("Run A") : tr("Run B"), QFileInfo(paths.first()).fileName())
                         .arg(paths.size() > 1 ? tr(" (+%1)").arg(paths.size() - 1) : QString()));
    r.title->setToolTip(paths.join(QLatin1Char('\n')));
    // The same loco as the other run when it has it, else its first.
    const QString other = m_run[side == A ? B : A].loco;
    setLoco(side, frames.contains(other) ? other : keys.first());
    return true;
}

QStringList RunCompareWindow::locos(Side side) const
{
    QStringList k = m_run[side].frames.keys();
    k.sort();
    return k;
}

QString RunCompareWindow::loco(Side side) const { return m_run[side].loco; }

void RunCompareWindow::setLoco(Side side, const QString &key)
{
    Run &r = m_run[side];
    if (!r.frames.contains(key)) return;
    r.loco = key;
    m_updating = true;
    r.locoBox->setCurrentIndex(r.locoBox->findData(key));
    m_updating = false;
    setStart(side, r.frames.value(key).first().ms);
}

void RunCompareWindow::setStart(Side side, qint64 ms)
{
    Run &r = m_run[side];
    r.startMs = ms;
    m_updating = true;
    r.startEdit->setDateTime(QDateTime::fromMSecsSinceEpoch(ms));
    m_updating = false;
    refresh();
}

bool RunCompareWindow::align(AlignAt at, QString *why)
{
    qint64 found[2] = { 0, 0 };
    for (int s = 0; s < 2; ++s) {
        const Run &r = m_run[s];
        const QVector<Frame> frames = r.frames.value(r.loco);
        if (frames.isEmpty()) {
            if (why) *why = tr("Open both runs first.");
            return false;
        }
        if (at == AlignAt::FirstDmi) {
            found[s] = frames.first().ms;
            continue;
        }
        QString firstMode;
        for (const Frame &f : frames) {
            const DmiState st = dmiStateFromLine(f.line);
            if (!st.valid) continue;
            if (at == AlignAt::FirstBrake && (st.brakeType == 3 || st.brakeType == 4)) { found[s] = f.ms; break; }
            if (at == AlignAt::FirstModeChange) {
                if (firstMode.isNull()) firstMode = st.modeText;
                else if (st.modeText != firstMode) { found[s] = f.ms; break; }
            }
        }
        if (!found[s]) {
            if (why) *why = tr("%1 (loco %2) has no %3: its start is left as it was.")
                                .arg(s == A ? tr("Run A") : tr("Run B"), r.loco,
                                     at == AlignAt::FirstBrake ? tr("FSB or EB") : tr("change of mode"));
            return false;
        }
    }
    setStart(A, found[A]);
    setStart(B, found[B]);
    setT(0);
    return true;
}

qint64 RunCompareWindow::length() const
{
    qint64 len = 0;
    for (const Run &r : m_run) {
        const QVector<Frame> f = r.frames.value(r.loco);
        if (!f.isEmpty()) len = qMax(len, f.last().ms - r.startMs);
    }
    return len;
}

void RunCompareWindow::setT(qint64 ms)
{
    m_t = qBound<qint64>(0, ms, qMax<qint64>(0, length()));
    refresh();
}

void RunCompareWindow::setSpeed(int times)
{
    const int i = m_speed->findData(times);
    if (i >= 0) m_speed->setCurrentIndex(i);
}

void RunCompareWindow::play()
{
    if (length() <= 0) return;
    if (m_t >= length()) m_t = 0;
    m_playing = true;
    m_lastTick = QDateTime::currentMSecsSinceEpoch();
    m_timer->start();
    refresh();
}

void RunCompareWindow::pause()
{
    m_playing = false;
    m_timer->stop();
    refresh();
}

void RunCompareWindow::advance(qint64 wallMs)
{
    if (!m_playing) return;
    const qint64 next = m_t + wallMs * qMax(1, m_speed->currentData().toInt());
    if (next >= length()) {
        m_t = length();
        pause();
        m_status->ok(tr("End of both runs."));
        return;
    }
    setT(next);
}

const RunCompareWindow::Frame *RunCompareWindow::frameAt(Side side, qint64 ms) const
{
    const Run &r = m_run[side];
    auto it = r.frames.constFind(r.loco);
    if (it == r.frames.constEnd() || it->isEmpty()) return nullptr;
    auto after = std::upper_bound(it->begin(), it->end(), ms, [](qint64 v, const Frame &f) { return v < f.ms; });
    if (after == it->begin()) return nullptr;
    return &*(after - 1);
}

DmiState RunCompareWindow::state(Side side) const
{
    const Frame *f = frameAt(side, m_run[side].startMs + m_t);
    return f ? dmiStateFromLine(f->line) : DmiState();
}

qint64 RunCompareWindow::frameMs(Side side) const
{
    const Frame *f = frameAt(side, m_run[side].startMs + m_t);
    return f ? f->ms : 0;
}

QVector<DmiDiff::Item> RunCompareWindow::differences() const
{
    return DmiDiff::compare(state(A), state(B));
}

void RunCompareWindow::refreshSide(Side side)
{
    Run &r = m_run[side];
    if (r.loco.isEmpty()) {
        r.view->setState(DmiState());
        r.caption->clear();
        return;
    }
    const qint64 at = r.startMs + m_t;
    const Frame *f = frameAt(side, at);
    if (!f) {
        r.view->setState(DmiState());
        r.view->setEmptyText(tr("No @dmi of loco %1 yet").arg(r.loco));
        r.caption->setText(tr("Loco %1 · %2 · before its first frame")
                               .arg(r.loco, QDateTime::fromMSecsSinceEpoch(at).toString(QStringLiteral("HH:mm:ss.z"))));
        return;
    }
    r.view->setState(dmiStateFromLine(f->line));
    const qint64 age = at - f->ms;
    r.view->setStale(age > kStaleMs);
    r.caption->setText(age > kStaleMs
                           ? tr("Loco %1 · %2 · last frame %3 s before")
                                 .arg(r.loco, QDateTime::fromMSecsSinceEpoch(at).toString(QStringLiteral("HH:mm:ss.z")))
                                 .arg(age / 1000)
                           : tr("Loco %1 · %2")
                                 .arg(r.loco, QDateTime::fromMSecsSinceEpoch(at).toString(QStringLiteral("HH:mm:ss.z"))));
}

void RunCompareWindow::refresh()
{
    refreshSide(A);
    refreshSide(B);
    const qint64 len = length();
    m_updating = true;
    m_slider->setRange(0, int(len / 100));
    m_slider->setValue(int(m_t / 100));
    m_updating = false;
    m_clock->setText(tr("T+%1 / %2").arg(clockText(m_t), clockText(len)));
    m_play->setText(m_playing ? tr("❚❚ Pause") : tr("▶ Play"));

    const QVector<DmiDiff::Item> d = (m_run[A].loco.isEmpty() || m_run[B].loco.isEmpty())
                                         ? QVector<DmiDiff::Item>() : differences();
    m_diffs->setRowCount(d.size());
    for (int i = 0; i < d.size(); ++i) {
        m_diffs->setItem(i, 0, new QTableWidgetItem(d.at(i).label));
        m_diffs->setItem(i, 1, new QTableWidgetItem(d.at(i).a));
        m_diffs->setItem(i, 2, new QTableWidgetItem(d.at(i).b));
    }
    m_same->setText(m_run[A].loco.isEmpty() || m_run[B].loco.isEmpty()
                        ? tr("Open both runs to compare them.")
                        : d.isEmpty() ? tr("The two DMIs show the same.") : QString());
}

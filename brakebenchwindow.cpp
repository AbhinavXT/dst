#include "brakebenchwindow.h"

#include "biusim.h"
#include "brakebenchpreset.h"
#include "pressuregauge.h"
#include "settings.h"
#include "simwidgets.h"
#include "statusline.h"
#include "udpsender.h"
#include "uicolors.h"
#include "windowgeometry.h"
#include "flasher/flashercore.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QPair>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

using namespace CabIo;
using SimUi::segmented;
using SimUi::SignalLamp;

namespace {

constexpr int kLogCap = 2000;

QString hexN(quint32 w, int digits)
{
    return QStringLiteral("0x") + QString::number(w, 16).toUpper().rightJustified(digits, QLatin1Char('0'));
}

QString kg(double v) { return QString::number(v, 'f', 2); }

QString ago(qint64 atMs, qint64 now)
{
    if (atMs <= 0) return QStringLiteral("never");
    const double s = (now - atMs) / 1000.0;
    if (s < 60) return QStringLiteral("%1 s ago").arg(s, 0, 'f', 1);
    return QStringLiteral("%1 min ago").arg(int(s / 60));
}

QString idRange(const QVector<quint8> &ids)
{
    QStringList s;
    for (quint8 id : ids) s << QString::number(id);
    return s.isEmpty() ? QStringLiteral("none") : s.join(QStringLiteral(", "));
}

// A card: a framed panel with a title, its body laid out by the caller.
QFrame *card(const QString &title, QWidget *parent, QVBoxLayout **body)
{
    auto *c = new QFrame(parent);
    c->setObjectName(QStringLiteral("card"));
    auto *v = new QVBoxLayout(c);
    v->setContentsMargins(14, 10, 14, 12);
    v->setSpacing(8);
    if (!title.isEmpty()) {
        auto *t = new QLabel(title, c);
        t->setObjectName(QStringLiteral("cardTitle"));
        v->addWidget(t);
    }
    *body = v;
    return c;
}

QLabel *muted(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("muted"));
    return l;
}

QDoubleSpinBox *kgSpin(QWidget *parent, double max = 12.75, double step = 0.05, int decimals = 2)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(0, max);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    return s;
}

QWidget *scrolled(QWidget *content, QWidget *parent)
{
    auto *sa = new QScrollArea(parent);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidget(content);
    return sa;
}

}  // namespace

// =============================================================================
//  The window
// =============================================================================

BrakeBenchWindow::BrakeBenchWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setObjectName(QStringLiteral("brakeBenchWindow"));
    setWindowTitle(tr("Brake Test Bench"));
    setAttribute(Qt::WA_DeleteOnClose);
    WindowGeometry::makeResizableWindow(this);
    m_clock = []() { return QDateTime::currentMSecsSinceEpoch(); };

    m_biu = new BiuSimulator(this);
    m_anUdp = new UdpSender(this);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(10);
    root->addWidget(buildTopBar());

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(buildBenchTab(),   tr("Bench"));
    m_tabs->addTab(buildStatusTab(),  tr("BIU \u2192 VCC"));
    m_tabs->addTab(buildNetworkTab(), tr("Network"));
    m_tabs->addTab(buildAnalogTab(),  tr("Analog \u2192 VCC"));
    m_tabs->addTab(buildRelaysTab(),  tr("Brake relays"));
    m_tabs->addTab(buildFramesTab(),  tr("Frames"));
    root->addWidget(m_tabs, 1);

    m_status = new StatusLine(this);
    root->addWidget(m_status);

    connect(m_biu, &BiuSimulator::logged, this, [this](const BiuSimulator::LogItem &i) {
        const bool hb = i.frame.cob == Biu::kCobHeartbeatBiu || i.frame.cob == Biu::kCobHeartbeatKavach
                        || i.frame.cob == Biu::kCobNmt;
        logRow(i.ms, i.tx, hexN(i.frame.cob, 3), Biu::frameName(i.frame.cob), i.frame.data,
               Biu::describe(i.frame), i.note, hb);
    });
    connect(m_biu, &BiuSimulator::problem, this, [this](const QString &what) { m_status->warn(what); });
    connect(m_anUdp, &UdpSender::error, this, [this](const QString &what) {
        m_status->fail(tr("Analog over UDP: %1").arg(what));
    });

    m_anTimer = new QTimer(this);
    m_anTimer->setTimerType(Qt::PreciseTimer);
    m_anTimer->setInterval(100);
    connect(m_anTimer, &QTimer::timeout, this, &BrakeBenchWindow::sendAnalogNow);

    // The BIU's clock: 50 ms, so the 250 ms status frames and 500 ms
    // heartbeat leave on time and the air moves smoothly.
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(50);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        const qint64 t = now();
        const double dt = m_lastAdvanceMs ? qBound(0.0, (t - m_lastAdvanceMs) / 1000.0, 0.5) : 0.05;
        advance(dt);
    });
    m_timer->start();

    m_uiTimer = new QTimer(this);
    m_uiTimer->setInterval(250);
    connect(m_uiTimer, &QTimer::timeout, this, &BrakeBenchWindow::onUiTick);
    m_uiTimer->start();

    loadSettings();
    m_clean = currentPreset();
    restyle();
    UiColor::onThemeChange(this, [this]() { restyle(); });
    onUiTick();

    resize(1320, 800);
    WindowGeometry::restore(this, QStringLiteral("brakeBench"));
    m_status->state(tr("Press Start BIU to listen on UDP %1 and answer the VCC.").arg(listenPort()));
}

BrakeBenchWindow::~BrakeBenchWindow() = default;

void BrakeBenchWindow::closeEvent(QCloseEvent *e)
{
    saveSettings();
    WindowGeometry::save(this, QStringLiteral("brakeBench"));
    m_biu->setRunning(false, now());
    m_biu->close();
    QWidget::closeEvent(e);
}

void BrakeBenchWindow::restyle()
{
    setStyleSheet(SimUi::baseStyleSheet()
                  + QStringLiteral("QLabel#bigValue { font-size: 15pt; font-weight: 600; }"
                                   "QLabel#gaugeNote { font-size: 9pt; }"));
    for (QLabel *c : m_cmdBits1) styleChip(c, c->property("on").toBool(), c->property("known").toBool());
    for (QLabel *c : m_cmdBits2) styleChip(c, c->property("on").toBool(), c->property("known").toBool());
    for (auto &byte : m_bitNow)
        for (QLabel *c : byte)
            if (c) styleChip(c, c->property("on").toBool(), true);
    for (QLabel *c : m_pValidNow)
        if (c) styleChip(c, c->property("on").toBool(), true);
    for (SignalLamp *l : m_relayLamps) l->update();
    refreshTop();
}

// A bit as a chip: lit (accent, Base text, the checked-segment colours that
// the contrast audit holds) or plain (a frame and muted text). Its own style
// sheet, set when it changes: a property selector kept Qt 5's labels in the
// last theme's colours (session 358).
void BrakeBenchWindow::styleChip(QLabel *chip, bool on, bool known)
{
    chip->setProperty("on", on);
    chip->setProperty("known", known);
    const QPalette pal = palette();
    const QString frame = UiColor::frame().name();
    if (on && known)
        chip->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border: 1px solid %1; border-radius: 10px; padding: 1px 8px; font-weight: 600; }")
                                .arg(UiColor::accent().name(), pal.color(QPalette::Base).name()));
    else
        chip->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border: 1px %4 %3; border-radius: 10px; padding: 1px 8px; }")
                                .arg(pal.color(QPalette::Base).name(), UiColor::muted().name(), frame,
                                     known ? QStringLiteral("solid") : QStringLiteral("dashed")));
}

// ---- building ----------------------------------------------------------------------

QWidget *BrakeBenchWindow::buildTopBar()
{
    auto *bar = new QWidget(this);
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    m_startBtn = new QPushButton(tr("Start BIU"), bar);
    m_startBtn->setObjectName(QStringLiteral("primary"));
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setToolTip(tr("Listen for the VCC's CAN frames over UDP and answer as a BIU: heartbeat, NMT, "
                              "status frames. Leave stopped while a real BIU is on the line."));
    connect(m_startBtn, &QPushButton::clicked, this, [this]() { if (biuRunning()) stopBiu(); else startBiu(); });
    h->addWidget(m_startBtn);
    // Session 362: the fault setup saved under a name, loaded on request.
    m_presetBtn = new QToolButton(bar);
    m_presetBtn->setObjectName(QStringLiteral("benchPresets"));
    m_presetBtn->setText(tr("Presets"));
    m_presetBtn->setPopupMode(QToolButton::InstantPopup);
    m_presetBtn->setToolTip(tr("Save the fault setup under a name, or load one: plant faults and rates, analog offsets, "
                               "manual pressures, validity, forced bits, queued codes, frames off, silence.\n"
                               "The network, the analog card and the brake relays are not part of it."));
    auto *presetMenu = new QMenu(m_presetBtn);
    connect(presetMenu, &QMenu::aboutToShow, this, &BrakeBenchWindow::buildPresetMenu);
    m_presetBtn->setMenu(presetMenu);
    h->addWidget(m_presetBtn);
    m_runState = new QLabel(bar);
    h->addWidget(m_runState);
    h->addStretch(1);
    m_kavachChip = new QLabel(bar);
    m_kavachChip->setToolTip(tr("KAVACH's heartbeat (0x720) as the BIU last heard it"));
    h->addWidget(m_kavachChip);
    m_cmdChip = new QLabel(bar);
    m_cmdChip->setObjectName(QStringLiteral("muted"));
    m_cmdChip->setToolTip(tr("KAVACH's command frames (0x190): every 250 ms is the spec's period"));
    h->addWidget(m_cmdChip);
    return bar;
}

QWidget *BrakeBenchWindow::buildBenchTab()
{
    auto *page = new QWidget(this);
    auto *outer = new QHBoxLayout(page);
    outer->setContentsMargins(0, 10, 0, 0);
    outer->setSpacing(12);

    // ---- left: the air ----
    QVBoxLayout *pv = nullptr;
    QFrame *pressures = card(QString(), page, &pv);
    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    auto *title = new QLabel(tr("Pressures"), pressures);
    title->setObjectName(QStringLiteral("cardTitle"));
    row->addWidget(title);
    row->addStretch(1);
    row->addWidget(muted(tr("follow"), pressures));
    row->addWidget(segmented({ tr("BIU command"), tr("Brake relays"), tr("Manual") }, &m_sourceGroup, pressures));
    m_sourceGroup->button(0)->setToolTip(tr("BP and BC go where the BIU takes them: KAVACH's A9 / SA9 when valid, "
                                            "emergency when KAVACH is lost, released when isolated"));
    m_sourceGroup->button(1)->setToolTip(tr("BP, BC and MR go to the values set for the strongest brake relay "
                                            "(Brake relays tab, from the Cab I/O Simulator's output word)"));
    m_sourceGroup->button(2)->setToolTip(tr("Only a hand on a dial moves them"));
    connect(m_sourceGroup, &QButtonGroup::idClicked, this, [this](int id) { setSource(Source(id)); });
    pv->addLayout(row);

    auto *dials = new QHBoxLayout;
    dials->setSpacing(10);
    static const char *const names[BrakePlant::kChannels] = { "BP", "BC", "MR" };
    for (int ch = 0; ch < BrakePlant::kChannels; ++ch) {
        auto *col = new QVBoxLayout;
        col->setSpacing(4);
        auto *g = new PressureGauge(QString::fromLatin1(names[ch]), pressures);
        g->setObjectName(QStringLiteral("benchGauge"));
        g->setMaximum(10.2);
        g->setMinimumSize(150, 150);
        g->setToolTip(tr("%1 on the bench. Drag to set it by hand; the green mark is where it is going.")
                          .arg(QString::fromLatin1(names[ch])));
        connect(g, &PressureGauge::valueEdited, this, [this, ch](double v) {
            m_plant.setValue(ch, v);
            if (m_source == Source::Manual) m_plant.setTarget(ch, v);
            feedAnalog();
            refreshBench();
        });
        m_gauge[ch] = g;
        col->addWidget(g, 1);
        m_gaugeNote[ch] = new QLabel(pressures);
        m_gaugeNote[ch]->setObjectName(QStringLiteral("gaugeNote"));
        m_gaugeNote[ch]->setAlignment(Qt::AlignHCenter);
        m_gaugeNote[ch]->setWordWrap(true);
        col->addWidget(m_gaugeNote[ch]);
        dials->addLayout(col, 1);
    }
    pv->addLayout(dials, 1);
    m_targetWhy = muted(QString(), pressures);
    m_targetWhy->setWordWrap(true);
    pv->addWidget(m_targetWhy);

    // Rates and faults, a row per pressure.
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    const QStringList heads{ QString(), tr("Apply"), tr("Release"), tr("Fault"), QString(), tr("Analog reads") };
    for (int c = 0; c < heads.size(); ++c) {
        auto *hl = new QLabel(heads.at(c).toUpper(), pressures);
        hl->setObjectName(QStringLiteral("groupHeading"));
        grid->addWidget(hl, 0, c);
    }
    for (int ch = 0; ch < BrakePlant::kChannels; ++ch) {
        grid->addWidget(new QLabel(QString::fromLatin1(names[ch]), pressures), ch + 1, 0);
        m_apply[ch] = kgSpin(pressures, 20, 0.1, 2);
        m_apply[ch]->setSuffix(tr(" /s"));
        m_apply[ch]->setToolTip(ch == BrakePlant::Mr ? tr("MR charging, kg/cm\u00B2 per second")
                                                     : tr("%1 toward more braking, kg/cm\u00B2 per second").arg(QString::fromLatin1(names[ch])));
        m_release[ch] = kgSpin(pressures, 20, 0.1, 2);
        m_release[ch]->setSuffix(tr(" /s"));
        m_release[ch]->setToolTip(tr("%1 back toward released, kg/cm\u00B2 per second").arg(QString::fromLatin1(names[ch])));
        for (QDoubleSpinBox *s : { m_apply[ch], m_release[ch] })
            connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, ch]() {
                m_plant.setRates(ch, m_apply[ch]->value(), m_release[ch]->value());
                saveSettings();
            });
        grid->addWidget(m_apply[ch], ch + 1, 1);
        grid->addWidget(m_release[ch], ch + 1, 2);
        m_faultBox[ch] = new QComboBox(pressures);
        m_faultBox[ch]->addItem(tr("None"), int(BrakePlant::Fault::None));
        m_faultBox[ch]->addItem(tr("Stuck"), int(BrakePlant::Fault::Stuck));
        m_faultBox[ch]->addItem(tr("Slow \u00D7"), int(BrakePlant::Fault::Slow));
        m_faultBox[ch]->addItem(tr("Leak /s"), int(BrakePlant::Fault::Leak));
        m_faultBox[ch]->setToolTip(tr("Stuck: does not move. Slow: its rates divided by the number. "
                                      "Leak: falls by the number each second, whatever it is asked for."));
        m_faultParam[ch] = kgSpin(pressures, 50, 0.1, 1);
        m_faultParam[ch]->setValue(ch == BrakePlant::Mr ? 0.2 : 3.0);
        auto applyFault = [this, ch]() {
            m_plant.setFault(ch, BrakePlant::Fault(m_faultBox[ch]->currentData().toInt()), m_faultParam[ch]->value());
            m_faultParam[ch]->setEnabled(m_faultBox[ch]->currentIndex() >= 2);
        };
        connect(m_faultBox[ch], QOverload<int>::of(&QComboBox::currentIndexChanged), this, applyFault);
        connect(m_faultParam[ch], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, applyFault);
        m_faultParam[ch]->setEnabled(false);
        grid->addWidget(m_faultBox[ch], ch + 1, 3);
        grid->addWidget(m_faultParam[ch], ch + 1, 4);
        m_offsetBox[ch] = new QDoubleSpinBox(pressures);
        m_offsetBox[ch]->setRange(-10, 10);
        m_offsetBox[ch]->setDecimals(2);
        m_offsetBox[ch]->setSingleStep(0.1);
        m_offsetBox[ch]->setPrefix(QStringLiteral("\u00B1 "));
        m_offsetBox[ch]->setToolTip(tr("The analog card reads %1 off by this from what the BIU reports: "
                                       "for the firmware's cross-check").arg(QString::fromLatin1(names[ch])));
        connect(m_offsetBox[ch], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, ch](double v) { m_analogOffset[ch] = v; feedAnalog(); });
        grid->addWidget(m_offsetBox[ch], ch + 1, 5);
    }
    grid->setColumnStretch(6, 1);
    pv->addLayout(grid);
    outer->addWidget(pressures, 3);

    // ---- right: KAVACH and the BIU ----
    auto *right = new QVBoxLayout;
    right->setSpacing(12);
    QVBoxLayout *cv = nullptr;
    QFrame *cmd = card(tr("KAVACH command"), page, &cv);
    auto *vals = new QGridLayout;
    vals->setHorizontalSpacing(14);
    vals->setVerticalSpacing(0);
    m_cmdSpeed = new QLabel(QStringLiteral("\u2014"), cmd);
    m_cmdA9 = new QLabel(QStringLiteral("\u2014"), cmd);
    m_cmdSa9 = new QLabel(QStringLiteral("\u2014"), cmd);
    const QStringList cmdHeads{ tr("Speed"), tr("A9 \u2192 BP"), tr("SA9 \u2192 BC") };
    QLabel *const big[3] = { m_cmdSpeed, m_cmdA9, m_cmdSa9 };
    for (int i = 0; i < 3; ++i) {
        auto *hl = new QLabel(cmdHeads.at(i).toUpper(), cmd);
        hl->setObjectName(QStringLiteral("groupHeading"));
        vals->addWidget(hl, 0, i);
        big[i]->setObjectName(QStringLiteral("bigValue"));
        big[i]->setTextInteractionFlags(Qt::TextSelectableByMouse);
        vals->addWidget(big[i], 1, i);
    }
    vals->setColumnStretch(3, 1);
    cv->addLayout(vals);
    auto *chips = new QGridLayout;
    chips->setHorizontalSpacing(6);
    chips->setVerticalSpacing(6);
    const QStringList b1 = Biu::commandBitNames(1), b2 = Biu::commandBitNames(2);
    int n = 0;
    for (const QString &name : b1) {
        auto *c = new QLabel(name, cmd);
        c->setToolTip(tr("Discrete byte 1, bit %1").arg(n));
        m_cmdBits1 << c;
        chips->addWidget(c, n / 3, n % 3);
        ++n;
    }
    int k = 0;
    for (const QString &name : b2) {
        auto *c = new QLabel(name, cmd);
        c->setToolTip(tr("Discrete byte 2, bit %1").arg(k++));
        m_cmdBits2 << c;
        chips->addWidget(c, n / 3, n % 3);
        ++n;
    }
    cv->addLayout(chips);
    m_cmdMeta = muted(QString(), cmd);
    m_cmdMeta->setWordWrap(true);
    cv->addWidget(m_cmdMeta);
    right->addWidget(cmd);

    QVBoxLayout *bv = nullptr;
    QFrame *biuCard = card(tr("BIU"), page, &bv);
    m_biuNow = new QLabel(biuCard);
    m_biuNow->setWordWrap(true);
    m_biuNow->setTextFormat(Qt::RichText);
    bv->addWidget(m_biuNow);
    auto *quick = new QHBoxLayout;
    m_isolateBox = new QCheckBox(tr("Isolate the BIU"), biuCard);
    m_isolateBox->setToolTip(tr("The BIU's manual isolate switch: it stops applying KAVACH's brakes, says so in "
                                "discrete byte 1 and raises 0x1040 BIU ISOLATED"));
    connect(m_isolateBox, &QCheckBox::toggled, this, [this](bool on) {
        m_biu->setBitMode(1, Biu::StManualIsolate, on ? BiuSimulator::BitMode::On : BiuSimulator::BitMode::Auto);
        refreshStatusTab();
    });
    quick->addWidget(m_isolateBox);
    m_silentBox = new QCheckBox(tr("Silence the BIU"), biuCard);
    m_silentBox->setToolTip(tr("Send nothing at all, heartbeat included: a CAN failure as KAVACH sees it"));
    connect(m_silentBox, &QCheckBox::toggled, this, [this](bool on) { m_biu->setSilent(on); });
    quick->addWidget(m_silentBox);
    quick->addStretch(1);
    bv->addLayout(quick);
    right->addWidget(biuCard);
    right->addStretch(1);
    outer->addLayout(right, 2);
    return scrolled(page, this);
}

QWidget *BrakeBenchWindow::buildStatusTab()
{
    auto *page = new QWidget(this);
    auto *outer = new QHBoxLayout(page);
    outer->setContentsMargins(0, 10, 0, 0);
    outer->setSpacing(12);
    auto *left = new QVBoxLayout;
    left->setSpacing(12);

    // ---- pressures frame ----
    QVBoxLayout *pv = nullptr;
    QFrame *pf = card(tr("Pressures \u00B7 0x220"), page, &pv);
    auto *pIntro = muted(tr("Auto sends the bench's BP, BC and MR and the references the BIU works to; Manual "
                            "sends the value set. The validity bit on Auto is 1 up to the spec's limit."), pf);
    pIntro->setWordWrap(true);
    pv->addWidget(pIntro);
    auto *g = new QGridLayout;
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(5);
    const QStringList heads{ QString(), tr("Sends"), tr("Source"), tr("kg/cm\u00B2"), tr("Validity bit"), QString() };
    for (int c = 0; c < heads.size(); ++c) {
        auto *hl = new QLabel(heads.at(c).toUpper(), pf);
        hl->setObjectName(QStringLiteral("groupHeading"));
        g->addWidget(hl, 0, c);
    }
    for (int id = 0; id < Biu::kPressures; ++id) {
        auto *name = new QLabel(Biu::pressureName(id), pf);
        name->setToolTip(tr("%1; valid up to %2 kg/cm\u00B2").arg(Biu::pressureLongName(id)).arg(Biu::validLimit(id), 0, 'f', 1));
        g->addWidget(name, id + 1, 0);
        m_pSends[id] = new QLabel(pf);
        m_pSends[id]->setObjectName(QStringLiteral("mono"));
        m_pSends[id]->setMinimumWidth(70);
        g->addWidget(m_pSends[id], id + 1, 1);
        g->addWidget(segmented({ tr("Auto"), tr("Manual") }, &m_pMode[id], pf), id + 1, 2);
        m_pManual[id] = kgSpin(pf);
        m_pManual[id]->setValue(m_biu->pressureManualValue(id));
        g->addWidget(m_pManual[id], id + 1, 3);
        auto apply = [this, id]() {
            m_biu->setPressureManual(id, m_pMode[id]->checkedId() == 1, m_pManual[id]->value());
            m_pManual[id]->setEnabled(m_pMode[id]->checkedId() == 1);
            refreshStatusTab();
        };
        connect(m_pMode[id], &QButtonGroup::idClicked, this, apply);
        connect(m_pManual[id], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, apply);
        m_pManual[id]->setEnabled(false);
        g->addWidget(segmented({ tr("Auto"), QStringLiteral("1"), QStringLiteral("0") }, &m_pValid[id], pf), id + 1, 4);
        m_pValid[id]->button(1)->setToolTip(tr("Valid, whatever the value"));
        m_pValid[id]->button(2)->setToolTip(tr("Not valid, whatever the value: KAVACH should not use it"));
        connect(m_pValid[id], &QButtonGroup::idClicked, this, [this, id](int v) {
            m_biu->setValidMode(id, BiuSimulator::ValidMode(v));
            refreshStatusTab();
        });
        m_pValidNow[id] = new QLabel(tr("valid"), pf);
        g->addWidget(m_pValidNow[id], id + 1, 5);
    }
    g->setColumnStretch(6, 1);
    pv->addLayout(g);
    left->addWidget(pf);

    // ---- codes frame ----
    QVBoxLayout *cv = nullptr;
    QFrame *cf = card(tr("Codes \u00B7 0x320"), page, &cv);
    auto *iso = new QHBoxLayout;
    iso->addWidget(new QLabel(tr("Isolation counter"), cf));
    m_isoCount = new QSpinBox(cf);
    m_isoCount->setRange(0, 65535);
    connect(m_isoCount, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { m_biu->setIsolationCounter(quint16(v)); });
    iso->addWidget(m_isoCount);
    m_countIso = new QCheckBox(tr("+1 on each isolation"), cf);
    connect(m_countIso, &QCheckBox::toggled, this, [this](bool on) { m_biu->setCountIsolations(on); saveSettings(); });
    iso->addWidget(m_countIso);
    m_raiseIso = new QCheckBox(tr("and raise 0x1040"), cf);
    connect(m_raiseIso, &QCheckBox::toggled, this, [this](bool on) { m_biu->setRaiseIsolatedFault(on); saveSettings(); });
    iso->addWidget(m_raiseIso);
    iso->addStretch(1);
    cv->addLayout(iso);

    auto codeRow = [this, cf, cv](const QString &label, const QVector<Biu::Code> &codes, QComboBox **pick,
                                  QLabel **queue, bool fault) {
        auto *r = new QHBoxLayout;
        r->addWidget(new QLabel(label, cf));
        auto *c = new QComboBox(cf);
        c->setEditable(true);
        c->setInsertPolicy(QComboBox::NoInsert);
        for (const Biu::Code &code : codes) c->addItem(QStringLiteral("%1  %2").arg(hexN(code.code, 4), code.text), int(code.code));
        c->setToolTip(tr("A code from the spec's list, or type one in hex (0x1048)"));
        c->setMinimumWidth(280);
        *pick = c;
        r->addWidget(c, 1);
        auto *raise = new QPushButton(tr("Raise"), cf);
        connect(raise, &QPushButton::clicked, this, [this, c, fault]() {
            bool ok = false;
            const int i = c->findText(c->currentText());
            quint16 code = i >= 0 ? quint16(c->itemData(i).toInt()) : 0;
            if (!code) code = quint16(c->currentText().section(QLatin1Char(' '), 0, 0).toUInt(&ok, 16));
            if (!code) { m_status->fail(tr("\u201C%1\u201D is not a code").arg(c->currentText())); return; }
            if (fault) m_biu->raiseFault(code); else m_biu->raiseDisplay(code);
            refreshStatusTab();
        });
        r->addWidget(raise);
        auto *clear = new QPushButton(tr("Clear"), cf);
        connect(clear, &QPushButton::clicked, this, [this, fault]() {
            if (fault) m_biu->clearFaults(); else m_biu->clearDisplays();
            refreshStatusTab();
        });
        r->addWidget(clear);
        cv->addLayout(r);
        *queue = muted(QString(), cf);
        (*queue)->setWordWrap(true);
        cv->addWidget(*queue);
    };
    codeRow(tr("Fault"), Biu::faultCodes(), &m_faultPick, &m_faultQueue, true);
    codeRow(tr("Display"), Biu::displayCodes(), &m_displayPick, &m_displayQueue, false);
    m_waitAck = new QCheckBox(tr("Hold each code until KAVACH acknowledges it (Annexure-1 3)"), cf);
    m_waitAck->setToolTip(tr("Off: keep sending a code, acknowledged or not, until it is cleared here"));
    connect(m_waitAck, &QCheckBox::toggled, this, [this](bool on) { m_biu->setWaitForAck(on); saveSettings(); });
    cv->addWidget(m_waitAck);
    m_ackNote = muted(QString(), cf);
    cv->addWidget(m_ackNote);
    left->addWidget(cf);
    left->addStretch(1);
    outer->addLayout(left, 3);

    // ---- discretes frame and timing ----
    auto *right = new QVBoxLayout;
    right->setSpacing(12);
    QVBoxLayout *dv = nullptr;
    QFrame *df = card(tr("Discretes \u00B7 0x420"), page, &dv);
    auto *dIntro = muted(tr("Auto is what a BIU would say; 1 and 0 force a bit. The chip is what goes out."), df);
    dIntro->setWordWrap(true);
    dv->addWidget(dIntro);
    auto *dg = new QGridLayout;
    dg->setHorizontalSpacing(8);
    dg->setVerticalSpacing(4);
    int row = 0;
    for (int byte = 1; byte <= 3; ++byte) {
        auto *hl = new QLabel(tr("Byte %1").arg(byte).toUpper(), df);
        hl->setObjectName(QStringLiteral("groupHeading"));
        dg->addWidget(hl, row++, 0, 1, 3);
        const QStringList names = Biu::statusBitNames(byte);
        for (int bit = 0; bit < names.size(); ++bit) {
            auto *name = new QLabel(names.at(bit), df);
            name->setToolTip(tr("Bit %1: %2").arg(bit).arg(Biu::statusBitMeaning(byte, bit)));
            dg->addWidget(name, row, 0);
            dg->addWidget(segmented({ tr("Auto"), QStringLiteral("1"), QStringLiteral("0") }, &m_bitMode[byte - 1][bit], df), row, 1);
            connect(m_bitMode[byte - 1][bit], &QButtonGroup::idClicked, this, [this, byte, bit](int m) {
                m_biu->setBitMode(byte, bit, BiuSimulator::BitMode(m));
                if (byte == 1 && bit == Biu::StManualIsolate) {
                    const QSignalBlocker b(m_isolateBox);
                    m_isolateBox->setChecked(m == int(BiuSimulator::BitMode::On));
                }
                refreshStatusTab();
            });
            m_bitNow[byte - 1][bit] = new QLabel(QStringLiteral("0"), df);
            m_bitNow[byte - 1][bit]->setAlignment(Qt::AlignCenter);
            m_bitNow[byte - 1][bit]->setMinimumWidth(28);
            dg->addWidget(m_bitNow[byte - 1][bit], row, 2);
            ++row;
        }
    }
    dg->setColumnStretch(3, 1);
    dv->addLayout(dg);
    right->addWidget(df);

    QVBoxLayout *tv = nullptr;
    QFrame *tf = card(tr("Timing and panel test"), page, &tv);
    auto *per = new QHBoxLayout;
    per->addWidget(new QLabel(tr("Every"), tf));
    m_statusPeriod = new QSpinBox(tf);
    m_statusPeriod->setRange(10, 10000);
    m_statusPeriod->setSingleStep(10);
    m_statusPeriod->setSuffix(tr(" ms"));
    m_statusPeriod->setToolTip(tr("The spec's period is 250 ms"));
    connect(m_statusPeriod, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) { m_biu->setStatusPeriodMs(ms); saveSettings(); });
    per->addWidget(m_statusPeriod);
    per->addWidget(muted(tr("send"), tf));
    const QStringList frames{ tr("Pressures"), tr("Codes"), tr("Discretes") };
    for (int i = 0; i < 3; ++i) {
        m_frameOn[i] = new QCheckBox(frames.at(i), tf);
        m_frameOn[i]->setChecked(true);
        m_frameOn[i]->setToolTip(tr("Untick to stop this frame: a missing frame as KAVACH sees it"));
        connect(m_frameOn[i], &QCheckBox::toggled, this, [this, i](bool on) { m_biu->setFrameOn(i, on); });
        per->addWidget(m_frameOn[i]);
    }
    per->addStretch(1);
    tv->addLayout(per);
    auto *test = new QHBoxLayout;
    test->addWidget(new QLabel(tr("Panel test takes"), tf));
    m_testSeconds = kgSpin(tf, 600, 1, 0);
    m_testSeconds->setSuffix(tr(" s"));
    connect(m_testSeconds, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double s) { m_biu->setPanelTestSeconds(s); saveSettings(); });
    test->addWidget(m_testSeconds);
    test->addWidget(muted(tr("and"), tf));
    test->addWidget(segmented({ tr("passes"), tr("fails") }, &m_testResult, tf));
    connect(m_testResult, &QButtonGroup::idClicked, this, [this](int id) { m_biu->setPanelTestPasses(id == 0); saveSettings(); });
    test->addStretch(1);
    tv->addLayout(test);
    right->addWidget(tf);
    right->addStretch(1);
    outer->addLayout(right, 2);
    return scrolled(page, this);
}

QWidget *BrakeBenchWindow::buildNetworkTab()
{
    auto *page = new QWidget(this);
    auto *outer = new QHBoxLayout(page);
    outer->setContentsMargins(0, 10, 0, 0);
    outer->setSpacing(12);
    auto *left = new QVBoxLayout;
    left->setSpacing(12);

    QVBoxLayout *lv = nullptr;
    QFrame *link = card(tr("Link"), page, &lv);
    auto *lg = new QGridLayout;
    lg->setHorizontalSpacing(8);
    lg->addWidget(new QLabel(tr("Listen on UDP port"), link), 0, 0);
    m_listenPort = new QSpinBox(link);
    m_listenPort->setRange(1, 65535);
    m_listenPort->setToolTip(tr("Where the VCC sends its CAN frames (the firmware's BIU_SIM_PORT), on every address of this PC"));
    connect(m_listenPort, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { saveSettings(); refreshTop(); });
    lg->addWidget(m_listenPort, 0, 1);
    lg->addWidget(new QLabel(tr("Answer"), link), 1, 0);
    lg->addWidget(segmented({ tr("the sender"), tr("these VCCs") }, &m_replyMode, link), 1, 1, 1, 3);
    m_replyMode->button(0)->setToolTip(tr("The address the VCC's frames come from"));
    m_replyMode->button(1)->setToolTip(tr("Every address listed: a loco's four VCCs all keep the BIU's state, "
                                          "though only one sends the command"));
    // Session 361: a list. One VCC sends the command; the BIU's frames must
    // reach all four.
    auto *hostRow = new QHBoxLayout;
    hostRow->setSpacing(6);
    m_replyHost = new QLineEdit(link);
    m_replyHost->setObjectName(QStringLiteral("biuReplyHosts"));
    m_replyHost->setPlaceholderText(QStringLiteral("192.168.25.16, 192.168.25.102, …"));
    m_replyHostTip = tr("The VCCs' addresses, separated by commas. a.b.c.d:port sets a port for one VCC only.\n"
                        "192.168.25.126 and .101 are the PCs DLConsole runs on, not VCCs.");
    m_replyHost->setToolTip(m_replyHostTip);
    m_replyHost->setMinimumWidth(260);
    hostRow->addWidget(m_replyHost, 1);
    m_locoPick = new QToolButton(link);
    m_locoPick->setObjectName(QStringLiteral("biuLocoPick"));
    m_locoPick->setText(tr("Loco"));
    m_locoPick->setPopupMode(QToolButton::InstantPopup);
    m_locoPick->setToolTip(tr("A loco's four VCC controllers, from the Firmware Flasher's address table (session 300)"));
    auto *locoMenu = new QMenu(m_locoPick);
    for (int loco = 1; loco <= Flasher::locoCount(); ++loco) {
        QStringList ips, octets;
        for (const Flasher::LocoController &c : Flasher::locoControllers())
            if (c.loco == loco) { ips << c.ip; octets << c.ip.mid(c.ip.lastIndexOf(QLatin1Char('.'))); }
        locoMenu->addAction(tr("Loco %1: all 4 VCCs (%2)").arg(loco).arg(octets.join(QStringLiteral(" "))), this,
                            [this, ips]() { setReplyTo(ips.join(QStringLiteral(", ")), quint16(m_replyPort->value())); });
    }
    m_locoPick->setMenu(locoMenu);
    hostRow->addWidget(m_locoPick);
    lg->addWidget(new QLabel(tr("VCCs"), link), 2, 0);
    lg->addLayout(hostRow, 2, 1, 1, 4);
    lg->addWidget(new QLabel(tr("on port"), link), 3, 0);
    m_replyPort = new QSpinBox(link);
    m_replyPort->setRange(1, 65535);
    m_replyPort->setToolTip(tr("The VCCs' port for the BIU's frames"));
    lg->addWidget(m_replyPort, 3, 1);
    m_replyList = muted(QString(), link);
    m_replyList->setObjectName(QStringLiteral("biuReplyList"));
    m_replyList->setWordWrap(true);
    m_replyList->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lg->addWidget(m_replyList, 4, 1, 1, 4);
    auto applyReply = [this]() {
        applyReplyHosts();
        saveSettings();          // a typo is kept, marked, to be fixed
        refreshTop();
    };
    connect(m_replyMode, &QButtonGroup::idClicked, this, applyReply);
    connect(m_replyHost, &QLineEdit::editingFinished, this, applyReply);
    connect(m_replyPort, QOverload<int>::of(&QSpinBox::valueChanged), this, applyReply);
    lg->setColumnStretch(4, 1);
    lv->addLayout(lg);
    auto *note = muted(tr("On the loco the BIU is on the VCC's CAN bus. For the bench, build the firmware with its "
                          "UDP link selected (biu_link_select(BIU_LINK_UDP)): it then sends the same frames here, "
                          "one per datagram, [COB-ID lo][COB-ID hi][3 + n][n data bytes]. The BIU is node 0x10, "
                          "KAVACH 0x20: commands on 0x190, status on 0x220 / 0x320 / 0x420, heartbeats on "
                          "0x710 / 0x720."), link);
    note->setWordWrap(true);
    lv->addWidget(note);
    left->addWidget(link);

    QVBoxLayout *hv = nullptr;
    QFrame *hb = card(tr("BIU heartbeat \u00B7 0x710"), page, &hv);
    auto *hr = new QHBoxLayout;
    m_hbOn = new QCheckBox(tr("Send every"), hb);
    connect(m_hbOn, &QCheckBox::toggled, this, [this](bool on) { m_biu->setHeartbeatOn(on); saveSettings(); });
    hr->addWidget(m_hbOn);
    m_hbPeriod = new QSpinBox(hb);
    m_hbPeriod->setRange(10, 10000);
    m_hbPeriod->setSingleStep(50);
    m_hbPeriod->setSuffix(tr(" ms"));
    m_hbPeriod->setToolTip(tr("The spec's period is 500 ms"));
    connect(m_hbPeriod, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) { m_biu->setHeartbeatPeriodMs(ms); saveSettings(); });
    hr->addWidget(m_hbPeriod);
    hr->addStretch(1);
    hv->addLayout(hr);
    auto *sr = new QHBoxLayout;
    sr->addWidget(new QLabel(tr("Saying"), hb));
    sr->addWidget(segmented({ tr("Operational"), tr("Pre-operational"), tr("Stopped"), tr("Boot-up") }, &m_biuState, hb));
    connect(m_biuState, &QButtonGroup::idClicked, this, [this](int id) {
        static const quint8 st[4] = { Biu::Operational, Biu::PreOperational, Biu::Stopped, Biu::BootUp };
        m_biu->setBiuState(st[id]);
    });
    sr->addStretch(1);
    hv->addLayout(sr);
    left->addWidget(hb);
    left->addStretch(1);
    outer->addLayout(left, 1);

    auto *right = new QVBoxLayout;
    right->setSpacing(12);
    QVBoxLayout *kv = nullptr;
    QFrame *kav = card(tr("KAVACH \u00B7 NMT"), page, &kv);
    m_kavachNow = new QLabel(kav);
    m_kavachNow->setWordWrap(true);
    kv->addWidget(m_kavachNow);
    m_autoStart = new QCheckBox(tr("Start KAVACH (NMT 0x01) while it is not operational"), kav);
    m_autoStart->setToolTip(tr("What a BIU does: \u201Csend Network Management continuously, whenever required\u201D (Annexure-1 4)"));
    connect(m_autoStart, &QCheckBox::toggled, this, [this](bool on) { m_biu->setAutoStart(on); saveSettings(); });
    kv->addWidget(m_autoStart);
    auto *nr = new QHBoxLayout;
    nr->addWidget(new QLabel(tr("Send to"), kav));
    nr->addWidget(segmented({ tr("KAVACH 0x20"), tr("all") }, &m_nmtNode, kav));
    nr->addStretch(1);
    kv->addLayout(nr);
    auto *nb = new QGridLayout;
    nb->setHorizontalSpacing(6);
    nb->setVerticalSpacing(6);
    const QVector<QPair<quint8, QString>> cmds{
        { Biu::NmtStart, tr("Start") }, { Biu::NmtStop, tr("Stop") }, { Biu::NmtPreOp, tr("Pre-operational") },
        { Biu::NmtResetNode, tr("Reset node") }, { Biu::NmtResetComm, tr("Reset comm.") } };
    for (int i = 0; i < cmds.size(); ++i) {
        auto *b = new QPushButton(cmds.at(i).second, kav);
        const quint8 cs = cmds.at(i).first;
        b->setToolTip(tr("NMT %1 (0x%2)").arg(Biu::nmtCommandName(cs)).arg(cs, 2, 16, QLatin1Char('0')));
        connect(b, &QPushButton::clicked, this, [this, cs]() {
            if (!biuRunning()) { m_status->fail(tr("Start the BIU first.")); return; }
            m_biu->sendNmt(cs, m_nmtNode->checkedId() == 0 ? Biu::kKavachNode : 0, now());
            m_status->ok(tr("Sent NMT %1.").arg(Biu::nmtCommandName(cs)));
        });
        nb->addWidget(b, i / 3, i % 3);
    }
    kv->addLayout(nb);
    auto *lr = new QHBoxLayout;
    lr->addWidget(new QLabel(tr("KAVACH is lost after"), kav));
    m_kavachTimeout = new QSpinBox(kav);
    m_kavachTimeout->setRange(100, 60000);
    m_kavachTimeout->setSingleStep(100);
    m_kavachTimeout->setSuffix(tr(" ms"));
    m_kavachTimeout->setToolTip(tr("Of no heartbeat and no command: a CAN failure"));
    connect(m_kavachTimeout, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) { m_biu->setKavachTimeoutMs(ms); saveSettings(); });
    lr->addWidget(m_kavachTimeout);
    lr->addStretch(1);
    kv->addLayout(lr);
    auto *or_ = new QHBoxLayout;
    or_->addWidget(new QLabel(tr("then"), kav));
    or_->addWidget(segmented({ tr("apply emergency"), tr("hold"), tr("release") }, &m_onLoss, kav));
    m_onLoss->button(0)->setToolTip(tr("Annexure-1 4 c: \u201CIn case of KAVACH, BIU shall apply emergency brake\u201D"));
    connect(m_onLoss, &QButtonGroup::idClicked, this, [this](int id) { m_biu->setOnLoss(BiuSimulator::OnLoss(id)); saveSettings(); });
    or_->addStretch(1);
    kv->addLayout(or_);
    right->addWidget(kav);
    right->addStretch(1);
    outer->addLayout(right, 1);
    return scrolled(page, this);
}

QWidget *BrakeBenchWindow::buildAnalogTab()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 10, 0, 0);
    v->setSpacing(8);

    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    row->addWidget(segmented({ tr("Off"), tr("Send every") }, &m_anSendGroup, page));
    m_anSendGroup->button(1)->setToolTip(tr("Send the six channels over UDP as each ticked controller, unasked, every period. "
                                            "Leave Off while a real analog card is on the line."));
    connect(m_anSendGroup, &QButtonGroup::idClicked, this, [this](int id) { setAnalogSending(id == 1); });
    m_anPeriod = new QSpinBox(page);
    m_anPeriod->setRange(10, 10000);
    m_anPeriod->setSingleStep(10);
    m_anPeriod->setValue(100);
    m_anPeriod->setSuffix(tr(" ms"));
    connect(m_anPeriod, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) { setAnalogPeriodMs(ms); });
    row->addWidget(m_anPeriod);
    auto *once = new QPushButton(tr("Send now"), page);
    once->setToolTip(tr("Send the six channels once, as each ticked controller"));
    connect(once, &QPushButton::clicked, this, &BrakeBenchWindow::sendAnalogNow);
    row->addWidget(once);
    row->addSpacing(12);
    row->addWidget(muted(tr("as"), page));
    for (quint8 id : { kAnalogTopId, kAnalogBottomId }) {
        auto *chip = new QPushButton(id == kAnalogTopId ? tr("15 top") : tr("16 bottom"), page);
        chip->setObjectName(QStringLiteral("idChip"));
        chip->setCheckable(true);
        chip->setChecked(id == kAnalogTopId);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setProperty("cardId", int(id));
        chip->setToolTip(tr("Send as analog controller %1 (src_id %1)").arg(id));
        connect(chip, &QPushButton::toggled, this, [this]() { refreshAnalog(); saveSettings(); });
        m_anCardChips << chip;
        row->addWidget(chip);
    }
    row->addStretch(1);
    m_anFollow = new QCheckBox(tr("BP, BC, MR follow the bench"), page);
    m_anFollow->setChecked(true);
    m_anFollow->setToolTip(tr("Channels 1-3 read the bench's pressures (plus any offset set on the Bench tab)"));
    connect(m_anFollow, &QCheckBox::toggled, this, [this](bool on) { setAnalogFollows(on); });
    row->addWidget(m_anFollow);
    v->addLayout(row);

    // Session 350: the analog card's frames go over UDP, one datagram each.
    auto *to = new QHBoxLayout;
    to->setSpacing(8);
    to->addWidget(new QLabel(tr("To (UDP)"), page));
    m_anHost = new QLineEdit(QStringLiteral("127.0.0.1"), page);
    m_anHost->setMaximumWidth(160);
    m_anHost->setToolTip(tr("The VCC's address: a dotted quad or a host name"));
    to->addWidget(m_anHost);
    to->addWidget(new QLabel(tr("port"), page));
    m_anPort = new QSpinBox(page);
    m_anPort->setRange(1, 65535);
    m_anPort->setValue(20000);
    to->addWidget(m_anPort);
    connect(m_anHost, &QLineEdit::textChanged, this, [this]() { refreshAnalog(); });
    connect(m_anPort, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { refreshAnalog(); });
    to->addWidget(muted(tr("one 34-byte frame per datagram, in the Cab I/O Simulator's wire format"), page));
    to->addStretch(1);
    v->addLayout(to);

    auto *hint = muted(tr("Set a channel's loop current, or the pressure it should read as. The VCC reads "
                          "raw / 2\u00B3\u00B9 \u00D7 75 mA (the firmware's 2500/33 is integer division), then "
                          "4\u201320 mA across the sensor's full scale \u00D7 1.02 in kg/cm\u00B2. Below 3.5 mA it is an "
                          "open loop, above 20.5 mA a short: no pressure."), page);
    hint->setWordWrap(true);
    v->addWidget(hint);

    // Session 351: a dial per channel, its pressure box under it.
    auto *dials = new QHBoxLayout;
    dials->setSpacing(8);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    const QStringList heads{ tr("Channel"), tr("Current"), tr("Sensor"), tr("The VCC reads") };
    for (int c = 0; c < heads.size(); ++c) {
        auto *h = new QLabel(heads.at(c).toUpper(), page);
        h->setObjectName(QStringLiteral("groupHeading"));
        grid->addWidget(h, 0, c);
    }
    for (int ch = 0; ch < kAnalogChannels; ++ch) {
        AnRow r;
        grid->addWidget(new QLabel(QStringLiteral("%1 <span style=\"%2\">\u00B7 channel%3_data</span>")
                                       .arg(analogChannelName(ch), UiColor::mutedStyle()).arg(ch + 1), page),
                        ch + 1, 0);
        r.mA = new QDoubleSpinBox(page);
        r.mA->setRange(0, 25);
        r.mA->setDecimals(3);
        r.mA->setSingleStep(0.1);
        r.mA->setSuffix(tr(" mA"));
        r.mA->setValue(4);
        connect(r.mA, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { refreshAnalog(); });
        grid->addWidget(r.mA, ch + 1, 1);
        r.kg = new QDoubleSpinBox(page);
        r.kg->setRange(0, 16.32);
        r.kg->setDecimals(2);
        r.kg->setSingleStep(0.1);
        r.kg->setSuffix(tr(" kg/cm\u00B2"));
        r.kg->setToolTip(tr("Sets the current that reads as this pressure"));
        r.kg->setMinimumWidth(80);
        // A hand on BP / BC / MR while they follow the bench moves the bench.
        auto byHand = [this, ch](double kgcm2) {
            if (ch < BrakePlant::kChannels && analogFollows()) {
                m_plant.setValue(ch, kgcm2 - m_analogOffset[ch]);
                if (m_source == Source::Manual) m_plant.setTarget(ch, m_plant.value(ch));
                refreshBench();
            }
            setAnalogPressure(ch, kgcm2);
        };
        connect(r.kg, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, byHand);
        r.gauge = new PressureGauge(analogChannelName(ch), page);
        connect(r.gauge, &PressureGauge::valueEdited, this, byHand);
        auto *dial = new QVBoxLayout;
        dial->setSpacing(4);
        dial->addWidget(r.gauge, 1);
        dial->addWidget(r.kg);
        dials->addLayout(dial, 1);
        r.scale = new QComboBox(page);
        r.scale->addItem(tr("10 bar"), 10);
        r.scale->addItem(tr("16 bar"), 16);
        connect(r.scale, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            for (double &f : m_fedKg) f = -1;     // the same pressure is another current now
            feedAnalog();
            refreshAnalog();
        });
        grid->addWidget(r.scale, ch + 1, 2);
        r.reads = new QLabel(page);
        r.reads->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(r.reads, ch + 1, 3);
        m_anRows << r;
    }
    grid->setColumnStretch(3, 1);
    v->addLayout(dials, 1);
    v->addLayout(grid);
    m_anSummary = new QLabel(page);
    m_anSummary->setWordWrap(true);
    v->addWidget(m_anSummary);
    return scrolled(page, this);
}

QWidget *BrakeBenchWindow::buildRelaysTab()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 10, 0, 0);
    v->setSpacing(10);
    QVBoxLayout *cv = nullptr;
    QFrame *c = card(tr("Brake relays"), page, &cv);
    auto *intro = muted(tr("For a VCC that brakes by relay: with \u201CBrake relays\u201D picked on the Bench tab, "
                           "BP, BC and MR go to the values below for the strongest brake in the output word the "
                           "Cab I/O Simulator receives (EB over FSB over NB)."), c);
    intro->setWordWrap(true);
    cv->addWidget(intro);
    auto *row = new QHBoxLayout;
    row->setSpacing(14);
    auto *lamps = new QGridLayout;
    lamps->setSpacing(2);
    const char *const lampNames[4] = { "NB", "FSB", "EB", "LE" };
    for (int i = 0; i < 4; ++i) {
        auto *lamp = new SignalLamp(QString::fromLatin1(lampNames[i]), QStringLiteral("red"), c);
        lamp->setObjectName(QStringLiteral("brakeLamp"));
        lamp->setTexts(tr("applied"), tr("released"), tr("no output data yet"));
        m_relayLamps << lamp;
        lamps->addWidget(lamp, i / 2, i % 2);
    }
    row->addLayout(lamps);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(3);
    auto *unit = muted(tr("kg/cm\u00B2"), c);
    grid->addWidget(unit, 0, 0);
    for (int ch = 0; ch < kBrakeChannels; ++ch) {
        auto *head = new QLabel(analogChannelName(ch), c);
        head->setObjectName(QStringLiteral("groupHeading"));
        head->setAlignment(Qt::AlignCenter);
        grid->addWidget(head, 0, ch + 1);
    }
    for (int level = 0; level < kBrakeLevels; ++level) {
        grid->addWidget(new QLabel(level == NoBrake ? tr("No brake") : brakeLevelName(level), c), level + 1, 0);
        for (int ch = 0; ch < kBrakeChannels; ++ch) {
            auto *k = new QDoubleSpinBox(c);
            k->setRange(0.0, 16.32);
            k->setDecimals(1);
            k->setSingleStep(0.1);
            k->setValue(defaultBrakePressure(level, ch));
            k->setToolTip(tr("%1 with %2, kg/cm\u00B2").arg(analogChannelName(ch),
                                                            level == NoBrake ? tr("no brake") : brakeLevelName(level)));
            connect(k, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { saveSettings(); applyTargets(); });
            m_brakeKg[level][ch] = k;
            grid->addWidget(k, level + 1, ch + 1);
        }
    }
    row->addLayout(grid);
    row->addStretch(1);
    cv->addLayout(row);
    m_relayNow = new QLabel(c);
    m_relayNow->setWordWrap(true);
    cv->addWidget(m_relayNow);
    v->addWidget(c);
    auto *note = muted(tr("No brake's BP, BC and MR are also where the BIU leaves the air when it is not braking "
                          "(released, isolated, or A9 / SA9 not valid)."), page);
    note->setWordWrap(true);
    v->addWidget(note);
    v->addStretch(1);
    return scrolled(page, this);
}

QWidget *BrakeBenchWindow::buildFramesTab()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 10, 0, 0);
    v->setSpacing(8);
    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    m_logHeartbeats = new QCheckBox(tr("Heartbeats and NMT"), page);
    m_logStatus = new QCheckBox(tr("BIU status frames"), page);
    m_logAnalog = new QCheckBox(tr("Analog frames"), page);
    m_logPause = new QCheckBox(tr("Pause"), page);
    m_logHeartbeats->setChecked(true);
    m_logStatus->setChecked(true);
    m_logAnalog->setChecked(false);
    for (QCheckBox *b : { m_logHeartbeats, m_logStatus, m_logAnalog, m_logPause }) row->addWidget(b);
    row->addStretch(1);
    auto *clear = new QPushButton(tr("Clear"), page);
    connect(clear, &QPushButton::clicked, this, [this]() { m_log->setRowCount(0); m_pending.clear(); });
    row->addWidget(clear);
    v->addLayout(row);
    m_log = new QTableWidget(0, 7, page);
    m_log->setHorizontalHeaderLabels({ tr("Time"), QString(), tr("COB-ID"), tr("Frame"), tr("Bytes"), tr("What it says"), tr("Note") });
    m_log->verticalHeader()->hide();
    m_log->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_log->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_log->setWordWrap(false);
    QHeaderView *hh = m_log->horizontalHeader();
    for (int c = 0; c < 5; ++c) hh->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(5, QHeaderView::Interactive);
    hh->resizeSection(5, 460);
    hh->setStretchLastSection(true);
    v->addWidget(m_log, 1);
    return page;
}

// ---- settings ----------------------------------------------------------------------

void BrakeBenchWindow::loadSettings()
{
    m_loading = true;
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("brakebench"));
    m_listenPort->setValue(s.value(QStringLiteral("listenPort"), 51234).toInt());
    const bool fixed = s.value(QStringLiteral("replyFixed"), false).toBool();
    m_replyMode->button(fixed ? 1 : 0)->setChecked(true);
    m_replyHost->setText(s.value(QStringLiteral("replyHost"), defaultReplyHosts()).toString());
    m_replyHost->setCursorPosition(0);   // the first address, not the tail of the list
    m_replyPort->setValue(s.value(QStringLiteral("replyPort"), 50001).toInt());
    applyReplyHosts();
    m_hbOn->setChecked(s.value(QStringLiteral("heartbeat"), true).toBool());
    m_hbPeriod->setValue(s.value(QStringLiteral("heartbeatMs"), 500).toInt());
    m_autoStart->setChecked(s.value(QStringLiteral("autoStart"), true).toBool());
    m_kavachTimeout->setValue(s.value(QStringLiteral("kavachTimeoutMs"), 1500).toInt());
    const int loss = qBound(0, s.value(QStringLiteral("onLoss"), 0).toInt(), 2);
    m_onLoss->button(loss)->setChecked(true);
    m_biu->setOnLoss(BiuSimulator::OnLoss(loss));
    m_statusPeriod->setValue(s.value(QStringLiteral("statusMs"), 250).toInt());
    m_testSeconds->setValue(s.value(QStringLiteral("panelTestS"), 10).toDouble());
    const bool passes = s.value(QStringLiteral("panelTestPasses"), true).toBool();
    m_testResult->button(passes ? 0 : 1)->setChecked(true);
    m_biu->setPanelTestPasses(passes);
    m_waitAck->setChecked(s.value(QStringLiteral("waitForAck"), true).toBool());
    m_countIso->setChecked(s.value(QStringLiteral("countIsolations"), true).toBool());
    m_raiseIso->setChecked(s.value(QStringLiteral("raiseIsolated"), true).toBool());
    // The spec's values are the defaults: m_biu was built with them, so
    // the spins' signals (not fired for an unchanged value) are set too.
    m_biu->setHeartbeatOn(m_hbOn->isChecked());
    m_biu->setHeartbeatPeriodMs(m_hbPeriod->value());
    m_biu->setAutoStart(m_autoStart->isChecked());
    m_biu->setKavachTimeoutMs(m_kavachTimeout->value());
    m_biu->setStatusPeriodMs(m_statusPeriod->value());
    m_biu->setPanelTestSeconds(m_testSeconds->value());
    m_biu->setWaitForAck(m_waitAck->isChecked());
    m_biu->setCountIsolations(m_countIso->isChecked());
    m_biu->setRaiseIsolatedFault(m_raiseIso->isChecked());

    const int src = qBound(0, s.value(QStringLiteral("source"), 0).toInt(), 2);
    m_sourceGroup->button(src)->setChecked(true);
    m_source = Source(src);
    for (int ch = 0; ch < BrakePlant::kChannels; ++ch) {
        const QString n = analogChannelName(ch);
        m_apply[ch]->setValue(s.value(QStringLiteral("apply_%1").arg(n), 1.0).toDouble());
        m_release[ch]->setValue(s.value(QStringLiteral("release_%1").arg(n), 1.0).toDouble());
        m_plant.setRates(ch, m_apply[ch]->value(), m_release[ch]->value());
    }

    // The analog card and the per-brake pressures were the Cab I/O
    // Simulator's until session 360: a first open reads them from there.
    const bool moved = s.contains(QStringLiteral("analogPeriodMs"));
    s.endGroup();
    s.beginGroup(moved ? QStringLiteral("brakebench") : QStringLiteral("cabio"));
    for (int level = 0; level < kBrakeLevels; ++level)
        for (int ch = 0; ch < kBrakeChannels; ++ch) {
            const QSignalBlocker b(m_brakeKg[level][ch]);
            m_brakeKg[level][ch]->setValue(
                s.value(QStringLiteral("brakeKg_%1_%2").arg(brakeLevelName(level), analogChannelName(ch)),
                        defaultBrakePressure(level, ch)).toDouble());
        }
    static const double defaultKg[kAnalogChannels] = { 5.0, 0, 9.0, 0, 0, 0 };
    for (int ch = 0; ch < kAnalogChannels; ++ch) {
        setAnalogScale(ch, s.value(QStringLiteral("analogBar_%1").arg(ch + 1), 10).toInt());
        const QVariant ma = s.value(QStringLiteral("analogMa_%1").arg(ch + 1));
        if (ma.isValid()) setAnalogMa(ch, ma.toDouble()); else setAnalogPressure(ch, defaultKg[ch]);
    }
    setAnalogPeriodMs(s.value(QStringLiteral("analogPeriodMs"), 100).toInt());
    setAnalogTarget(s.value(QStringLiteral("analogHost"), QStringLiteral("127.0.0.1")).toString(),
                    quint16(s.value(QStringLiteral("analogPort"), 20000).toUInt()));
    QVector<quint8> anIds;
    for (const QString &id : s.value(QStringLiteral("analogIds"), QStringLiteral("15")).toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
        anIds << quint8(id.toInt());
    setAnalogCards(anIds);
    s.endGroup();
    {
        const QSignalBlocker b(m_anFollow);
        m_anFollow->setChecked(s.value(QStringLiteral("brakebench/analogFollows"), true).toBool());
    }
    // The wire format stays the Cab I/O Simulator's (its Frames tab): the
    // same VCC reads both. MainWindow passes changes on while both are open.
    WireFormat wf;
    wf.crc = Crc(s.value(QStringLiteral("cabio/crc"), int(wf.crc)).toInt());
    wf.length = LengthRule(s.value(QStringLiteral("cabio/length"), int(wf.length)).toInt());
    wf.echoSeq = s.value(QStringLiteral("cabio/echoSeq"), true).toBool();
    m_wf = wf;

    // The bench starts where the analog card's BP, BC, MR are.
    for (int ch = 0; ch < BrakePlant::kChannels; ++ch) {
        const PressureReading p = currentToPressure(analogMa(ch), analogScale(ch));
        const double v = p.status == PressureReading::Ok ? p.kgcm2 : brakePressure(NoBrake, ch);
        m_plant.setValue(ch, v);
        m_plant.setTarget(ch, v);
    }
    m_loading = false;
    applyTargets();
}

void BrakeBenchWindow::saveSettings() const
{
    if (m_loading) return;
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("brakebench"));
    s.setValue(QStringLiteral("listenPort"), m_listenPort->value());
    s.setValue(QStringLiteral("replyFixed"), m_replyMode->checkedId() == 1);
    s.setValue(QStringLiteral("replyHost"), m_replyHost->text().trimmed());
    s.setValue(QStringLiteral("replyPort"), m_replyPort->value());
    s.setValue(QStringLiteral("heartbeat"), m_hbOn->isChecked());
    s.setValue(QStringLiteral("heartbeatMs"), m_hbPeriod->value());
    s.setValue(QStringLiteral("autoStart"), m_autoStart->isChecked());
    s.setValue(QStringLiteral("kavachTimeoutMs"), m_kavachTimeout->value());
    s.setValue(QStringLiteral("onLoss"), m_onLoss->checkedId());
    s.setValue(QStringLiteral("statusMs"), m_statusPeriod->value());
    s.setValue(QStringLiteral("panelTestS"), m_testSeconds->value());
    s.setValue(QStringLiteral("panelTestPasses"), m_testResult->checkedId() == 0);
    s.setValue(QStringLiteral("waitForAck"), m_waitAck->isChecked());
    s.setValue(QStringLiteral("countIsolations"), m_countIso->isChecked());
    s.setValue(QStringLiteral("raiseIsolated"), m_raiseIso->isChecked());
    s.setValue(QStringLiteral("source"), int(m_source));
    for (int ch = 0; ch < BrakePlant::kChannels; ++ch) {
        s.setValue(QStringLiteral("apply_%1").arg(analogChannelName(ch)), m_apply[ch]->value());
        s.setValue(QStringLiteral("release_%1").arg(analogChannelName(ch)), m_release[ch]->value());
    }
    for (int level = 0; level < kBrakeLevels; ++level)
        for (int ch = 0; ch < kBrakeChannels; ++ch)
            s.setValue(QStringLiteral("brakeKg_%1_%2").arg(brakeLevelName(level), analogChannelName(ch)), brakePressure(level, ch));
    for (int ch = 0; ch < m_anRows.size(); ++ch) {
        s.setValue(QStringLiteral("analogMa_%1").arg(ch + 1), analogMa(ch));
        s.setValue(QStringLiteral("analogBar_%1").arg(ch + 1), analogScale(ch));
    }
    s.setValue(QStringLiteral("analogPeriodMs"), m_anPeriod->value());
    s.setValue(QStringLiteral("analogHost"), analogHost());
    s.setValue(QStringLiteral("analogPort"), analogPort());
    QStringList anIds;
    for (quint8 id : analogCards()) anIds << QString::number(id);
    s.setValue(QStringLiteral("analogIds"), anIds.join(QLatin1Char(',')));
    s.setValue(QStringLiteral("analogFollows"), analogFollows());
    s.endGroup();
}

// ---- the BIU -------------------------------------------------------------------------

bool BrakeBenchWindow::startBiu()
{
    QString why;
    if (!m_biu->listen(quint16(m_listenPort->value()), &why)) {
        m_status->fail(tr("Cannot listen on UDP %1: %2").arg(m_listenPort->value()).arg(why));
        refreshTop();
        return false;
    }
    m_biu->setRunning(true, now());
    m_listenPort->setEnabled(false);
    m_status->ok(tr("BIU running: listening on UDP %1.").arg(m_listenPort->value()));
    refreshTop();
    return true;
}

void BrakeBenchWindow::stopBiu()
{
    m_biu->setRunning(false, now());
    m_biu->close();
    m_listenPort->setEnabled(true);
    m_status->say(tr("BIU stopped."));
    refreshTop();
}

bool BrakeBenchWindow::biuRunning() const { return m_biu->running(); }

void BrakeBenchWindow::setListenPort(quint16 port) { m_listenPort->setValue(port); }
quint16 BrakeBenchWindow::listenPort() const { return quint16(m_listenPort->value()); }

void BrakeBenchWindow::setReplyTo(const QString &hosts, quint16 port)
{
    m_replyMode->button(hosts.trimmed().isEmpty() ? 0 : 1)->setChecked(true);
    if (!hosts.trimmed().isEmpty()) {
        m_replyHost->setText(hosts);
        m_replyHost->setCursorPosition(0);
    }
    {
        const QSignalBlocker block(m_replyPort);
        m_replyPort->setValue(port);
    }
    applyReplyHosts();
    saveSettings();
    refreshTop();
}

QString BrakeBenchWindow::replyListText()
{
    refreshReplyList();
    return m_replyList ? m_replyList->text() : QString();
}

QString BrakeBenchWindow::defaultReplyHosts()
{
    // Loco 1's four VCC controllers (session 300's table): a starting point.
    QStringList ips;
    for (const Flasher::LocoController &c : Flasher::locoControllers())
        if (c.loco == 1) ips << c.ip;
    return ips.join(QStringLiteral(", "));
}

bool BrakeBenchWindow::applyReplyHosts()
{
    const bool fixed = m_replyMode->checkedId() == 1;
    m_replyHost->setEnabled(fixed);
    m_locoPick->setEnabled(fixed);
    m_biu->setReplyPort(quint16(m_replyPort->value()));
    QStringList hosts;
    QString why;
    bool ok = true;
    if (fixed && !BiuSimulator::parseHosts(m_replyHost->text(), &hosts, &why)) {
        ok = false;
    } else if (fixed && hosts.isEmpty()) {
        why = tr("No VCC address listed");
        ok = false;
    }
    if (ok) m_biu->setReplyHosts(fixed ? hosts : QStringList());
    m_replyHostError = ok ? QString() : why;
    m_replyHost->setStyleSheet(ok ? QString()
                                  : QStringLiteral("QLineEdit { color:%1; border:1px solid %1; border-radius:4px; }")
                                        .arg(UiColor::error().name()));
    m_replyHost->setToolTip(ok ? m_replyHostTip : why);
    if (!ok && !m_loading) m_status->fail(tr("The BIU's reply list: %1. It still answers %2.")
                                              .arg(why, m_biu->replyTarget().isEmpty() ? tr("nobody") : m_biu->replyTarget()));
    refreshReplyList();
    return ok;
}

void BrakeBenchWindow::refreshReplyList()
{
    if (!m_replyList) return;
    if (!m_replyHostError.isEmpty()) {
        m_replyList->setText(m_replyHostError);
        m_replyList->setStyleSheet(UiColor::errorStyle());
        return;
    }
    m_replyList->setStyleSheet(QString());
    const QString commander = m_biu->hasCommand() ? m_biu->lastPeer().section(QLatin1Char(':'), 0, 0) : QString();
    if (m_replyMode->checkedId() == 0) {
        m_replyList->setText(tr("Each frame goes to whoever sent the last one, on port %1. The other VCCs of the loco hear nothing.")
                                 .arg(m_replyPort->value()));
        return;
    }
    QStringList lines;
    for (const QString &target : m_biu->replyTargets()) {
        const QString ip = target.section(QLatin1Char(':'), 0, 0);
        QString line = target;
        const Flasher::LocoController c = Flasher::locoControllerAt(ip);
        if (c.loco) line += QStringLiteral("  ·  ") + c.name();
        if (ip == commander) line += QStringLiteral("  ·  ") + tr("sends the command");
        lines << line;
    }
    QString head = lines.size() == 1 ? tr("Each frame goes to 1 VCC:") : tr("Each frame goes to %1 VCCs, one datagram each:").arg(lines.size());
    if (!commander.isEmpty() && !m_biu->inReplyList(commander))
        lines << tr("The command comes from %1, which is not listed: it is sent nothing.").arg(commander);
    m_replyList->setText(head + QLatin1Char('\n') + lines.join(QLatin1Char('\n')));
}

// ---- saved presets (session 362) ------------------------------------------------------------

BenchPreset BrakeBenchWindow::currentPreset() const
{
    BenchPreset p;
    for (int ch = 0; ch < BrakePlant::kChannels; ++ch) {
        BenchPreset::Air &a = p.air[ch];
        a.apply = m_apply[ch]->value();
        a.release = m_release[ch]->value();
        a.fault = int(m_plant.fault(ch));
        a.param = m_faultParam[ch]->value();
        a.offset = m_analogOffset[ch];
    }
    for (int id = 0; id < Biu::kPressures; ++id) {
        BenchPreset::Pressure &q = p.pressure[id];
        q.manual = m_biu->pressureManual(id);
        q.kg = m_biu->pressureManualValue(id);
        q.valid = int(m_biu->validMode(id));
    }
    for (int byte = 1; byte <= 3; ++byte)
        for (int bit = 0; bit < 8; ++bit) p.bits[byte - 1][bit] = int(m_biu->bitMode(byte, bit));
    p.faults = m_biu->faultQueue();
    p.displays = m_biu->displayQueue();
    for (int i = 0; i < 3; ++i) p.frameOn[i] = m_biu->frameOn(i);
    p.silent = m_biu->silent();
    return p;
}

// Through the widgets, so what the tabs show is what the bench does: each
// control's own handler moves the model, as a click would.
void BrakeBenchWindow::applyPreset(const BenchPreset &p)
{
    for (int ch = 0; ch < BrakePlant::kChannels; ++ch) {
        const BenchPreset::Air &a = p.air[ch];
        m_apply[ch]->setValue(a.apply);
        m_release[ch]->setValue(a.release);
        setPlantFault(ch, BrakePlant::Fault(qBound(0, a.fault, 3)), a.param);
        setAnalogOffset(ch, a.offset);
    }
    for (int id = 0; id < Biu::kPressures; ++id) {
        const BenchPreset::Pressure &q = p.pressure[id];
        if (m_pManual[id]) m_pManual[id]->setValue(q.kg);
        if (m_pMode[id]) m_pMode[id]->button(q.manual ? 1 : 0)->click();
        else m_biu->setPressureManual(id, q.manual, q.kg);
        if (m_pValid[id]) m_pValid[id]->button(qBound(0, q.valid, 2))->click();
        else m_biu->setValidMode(id, BiuSimulator::ValidMode(qBound(0, q.valid, 2)));
    }
    for (int byte = 1; byte <= 3; ++byte)
        for (int bit = 0; bit < 8; ++bit) {
            const int m = qBound(0, p.bits[byte - 1][bit], 2);
            if (QButtonGroup *g = m_bitMode[byte - 1][bit]) g->button(m)->click();
            else m_biu->setBitMode(byte, bit, BiuSimulator::BitMode(m));
        }
    m_biu->clearFaults();
    m_biu->clearDisplays();
    for (quint16 c : p.faults) m_biu->raiseFault(c);
    for (quint16 c : p.displays) m_biu->raiseDisplay(c);
    for (int i = 0; i < 3; ++i) m_frameOn[i]->setChecked(p.frameOn[i]);
    m_silentBox->setChecked(p.silent);
    // And the model itself: a widget set to the value it already shows says
    // nothing, and the model may have been moved without it (the tests do).
    for (int ch = 0; ch < BrakePlant::kChannels; ++ch) {
        m_plant.setRates(ch, p.air[ch].apply, p.air[ch].release);
        m_plant.setFault(ch, BrakePlant::Fault(qBound(0, p.air[ch].fault, 3)), p.air[ch].param);
        m_analogOffset[ch] = p.air[ch].offset;
    }
    for (int id = 0; id < Biu::kPressures; ++id) {
        m_biu->setPressureManual(id, p.pressure[id].manual, p.pressure[id].kg);
        m_biu->setValidMode(id, BiuSimulator::ValidMode(qBound(0, p.pressure[id].valid, 2)));
    }
    for (int byte = 1; byte <= 3; ++byte)
        for (int bit = 0; bit < 8; ++bit) m_biu->setBitMode(byte, bit, BiuSimulator::BitMode(qBound(0, p.bits[byte - 1][bit], 2)));
    for (int i = 0; i < 3; ++i) m_biu->setFrameOn(i, p.frameOn[i]);
    m_biu->setSilent(p.silent);
    {
        const QSignalBlocker b(m_isolateBox);
        m_isolateBox->setChecked(p.bits[0][Biu::StManualIsolate] == int(BiuSimulator::BitMode::On));
    }
    applyTargets();
    refreshStatusTab();
    refreshTop();
}

bool BrakeBenchWindow::savePreset(const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) {
        m_status->fail(tr("A preset needs a name."));
        return false;
    }
    const BenchPreset p = currentPreset();
    BenchPreset::save(n, p);
    m_activePreset = n;
    m_activeJson = p.toJson();
    const QStringList diff = p.differences(m_clean);
    m_status->ok(diff.isEmpty() ? tr("Saved preset “%1”: nothing set, the bench as it opens.").arg(n)
                                : tr("Saved preset “%1”: %2.").arg(n, diff.join(QStringLiteral(" · "))));
    refreshTop();
    return true;
}

bool BrakeBenchWindow::loadPreset(const QString &name)
{
    BenchPreset p;
    if (!BenchPreset::load(name, m_clean, &p)) {
        m_status->fail(tr("No preset called “%1”.").arg(name));
        return false;
    }
    applyPreset(p);
    m_activePreset = name;
    m_activeJson = currentPreset().toJson();
    const QStringList diff = p.differences(m_clean);
    m_status->ok(diff.isEmpty() ? tr("Loaded preset “%1”: nothing set, the bench as it opens.").arg(name)
                                : tr("Loaded preset “%1”: %2.").arg(name, diff.join(QStringLiteral(" · "))));
    refreshTop();
    return true;
}

void BrakeBenchWindow::deletePreset(const QString &name)
{
    BenchPreset::remove(name);
    if (m_activePreset == name) m_activePreset.clear();
    m_status->say(tr("Deleted preset “%1”.").arg(name));
    refreshTop();
}

void BrakeBenchWindow::startClean()
{
    applyPreset(m_clean);
    m_activePreset.clear();
    m_status->say(tr("Fault setup cleared: the bench as it opens."));
    refreshTop();
}

void BrakeBenchWindow::buildPresetMenu()
{
    QMenu *menu = m_presetBtn->menu();
    menu->clear();
    menu->setToolTipsVisible(true);
    menu->addAction(tr("Save the fault setup as…"), this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save preset"),
                                                   tr("Name for this fault setup (e.g. “BC stuck at 1.0”):"),
                                                   QLineEdit::Normal, m_activePreset, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        if (BenchPreset::exists(name)
            && QMessageBox::question(this, tr("Save preset"), tr("Replace the preset “%1”?").arg(name))
                   != QMessageBox::Yes)
            return;
        savePreset(name);
    });
    menu->addAction(tr("Start clean"), this, &BrakeBenchWindow::startClean)
        ->setToolTip(tr("Every fault, forced bit and queued code back to how the window opens"));
    const QStringList names = BenchPreset::names();
    menu->addSection(names.isEmpty() ? tr("No presets saved") : tr("Load"));
    for (const QString &n : names) {
        QAction *a = menu->addAction(n, this, [this, n]() { loadPreset(n); });
        BenchPreset p;
        BenchPreset::load(n, m_clean, &p);
        const QStringList diff = p.differences(m_clean);
        a->setToolTip(diff.isEmpty() ? tr("Nothing set") : diff.join(QLatin1Char('\n')));
        a->setCheckable(true);
        a->setChecked(n == m_activePreset);
    }
    if (!names.isEmpty()) {
        QMenu *del = menu->addMenu(tr("Delete"));
        for (const QString &n : names)
            del->addAction(n, this, [this, n]() {
                if (QMessageBox::question(this, tr("Delete preset"), tr("Delete the preset “%1”?").arg(n))
                    == QMessageBox::Yes)
                    deletePreset(n);
            });
    }
}

// ---- the air ---------------------------------------------------------------------------

void BrakeBenchWindow::setSource(Source s)
{
    m_source = s;
    { const QSignalBlocker b(m_sourceGroup); m_sourceGroup->button(int(s))->setChecked(true); }
    if (s == Source::Manual)
        for (int ch = 0; ch < BrakePlant::kChannels; ++ch) m_plant.setTarget(ch, m_plant.value(ch));
    saveSettings();
    applyTargets();
    refreshBench();
}

void BrakeBenchWindow::setRates(int ch, double apply, double release)
{
    if (ch < 0 || ch >= BrakePlant::kChannels) return;
    m_apply[ch]->setValue(apply);
    m_release[ch]->setValue(release);
    m_plant.setRates(ch, apply, release);
}

void BrakeBenchWindow::setPlantFault(int ch, BrakePlant::Fault f, double param)
{
    if (ch < 0 || ch >= BrakePlant::kChannels) return;
    m_faultParam[ch]->setValue(param);
    m_faultBox[ch]->setCurrentIndex(m_faultBox[ch]->findData(int(f)));
    m_plant.setFault(ch, f, param);
}

void BrakeBenchWindow::setAnalogOffset(int ch, double kgcm2)
{
    if (ch < 0 || ch >= BrakePlant::kChannels) return;
    m_offsetBox[ch]->setValue(kgcm2);
    m_analogOffset[ch] = kgcm2;
    feedAnalog();
}

// Where the air is asked to go, from the source.
void BrakeBenchWindow::applyTargets()
{
    switch (m_source) {
    case Source::Manual:
        break;
    case Source::Relays: {
        const int level = brakeLevelNow();
        for (int ch = 0; ch < BrakePlant::kChannels; ++ch)
            m_plant.setTarget(ch, brakePressure(level < 0 ? NoBrake : level, ch));
        break;
    }
    case Source::Biu: {
        const BiuSimulator::Targets t = m_biu->targets(now());
        m_plant.setTarget(BrakePlant::Bp, t.controlling && t.bpSet ? t.bp : brakePressure(NoBrake, BrakePlant::Bp));
        m_plant.setTarget(BrakePlant::Bc, t.controlling && t.bcSet ? t.bc : brakePressure(NoBrake, BrakePlant::Bc));
        m_plant.setTarget(BrakePlant::Mr, brakePressure(NoBrake, BrakePlant::Mr));
        break;
    }
    }
}

void BrakeBenchWindow::advance(double seconds)
{
    const qint64 t = now();
    m_lastAdvanceMs = t;
    m_biu->tick(t);
    applyTargets();
    m_plant.step(seconds);
    m_biu->setLivePressure(Biu::Bp, m_plant.value(BrakePlant::Bp));
    m_biu->setLivePressure(Biu::Bc, m_plant.value(BrakePlant::Bc));
    m_biu->setLivePressure(Biu::Mr, m_plant.value(BrakePlant::Mr));
    feedAnalog();
    for (int ch = 0; ch < BrakePlant::kChannels; ++ch) {
        if (!m_gauge[ch]) continue;
        m_gauge[ch]->setValue(m_plant.value(ch));
        m_gauge[ch]->setTarget(m_source == Source::Manual ? -1 : m_plant.target(ch));
    }
}

// Channels 1-3 of the analog card read the bench (and its offsets).
void BrakeBenchWindow::feedAnalog()
{
    if (!analogFollows() || m_anRows.size() < BrakePlant::kChannels) return;
    for (int ch = 0; ch < BrakePlant::kChannels; ++ch) {
        const double v = qMax(0.0, m_plant.value(ch) + m_analogOffset[ch]);
        if (std::fabs(v - m_fedKg[ch]) < 1e-4) continue;
        m_fedKg[ch] = v;
        setAnalogPressure(ch, v);
    }
}

// ---- relays --------------------------------------------------------------------------

void BrakeBenchWindow::observeOutputWord(int cab, quint16 word)
{
    m_relayCab = cab;
    m_relayWord = word;
    m_relayKnown = true;
    applyTargets();
    refreshRelays();
}

int BrakeBenchWindow::brakeLevelNow() const
{
    return m_relayKnown ? brakeLevel(Cab(m_relayCab), m_relayWord) : -1;
}

void BrakeBenchWindow::setBrakePressure(int level, int ch, double kgcm2)
{
    if (level >= 0 && level < kBrakeLevels && ch >= 0 && ch < kBrakeChannels) m_brakeKg[level][ch]->setValue(kgcm2);
}

double BrakeBenchWindow::brakePressure(int level, int ch) const
{
    return level >= 0 && level < kBrakeLevels && ch >= 0 && ch < kBrakeChannels && m_brakeKg[level][ch]
               ? m_brakeKg[level][ch]->value() : 0.0;
}

// ---- analog card ----------------------------------------------------------------------

void BrakeBenchWindow::setAnalogMa(int ch, double mA) { if (ch >= 0 && ch < m_anRows.size()) m_anRows[ch].mA->setValue(mA); }
double BrakeBenchWindow::analogMa(int ch) const { return m_anRows.value(ch).mA ? m_anRows.at(ch).mA->value() : 0; }

void BrakeBenchWindow::setAnalogPressure(int ch, double kgcm2)
{
    if (ch < 0 || ch >= m_anRows.size()) return;
    setAnalogMa(ch, 4.0 + 16.0 * kgcm2 / (analogScale(ch) * 1.02));
}

void BrakeBenchWindow::setAnalogScale(int ch, int bar)
{
    if (ch < 0 || ch >= m_anRows.size()) return;
    m_anRows[ch].scale->setCurrentIndex(qMax(0, m_anRows[ch].scale->findData(bar)));
}

int BrakeBenchWindow::analogScale(int ch) const
{
    return m_anRows.value(ch).scale ? m_anRows.at(ch).scale->currentData().toInt() : 10;
}

void BrakeBenchWindow::setAnalogCards(const QVector<quint8> &ids)
{
    for (QPushButton *c : m_anCardChips) {
        const QSignalBlocker b(c);
        c->setChecked(ids.contains(quint8(c->property("cardId").toInt())));
    }
    refreshAnalog();
}

QVector<quint8> BrakeBenchWindow::analogCards() const
{
    QVector<quint8> ids;
    for (QPushButton *c : m_anCardChips)
        if (c->isChecked()) ids << quint8(c->property("cardId").toInt());
    return ids;
}

void BrakeBenchWindow::setAnalogSending(bool on)
{
    { const QSignalBlocker b(m_anSendGroup); m_anSendGroup->button(on ? 1 : 0)->setChecked(true); }
    if (on) m_anTimer->start(); else m_anTimer->stop();
    refreshAnalog();
}

bool BrakeBenchWindow::analogSending() const { return m_anTimer->isActive(); }

void BrakeBenchWindow::setAnalogPeriodMs(int ms)
{
    { const QSignalBlocker b(m_anPeriod); m_anPeriod->setValue(ms); }
    m_anTimer->setInterval(ms);
    refreshAnalog();
    saveSettings();
}

void BrakeBenchWindow::setAnalogTarget(const QString &host, quint16 port)
{
    m_anHost->setText(host);
    m_anPort->setValue(port);
}

QString BrakeBenchWindow::analogHost() const { return m_anHost->text().trimmed(); }
quint16 BrakeBenchWindow::analogPort() const { return quint16(m_anPort->value()); }

void BrakeBenchWindow::setAnalogFollows(bool on)
{
    { const QSignalBlocker b(m_anFollow); m_anFollow->setChecked(on); }
    for (double &f : m_fedKg) f = -1;
    feedAnalog();
    saveSettings();
}

bool BrakeBenchWindow::analogFollows() const { return m_anFollow && m_anFollow->isChecked(); }

void BrakeBenchWindow::sendAnalogNow()
{
    QVector<quint32> raw;
    for (int ch = 0; ch < kAnalogChannels; ++ch) raw << analogRawFor(analogMa(ch));
    for (quint8 id : analogCards()) {
        const QByteArray bytes = analogData(id, m_anSeq++, raw, m_wf);
        if (m_analogSink) m_analogSink(bytes);
        else if (!m_anUdp->sendOnce(bytes, analogHost(), analogPort())) continue;   // said by UdpSender::error
        ++m_analogSent;
        if (m_logAnalog && m_logAnalog->isChecked()) {
            QStringList ch;
            for (int i = 0; i < kAnalogChannels; ++i) ch << QStringLiteral("%1 mA").arg(CabIo::analogMa(raw.at(i)), 0, 'f', 2);
            logRow(now(), true, QStringLiteral("\u2014"), tr("Analog %1").arg(id), bytes, ch.join(QStringLiteral(" \u00B7 ")),
                   QString(), false);
        }
    }
}

// ---- the log -----------------------------------------------------------------------------

void BrakeBenchWindow::logRow(qint64 ms, bool tx, const QString &cob, const QString &name, const QByteArray &bytes,
                              const QString &says, const QString &note, bool heartbeat)
{
    if (m_logPause && m_logPause->isChecked()) return;
    if (heartbeat && m_logHeartbeats && !m_logHeartbeats->isChecked()) return;
    const bool status = name == Biu::frameName(Biu::kCobPressures) || name == Biu::frameName(Biu::kCobCodes)
                        || name == Biu::frameName(Biu::kCobDiscretes);
    if (status && tx && note.isEmpty() && m_logStatus && !m_logStatus->isChecked()) return;
    m_pending.append({ ms, tx, cob, name, Biu::hexBytes(bytes), says, note });
    if (m_pending.size() > kLogCap) m_pending.remove(0, m_pending.size() - kLogCap);
}

void BrakeBenchWindow::flushLog()
{
    if (!m_log || m_pending.isEmpty()) return;
    QScrollBar *sb = m_log->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 2;
    m_log->setUpdatesEnabled(false);
    for (const PendingRow &p : m_pending) {
        const int r = m_log->rowCount();
        m_log->insertRow(r);
        const QStringList cells{ QDateTime::fromMSecsSinceEpoch(p.ms).toString(QStringLiteral("HH:mm:ss.zzz")),
                                 p.tx ? QStringLiteral("\u2192") : QStringLiteral("\u2190"),
                                 p.cob, p.name, p.bytes, p.says, p.note };
        for (int c = 0; c < cells.size(); ++c) {
            auto *it = new QTableWidgetItem(cells.at(c));
            if (c == 1) it->setToolTip(p.tx ? tr("BIU \u2192 VCC") : tr("VCC \u2192 BIU"));
            if (c == 6 && !p.note.isEmpty()) it->setForeground(UiColor::warning());
            if (c == 4 || c == 2) it->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            m_log->setItem(r, c, it);
        }
    }
    m_pending.clear();
    if (m_log->rowCount() > kLogCap) {
        const int drop = m_log->rowCount() - kLogCap;
        for (int i = 0; i < drop; ++i) m_log->removeRow(0);
    }
    m_log->setUpdatesEnabled(true);
    if (atBottom) m_log->scrollToBottom();
}

int BrakeBenchWindow::logRows() const { return m_log ? m_log->rowCount() : 0; }

// ---- refresh --------------------------------------------------------------------------------

void BrakeBenchWindow::onUiTick()
{
    refreshTop();
    refreshBench();
    refreshStatusTab();
    refreshAnalog();
    refreshRelays();
    flushLog();
}

void BrakeBenchWindow::refreshTop()
{
    if (!m_startBtn) return;
    refreshReplyList();
    const qint64 t = now();
    const bool run = biuRunning();
    m_startBtn->setText(run ? tr("Stop BIU") : tr("Start BIU"));
    if (m_presetBtn) {
        // The code queues move by themselves (acks, 0x1040 on isolation):
        // only what the operator sets counts as a change.
        auto settable = [](QJsonObject o) {
            o.remove(QStringLiteral("faults"));
            o.remove(QStringLiteral("displays"));
            return o;
        };
        const bool changed = !m_activePreset.isEmpty() && settable(currentPreset().toJson()) != settable(m_activeJson);
        m_presetBtn->setText(m_activePreset.isEmpty() ? tr("Presets")
                             : changed ? tr("Preset: %1 (changed)").arg(m_activePreset)
                                       : tr("Preset: %1").arg(m_activePreset));
    }
    if (run) {
        const QStringList targets = m_biu->replyTargets();
        const QString to = targets.size() > 1 ? tr("%1 VCCs").arg(targets.size()) : targets.join(QString());
        m_runState->setText(QStringLiteral("\u25CF ") + tr("Running \u00B7 %1 \u00B7 answers %2")
                                .arg(m_biu->isListening() ? tr("UDP %1").arg(m_biu->listenPort()) : tr("not listening"))
                                .arg(to.isEmpty() ? tr("the VCC, once it sends") : to));
        m_runState->setStyleSheet(m_biu->silent() ? UiColor::warningStyle() : UiColor::okStyle());
    } else {
        m_runState->setText(QStringLiteral("\u25CB ") + tr("Stopped"));
        m_runState->setStyleSheet(UiColor::mutedStyle());
    }
    const int ks = m_biu->kavachState();
    if (ks < 0) {
        m_kavachChip->setText(tr("KAVACH: not heard"));
        m_kavachChip->setStyleSheet(UiColor::mutedStyle());
    } else {
        const bool lost = m_biu->kavachLost(t);
        m_kavachChip->setText(tr("KAVACH: %1 \u00B7 heartbeat %2").arg(lost ? tr("lost") : Biu::nmtStateName(ks),
                                                                         ago(m_biu->kavachHeartbeatMs(), t)));
        m_kavachChip->setStyleSheet(lost ? UiColor::errorStyle()
                                         : ks == Biu::Operational ? UiColor::okStyle() : UiColor::warningStyle());
    }
    const double per = m_biu->commandPeriodMs();
    m_cmdChip->setText(m_biu->hasCommand()
                           ? tr("%1 commands%2").arg(m_biu->commandCount())
                                 .arg(per > 0 ? tr(", every %1 ms").arg(per, 0, 'f', 0) : QString())
                           : tr("No commands yet"));
}

void BrakeBenchWindow::refreshBench()
{
    if (!m_targetWhy) return;
    const qint64 t = now();
    const Biu::Status st = m_biu->lastStatus();
    static const int biuId[BrakePlant::kChannels] = { Biu::Bp, Biu::Bc, Biu::Mr };
    for (int ch = 0; ch < BrakePlant::kChannels; ++ch) {
        const double v = m_plant.value(ch), target = m_plant.target(ch);
        QStringList parts;
        parts << (m_plant.settled(ch) || m_source == Source::Manual ? kg(v) : tr("%1 \u2192 %2").arg(kg(v), kg(target)));
        if (biuRunning() && m_biu->frameOn(0))
            parts << tr("BIU %1%2").arg(kg(Biu::byteToKg(st.raw[biuId[ch]])))
                         .arg(st.valid[biuId[ch]] ? QString() : tr(" not valid"));
        if (analogFollows()) {
            const PressureReading p = currentToPressure(analogMa(ch), analogScale(ch));
            parts << (p.status == PressureReading::Ok ? tr("analog %1").arg(kg(p.kgcm2)) : tr("analog: no reading"));
        }
        if (m_plant.fault(ch) != BrakePlant::Fault::None) parts << m_faultBox[ch]->currentText().section(QLatin1Char(' '), 0, 0).toLower();
        m_gaugeNote[ch]->setText(parts.join(QStringLiteral(" \u00B7 ")));
    }
    switch (m_source) {
    case Source::Biu:    m_targetWhy->setText(m_biu->targets(t).why); break;
    case Source::Relays: {
        const int level = brakeLevelNow();
        m_targetWhy->setText(level < 0 ? tr("Waiting for the Cab I/O Simulator's output data: released meanwhile.")
                                       : tr("Brake relays: %1.").arg(level == NoBrake ? tr("no brake") : tr("%1 applied").arg(brakeLevelName(level))));
        break;
    }
    case Source::Manual: m_targetWhy->setText(tr("Manual: drag a dial; nothing else moves them.")); break;
    }

    // KAVACH's command
    if (m_biu->hasCommand()) {
        const Biu::Command c = m_biu->lastCommand();
        m_cmdSpeed->setText(tr("%1 m/s").arg(c.speedMs(), 0, 'f', 2));
        m_cmdA9->setText(QStringLiteral("%1%2").arg(kg(c.a9()), c.a9Valid() ? QString() : tr(" \u2715")));
        m_cmdA9->setToolTip(c.a9Valid() ? tr("valid") : tr("not valid: the BIU ignores it"));
        m_cmdSa9->setText(QStringLiteral("%1%2").arg(kg(c.sa9()), c.sa9Valid() ? QString() : tr(" \u2715")));
        m_cmdSa9->setToolTip(c.sa9Valid() ? tr("valid") : tr("not valid: the BIU ignores it"));
        for (int b = 0; b < m_cmdBits1.size(); ++b) styleChip(m_cmdBits1[b], c.bit1(b));
        for (int b = 0; b < m_cmdBits2.size(); ++b) styleChip(m_cmdBits2[b], c.bit2(b));
        m_cmdMeta->setText(tr("Last %1 from %2.").arg(ago(m_biu->lastCommandMs(), t), m_biu->lastPeer()));
    } else {
        for (QLabel *c : m_cmdBits1) styleChip(c, false, false);
        for (QLabel *c : m_cmdBits2) styleChip(c, false, false);
        m_cmdMeta->setText(biuRunning() ? tr("No command yet: the VCC sends them to UDP %1 once KAVACH is operational, when its firmware "
                                                 "is on the UDP link (on CAN it talks to a real BIU).")
                                              .arg(m_biu->listenPort())
                                        : tr("Start the BIU to hear KAVACH."));
    }

    // The BIU
    const BiuSimulator::PanelTest pt = m_biu->panelTest(t);
    const QString test = pt == BiuSimulator::PanelTest::InProgress ? tr("in progress")
                       : pt == BiuSimulator::PanelTest::Passed ? tr("passed")
                       : pt == BiuSimulator::PanelTest::Failed ? tr("failed") : tr("not asked for");
    QStringList lines;
    lines << tr("Saying <b>%1</b>%2").arg(Biu::nmtStateName(m_biu->biuState()),
                                           m_biu->heartbeatOn() ? QString() : tr(", heartbeat off"));
    lines << (m_biu->isolated() ? QStringLiteral("<span style=\"%1\">%2</span>").arg(UiColor::warningStyle(), tr("Isolated"))
                                : tr("Not isolated"))
          + tr(" \u00B7 isolations %1").arg(m_biu->isolationCounter());
    const QVector<quint16> f = m_biu->faultQueue(), d = m_biu->displayQueue();
    lines << tr("Fault code: %1").arg(f.isEmpty() ? tr("none") : QStringLiteral("%1 %2").arg(hexN(f.first(), 4), Biu::codeText(f.first()).toHtmlEscaped())
                                                                       + (f.size() > 1 ? tr(" (+%1 waiting)").arg(f.size() - 1) : QString()));
    lines << tr("Display code: %1").arg(d.isEmpty() ? tr("none") : QStringLiteral("%1 %2").arg(hexN(d.first(), 4), Biu::codeText(d.first()).toHtmlEscaped())
                                                                         + (d.size() > 1 ? tr(" (+%1 waiting)").arg(d.size() - 1) : QString()));
    lines << tr("Panel test: %1").arg(test);
    if (m_biu->kavachLost(t))
        lines << QStringLiteral("<span style=\"%1\">%2</span>").arg(UiColor::errorStyle(), m_biu->targets(t).why.toHtmlEscaped());
    m_biuNow->setText(lines.join(QStringLiteral("<br>")));
    { const QSignalBlocker b(m_isolateBox); m_isolateBox->setChecked(m_biu->bitMode(1, Biu::StManualIsolate) == BiuSimulator::BitMode::On); }
}

void BrakeBenchWindow::refreshStatusTab()
{
    if (!m_pSends[0]) return;
    const qint64 t = now();
    const Biu::Status s = m_biu->buildStatus(t);
    for (int id = 0; id < Biu::kPressures; ++id) {
        m_pSends[id]->setText(kg(Biu::byteToKg(s.raw[id])));
        m_pValidNow[id]->setText(s.valid[id] ? tr("valid") : tr("not valid"));
        styleChip(m_pValidNow[id], s.valid[id]);
    }
    const quint8 bytes[3] = { s.d1, s.d2, s.d3 };
    for (int byte = 0; byte < 3; ++byte)
        for (int bit = 0; bit < 8; ++bit) {
            QLabel *l = m_bitNow[byte][bit];
            if (!l) continue;
            const bool on = (bytes[byte] >> bit) & 1;
            l->setText(on ? QStringLiteral("1") : QStringLiteral("0"));
            styleChip(l, on);
            const QSignalBlocker b(m_bitMode[byte][bit]);
            m_bitMode[byte][bit]->button(int(m_biu->bitMode(byte + 1, bit)))->setChecked(true);
        }
    {
        const QSignalBlocker b(m_isoCount);
        m_isoCount->setValue(m_biu->isolationCounter());
    }
    auto queueText = [](const QVector<quint16> &q) {
        if (q.isEmpty()) return QObject::tr("Sending 0x0000: none in flight.");
        QStringList rest;
        for (int i = 1; i < q.size(); ++i) rest << hexN(q.at(i), 4);
        return QObject::tr("Sending %1 %2%3").arg(hexN(q.first(), 4), Biu::codeText(q.first()))
               .arg(rest.isEmpty() ? QString() : QObject::tr("; then %1").arg(rest.join(QStringLiteral(", "))));
    };
    m_faultQueue->setText(queueText(m_biu->faultQueue()));
    m_displayQueue->setText(queueText(m_biu->displayQueue()));
    QStringList ack;
    if (m_biu->lastFaultAckMs() >= 0) ack << tr("last fault acknowledged after %1 ms").arg(m_biu->lastFaultAckMs());
    if (m_biu->lastDisplayAckMs() >= 0) ack << tr("last display code after %1 ms").arg(m_biu->lastDisplayAckMs());
    m_ackNote->setText(ack.isEmpty() ? tr("No acknowledgement yet.") : ack.join(QStringLiteral("; ")) + QLatin1Char('.'));

    if (m_kavachNow) {
        const int ks = m_biu->kavachState();
        m_kavachNow->setText(ks < 0 ? tr("KAVACH's heartbeat (0x720) not heard yet.")
                                    : tr("KAVACH says %1; its heartbeat %2.").arg(Biu::nmtStateName(ks), ago(m_biu->kavachHeartbeatMs(), t)));
    }
}

void BrakeBenchWindow::refreshAnalog()
{
    if (m_anRows.isEmpty() || !m_anSummary) return;
    for (int ch = 0; ch < m_anRows.size(); ++ch) {
        AnRow &r = m_anRows[ch];
        const quint32 raw = analogRawFor(r.mA->value());
        const double ma = CabIo::analogMa(raw);
        const int bar = r.scale->currentData().toInt();
        const PressureReading p = currentToPressure(ma, bar);
        { const QSignalBlocker block(r.kg); r.kg->setMaximum(bar * 1.02); }
        QString t = QStringLiteral("%1 \u2192 %2 mA \u2192 ").arg(hexN(raw, 8)).arg(ma, 0, 'f', 3);
        r.gauge->setMaximum(bar * 1.02);
        if (p.status == PressureReading::Ok) {
            t += tr("%1 kg/cm\u00B2 (%2 kPa)").arg(p.kgcm2, 0, 'f', 2).arg(p.kgcm2 * 98.0665, 0, 'f', 0);
            const QSignalBlocker block(r.kg);
            r.kg->setValue(p.kgcm2);
            r.gauge->setValue(p.kgcm2);
            r.gauge->setFault(QString());
        } else {
            t += p.status == PressureReading::Open ? tr("open loop (< 3.5 mA): no pressure")
                                                   : tr("short (> 20.5 mA): no pressure");
            r.gauge->setFault(p.status == PressureReading::Open ? tr("open loop") : tr("short"));
        }
        r.kg->setEnabled(p.status == PressureReading::Ok);
        r.reads->setText(t);
        r.reads->setStyleSheet(p.status == PressureReading::Ok ? QString() : UiColor::warningStyle());
    }
    const QVector<quint8> cards = analogCards();
    QString sum = analogSending() ? tr("Sending every %1 ms as %2 to %3:%4 (UDP).").arg(m_anTimer->interval())
                                        .arg(cards.isEmpty() ? tr("no controller (tick 15 or 16)") : idRange(cards))
                                        .arg(analogHost()).arg(analogPort())
                                  : tr("Off: nothing sent unasked.");
    sum += tr(" %1 sent.").arg(m_analogSent == 1 ? tr("1 analog frame") : tr("%1 analog frames").arg(m_analogSent));
    if (analogFollows()) sum += tr(" BP, BC and MR follow the bench.");
    m_anSummary->setText(sum);
    m_anSummary->setStyleSheet(analogSending() ? QString() : UiColor::mutedStyle());
}

void BrakeBenchWindow::refreshRelays()
{
    if (!m_relayNow) return;
    const char *const outputs[4] = { "normal_brake", "full_service_brake", "emergency_brake", "light_engine_brake" };
    for (int i = 0; i < m_relayLamps.size(); ++i) {
        bool on = false;
        if (m_relayKnown)
            for (const OutputBit &o : outputBits(Cab(m_relayCab)))
                if (o.name == QLatin1String(outputs[i])) on = outputActive(o, m_relayWord);
        m_relayLamps[i]->setValues(m_relayKnown, on);
    }
    const int level = brakeLevelNow();
    QString t = level < 0 ? tr("No output data yet: open the Cab I/O Simulator and its port.")
                          : tr("Output word %1: %2.").arg(hexN(m_relayWord, 4), level == NoBrake ? tr("no brake") : tr("%1 applied").arg(brakeLevelName(level)));
    if (m_source != Source::Relays) t += tr(" Not in use: the bench follows %1.").arg(m_source == Source::Biu ? tr("the BIU") : tr("your hand"));
    m_relayNow->setText(t);
}

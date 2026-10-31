#include "stationsteps.h"

#include "uicolors.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

StationStepsPanel::StationStepsPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("stationSteps"));
    auto *title = new QLabel(tr("Build the station"), this);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    m_new = new QPushButton(tr("New station…"), this);
    m_new->setObjectName(QStringLiteral("stepsNewStation"));
    m_new->setToolTip(tr("A station from its ID, centre and loops: DN and UP main, loops, points, tag pairs and signals, "
                         "encoded with their CRC-30, ready to move to the surveyed locations"));
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("stepsList"));
    m_list->setToolTip(tr("The steps, in the order a station is best filled in: each one's rows, and ⚠ its problems"));
    for (int s = 0; s < StationBuild::StepCount; ++s) m_list->addItem(QString());
    // As tall as its rows: the room below is the help's and the problems'.
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setFixedHeight(m_list->sizeHintForRow(0) * StationBuild::StepCount + 2 * m_list->frameWidth() + 4);
    m_help = new QLabel(this);
    m_help->setObjectName(QStringLiteral("stepsHelp"));
    m_help->setWordWrap(true);
    m_help->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_help->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_problemsTitle = new QLabel(this);
    m_problems = new QListWidget(this);
    m_problems->setObjectName(QStringLiteral("stepsProblems"));
    m_problems->setWordWrap(true);
    m_problems->setToolTip(tr("Click a problem to go to its row"));
    m_back = new QPushButton(tr("‹ Back"), this);
    m_next = new QPushButton(tr("Next ›"), this);

    auto *nav = new QHBoxLayout;
    nav->addWidget(m_back);
    nav->addWidget(m_next);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 8, 4);
    v->addWidget(title);
    v->addWidget(m_new);
    v->addWidget(m_list);
    v->addLayout(nav);
    v->addWidget(m_help);
    v->addWidget(m_problemsTitle);
    v->addWidget(m_problems, 1);
    v->addStretch(0);
    // No floor of its own: Station Layout's minimum width stays what it was without the steps (session 210:
    // <= 1000 px, with Linux's wider fonts too); the window gives it its width (StationLayoutWindow).
    setMinimumWidth(1);
    setMaximumWidth(340);

    connect(m_new, &QPushButton::clicked, this, &StationStepsPanel::newStationRequested);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        showStep();
        if (!m_quiet && row >= 0) emit stepChosen(row);
    });
    connect(m_back, &QPushButton::clicked, this, [this]() { m_list->setCurrentRow(qMax(0, m_list->currentRow() - 1)); });
    connect(m_next, &QPushButton::clicked, this, [this]() {
        m_list->setCurrentRow(qMin(int(StationBuild::StepCount) - 1, m_list->currentRow() + 1));
    });
    connect(m_problems, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        const QVariant sheet = it->data(Qt::UserRole);
        if (sheet.isValid()) emit problemChosen(sheet.toInt(), it->data(Qt::UserRole + 1).toInt());
    });
    m_quiet = true;
    m_list->setCurrentRow(0);
    m_quiet = false;
    setSteps(QVector<StationBuild::StepState>(StationBuild::StepCount));
}

void StationStepsPanel::setSteps(const QVector<StationBuild::StepState> &steps)
{
    m_steps = steps;
    for (int s = 0; s < StationBuild::StepCount && s < m_list->count(); ++s) {
        const StationBuild::StepState st = m_steps.value(s);
        QString text = tr("%1. %2").arg(s + 1).arg(StationBuild::stepTitle(s));
        if (s != StationBuild::StepCheck && st.count) text += QStringLiteral("   %1").arg(st.count);
        if (!st.problems.isEmpty()) text += QStringLiteral("   ⚠ %1").arg(st.problems.size());
        else if (s == StationBuild::StepCheck) text += tr("   nothing to report");
        else if (!st.count && StationBuild::stepOptional(s)) text += tr("   optional");
        QListWidgetItem *it = m_list->item(s);
        it->setText(text);
        it->setForeground(st.problems.isEmpty() ? QBrush() : QBrush(UiColor::warning()));
        it->setToolTip(st.problems.isEmpty() ? (st.count ? tr("Filled in, nothing to report") : tr("Empty"))
                                             : tr("%n problem(s) in this step", nullptr, st.problems.size()));
    }
    showStep();
}

void StationStepsPanel::setCurrentStep(int step)
{
    if (step < 0 || step >= m_list->count() || step == m_list->currentRow()) return;
    m_quiet = true;
    m_list->setCurrentRow(step);
    m_quiet = false;
}

int StationStepsPanel::currentStep() const { return m_list->currentRow(); }

void StationStepsPanel::showStep()
{
    const int s = qMax(0, m_list->currentRow());
    m_help->setText(StationBuild::stepHelp(s));
    m_back->setEnabled(s > 0);
    m_next->setEnabled(s < StationBuild::StepCount - 1);
    const StationBuild::StepState st = m_steps.value(s);
    m_problemsTitle->setText(st.problems.isEmpty() ? tr("✓ Nothing to report in this step")
                                                   : tr("%n problem(s) in this step:", nullptr, st.problems.size()));
    m_problems->setVisible(!st.problems.isEmpty());
    m_problems->clear();
    for (const StationLayout::Problem &p : st.problems) {
        auto *it = new QListWidgetItem(p.text, m_problems);
        if (p.sheet != StationLayout::SheetNone) {
            it->setData(Qt::UserRole, p.sheet);
            it->setData(Qt::UserRole + 1, p.row);
        }
    }
}

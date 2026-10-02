#include "flasherflashingpage.h"

#include "blockmapwidget.h"
#include "flasherstyle.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QApplication>
#include <QResizeEvent>
#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// Index into the per-phase timestamp arrays.
int phaseSlot(kflash::Phase phase)
{
    return static_cast<int>(phase);
}

// The session log keeps this many lines on screen; history keeps them all.
const int kMaxLogBlocks = 5000;

// HTML-escape for appendHtml.
QString escapeHtml(const QString &text)
{
    return text.toHtmlEscaped();
}

}  // namespace

// =============================================================================
//  FlasherPhaseStepper
// =============================================================================

FlasherPhaseStepper::FlasherPhaseStepper(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    UiColor::onThemeChange(this, [this]() { update(); });
    reset();
}

void FlasherPhaseStepper::reset()
{
    const QString defaultLabels[StepCount] = {
        tr("Session"), tr("Send + early repair"), tr("Repair"), tr("Accepted")
    };
    for (int step = 0; step < StepCount; ++step) {
        m_states[step] = StepState::Pending;
        m_fractions[step] = 0.0;
        m_labels[step] = defaultLabels[step];
        m_times[step].clear();
    }
    update();
}

void FlasherPhaseStepper::setStep(int step, StepState state, double fraction,
                                  const QString &label, const QString &time)
{
    if (step < 0 || step >= StepCount) {
        return;
    }
    m_states[step] = state;
    m_fractions[step] = fraction;
    if (!label.isEmpty()) {
        m_labels[step] = label;
    }
    m_times[step] = time;
    update();
}

// Under the bars (session 134): the phase name, wrapping onto a second
// line if it must, and its time under that. On one line beside the time, a
// step a quarter of the card wide cut "Waiting for updater" to "Waiting
// for up…".
QSize FlasherPhaseStepper::sizeHint() const
{
    return QSize(600, 3 * fontMetrics().height() + 16);
}

QSize FlasherPhaseStepper::minimumSizeHint() const
{
    return QSize(300, 3 * fontMetrics().height() + 16);
}

void FlasherPhaseStepper::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int gap = 10;
    const int stepWidth = (width() - gap * (StepCount - 1)) / StepCount;
    const int barHeight = 6;
    const int lineHeight = fontMetrics().height();

    for (int step = 0; step < StepCount; ++step) {
        const int left = step * (stepWidth + gap);
        const QRectF barRect(left, 0, stepWidth, barHeight);

        // The empty track, then the filled part on top of it.
        QPainterPath track;
        track.addRoundedRect(barRect, 3, 3);
        painter.fillPath(track, FlasherStyle::notSent());

        QColor fillColor = FlasherStyle::primary();
        double fraction = 0.0;
        if (m_states[step] == StepState::Done) {
            fraction = 1.0;
        } else if (m_states[step] == StepState::Active) {
            fillColor = FlasherStyle::active();
            fraction = m_fractions[step];
            // An active step always shows something, or "active with 0 %"
            // looks exactly like "not started".
            if (fraction < 0.04) {
                fraction = 0.04;
            }
        } else if (m_states[step] == StepState::Failed) {
            fillColor = FlasherStyle::danger();
            fraction = 1.0;
        }
        if (fraction > 1.0) {
            fraction = 1.0;
        }
        if (fraction > 0.0) {
            painter.save();
            painter.setClipRect(QRectF(left, 0, stepWidth * fraction, barHeight));
            painter.fillPath(track, fillColor);
            painter.restore();
        }

        // Label left, time right, under the bar.
        QFont labelFont = font();
        QColor labelColor = palette().color(QPalette::Text);
        if (m_states[step] == StepState::Pending) {
            labelColor = FlasherStyle::muted();
        } else {
            labelFont.setBold(true);
        }
        if (m_states[step] == StepState::Active) {
            labelColor = FlasherStyle::active();
        } else if (m_states[step] == StepState::Failed) {
            labelColor = FlasherStyle::danger();
        }
        const QRect labelRect(left, barHeight + 8, stepWidth, 2 * lineHeight);
        painter.setFont(labelFont);
        painter.setPen(labelColor);
        const QFontMetrics labelMetrics(labelFont);
        const QRect needed = labelMetrics.boundingRect(labelRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                                                       m_labels[step]);
        if (needed.height() <= labelRect.height())
            painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_labels[step]);
        else   // longer than two lines even so: one line, elided
            painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignTop,
                             labelMetrics.elidedText(m_labels[step], Qt::ElideRight, stepWidth));
        const int labelLines = needed.height() > lineHeight + 2 && needed.height() <= labelRect.height() ? 2 : 1;
        painter.setFont(font());
        painter.setPen(FlasherStyle::muted());
        painter.drawText(QRect(left, barHeight + 8 + labelLines * lineHeight, stepWidth, lineHeight),
                         Qt::AlignLeft | Qt::AlignVCenter, m_times[step]);
    }
}

// =============================================================================
//  FlasherBatchRow
// =============================================================================

FlasherBatchRow::FlasherBatchRow(int cardType, const QString &fileName, QWidget *parent)
    : QWidget(parent)
{
    auto *rowLayout = new QHBoxLayout(this);
    // Left margin leaves room for the painted status dot.
    rowLayout->setContentsMargins(18 + 24 + 12, 12, 18, 12);
    rowLayout->setSpacing(8);
    // Name and status on the first line, the file on its own line under
    // them (session 134): beside the status, the file name was cut at a
    // fixed 170 px and then clipped again by "Waiting" ("KAVACH_…ppimag").
    auto *textColumn = new QVBoxLayout();
    textColumn->setSpacing(2);
    auto *nameRow = new QHBoxLayout();
    nameRow->setSpacing(8);
    auto *name = new QLabel(Flasher::cardName(cardType), this);
    QFont nameFont = name->font();
    nameFont.setBold(true);
    name->setFont(nameFont);
    nameRow->addWidget(name, 1);
    m_status = new QLabel(this);
    nameRow->addWidget(m_status);
    textColumn->addLayout(nameRow);
    m_fileName = fileName;
    m_file = new QLabel(this);
    m_file->setObjectName(QStringLiteral("flasherBatchFile"));
    m_file->setFont(UiStyle::monoFont());
    m_file->setStyleSheet(UiColor::mutedStyle());
    m_file->setToolTip(fileName);
    m_file->setMinimumWidth(40);
    m_file->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_file->setText(fileName);
    textColumn->addWidget(m_file);
    rowLayout->addLayout(textColumn, 1);
    setOutcome(Flasher::CardOutcome::Queued, tr("Queued"));
    setAutoFillBackground(false);
    UiColor::onThemeChange(this, [this]() { setOutcome(m_outcome, m_status->text()); });
}

void FlasherBatchRow::setOutcome(Flasher::CardOutcome outcome, const QString &statusText)
{
    m_outcome = outcome;
    m_status->setText(statusText);
    QFont statusFont = m_status->font();
    statusFont.setBold(outcome != Flasher::CardOutcome::Queued && outcome != Flasher::CardOutcome::NotRun);
    m_status->setFont(statusFont);

    if (outcome == Flasher::CardOutcome::Running) {
        m_status->setStyleSheet(UiColor::style(FlasherStyle::active()));
    } else if (Flasher::outcomeIsSuccess(outcome)) {
        m_status->setStyleSheet(UiColor::style(FlasherStyle::accepted()));
    } else if (outcome == Flasher::CardOutcome::Failed || outcome == Flasher::CardOutcome::Cancelled) {
        m_status->setStyleSheet(UiColor::style(FlasherStyle::danger()));
    } else {
        m_status->setStyleSheet(UiColor::mutedStyle());
    }
    update();
}

void FlasherBatchRow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_file) m_file->setText(m_file->fontMetrics().elidedText(m_fileName, Qt::ElideMiddle, m_file->width()));
}

void FlasherBatchRow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // The active row is tinted, as in the design.
    if (m_outcome == Flasher::CardOutcome::Running) {
        painter.fillRect(rect(), FlasherStyle::tint(FlasherStyle::active(), 0.10));
    }
    // Row separator.
    painter.setPen(FlasherStyle::border());
    painter.drawLine(rect().topLeft(), rect().topRight());

    // The dot: filled + tick = accepted, filled = active, red + cross =
    // failed, hollow ring = queued / not run. Shape carries the meaning as
    // well as colour.
    const QRectF dot(18, height() / 2.0 - 12, 24, 24);
    QColor dotColor = FlasherStyle::muted();
    QString glyph;
    bool filled = true;
    if (Flasher::outcomeIsSuccess(m_outcome)) {
        dotColor = FlasherStyle::primary();
        glyph = QStringLiteral("✓");
    } else if (m_outcome == Flasher::CardOutcome::Running) {
        dotColor = FlasherStyle::active();
    } else if (m_outcome == Flasher::CardOutcome::Failed || m_outcome == Flasher::CardOutcome::Cancelled) {
        dotColor = FlasherStyle::danger();
        glyph = QStringLiteral("✕");
    } else {
        filled = false;
    }
    if (filled) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(dotColor);
        painter.drawEllipse(dot);
        if (!glyph.isEmpty()) {
            QFont glyphFont = font();
            glyphFont.setBold(true);
            painter.setFont(glyphFont);
            painter.setPen(palette().color(QPalette::Base));
            painter.drawText(dot, Qt::AlignCenter, glyph);
        }
    } else {
        QPen ring(FlasherStyle::border());
        ring.setWidthF(2.0);
        painter.setPen(ring);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(dot.adjusted(1, 1, -1, -1));
    }
}

// =============================================================================
//  FlasherFlashingPage
// =============================================================================

FlasherFlashingPage::FlasherFlashingPage(QWidget *parent)
    : QWidget(parent)
{
    auto *pageLayout = new QHBoxLayout(this);
    pageLayout->setContentsMargins(16, 16, 16, 16);
    pageLayout->setSpacing(16);
    pageLayout->addWidget(buildBatchColumn());
    pageLayout->addWidget(buildCentre(), 1);

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(250);
    connect(m_tickTimer, &QTimer::timeout, this, &FlasherFlashingPage::tick);

    UiColor::onThemeChange(this, [this]() { restyle(); });
    restyle();
}

QWidget *FlasherFlashingPage::buildBatchColumn()
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("flasherBatchCard"));
    m_cards.append(card);
    card->setFixedWidth(250);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 16, 0, 16);
    cardLayout->setSpacing(10);

    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(18, 0, 18, 0);
    auto *title = new QLabel(tr("Card"), card);
    m_sectionLabels.append(title);
    titleRow->addWidget(title, 1);
    m_batchCount = new QLabel(card);
    m_batchCount->setStyleSheet(UiColor::mutedStyle());
    titleRow->addWidget(m_batchCount);
    cardLayout->addLayout(titleRow);

    m_batchRowsLayout = new QVBoxLayout();
    m_batchRowsLayout->setSpacing(0);
    cardLayout->addLayout(m_batchRowsLayout);
    cardLayout->addStretch(1);

    auto *buttons = new QVBoxLayout();
    buttons->setContentsMargins(18, 0, 18, 0);
    buttons->setSpacing(8);
    m_abortButton = new QPushButton(tr("Abort now…"), card);
    m_abortButton->setObjectName(QStringLiteral("flasherAbortButton"));
    m_abortButton->setMinimumHeight(34);
    m_abortButton->setToolTip(tr("Stop waiting, or stop the transfer that is running. Asks first."));
    connect(m_abortButton, &QPushButton::clicked, this, &FlasherFlashingPage::abortRequested);
    buttons->addWidget(m_abortButton);
    cardLayout->addLayout(buttons);
    return card;
}

QFrame *FlasherFlashingPage::makeStatCard(const QString &title, QLabel **value, QLabel **sub)
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("flasherStat%1").arg(m_cards.size()));
    m_cards.append(card);
    // Value and caption on one line (session 134): three lines a card made
    // the column of five 540 px tall, and with the log under it the window
    // could not be shorter than 863 px -- taller than a 768-px laptop.
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(14, 8, 14, 8);
    cardLayout->setSpacing(0);
    auto *titleLabel = new QLabel(title, card);
    m_sectionLabels.append(titleLabel);
    cardLayout->addWidget(titleLabel);
    auto *valueRow = new QHBoxLayout();
    valueRow->setSpacing(8);
    *value = new QLabel(QStringLiteral("—"), card);
    (*value)->setFont(FlasherStyle::statValueFont());
    valueRow->addWidget(*value, 0, Qt::AlignBaseline);
    *sub = new QLabel(card);
    (*sub)->setStyleSheet(UiColor::mutedStyle());
    (*sub)->setWordWrap(true);
    valueRow->addWidget(*sub, 1, Qt::AlignBaseline);
    cardLayout->addLayout(valueRow);
    return card;
}

QWidget *FlasherFlashingPage::buildStatsColumn()
{
    auto *column = new QWidget(this);
    column->setFixedWidth(220);
    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(10);
    columnLayout->addWidget(makeStatCard(tr("Blocks"), &m_statBlocks, &m_statBlocksSub));
    columnLayout->addWidget(makeStatCard(tr("Repair round"), &m_statRound, &m_statRoundSub));
    columnLayout->addWidget(makeStatCard(tr("Resent"), &m_statResent, &m_statResentSub));
    columnLayout->addWidget(makeStatCard(tr("Send rate"), &m_statRate, &m_statRateSub));
    columnLayout->addWidget(makeStatCard(tr("Round trip"), &m_statRtt, &m_statRttSub));
    columnLayout->addStretch(1);
    return column;
}

QWidget *FlasherFlashingPage::buildCentre()
{
    auto *centre = new QWidget(this);
    auto *centreLayout = new QVBoxLayout(centre);
    centreLayout->setContentsMargins(0, 0, 0, 0);
    centreLayout->setSpacing(16);

    auto *upper = new QHBoxLayout();
    upper->setSpacing(16);

    // ---- the card panel: header, stepper, block map ----------------------------
    auto *mainCard = new QFrame(centre);
    mainCard->setObjectName(QStringLiteral("flasherMainCard"));
    m_cards.append(mainCard);
    auto *mainLayout = new QVBoxLayout(mainCard);
    mainLayout->setContentsMargins(22, 18, 22, 18);
    mainLayout->setSpacing(14);

    auto *headerRow = new QHBoxLayout();
    auto *headerText = new QVBoxLayout();
    headerText->setSpacing(2);
    m_cardTitle = new QLabel(mainCard);
    m_cardTitle->setFont(FlasherStyle::scaledFont(m_cardTitle->font(), 1.5, true));
    headerText->addWidget(m_cardTitle);
    m_cardSubtitle = new QLabel(mainCard);
    m_cardSubtitle->setStyleSheet(UiColor::mutedStyle());
    headerText->addWidget(m_cardSubtitle);
    headerRow->addLayout(headerText, 1);
    auto *elapsedColumn = new QVBoxLayout();
    elapsedColumn->setSpacing(2);
    auto *elapsedTitle = new QLabel(tr("Elapsed"), mainCard);
    m_sectionLabels.append(elapsedTitle);
    elapsedTitle->setAlignment(Qt::AlignRight);
    elapsedColumn->addWidget(elapsedTitle);
    m_elapsed = new QLabel(QStringLiteral("00:00"), mainCard);
    m_elapsed->setFont(FlasherStyle::statValueFont());
    m_elapsed->setAlignment(Qt::AlignRight);
    elapsedColumn->addWidget(m_elapsed);
    headerRow->addLayout(elapsedColumn);
    mainLayout->addLayout(headerRow);

    // The power-cycle prompt: shown from the moment Flash is pressed until
    // the updater answers. The caution-banner colours, because this is the
    // one moment the operator has to act on the hardware.
    m_waitBanner = new QFrame(mainCard);
    m_waitBanner->setObjectName(QStringLiteral("flasherWaitBanner"));
    auto *waitLayout = new QHBoxLayout(m_waitBanner);
    waitLayout->setContentsMargins(16, 12, 16, 12);
    m_waitText = new QLabel(m_waitBanner);
    m_waitText->setWordWrap(true);
    m_waitText->setFont(FlasherStyle::scaledFont(m_waitText->font(), 1.1, true));
    waitLayout->addWidget(m_waitText, 1);
    m_waitBanner->hide();
    mainLayout->addWidget(m_waitBanner);

    m_stepper = new FlasherPhaseStepper(mainCard);
    mainLayout->addWidget(m_stepper);

    auto *mapTitleRow = new QHBoxLayout();
    auto *mapTitle = new QLabel(tr("Block map"), mainCard);
    m_sectionLabels.append(mapTitle);
    mapTitleRow->addWidget(mapTitle);
    // Short, with the source in the tooltip (session 134): the full sentence
    // on one line made this card at least 681 px wide.
    auto *mapCaption = new QLabel(tr("%1 B per block · hover a cell for its offset").arg(kflash::kBlockSize), mainCard);
    mapCaption->setToolTip(tr("From the board's STATUS bitmap"));
    mapCaption->setStyleSheet(UiColor::mutedStyle());
    mapTitleRow->addWidget(mapCaption, 1, Qt::AlignRight);
    mainLayout->addLayout(mapTitleRow);

    // The map can hold up to ~11 000 cells for the largest image the STATUS
    // bitmap allows, so it scrolls rather than pushing the log off-screen.
    auto *mapScroll = new QScrollArea(mainCard);
    mapScroll->setWidgetResizable(true);
    mapScroll->setFrameShape(QFrame::NoFrame);
    mapScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_blockMap = new BlockMapWidget(mapScroll);
    mapScroll->setWidget(m_blockMap);
    mapScroll->setMinimumHeight(4 * 17);   // four rows of 14-px cells; it scrolls past that
    mainLayout->addWidget(mapScroll, 1);

    // Legend, with swatches in the same colours the map paints.
    auto *legend = new QHBoxLayout();
    legend->setSpacing(18);
    auto makeLegend = [mainCard, legend](const QString &text) -> QLabel * {
        auto *entry = new QLabel(text, mainCard);
        legend->addWidget(entry);
        return entry;
    };
    m_legendHeld = makeLegend(tr("Acknowledged"));
    m_legendRepaired = makeLegend(tr("Resent this round"));
    m_legendMissing = makeLegend(tr("Still missing"));
    legend->addStretch(1);
    mainLayout->addLayout(legend);

    upper->addWidget(mainCard, 1);
    upper->addWidget(buildStatsColumn());
    centreLayout->addLayout(upper, 1);

    // ---- session log ---------------------------------------------------------
    auto *logCard = new QFrame(centre);
    logCard->setObjectName(QStringLiteral("flasherLogCard"));
    m_cards.append(logCard);
    auto *logLayout = new QVBoxLayout(logCard);
    logLayout->setContentsMargins(16, 10, 16, 12);
    logLayout->setSpacing(6);
    auto *logTitleRow = new QHBoxLayout();
    auto *logTitle = new QLabel(tr("Session log"), logCard);
    m_sectionLabels.append(logTitle);
    logTitleRow->addWidget(logTitle, 1);
    auto *copyButton = new QPushButton(tr("Copy"), logCard);
    connect(copyButton, &QPushButton::clicked, this, &FlasherFlashingPage::copyLog);
    logTitleRow->addWidget(copyButton);
    auto *saveButton = new QPushButton(tr("Save .log"), logCard);
    connect(saveButton, &QPushButton::clicked, this, &FlasherFlashingPage::saveLog);
    logTitleRow->addWidget(saveButton);
    logLayout->addLayout(logTitleRow);
    m_log = new QPlainTextEdit(logCard);
    m_log->setReadOnly(true);
    m_log->setFont(UiStyle::monoFont());
    m_log->setMaximumBlockCount(kMaxLogBlocks);
    // Six lines, scrolling (was a fixed 150 px; the log is the history of
    // the run, the block map above it is what to watch).
    m_log->setFixedHeight(QFontMetrics(UiStyle::monoFont()).lineSpacing() * 6 + 10);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    logLayout->addWidget(m_log);
    centreLayout->addWidget(logCard);

    return centre;
}

void FlasherFlashingPage::restyle()
{
    for (QFrame *card : m_cards) {
        card->setStyleSheet(FlasherStyle::cardSheet(card->objectName()));
    }
    for (QLabel *label : m_sectionLabels) {
        FlasherStyle::makeSectionLabel(label);
    }
    // Abort: danger-coloured outline, as in the design. Reads as "this one
    // is different" without being a big red button that invites a click.
    const QColor danger = FlasherStyle::danger();
    m_waitBanner->setStyleSheet(
        QStringLiteral("QFrame#flasherWaitBanner { background-color:%1; border:1px solid %2; border-radius:8px; }"
                       "QFrame#flasherWaitBanner QLabel { color:%3; }")
            .arg(UiColor::bannerBg().name(), UiColor::bannerBorder().name(QColor::HexArgb),
                 UiColor::bannerFg().name()));
    m_abortButton->setStyleSheet(
        QStringLiteral("QPushButton#flasherAbortButton { color:%1; border:1px solid %1;"
                       " border-radius:6px; background-color:%2; font-weight:600; }"
                       "QPushButton#flasherAbortButton:disabled { color:%3; border-color:%3; }")
            .arg(danger.name(), palette().color(QPalette::Base).name(),
                 palette().color(QPalette::Disabled, QPalette::Text).name()));

    // Legend swatches: a small square of the same colour the map uses, drawn
    // as a bullet so the label stays plain text.
    auto swatch = [](const QColor &colour) {
        return QStringLiteral("<span style=\"color:%1\">■</span>").arg(colour.name());
    };
    m_legendHeld->setText(swatch(FlasherStyle::primary()) + QStringLiteral("  ") + tr("Acknowledged"));
    m_legendRepaired->setText(swatch(FlasherStyle::active()) + QStringLiteral("  ") + tr("Resent this round"));
    m_legendMissing->setText(swatch(FlasherStyle::danger()) + QStringLiteral("  ") + tr("Still missing (hatched)"));
}

// -----------------------------------------------------------------------------
//  Batch and card lifecycle
// -----------------------------------------------------------------------------

void FlasherFlashingPage::beginBatch(const Flasher::BatchPlan &plan)
{
    for (FlasherBatchRow *row : m_batchRows) {
        row->deleteLater();
    }
    m_batchRows.clear();
    for (const Flasher::BatchEntry &entry : plan.entries()) {
        auto *row = new FlasherBatchRow(entry.cardType, QFileInfo(entry.image.path).fileName(), this);
        m_batchRowsLayout->addWidget(row);
        m_batchRows.append(row);
    }
    m_log->clear();
    m_logLines.clear();
    m_abortButton->setEnabled(true);
    m_abortButton->setText(tr("Abort now…"));
    m_batchCount->clear();
}

void FlasherFlashingPage::beginCard(int index, const Flasher::BatchEntry &entry)
{
    m_currentIndex = index;
    m_currentCardType = entry.cardType;
    m_haveProgress = false;
    m_lastProgress = kflash::Progress();
    m_phase = kflash::Phase::Idle;
    m_cardFinished = false;
    m_cardSucceeded = false;
    for (int slot = 0; slot < 5; ++slot) {
        m_phaseStartMs[slot] = 0;
        m_phaseEndMs[slot] = 0;
    }
    m_cardClock.start();

    m_cardTitle->setText(Flasher::cardName(entry.cardType));
    m_cardSubtitle->setText(Flasher::deliveryText(entry.cardType));
    m_batchCount->clear();

    if (index >= 0 && index < m_batchRows.size()) {
        m_batchRows[index]->setOutcome(Flasher::CardOutcome::Running, tr("Waiting"));
    }

    m_blockMap->reset(entry.image.blockCount);
    m_stepper->reset();

    m_statBlocks->setText(QStringLiteral("0"));
    m_statBlocksSub->setText(tr("of %1").arg(entry.image.blockCount));
    m_statRound->setText(QStringLiteral("—"));
    m_statRoundSub->setText(tr("of %1 max").arg(m_maxRounds));
    m_statResent->setText(QStringLiteral("0"));
    m_statResentSub->setText(tr("blocks"));
    m_statRate->setText(QStringLiteral("—"));
    m_statRateSub->setText(QString());
    m_statRtt->setText(QStringLiteral("—"));
    m_statRttSub->setText(QString());

    // A divider in the log between cards, so one saved log reads as a batch.
    appendLog(static_cast<int>(kflash::LogLevel::Info),
              tr("==== %1 · %2 · %3 ====").arg(Flasher::cardName(entry.cardType),
                                               QFileInfo(entry.image.path).fileName(),
                                               entry.image.crcText()));
    m_tickTimer->start();
    tick();
}

void FlasherFlashingPage::finishCard(int index, const Flasher::BatchEntry &entry)
{
    m_cardFinished = true;
    m_cardSucceeded = Flasher::outcomeIsSuccess(entry.outcome);
    const qint64 now = m_cardClock.elapsed();
    if (m_phase != kflash::Phase::Idle) {
        m_phaseEndMs[phaseSlot(m_phase)] = now;
    }
    m_tickTimer->stop();
    tick();

    if (index >= 0 && index < m_batchRows.size()) {
        m_batchRows[index]->setOutcome(entry.outcome, Flasher::outcomeText(entry.outcome));
    }
}

void FlasherFlashingPage::refreshBatchRows(const Flasher::BatchPlan &plan)
{
    for (int index = 0; index < plan.size() && index < m_batchRows.size(); ++index) {
        const Flasher::CardOutcome outcome = plan.entries().at(index).outcome;
        if (m_batchRows[index]->outcome() != outcome) {
            m_batchRows[index]->setOutcome(outcome, Flasher::outcomeText(outcome));
        }
    }
    m_abortButton->setEnabled(false);
}

void FlasherFlashingPage::setAborting()
{
    m_abortButton->setText(tr("Aborting…"));
    m_abortButton->setEnabled(false);
}

bool FlasherFlashingPage::waitingForUpdater() const
{
    return m_waitBanner->isVisibleTo(const_cast<FlasherFlashingPage *>(this));
}

void FlasherFlashingPage::updateWaitPrompt()
{
    // Waiting = the card has started, is not finished, and the engine has not
    // left the handshake (Idle before its first report, Handshake after).
    const bool waiting = m_cardClock.isValid() && !m_cardFinished
                         && (m_phase == kflash::Phase::Idle || m_phase == kflash::Phase::Handshake);
    if (!waiting) {
        m_waitBanner->hide();
        return;
    }
    qint64 remainingMs = m_updaterWaitMs - m_cardClock.elapsed();
    if (remainingMs < 0) {
        remainingMs = 0;
    }
    m_waitText->setText(tr("Power-cycle the chassis now — waiting for the updater · %1 s left")
                            .arg((remainingMs + 999) / 1000));
    m_waitBanner->show();
}

// -----------------------------------------------------------------------------
//  Live updates
// -----------------------------------------------------------------------------

void FlasherFlashingPage::onProgress(const kflash::Progress &progress)
{
    const qint64 now = m_cardClock.elapsed();

    // Phase transitions: close the old phase, open the new one. Done is not
    // tracked as a phase of its own: the engine enters it on success AND on
    // failure, so it says nothing about which. Keeping m_phase at the last
    // working phase is what lets the stepper mark the phase a card failed in.
    if (progress.phase != m_phase && progress.phase != kflash::Phase::Done) {
        if (m_phase != kflash::Phase::Idle) {
            m_phaseEndMs[phaseSlot(m_phase)] = now;
        }
        m_phase = progress.phase;
        if (m_phaseStartMs[phaseSlot(m_phase)] == 0) {
            m_phaseStartMs[phaseSlot(m_phase)] = now;
        }
    }
    m_lastProgress = progress;
    m_haveProgress = true;

    // During the first pass the map also shows what has been SENT but not yet
    // confirmed, which is most of it until the first checkpoint STATUS.
    if (progress.phase == kflash::Phase::InitialSend) {
        m_blockMap->setSendCursor(progress.sent_cursor);
    }

    // ---- stats -----------------------------------------------------------------
    m_statBlocks->setText(QString::number(progress.acked_blocks));
    m_statBlocksSub->setText(tr("of %1").arg(progress.total_blocks));
    if (progress.phase == kflash::Phase::Repair || progress.phase == kflash::Phase::Done) {
        m_statRound->setText(QString::number(progress.round));
    } else {
        m_statRound->setText(QStringLiteral("—"));
    }
    m_statRoundSub->setText(tr("of %1 max").arg(m_maxRounds));
    m_statResent->setText(QString::number(progress.blocks_resent));
    if (progress.total_blocks > 0) {
        m_statResentSub->setText(tr("%1 % of blocks")
                                     .arg(100.0 * progress.blocks_resent / progress.total_blocks, 0, 'f', 1));
    }
    m_statRate->setText(QString::number(qRound(progress.wire_rate_kBps)));
    m_statRateSub->setText(tr("KB/s · target %1").arg(qRound(progress.target_rate_kBps)));
    if (progress.rtt_ms > 0.0) {
        m_statRtt->setText(QString::number(progress.rtt_ms, 'f', 1));
    } else {
        m_statRtt->setText(QStringLiteral("—"));
    }
    m_statRttSub->setText(tr("ms · poll timeout %1 ms").arg(progress.poll_timeout_ms));

    // The batch row names the phase the card is in.
    if (m_currentIndex >= 0 && m_currentIndex < m_batchRows.size()) {
        QString phaseWord = QString::fromLatin1(kflash::phase_name(progress.phase));
        if (progress.phase == kflash::Phase::Repair) {
            phaseWord = tr("Repair %1").arg(progress.round);
        } else if (progress.phase == kflash::Phase::Handshake) {
            phaseWord = tr("Waiting");
        }
        m_batchRows[m_currentIndex]->setOutcome(Flasher::CardOutcome::Running, phaseWord);
    }
    updateStepper();
    updateWaitPrompt();
}

void FlasherFlashingPage::updateStepper()
{
    const qint64 now = m_cardClock.elapsed();

    // Duration of a phase: end - start once it has ended, now - start while
    // it is the current one, empty if it never started.
    auto phaseTime = [this, now](kflash::Phase phase) -> QString {
        const int slot = phaseSlot(phase);
        if (m_phaseStartMs[slot] == 0 && m_phase != phase) {
            return QString();
        }
        qint64 end = m_phaseEndMs[slot];
        if (end == 0) {
            end = now;
        }
        return Flasher::formatDuration(end - m_phaseStartMs[slot]);
    };

    double fraction = 0.0;
    if (m_lastProgress.total_blocks > 0) {
        fraction = static_cast<double>(m_lastProgress.acked_blocks) / m_lastProgress.total_blocks;
    }

    // Where each step stands, from the current phase. The engine moves
    // strictly forward (Handshake -> InitialSend -> Repair -> Done), except
    // that an "already held" image goes straight from Handshake to Done.
    using StepState = FlasherPhaseStepper::StepState;
    const int currentPhase = phaseSlot(m_phase);
    auto stateFor = [&](kflash::Phase phase) -> StepState {
        const int slot = phaseSlot(phase);
        if (m_cardFinished && !m_cardSucceeded && slot == currentPhase) {
            return StepState::Failed;
        }
        if (currentPhase > slot) {
            if (m_phaseStartMs[slot] == 0 && phase != kflash::Phase::Handshake) {
                return StepState::Pending;   // skipped (already held)
            }
            return StepState::Done;
        }
        if (currentPhase == slot) {
            if (m_cardFinished) {
                return StepState::Done;
            }
            return StepState::Active;
        }
        return StepState::Pending;
    };

    // While the handshake runs, the operator is power-cycling the chassis;
    // the step says so.
    QString sessionLabel = tr("Session");
    if (stateFor(kflash::Phase::Handshake) == StepState::Active) {
        sessionLabel = tr("Waiting for updater");
    }
    m_stepper->setStep(FlasherPhaseStepper::StepSession, stateFor(kflash::Phase::Handshake), 0.5,
                       sessionLabel, phaseTime(kflash::Phase::Handshake));
    double sendFraction = fraction;
    if (m_lastProgress.total_blocks > 0 && m_phase == kflash::Phase::InitialSend) {
        sendFraction = static_cast<double>(m_lastProgress.sent_cursor) / m_lastProgress.total_blocks;
    }
    m_stepper->setStep(FlasherPhaseStepper::StepSend, stateFor(kflash::Phase::InitialSend), sendFraction,
                       tr("Send + early repair"), phaseTime(kflash::Phase::InitialSend));

    QString repairLabel = tr("Repair");
    if (m_phaseStartMs[phaseSlot(kflash::Phase::Repair)] != 0 || m_phase == kflash::Phase::Repair) {
        repairLabel = tr("Repair · round %1").arg(m_lastProgress.round);
    }
    m_stepper->setStep(FlasherPhaseStepper::StepRepair, stateFor(kflash::Phase::Repair), fraction,
                       repairLabel, phaseTime(kflash::Phase::Repair));

    StepState acceptedState = StepState::Pending;
    QString acceptedLabel = tr("Accepted");
    if (m_cardFinished && m_cardSucceeded) {
        acceptedState = StepState::Done;
    } else if (m_cardFinished) {
        acceptedState = StepState::Failed;
        acceptedLabel = tr("Not accepted");
    }
    m_stepper->setStep(FlasherPhaseStepper::StepAccepted, acceptedState, 1.0, acceptedLabel, QString());
}

void FlasherFlashingPage::tick()
{
    if (m_cardClock.isValid()) {
        m_elapsed->setText(Flasher::formatClock(m_cardClock.elapsed()));
    }
    updateWaitPrompt();
    updateStepper();
}

void FlasherFlashingPage::appendLog(int level, const QString &text)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));

    // Colour by level, and pick out the session/rate lines -- the ones an
    // engineer scans for when a transfer is slow -- in the accent colour.
    QColor colour = palette().color(QPalette::Text);
    QString levelTag = QStringLiteral("INFO");
    if (level == static_cast<int>(kflash::LogLevel::Warn)) {
        colour = FlasherStyle::attention();
        levelTag = QStringLiteral("WARN");
    } else if (level == static_cast<int>(kflash::LogLevel::Error)) {
        colour = FlasherStyle::danger();
        levelTag = QStringLiteral("FAIL");
    } else if (text.contains(QLatin1String("Session"), Qt::CaseInsensitive)
               || text.contains(QLatin1String("META"))
               || text.contains(QLatin1String("rate"), Qt::CaseInsensitive)
               || text.contains(QLatin1String("RTT"))) {
        colour = FlasherStyle::primary();
    }

    m_logLines.append(QStringLiteral("%1  %2  %3").arg(stamp, levelTag, text));
    m_log->appendHtml(QStringLiteral("<span style=\"color:%1\">%2  %3</span>")
                          .arg(colour.name(), escapeHtml(stamp), escapeHtml(text)));
}

void FlasherFlashingPage::copyLog()
{
    QApplication::clipboard()->setText(m_logLines.join(QLatin1Char('\n')));
}

void FlasherFlashingPage::saveLog()
{
    const QString suggested = QStringLiteral("flash_%1.log")
                                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(this, tr("Save session log"), suggested,
                                                      tr("Log files (*.log);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save session log"),
                             tr("Could not write %1:\n%2").arg(path, file.errorString()));
        return;
    }
    QTextStream out(&file);
    for (const QString &line : m_logLines) {
        out << line << "\n";
    }
}

#include "flashersummarypage.h"

#include "flasherstyle.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

// The badge colour for an outcome. A function pointer rather than a QColor,
// so the badge can be re-coloured from the new palette on a theme toggle.
using ColourFn = QColor (*)();

ColourFn badgeColourFor(Flasher::CardOutcome outcome)
{
    if (Flasher::outcomeIsSuccess(outcome)) {
        return &FlasherStyle::accepted;
    }
    if (outcome == Flasher::CardOutcome::Failed || outcome == Flasher::CardOutcome::Cancelled) {
        return &FlasherStyle::danger;
    }
    return &FlasherStyle::muted;
}

}  // namespace

FlasherSummaryPage::FlasherSummaryPage(QWidget *parent)
    : QWidget(parent)
{
    auto *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(16, 16, 16, 16);
    pageLayout->setSpacing(16);

    // ---- verdict banner ----------------------------------------------------------
    m_banner = new QFrame(this);
    m_banner->setObjectName(QStringLiteral("flasherVerdict"));
    auto *bannerLayout = new QHBoxLayout(m_banner);
    bannerLayout->setContentsMargins(22, 18, 22, 18);
    bannerLayout->setSpacing(16);
    auto *bannerText = new QVBoxLayout();
    bannerText->setSpacing(4);
    m_headline = new QLabel(m_banner);
    m_headline->setFont(FlasherStyle::scaledFont(m_headline->font(), 1.6, true));
    m_headline->setWordWrap(true);
    bannerText->addWidget(m_headline);
    m_explanation = new QLabel(m_banner);
    m_explanation->setWordWrap(true);
    bannerText->addWidget(m_explanation);
    bannerLayout->addLayout(bannerText, 1);

    auto *exportButton = new QPushButton(tr("Export report"), m_banner);
    exportButton->setMinimumHeight(34);
    connect(exportButton, &QPushButton::clicked, this, &FlasherSummaryPage::exportReportRequested);
    bannerLayout->addWidget(exportButton, 0, Qt::AlignVCenter);
    m_retryButton = new QPushButton(tr("Try again"), m_banner);
    m_retryButton->setMinimumHeight(34);
    m_retryButton->setToolTip(tr("Back to the queue with this card selected.\n"
                                 "The pre-flight checks run again before anything is sent."));
    connect(m_retryButton, &QPushButton::clicked, this, &FlasherSummaryPage::retryRequested);
    bannerLayout->addWidget(m_retryButton, 0, Qt::AlignVCenter);
    pageLayout->addWidget(m_banner);

    // ---- one panel per card, in a scroll area ---------------------------------
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *cardsHost = new QWidget(scroll);
    m_cardsLayout = new QGridLayout(cardsHost);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(16);
    scroll->setWidget(cardsHost);
    pageLayout->addWidget(scroll, 1);

    // ---- footer ------------------------------------------------------------------
    auto *footer = new QHBoxLayout();
    m_footer = new QLabel(this);
    m_footer->setStyleSheet(UiColor::mutedStyle());
    m_footer->setTextInteractionFlags(Qt::TextSelectableByMouse);
    footer->addWidget(m_footer, 1);
    auto *historyButton = new QPushButton(tr("Open history"), this);
    connect(historyButton, &QPushButton::clicked, this, &FlasherSummaryPage::openHistoryRequested);
    footer->addWidget(historyButton);
    auto *backButton = new QPushButton(tr("Back to queue"), this);
    connect(backButton, &QPushButton::clicked, this, &FlasherSummaryPage::backToQueueRequested);
    footer->addWidget(backButton);
    pageLayout->addLayout(footer);

    UiColor::onThemeChange(this, [this]() { restyle(); });
}

QString FlasherSummaryPage::headline() const
{
    return m_headline->text();
}

void FlasherSummaryPage::showBatch(const Flasher::BatchPlan &plan, const QString &batchId,
                                   const QString &operatorName, bool historySaved)
{
    const Flasher::Verdict verdict = Flasher::batchVerdict(plan);
    m_tone = verdict.tone;
    m_headline->setText(verdict.headline);
    m_explanation->setText(verdict.explanation);
    m_explanation->setVisible(!verdict.explanation.isEmpty());
    m_retryButton->setVisible(plan.failedCount() + plan.notRunCount() > 0);

    // Replace the card panels.
    for (QFrame *panel : m_cardPanels) {
        panel->hide();   // until deleteLater runs, an old panel would still show
        panel->deleteLater();
    }
    m_cardPanels.clear();
    m_badges.clear();
    const int columns = 3;
    for (int index = 0; index < plan.size(); ++index) {
        QFrame *panel = buildCardPanel(plan.entries().at(index), index);
        m_cardsLayout->addWidget(panel, index / columns, index % columns, Qt::AlignTop);
        m_cardPanels.append(panel);
    }
    // Keep the panels at a third of the width each even with fewer than three.
    for (int column = 0; column < columns; ++column) {
        m_cardsLayout->setColumnStretch(column, 1);
    }
    m_cardsLayout->setRowStretch(m_cardsLayout->rowCount(), 1);

    QString savedText = tr("Saved to history as batch %1").arg(batchId);
    if (!historySaved) {
        savedText = tr("NOT saved to history (write failed) — batch %1").arg(batchId);
    }
    m_footer->setText(tr("%1 · operator %2").arg(savedText, operatorName));
    if (historySaved) {
        m_footer->setStyleSheet(UiColor::mutedStyle());
    } else {
        m_footer->setStyleSheet(UiColor::errorStyle());
    }
    restyle();
}

QFrame *FlasherSummaryPage::buildCardPanel(const Flasher::BatchEntry &entry, int index)
{
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("flasherSummaryCard%1").arg(index));
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(20, 18, 20, 18);
    panelLayout->setSpacing(8);

    auto *titleRow = new QHBoxLayout();
    auto *title = new QLabel(tr("%1 · %2").arg(Flasher::cardName(entry.cardType),
                                               Flasher::cardIdText(entry.cardType)), panel);
    title->setFont(FlasherStyle::scaledFont(title->font(), 1.15, true));
    titleRow->addWidget(title, 1);
    auto *badge = new QLabel(Flasher::outcomeText(entry.outcome), panel);
    m_badges.append(qMakePair(badge, badgeColourFor(entry.outcome)));
    titleRow->addWidget(badge);
    panelLayout->addLayout(titleRow);

    auto *file = new QLabel(QFileInfo(entry.image.path).fileName(), panel);
    file->setFont(UiStyle::monoFont());
    file->setToolTip(entry.image.path);
    panelLayout->addWidget(file);

    const bool ran = entry.outcome != Flasher::CardOutcome::NotRun
                     && entry.outcome != Flasher::CardOutcome::Queued;
    const bool failed = entry.outcome == Flasher::CardOutcome::Failed
                        || entry.outcome == Flasher::CardOutcome::Cancelled;
    // Not repeated when the banner above already says it, word for word
    // (session 134: a one-card run printed the same paragraph twice).
    if (failed && Flasher::failureExplanation(entry.result) != m_explanation->text()) {
        auto *reason = new QLabel(Flasher::failureExplanation(entry.result), panel);
        reason->setWordWrap(true);
        reason->setStyleSheet(UiColor::errorStyle());
        panelLayout->addWidget(reason);
    }
    if (entry.outcome == Flasher::CardOutcome::AlreadyHeld) {
        auto *note = new QLabel(tr("The board already held this exact image — nothing was sent."), panel);
        note->setWordWrap(true);
        note->setStyleSheet(UiColor::mutedStyle());
        panelLayout->addWidget(note);
    }

    // ---- key / value grid, from FlashResult ----------------------------------
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(4);
    int row = 0;
    auto addRow = [&grid, &row, panel](const QString &key, const QString &value, bool mono) {
        auto *keyLabel = new QLabel(key, panel);
        keyLabel->setStyleSheet(UiColor::mutedStyle());
        grid->addWidget(keyLabel, row, 0, Qt::AlignTop);
        auto *valueLabel = new QLabel(value, panel);
        valueLabel->setWordWrap(true);
        valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        if (mono) {
            valueLabel->setFont(UiStyle::monoFont());
        }
        grid->addWidget(valueLabel, row, 1);
        ++row;
    };

    const kflash::FlashResult &result = entry.result;
    addRow(tr("CRC-32"), entry.image.crcText(), true);
    if (!ran) {
        addRow(tr("Reason"), tr("Batch stopped"), false);
    } else if (failed) {
        if (Flasher::isImageFail(result)) {
            addRow(tr("Last status"), QStringLiteral("IMAGE_FAIL (0x03)"), true);
        }
        addRow(tr("Engine message"), QString::fromStdString(result.message), false);
        addRow(tr("Blocks resent"), tr("%1 / %2").arg(result.blocks_resent).arg(result.total_blocks), true);
        addRow(tr("Total"), Flasher::formatDuration(result.total_ms), true);
    } else {
        addRow(tr("Repair rounds"), QString::number(result.repair_rounds), true);
        addRow(tr("Blocks resent"), tr("%1 / %2").arg(result.blocks_resent).arg(result.total_blocks), true);
        addRow(tr("Bytes on wire"), Flasher::formatBytes(result.bytes_on_wire), true);
        addRow(tr("Avg throughput"), Flasher::formatRate(result.avg_kBps), true);
        if (entry.outcome == Flasher::CardOutcome::Accepted) {
            addRow(tr("Settled rate"),
                   tr("%1 · saved as next start rate").arg(Flasher::formatRate(result.final_rate_kBps)), false);
        }
        addRow(tr("Phases"),
               tr("%1 · %2 · %3").arg(Flasher::formatDuration(result.handshake_ms),
                                      Flasher::formatDuration(result.initial_send_ms),
                                      Flasher::formatDuration(result.repair_ms)), true);
        addRow(tr("Total"), Flasher::formatDuration(result.total_ms), true);
    }
    grid->setColumnStretch(1, 1);
    panelLayout->addLayout(grid);
    panelLayout->addStretch(1);
    return panel;
}

void FlasherSummaryPage::restyle()
{
    // Banner: the verdict's colour, as a tint with the headline in the ink.
    // Anything short of every card updated is red, as the design has it:
    // a half-updated chassis is a chassis that needs attention.
    QColor ink = FlasherStyle::accepted();
    if (m_tone != Flasher::Verdict::Tone::Success) {
        ink = FlasherStyle::danger();
    }
    m_banner->setStyleSheet(FlasherStyle::tintedPanelSheet(m_banner->objectName(), ink));
    m_headline->setStyleSheet(UiColor::style(ink));

    for (QFrame *panel : m_cardPanels) {
        panel->setStyleSheet(FlasherStyle::cardSheet(panel->objectName()));
    }
    for (const auto &badge : m_badges) {
        badge.first->setStyleSheet(FlasherStyle::badgeSheet(badge.second()));
    }
}

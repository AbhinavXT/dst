#ifndef FLASHERSUMMARYPAGE_H
#define FLASHERSUMMARYPAGE_H
// =============================================================================
//  flashersummarypage.{h,cpp} -- page 2 of the Firmware Flasher
//  (Summary.dc.html): what happened to each card, in words a technician can
//  act on, and the way back to the queue.
// =============================================================================
#include <QWidget>

#include "flashercore.h"

class QFrame;
class QGridLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

class FlasherSummaryPage : public QWidget
{
    Q_OBJECT
public:
    explicit FlasherSummaryPage(QWidget *parent = nullptr);

    void showBatch(const Flasher::BatchPlan &plan, const QString &batchId,
                   const QString &operatorName, bool historySaved);

    // For the tests.
    QString headline() const;

signals:
    void exportReportRequested();
    void retryRequested();          // failed + not-run cards
    void openHistoryRequested();
    void backToQueueRequested();

private:
    QFrame *buildCardPanel(const Flasher::BatchEntry &entry, int index);
    void    restyle();

    Flasher::Verdict::Tone m_tone = Flasher::Verdict::Tone::Success;
    QFrame      *m_banner = nullptr;
    QLabel      *m_headline = nullptr;
    QLabel      *m_explanation = nullptr;
    QPushButton *m_retryButton = nullptr;
    QGridLayout *m_cardsLayout = nullptr;
    QList<QFrame *> m_cardPanels;
    QList<QPair<QLabel *, QColor (*)()>> m_badges;   // re-coloured on theme change
    QLabel      *m_footer = nullptr;
};

#endif  // FLASHERSUMMARYPAGE_H

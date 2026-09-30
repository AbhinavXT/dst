#include "notificationcenter.h"
#include "uicolors.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

// ============================== bar ========================================

NotificationBar::NotificationBar(QWidget *parent)
    : QWidget(parent)
{
    m_label = new QLabel;
    m_label->setTextInteractionFlags(Qt::NoTextInteraction);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_label);

    m_clearTimer = new QTimer(this);
    m_clearTimer->setSingleShot(true);
    connect(m_clearTimer, &QTimer::timeout, this, [this]() {
        // Only the inline text clears. The history keeps everything —
        // that is the whole point of having one.
        m_label->clear();
        m_label->setStyleSheet(QString());
        refresh();
    });

    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Click to see all messages from this session"));
}

void NotificationBar::post(NoteLevel level, const QString &text,
                           const QString &detail)
{
    Notification n;
    n.level  = level;
    n.text   = text;
    n.detail = detail;
    n.when   = QDateTime::currentDateTime();

    m_history.append(n);
    while (m_history.size() > kMaxHistory) m_history.removeFirst();

    if (level == NoteLevel::Error) ++m_unreadErrors;

    m_label->setText(text);
    switch (level) {
    case NoteLevel::Error:
        m_label->setStyleSheet(UiColor::errorStyle() + QStringLiteral(" font-weight: bold;"));
        // Errors do NOT expire. One that scrolls away unread is the same as
        // no error at all, which is precisely the failure mode of the
        // five-second label this replaces.
        m_clearTimer->stop();
        break;
    case NoteLevel::Warning:
        m_label->setStyleSheet(UiColor::warningStyle());
        m_clearTimer->start(kInfoTimeoutMs);
        break;
    case NoteLevel::Info:
        m_label->setStyleSheet(QString());
        m_clearTimer->start(kInfoTimeoutMs);
        break;
    }
    refresh();
}

void NotificationBar::refresh()
{
    // A count of what is waiting, so a cleared line never means "nothing
    // happened".
    QString tip = tr("%1 message(s) this session").arg(m_history.size());
    if (m_unreadErrors > 0) {
        tip += tr("  ·  %1 error(s)").arg(m_unreadErrors);
        if (m_label->text().isEmpty()) {
            m_label->setText(tr("%1 error(s) — click for details")
                                 .arg(m_unreadErrors));
            m_label->setStyleSheet(UiColor::errorStyle() + QStringLiteral(" font-weight: bold;"));
        }
    }
    setToolTip(tip);
}

void NotificationBar::mouseReleaseEvent(QMouseEvent *)
{
    m_unreadErrors = 0;      // opening the log counts as reading it
    emit historyRequested();
    refresh();
}

// ============================== log window =================================

NotificationLog::NotificationLog(const QVector<Notification> &history,
                                 QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Messages"));
    resize(760, 420);

    m_table = new QTableWidget(history.size(), 3);
    m_table->setHorizontalHeaderLabels({ tr("Time"), tr("Level"), tr("Message") });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setColumnWidth(0, 160);
    m_table->setColumnWidth(1, 70);
    m_table->horizontalHeader()->setStretchLastSection(true);

    // Newest first: the reason anyone opens this is to see what just
    // happened, not what happened at start-up.
    for (int i = 0; i < history.size(); ++i) {
        const Notification &n = history.at(history.size() - 1 - i);
        const QString level = n.level == NoteLevel::Error   ? tr("Error")
                            : n.level == NoteLevel::Warning ? tr("Warning")
                                                            : tr("Info");
        const QStringList cells = {
            n.when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
            level, n.text
        };
        for (int c = 0; c < cells.size(); ++c) {
            auto *item = new QTableWidgetItem(cells.at(c));
            if (n.level == NoteLevel::Error)        item->setForeground(UiColor::error());
            else if (n.level == NoteLevel::Warning) item->setForeground(UiColor::warning());
            if (!n.detail.isEmpty()) item->setToolTip(n.detail);
            m_table->setItem(i, c, item);
        }
    }

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_table);
}

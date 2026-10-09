#include "sosrelay.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "uicolors.h"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cstdlib>

namespace {

QString hms(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")); }

QString delay(qint64 ms)
{
    const QString v = QStringLiteral("%1 s").arg(std::abs(ms) / 1000.0, 0, 'f', 1);
    return ms < 0 ? QStringLiteral("−") + v : QStringLiteral("+") + v;
}

QTableWidgetItem *cell(const QString &text, const QString &tip = QString())
{
    auto *it = new QTableWidgetItem(text);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    if (!tip.isEmpty()) it->setToolTip(tip);
    return it;
}

enum Col { ColSent, ColChange, ColHeard, ColHeardDelay, ColActed, ColActedDelay, ColDecision, ColCount };

}  // namespace

SosRelayPanel::SosRelayPanel(MessageDispatcher *dispatcher, QWidget *parent)
    : QWidget(parent)
    , m_dispatcher(dispatcher)
{
    m_picker = new QComboBox(this);
    m_picker->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_picker->setToolTip(tr("The other loco's log: a tab with @sos. Its broadcast emergency status is followed into this loco's log"));
    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->addWidget(new QLabel(tr("What this loco did with what"), this));
    top->addWidget(m_picker);
    top->addWidget(new QLabel(tr("sent"), this));
    top->addStretch(1);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels({ tr("Sent (its log)"), tr("Status change"), tr("Heard here"), tr("after"),
                                          tr("Acted on here"), tr("after"), tr("Decision") });
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setToolTip(tr("Double-click a time: that moment in that loco's log"));

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 4, 0, 0);
    root->addLayout(top);
    root->addWidget(m_table, 1);
    root->addWidget(m_summary);

    connect(m_picker, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { rebuild(); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (row < 0 || row >= m_relays.size()) return;
        const SosLog::Relay &r = m_relays.at(row);
        if (col == ColSent || col == ColChange) emit jumpRequested(senderKey(), r.sentMs);
        else if ((col == ColHeard || col == ColHeardDelay) && r.heardMs >= 0) emit jumpRequested(m_receiver, r.heardMs);
        else if (r.actedMs >= 0) emit jumpRequested(m_receiver, r.actedMs);
    });
    if (m_dispatcher)
        connect(m_dispatcher, &MessageDispatcher::tabRequested, this, [this](QString, QString) { refreshPicker(); });
}

QString SosRelayPanel::senderKey() const
{
    return m_picker->currentData().toString();
}

void SosRelayPanel::setReceiver(const QString &key, const SosLog::Timeline &timeline)
{
    m_receiver = key;
    m_receiverTimeline = timeline;
    refreshPicker();
    rebuild();
}

void SosRelayPanel::refreshPicker()
{
    if (!m_dispatcher) return;
    const QString keep = senderKey();
    QStringList keys = m_dispatcher->knownKeys();
    keys.sort();
    m_picker->blockSignals(true);
    m_picker->clear();
    for (const QString &k : keys) {
        if (k == m_receiver || !SosLog::hasSos(m_dispatcher->modelForKey(k))) continue;
        const QString friendly = m_dispatcher->friendlyNameFor(k);
        m_picker->addItem(friendly == k ? k : QStringLiteral("%1   (%2)").arg(friendly, k), k);
    }
    const int idx = m_picker->findData(keep);
    if (idx >= 0) m_picker->setCurrentIndex(idx);
    m_picker->blockSignals(false);
}

void SosRelayPanel::setSender(const QString &key)
{
    const int idx = key.isEmpty() ? (m_picker->count() > 0 ? 0 : -1) : m_picker->findData(key);
    if (idx >= 0 && idx != m_picker->currentIndex()) m_picker->setCurrentIndex(idx);   // rebuilds
    else rebuild();
}

void SosRelayPanel::rebuild()
{
    m_relays.clear();
    m_table->setRowCount(0);
    const QString sender = senderKey();
    if (sender.isEmpty() || !m_dispatcher) {
        m_summary->setText(m_picker->count() == 0
            ? tr("No other tab has @sos: open the other loco's log to see what it sent and what this loco did with it.")
            : QString());
        return;
    }
    const quint32 senderId = SosLog::locoOfKey(sender);
    const SosLog::Timeline st = SosLog::extract(m_dispatcher->modelForKey(sender));
    m_relays = SosLog::relay(st, m_receiverTimeline, senderId);

    int heard = 0, acted = 0, untracked = 0, early = 0;
    m_table->setRowCount(m_relays.size());
    for (int i = 0; i < m_relays.size(); ++i) {
        const SosLog::Relay &r = m_relays.at(i);
        m_table->setItem(i, ColSent, cell(hms(r.sentMs)));
        m_table->setItem(i, ColChange, cell(QStringLiteral("%1 → %2").arg(SosLog::emergencyName(r.fromStatus),
                                                                              SosLog::emergencyName(r.toStatus)),
                                            tr("Loco %1's own emergency status, which its ARP broadcasts").arg(senderId)));
        if (r.heardMs >= 0) {
            ++heard;
            if (r.heardMs < r.sentMs) ++early;
            m_table->setItem(i, ColHeard, cell(hms(r.heardMs)));
            auto *d = cell(delay(r.heardMs - r.sentMs), r.heardMs < r.sentMs
                ? tr("Heard before it was sent: the two locos' clocks differ by at least this much") : QString());
            if (r.heardMs < r.sentMs) d->setForeground(UiColor::warning());
            m_table->setItem(i, ColHeardDelay, d);
        } else {
            if (!r.tracked) ++untracked;
            auto *n = cell(r.tracked ? tr("not within %1 s").arg(SosLog::kRelayWindowMs / 1000)
                                     : tr("loco %1 not in this loco's SoS table").arg(senderId),
                           r.tracked ? tr("Its ARPs were in the table, but none showed the new status in time")
                                     : tr("Out of range, rejected, or the table was full: no @sossrc for it then"));
            n->setForeground(UiColor::warning());
            m_table->setItem(i, ColHeard, n);
        }
        if (r.actedMs >= 0) {
            ++acted;
            m_table->setItem(i, ColActed, cell(hms(r.actedMs)));
            m_table->setItem(i, ColActedDelay, cell(delay(r.actedMs - r.sentMs), tr("After it was sent")));
            m_table->setItem(i, ColDecision, cell(r.acted));
        } else {
            m_table->setItem(i, ColActed, cell(tr("no decision about it"), tr("No threat started or ended for loco %1 within %2 s")
                                                                          .arg(senderId).arg(SosLog::kRelayWindowMs / 1000)));
            m_table->item(i, ColActed)->setForeground(UiColor::muted());
        }
    }

    auto count = [](int n, const QString &one, const QString &many) {
        return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
    };
    QStringList parts;
    parts << tr("Loco %1: %2").arg(senderId).arg(count(m_relays.size(), tr("status change"), tr("status changes")))
          << tr("%1 heard here").arg(heard)
          << tr("%1 acted on").arg(acted);
    if (untracked) parts << tr("%1 while it was not in the table").arg(untracked);
    if (early) parts << tr("%1 heard before sent: the clocks differ").arg(early);
    bool byThreat = false;
    for (const SosLog::Relay &r : m_relays) byThreat = byThreat || r.byThreat;
    if (byThreat) parts << tr("heard = the threat it makes here (the minimal layout logs no ARP status)");
    m_summary->setText(parts.join(QStringLiteral(" · ")) + QStringLiteral(". ")
                       + tr("Delays use the two locos' own clocks."));
}

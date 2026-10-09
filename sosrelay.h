#ifndef SOSRELAY_H
#define SOSRELAY_H

// =============================================================================
//  Two logs (session 187) — the SoS window's "Two logs" tab
//  -----------------------------------------------------------------------------
//  What one loco sent, and what this loco did with it. Pick the other loco's
//  log (a tab with @sos); every change of the emergency status it broadcast
//  (its own @sos own_emergency_status, which is what its ARP carries) is one
//  row, followed into this loco's log (SosLog::relay):
//
//    sent       when the other loco's log shows the change, and from / to
//    heard      the first @sossrc here whose ARP status shows the new value,
//               and how long after
//    acted on   the first decision here about that loco after it (a threat
//               starting or ending, the target removed, a brake not applied,
//               a timeout, an eviction), and how long after
//
//  The two logs' clocks are the two locos' RTCs: a delay is only as good as
//  they agree, and one below 0 is shown as such. Double-click a cell: that
//  moment in that log.
// =============================================================================

#include "soslog.h"

#include <QWidget>

class MessageDispatcher;
class QComboBox;
class QLabel;
class QTableWidget;

class SosRelayPanel : public QWidget
{
    Q_OBJECT
public:
    explicit SosRelayPanel(MessageDispatcher *dispatcher, QWidget *parent = nullptr);

    // This loco's log (the receiver) and its timeline, from the SoS window.
    void setReceiver(const QString &key, const SosLog::Timeline &timeline);
    // The other loco's log (the sender); "" picks the first other tab with @sos.
    void setSender(const QString &key);
    QString senderKey() const;

    QComboBox    *picker() const { return m_picker; }
    QTableWidget *table() const { return m_table; }
    QLabel       *summary() const { return m_summary; }
    const QVector<SosLog::Relay> &relays() const { return m_relays; }

public slots:
    void refreshPicker();
    void rebuild();

signals:
    void jumpRequested(const QString &key, qint64 ms);

private:
    MessageDispatcher *m_dispatcher = nullptr;
    QString m_receiver;
    SosLog::Timeline m_receiverTimeline;
    QVector<SosLog::Relay> m_relays;
    QComboBox    *m_picker = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel       *m_summary = nullptr;
};

#endif // SOSRELAY_H

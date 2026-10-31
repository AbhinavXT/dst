#ifndef BRAKECHAINWINDOW_H
#define BRAKECHAINWINDOW_H

// Brake chain (session 453): brakechain.h as a table, one row per brake
// KAVACH applied with the delay to each link, and a chart of the row picked:
// pressures, targets and speed against time, a mark at each link.

#include "brakechain.h"

#include <QWidget>

class LogModel;
class QLabel;
class QTableWidget;
class StatusLine;

class BrakeChainChart : public QWidget
{
    Q_OBJECT
public:
    explicit BrakeChainChart(QWidget *parent = nullptr);
    void setApplication(const BrakeChain::Chain *chain, int app);   // app -1: none
    QSize sizeHint() const override { return QSize(900, 300); }
    QSize minimumSizeHint() const override { return QSize(420, 200); }
    qint64 fromMs() const { return m_from; }
    qint64 toMs() const { return m_to; }
    int    xAtMs(qint64 ms) const;
    QString describeAt(const QPoint &pos) const;     // tooltip text (tests)

signals:
    void timeClicked(qint64 ms);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    bool event(QEvent *e) override;

private:
    QRect plotRect() const;
    qint64 msAtX(int x) const;
    const BrakeChain::Chain *m_chain = nullptr;
    int    m_app = -1;
    qint64 m_from = 0, m_to = 0;
    double m_speedMax = 10;
};

class BrakeChainWindow : public QWidget
{
    Q_OBJECT
public:
    BrakeChainWindow(LogModel *model, const QString &tabKey, const QString &tabName, QWidget *parent = nullptr);
    const BrakeChain::Chain &chain() const { return m_chain; }
    QTableWidget *table() const { return m_table; }
    BrakeChainChart *chart() const { return m_chart; }

public slots:
    void rebuild();

signals:
    void jumpRequested(const QString &tabKey, qint64 epochMs);

private:
    void fillTable();
    void showApplication(int app);

    LogModel *m_model = nullptr;
    QString   m_tabKey;
    BrakeChain::Chain m_chain;
    QTableWidget    *m_table = nullptr;
    BrakeChainChart *m_chart = nullptr;
    QLabel          *m_detail = nullptr;
    StatusLine      *m_status = nullptr;
};

#endif // BRAKECHAINWINDOW_H

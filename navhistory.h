#ifndef NAVHISTORY_H
#define NAVHISTORY_H

// =============================================================================
//  navhistory.h -- Edit ▸ Back / Forward (Alt+← / Alt+→), session 331
//  ---------------------------------------------------------------------------
//  Where the operator was before each jump: Go to timestamp, next problem,
//  next bookmark, a minimap or ribbon click, a jump from any tool window.
//  A place is a tab and a moment (the selected row's time), not a row
//  number: rows shift under a filter and as live traffic arrives, a time
//  does not. Like a browser: a new jump after going Back drops Forward.
// =============================================================================

#include <QString>
#include <QVector>

struct NavPoint {
    QString tabKey;
    qint64  epochMs = -1;
    bool isValid() const { return !tabKey.isEmpty() && epochMs >= 0; }
    bool operator==(const NavPoint &o) const { return tabKey == o.tabKey && epochMs == o.epochMs; }
};

class NavHistory
{
public:
    static constexpr int kMax = 100;

    // Before a jump: remember where it left from. An invalid place (no row
    // selected) or the place already on top is not added.
    void record(const NavPoint &from);
    // Where Back goes ({} when nothing), `here` kept for Forward.
    NavPoint back(const NavPoint &here);
    NavPoint forward(const NavPoint &here);

    bool canBack() const    { return !m_back.isEmpty(); }
    bool canForward() const { return !m_forward.isEmpty(); }

private:
    QVector<NavPoint> m_back, m_forward;
};

#endif  // NAVHISTORY_H

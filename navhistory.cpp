#include "navhistory.h"

namespace {
void push(QVector<NavPoint> &stack, const NavPoint &p)
{
    if (!p.isValid() || (!stack.isEmpty() && stack.last() == p)) return;
    stack.append(p);
    if (stack.size() > NavHistory::kMax) stack.removeFirst();
}
}  // namespace

void NavHistory::record(const NavPoint &from)
{
    if (!from.isValid()) return;
    push(m_back, from);
    m_forward.clear();
}

NavPoint NavHistory::back(const NavPoint &here)
{
    // Skip entries equal to where we stand: going "back" to here is a no-op
    // the operator would read as a dead key.
    while (!m_back.isEmpty() && m_back.last() == here) m_back.removeLast();
    if (m_back.isEmpty()) return {};
    push(m_forward, here);
    return m_back.takeLast();
}

NavPoint NavHistory::forward(const NavPoint &here)
{
    while (!m_forward.isEmpty() && m_forward.last() == here) m_forward.removeLast();
    if (m_forward.isEmpty()) return {};
    push(m_back, here);
    return m_forward.takeLast();
}

#include "testcaseboard.h"

#include "testassertions.h"

#include <QTimer>

#include <algorithm>

TestCaseBoard::TestCaseBoard(TestAssertionEngine *engine, QObject *parent)
    : QObject(parent), m_engine(engine)
{
    if (m_engine) {
        connect(m_engine, &TestAssertionEngine::assertionObserved, this,
                [this](const QString &id, const QString &) { record(id, CaseState::Observed); });
        connect(m_engine, &TestAssertionEngine::assertionViolated, this,
                [this](const QString &id, const QString &, const QString &) {
                    record(id, CaseState::NotAnswered);
                });
    }
    m_timer = new QTimer(this);
    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, &TestCaseBoard::poll);
    m_timer->start();
}

CaseState TestCaseBoard::stateOf(const TestAssertion &a)
{
    // The report's order: an unanswered precondition outranks any number of
    // answered ones (testassertions.cpp, buildReportHtml).
    if (!a.queryOk)         return CaseState::QueryError;
    if (a.hasViolation())   return CaseState::NotAnswered;
    if (a.observed())       return CaseState::Observed;
    return CaseState::NotObserved;
}

QString TestCaseBoard::stateText(CaseState s)
{
    switch (s) {
    case CaseState::Observed:    return QStringLiteral("OBSERVED");
    case CaseState::NotAnswered: return QStringLiteral("NOT ANSWERED");
    case CaseState::QueryError:  return QStringLiteral("query error");
    case CaseState::NotObserved: break;
    }
    return QStringLiteral("not observed");
}

void TestCaseBoard::setSection(const QString &section)
{
    if (section == m_section) return;
    m_section = section;
    emit changed();
}

QStringList TestCaseBoard::sections() const
{
    QStringList out;
    if (!m_engine) return out;
    for (const TestAssertion &a : m_engine->assertions()) {
        const QString s = TestAssertionEngine::sectionOf(a.id);
        if (!s.isEmpty() && !out.contains(s)) out << s;
    }
    // Numbered sections in numeric order, then the named ones (SEC).
    std::sort(out.begin(), out.end(), [](const QString &x, const QString &y) {
        bool xn = false, yn = false;
        const int xi = x.toInt(&xn), yi = y.toInt(&yn);
        if (xn != yn) return xn;
        return xn ? xi < yi : x < y;
    });
    return out;
}

bool TestCaseBoard::inSection(const TestAssertion &a) const
{
    return m_section.isEmpty() || TestAssertionEngine::sectionOf(a.id) == m_section;
}

QString TestCaseBoard::sectionLabel() const
{
    if (m_section.isEmpty()) return tr("All test cases");
    bool numbered = false;
    m_section.toInt(&numbered);
    return numbered ? tr("SIF §%1").arg(m_section) : m_section;
}

CaseCounts TestCaseBoard::counts() const
{
    CaseCounts c;
    if (!m_engine) return c;
    for (const TestAssertion &a : m_engine->assertions()) {
        if (!inSection(a)) continue;
        switch (stateOf(a)) {
        case CaseState::Observed:    ++c.observed; break;
        case CaseState::NotAnswered: ++c.notAnswered; break;
        case CaseState::NotObserved: ++c.notObserved; break;
        case CaseState::QueryError:  ++c.queryError; break;
        }
    }
    return c;
}

QString TestCaseBoard::summary() const
{
    const CaseCounts c = counts();
    QString s = tr("%n case(s): ", nullptr, c.total())
              + tr("%1 OBSERVED · %2 NOT ANSWERED · %3 not observed")
                    .arg(c.observed).arg(c.notAnswered).arg(c.notObserved);
    if (c.queryError > 0) s += tr(" · %1 query error(s)").arg(c.queryError);
    return s;
}

const TestAssertion *TestCaseBoard::find(const QString &id) const
{
    if (!m_engine) return nullptr;
    for (const TestAssertion &a : m_engine->assertions())
        if (a.id == id) return &a;
    return nullptr;
}

void TestCaseBoard::record(const QString &id, CaseState state)
{
    const TestAssertion *a = find(id);
    if (!a) return;
    Event e;
    e.id = id;
    e.state = state;
    if (state == CaseState::NotAnswered) { e.ms = a->lastViolMs; e.tabKey = a->lastViolKey; }
    else                                 { e.ms = a->firstMs;    e.tabKey = a->firstKey; }
    m_events.append(e);
    if (m_events.size() > kMaxEvents) m_events.remove(0, m_events.size() - kMaxEvents);
}

TestCaseBoard::Event TestCaseBoard::lastEvent() const
{
    for (int i = m_events.size() - 1; i >= 0; --i) {
        const Event &e = m_events.at(i);
        const TestAssertion *a = find(e.id);
        // Still true? A reset or a reload makes an old event a lie.
        if (!a || !inSection(*a) || stateOf(*a) != e.state) continue;
        return e;
    }
    return Event();
}

void TestCaseBoard::poll()
{
    if (!m_engine || m_engine->generation() == m_seen) return;
    m_seen = m_engine->generation();
    emit changed();
}

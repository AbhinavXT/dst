#ifndef TESTCASEBOARD_H
#define TESTCASEBOARD_H

// =============================================================================
//  TestCaseBoard (session 306)
//  -----------------------------------------------------------------------------
//  What the loaded test cases have seen so far, for the views that show it
//  live: the Test cases dock (TestCasePanel) and the one-line strip under
//  the DMI window and the Live Loco Console (TestCaseStrip).
//
//  Owned by MainWindow and handed out, like the other session-state objects.
//  It reads MainWindow's TestAssertionEngine; it never feeds it. The engine
//  already watches every entry of every source (live or replayed into the
//  tabs); this only polls its generation counter twice a second and says
//  "changed" when something moved, so a 2000-frame/s burst redraws twice,
//  not 2000 times.
//
//  The section filter lives here, not in a view: the dock picks "§1" and
//  every strip then counts §1 too, so the DMI and the table agree.
//
//  WORDS
//    OBSERVED / NOT ANSWERED / not observed, as the report says them
//    (testassertions.h): never Pass/Fail. A case whose query did not parse
//    is "query error": it was never run.
// =============================================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QTimer;
class TestAssertionEngine;
struct TestAssertion;

enum class CaseState { Observed, NotAnswered, NotObserved, QueryError };

struct CaseCounts {
    int observed = 0;
    int notAnswered = 0;
    int notObserved = 0;
    int queryError = 0;
    int total() const { return observed + notAnswered + notObserved + queryError; }
};

class TestCaseBoard : public QObject
{
    Q_OBJECT
public:
    explicit TestCaseBoard(TestAssertionEngine *engine, QObject *parent = nullptr);

    const TestAssertionEngine *engine() const { return m_engine; }

    static CaseState stateOf(const TestAssertion &a);
    static QString   stateText(CaseState s);

    // "" = every loaded case; otherwise a section ("1", "32", "SEC").
    QString section() const { return m_section; }
    void    setSection(const QString &section);
    // The sections the loaded cases fall in, in numeric order.
    QStringList sections() const;
    bool inSection(const TestAssertion &a) const;
    // "SIF §1", "SEC" or "All test cases".
    QString sectionLabel() const;

    CaseCounts counts() const;      // in the current section
    // "76 cases: 7 OBSERVED · 2 NOT ANSWERED · 67 not observed", plain text.
    QString summary() const;

    // The latest case to become OBSERVED or NOT ANSWERED in the current
    // section, while it still is (a reset drops it). `ms` is message time:
    // when it was first seen, or when the unanswered precondition was.
    struct Event {
        QString   id;
        CaseState state = CaseState::NotObserved;
        qint64    ms = 0;
        QString   tabKey;
        bool valid() const { return !id.isEmpty(); }
    };
    Event lastEvent() const;

    // Emit changed() if the engine moved since the last call. The timer
    // calls it; tests call it directly.
    void poll();

public slots:
    void requestOpen() { emit openRequested(); }

signals:
    void changed();
    // A strip's "Results…" was pressed: MainWindow shows the dock.
    void openRequested();

private:
    void record(const QString &id, CaseState state);
    const TestAssertion *find(const QString &id) const;

    TestAssertionEngine *m_engine = nullptr;
    QTimer  *m_timer = nullptr;
    quint64  m_seen = 0;
    QString  m_section;
    QVector<Event> m_events;        // newest last, at most kMaxEvents
    static const int kMaxEvents = 64;
};

#endif // TESTCASEBOARD_H

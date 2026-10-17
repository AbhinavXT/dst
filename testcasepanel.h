#ifndef TESTCASEPANEL_H
#define TESTCASEPANEL_H

// =============================================================================
//  TestCasePanel / TestCaseStrip (session 306)
//  -----------------------------------------------------------------------------
//  The loaded test cases' results, live, while a run or a replay goes on.
//  Until now they reached the operator only as notifications and, after
//  the run, as the saved observation report.
//
//  TestCasePanel — the "Test cases" dock (Tools ▸ Test cases ▸ Show
//    results, or View ▸ Panels): a section picker, the counts, and one row
//    per case: Case | Clause | State | At | Source | Evidence. "At" is when
//    it was first seen, or, NOT ANSWERED, when the precondition that went
//    unanswered was. Double-click a row with a source: the log jumps there.
//    The row's tooltip carries the condition, the counts and the evidence
//    text the report would print.
//
//  TestCaseStrip — two lines under the DMI window and the Live Loco
//    Console: the section's counts and the latest case to change, with a
//    "Results…" button that brings up the dock. Hidden while no test case
//    is loaded, so a console without them looks as it did.
//
//  Both read a TestCaseBoard; neither touches the engine.
// =============================================================================

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class TestCaseBoard;

class TestCasePanel : public QWidget
{
    Q_OBJECT
public:
    explicit TestCasePanel(TestCaseBoard *board, QWidget *parent = nullptr);

    QTableWidget *table() const { return m_table; }      // for tests
    QComboBox    *sectionBox() const { return m_section; }
    QString       summaryText() const;

    enum Column { ColCase, ColClause, ColState, ColAt, ColSource, ColEvidence, ColCount };

signals:
    void jumpRequested(const QString &tabKey, qint64 epochMs);

public slots:
    void refresh();

private:
    void fillSections();
    void activateRow(int row);

    TestCaseBoard *m_board = nullptr;
    QComboBox     *m_section = nullptr;
    QLabel        *m_summary = nullptr;
    QTableWidget  *m_table = nullptr;
    bool           m_filling = false;
};

class TestCaseStrip : public QWidget
{
    Q_OBJECT
public:
    explicit TestCaseStrip(TestCaseBoard *board, QWidget *parent = nullptr);

    QString countsText() const;      // for tests
    QString lastText() const;

public slots:
    void refresh();

private:
    TestCaseBoard *m_board = nullptr;
    QLabel        *m_counts = nullptr;
    QLabel        *m_last = nullptr;
    QPushButton   *m_open = nullptr;
};

#endif // TESTCASEPANEL_H

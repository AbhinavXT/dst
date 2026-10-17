#include "testcasepanel.h"

#include "settings.h"
#include "testassertions.h"
#include "testcaseboard.h"
#include "uicolors.h"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString stamp(qint64 ms, bool utc)
{
    if (ms <= 0) return QString();
    return QDateTime::fromMSecsSinceEpoch(ms, utc ? Qt::UTC : Qt::LocalTime)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QColor stateColor(CaseState s)
{
    switch (s) {
    case CaseState::Observed:    return UiColor::ok();
    case CaseState::NotAnswered: return UiColor::error();
    case CaseState::QueryError:  return UiColor::warning();
    case CaseState::NotObserved: break;
    }
    return UiColor::muted();
}

// Where the row's evidence is: the unanswered precondition for a finding,
// else the frame that first satisfied the case.
qint64 evidenceMs(const TestAssertion &a)
{
    return a.hasViolation() ? a.worstMs : a.firstMs;
}
QString evidenceKey(const TestAssertion &a)
{
    return a.hasViolation() ? a.worstKey : a.firstKey;
}

} // namespace

// -----------------------------------------------------------------------------
//  TestCasePanel
// -----------------------------------------------------------------------------

TestCasePanel::TestCasePanel(TestCaseBoard *board, QWidget *parent)
    : QWidget(parent), m_board(board)
{
    m_section = new QComboBox(this);
    m_section->setObjectName(QStringLiteral("testCaseSection"));
    m_section->setToolTip(tr("Which cases to list and count. The DMI window's and the Live Loco "
                             "Console's strips count the same section."));
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("testCaseSummary"));
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Section:"), this));
    top->addWidget(m_section);
    top->addWidget(m_summary, 1);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setObjectName(QStringLiteral("testCaseTable"));
    m_table->setHorizontalHeaderLabels({ tr("Case"), tr("Clause"), tr("State"), tr("At"),
                                         tr("Source"), tr("Evidence") });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setWordWrap(false);
    m_table->setColumnWidth(ColCase, 90);
    m_table->setColumnWidth(ColClause, 260);
    m_table->setColumnWidth(ColState, 130);
    m_table->setColumnWidth(ColAt, 165);
    m_table->setColumnWidth(ColSource, 70);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->addLayout(top);
    root->addWidget(m_table, 1);

    connect(m_section, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_filling || !m_board) return;
        m_board->setSection(m_section->currentData().toString());
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) { activateRow(row); });
    if (m_board) connect(m_board, &TestCaseBoard::changed, this, &TestCasePanel::refresh);
    refresh();
}

QString TestCasePanel::summaryText() const { return m_summary->text(); }

void TestCasePanel::fillSections()
{
    // Rebuilt only when the list differs: a rebuild on every refresh would
    // close the combo under the operator's mouse.
    QStringList want{ QString() };
    if (m_board) want << m_board->sections();
    QStringList have;
    for (int i = 0; i < m_section->count(); ++i) have << m_section->itemData(i).toString();
    if (have == want && m_board && m_section->currentData().toString() == m_board->section()) return;

    m_filling = true;
    m_section->clear();
    m_section->addItem(tr("All"), QString());
    if (m_board)
        for (const QString &s : m_board->sections()) {
            bool numbered = false;
            s.toInt(&numbered);
            m_section->addItem(numbered ? tr("§%1").arg(s) : s, s);
        }
    const int at = m_board ? m_section->findData(m_board->section()) : 0;
    m_section->setCurrentIndex(at < 0 ? 0 : at);
    m_filling = false;
    // The section chosen may have gone with a reload: back to All.
    if (m_board && at < 0) m_board->setSection(QString());
}

void TestCasePanel::refresh()
{
    fillSections();
    const TestAssertionEngine *engine = m_board ? m_board->engine() : nullptr;
    if (!engine || engine->assertions().isEmpty()) {
        m_summary->setText(tr("No test cases loaded: Tools ▸ Test cases ▸ Load built-in test cases"));
        m_table->setRowCount(0);
        return;
    }
    m_summary->setText(m_board->summary());

    const QString keep = m_table->currentRow() >= 0 && m_table->item(m_table->currentRow(), ColCase)
                             ? m_table->item(m_table->currentRow(), ColCase)->text()
                             : QString();
    const bool utc = Settings::showUtc();

    QVector<const TestAssertion *> rows;
    for (const TestAssertion &a : engine->assertions())
        if (m_board->inSection(a)) rows << &a;

    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(rows.size());
    int keepRow = -1;
    for (int r = 0; r < rows.size(); ++r) {
        const TestAssertion &a = *rows.at(r);
        const CaseState st = TestCaseBoard::stateOf(a);
        auto set = [&](int col, const QString &text) {
            QTableWidgetItem *it = m_table->item(r, col);
            if (!it) { it = new QTableWidgetItem; m_table->setItem(r, col, it); }
            it->setText(text);
            return it;
        };
        set(ColCase, a.id);
        set(ColClause, a.title)->setToolTip(a.title);
        QTableWidgetItem *state = set(ColState, TestCaseBoard::stateText(st));
        state->setForeground(stateColor(st));
        QFont f = state->font();
        f.setBold(st == CaseState::NotAnswered || st == CaseState::Observed);
        state->setFont(f);
        set(ColAt, stamp(evidenceMs(a), utc));
        set(ColSource, evidenceKey(a));
        const QString evidence = a.hasViolation() ? a.violationText : a.firstText;
        set(ColEvidence, evidence);

        QString tip = QStringLiteral("<b>%1</b> — %2<br>").arg(a.id.toHtmlEscaped(), a.title.toHtmlEscaped());
        tip += QStringLiteral("<code>%1</code>").arg(a.queryText.toHtmlEscaped());
        if (!a.queryOk) tip += tr("<br>Query error: %1").arg(a.queryError.toHtmlEscaped());
        if (a.kind == AssertKind::Simple) tip += tr("<br>%n hit(s)", nullptr, a.hits);
        else tip += tr("<br>%1 completed, %2 not answered").arg(a.satisfied).arg(a.violated);
        if (a.slowestMs > 0) tip += tr(" (slowest %1 ms)").arg(a.slowestMs);
        if (!a.firstText.isEmpty())
            tip += tr("<br>First seen %1: <code>%2</code>").arg(stamp(a.firstMs, utc), a.firstText.toHtmlEscaped());
        if (!a.violationText.isEmpty())
            tip += tr("<br>Unanswered from %1: <code>%2</code>").arg(stamp(a.worstMs, utc),
                                                                    a.violationText.toHtmlEscaped());
        if (!evidenceKey(a).isEmpty()) tip += tr("<br>Double-click: show it in the log.");
        for (int c = 0; c < ColCount; ++c)
            if (c != ColClause) m_table->item(r, c)->setToolTip(tip);
        if (a.id == keep) keepRow = r;
    }
    if (keepRow >= 0) m_table->setCurrentCell(keepRow, m_table->currentColumn() < 0 ? 0 : m_table->currentColumn());
    m_table->setUpdatesEnabled(true);
}

void TestCasePanel::activateRow(int row)
{
    const TestAssertionEngine *engine = m_board ? m_board->engine() : nullptr;
    QTableWidgetItem *id = m_table->item(row, ColCase);
    if (!engine || !id) return;
    for (const TestAssertion &a : engine->assertions()) {
        if (a.id != id->text()) continue;
        if (!evidenceKey(a).isEmpty() && evidenceMs(a) > 0) emit jumpRequested(evidenceKey(a), evidenceMs(a));
        return;
    }
}

// -----------------------------------------------------------------------------
//  TestCaseStrip
// -----------------------------------------------------------------------------

TestCaseStrip::TestCaseStrip(TestCaseBoard *board, QWidget *parent)
    : QWidget(parent), m_board(board)
{
    setObjectName(QStringLiteral("testCaseStrip"));
    m_counts = new QLabel(this);
    m_counts->setObjectName(QStringLiteral("testCaseStripCounts"));
    m_last = new QLabel(this);
    m_last->setObjectName(QStringLiteral("testCaseStripLast"));
    m_open = new QPushButton(tr("Results…"), this);
    m_open->setObjectName(QStringLiteral("testCaseStripOpen"));
    m_open->setToolTip(tr("The Test cases panel: every case, its state and its evidence"));

    auto *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(0);
    text->addWidget(m_counts);
    text->addWidget(m_last);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(4, 2, 4, 2);
    row->addLayout(text, 1);
    row->addWidget(m_open);

    if (m_board) {
        connect(m_open, &QPushButton::clicked, m_board, &TestCaseBoard::requestOpen);
        connect(m_board, &TestCaseBoard::changed, this, &TestCaseStrip::refresh);
    }
    refresh();
}

QString TestCaseStrip::countsText() const { return m_counts->text(); }
QString TestCaseStrip::lastText() const { return m_last->text(); }

void TestCaseStrip::refresh()
{
    const TestAssertionEngine *engine = m_board ? m_board->engine() : nullptr;
    if (!engine || engine->assertions().isEmpty()) {
        m_counts->clear();
        m_last->clear();
        setVisible(false);
        return;
    }
    // Plain text, the verdict words themselves carrying the state, and the
    // finding count in the error colour only when there is one.
    const CaseCounts c = m_board->counts();
    m_counts->setText(m_board->sectionLabel() + QStringLiteral(": ") + m_board->summary());
    QPalette pal = m_counts->palette();
    pal.setColor(QPalette::WindowText, c.notAnswered > 0 ? UiColor::error() : palette().color(QPalette::WindowText));
    m_counts->setPalette(pal);

    const TestCaseBoard::Event e = m_board->lastEvent();
    if (e.valid()) {
        m_last->setText(tr("Last: %1 %2 at %3%4")
                            .arg(e.id, TestCaseBoard::stateText(e.state),
                                 stamp(e.ms, Settings::showUtc()),
                                 e.tabKey.isEmpty() ? QString() : tr(" (%1)").arg(e.tabKey)));
    } else {
        m_last->setText(tr("Last: nothing observed yet"));
    }
    if (parentWidget()) setVisible(true);   // never as a window of its own
}

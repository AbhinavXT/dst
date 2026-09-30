#include "tablefindbar.h"

#include "uicolors.h"

#include <QApplication>
#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QTableWidget>
#include <QToolButton>

namespace {

// The tint every match gets: the log find's recipe (LogModel, find hits),
// so a match looks the same wherever DLConsole shows one.
QBrush matchBrush()
{
    QColor colour = UiColor::accent();
    const bool dark = qApp->palette().color(QPalette::Base).lightness() < 128;
    if (dark) {
        colour.setAlpha(70);
    } else {
        colour.setAlpha(48);
    }
    return QBrush(colour);
}

QString fieldAt(const QTableWidget *table, int row)
{
    const QTableWidgetItem *item = table->item(row, 0);
    if (item == nullptr) {
        return QString();
    }
    return item->text().trimmed();
}

}  // namespace

TableFindBar::TableFindBar(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *label = new QLabel(tr("Find field:"), this);
    layout->addWidget(label);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("e.g. wheel dia, speed margin, apn"));
    m_input->setClearButtonEnabled(true);
    m_input->setMinimumWidth(240);
    m_input->setToolTip(tr("Case-insensitive; _ and space are the same; every word must appear.\n"
                           "Enter: next · Shift+Enter: previous · Esc: close"));
    m_input->installEventFilter(this);
    layout->addWidget(m_input, 1);

    m_previous = new QToolButton(this);
    m_previous->setText(QStringLiteral("▲"));
    m_previous->setToolTip(tr("Previous match (Shift+F3)"));
    layout->addWidget(m_previous);
    m_next = new QToolButton(this);
    m_next->setText(QStringLiteral("▼"));
    m_next->setToolTip(tr("Next match (F3)"));
    layout->addWidget(m_next);

    m_status = new QLabel(this);
    m_status->setMinimumWidth(150);
    layout->addWidget(m_status);

    m_valuesToo = new QCheckBox(tr("Values too"), this);
    m_valuesToo->setToolTip(tr("Also match the Value column (e.g. find which field is 0x7F000001)"));
    layout->addWidget(m_valuesToo);

    m_onlyMatching = new QCheckBox(tr("Only matching rows"), this);
    m_onlyMatching->setToolTip(tr("Hide every row that does not match"));
    layout->addWidget(m_onlyMatching);

    auto *closeButton = new QToolButton(this);
    closeButton->setText(QStringLiteral("✕"));
    closeButton->setToolTip(tr("Close find (Esc)"));
    layout->addWidget(closeButton);

    connect(m_input, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_terms = termsFor(text);
        m_currentField.clear();
        m_cursor = -1;
        rescan();
        // Typing moves to the first match, like a browser's find.
        if (!m_matchRows.isEmpty()) {
            jumpTo(0, true);
        }
    });
    connect(m_next, &QToolButton::clicked, this, &TableFindBar::findNext);
    connect(m_previous, &QToolButton::clicked, this, &TableFindBar::findPrevious);
    connect(m_valuesToo, &QCheckBox::toggled, this, [this]() {
        rescan();
        refind(true);   // the current match may no longer be one
    });
    connect(m_onlyMatching, &QCheckBox::toggled, this, [this]() { restyle(); });
    connect(closeButton, &QToolButton::clicked, this, &TableFindBar::close);

    hide();
}

// -----------------------------------------------------------------------------
//  Matching
// -----------------------------------------------------------------------------

QStringList TableFindBar::termsFor(const QString &query)
{
    QString normalised = query.toLower();
    normalised.replace(QLatin1Char('_'), QLatin1Char(' '));
    return normalised.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

bool TableFindBar::matches(const QString &text, const QStringList &terms)
{
    if (terms.isEmpty()) {
        return false;
    }
    QString haystack = text.toLower();
    haystack.replace(QLatin1Char('_'), QLatin1Char(' '));
    for (const QString &term : terms) {
        if (!haystack.contains(term)) {
            return false;
        }
    }
    return true;
}

bool TableFindBar::rowMatches(QTableWidget *table, int row) const
{
    if (matches(fieldAt(table, row), m_terms)) {
        return true;
    }
    if (m_valuesToo->isChecked()) {
        const QTableWidgetItem *value = table->item(row, 1);
        if (value != nullptr && matches(value->text(), m_terms)) {
            return true;
        }
    }
    return false;
}

int TableFindBar::countIn(QTableWidget *table) const
{
    if (table == nullptr) {
        return 0;
    }
    int count = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (rowMatches(table, row)) {
            ++count;
        }
    }
    return count;
}

// -----------------------------------------------------------------------------
//  Tables
// -----------------------------------------------------------------------------

void TableFindBar::setTable(QTableWidget *table)
{
    if (m_table == table) {
        return;
    }
    // Leave the previous table as it was found: no tint, nothing hidden.
    if (m_table) {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            m_table->setRowHidden(row, false);
            for (int column = 0; column < m_table->columnCount(); ++column) {
                if (QTableWidgetItem *item = m_table->item(row, column)) {
                    item->setBackground(QBrush());
                }
            }
        }
    }
    m_table = table;
    m_cursor = -1;
    m_currentField.clear();
    rescan();
    if (isActive() && !m_matchRows.isEmpty()) {
        jumpTo(0, true);
    }
}

void TableFindBar::setAllTables(const QVector<QPair<QString, QTableWidget *>> &tables)
{
    m_allTables.clear();
    for (const auto &entry : tables) {
        m_allTables.append(qMakePair(entry.first, QPointer<QTableWidget>(entry.second)));
    }
}

void TableFindBar::reapply()
{
    if (!isActive()) {
        return;
    }
    // Rows were rebuilt: find everything again, then put the cursor back on
    // the SAME FIELD, wherever it is now. No scrolling (see the header).
    rescan();
    refind(false);
}

int TableFindBar::occurrenceOf(int row) const
{
    const QString name = fieldAt(m_table, row);
    int occurrence = 0;
    for (int above = 0; above < row; ++above) {
        if (fieldAt(m_table, above) == name) {
            ++occurrence;
        }
    }
    return occurrence;
}

void TableFindBar::refind(bool jumpIfLost)
{
    m_cursor = -1;
    if (m_table && !m_currentField.isEmpty()) {
        for (int index = 0; index < m_matchRows.size(); ++index) {
            const int row = m_matchRows.at(index);
            if (fieldAt(m_table, row) == m_currentField && occurrenceOf(row) == m_currentOccurrence) {
                jumpTo(index, false);
                break;
            }
        }
    }
    if (m_cursor < 0 && jumpIfLost && !m_matchRows.isEmpty()) {
        jumpTo(0, true);
    }
    updateStatus();
}

void TableFindBar::rescan()
{
    m_matchRows.clear();
    if (m_table && !m_terms.isEmpty() && m_open) {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (rowMatches(m_table, row)) {
                m_matchRows.append(row);
            }
        }
    }
    restyle();
    updateStatus();
}

void TableFindBar::restyle()
{
    if (!m_table) {
        return;
    }
    const bool searching = isActive();
    const bool hideOthers = searching && m_onlyMatching->isChecked();
    const QBrush tint = matchBrush();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const bool isMatch = searching && m_matchRows.contains(row);
        for (int column = 0; column < m_table->columnCount(); ++column) {
            if (QTableWidgetItem *item = m_table->item(row, column)) {
                if (isMatch) {
                    item->setBackground(tint);
                } else {
                    item->setBackground(QBrush());
                }
            }
        }
        m_table->setRowHidden(row, hideOthers && !isMatch);
    }
}

void TableFindBar::jumpTo(int matchIndex, bool scroll)
{
    if (!m_table || matchIndex < 0 || matchIndex >= m_matchRows.size()) {
        return;
    }
    m_cursor = matchIndex;
    const int row = m_matchRows.at(matchIndex);
    m_currentField = fieldAt(m_table, row);
    m_currentOccurrence = occurrenceOf(row);
    const QModelIndex index = m_table->model()->index(row, 0);
    if (scroll) {
        // Navigation: make it current (which scrolls) and centre it.
        m_table->setCurrentIndex(index);
        m_table->scrollTo(index, QAbstractItemView::PositionAtCenter);
    }
    // Refresh: select only. Changing the current index would scroll.
    m_table->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    updateStatus();
}

int TableFindBar::currentRow() const
{
    if (m_cursor < 0 || m_cursor >= m_matchRows.size()) {
        return -1;
    }
    return m_matchRows.at(m_cursor);
}

// -----------------------------------------------------------------------------
//  Navigation
// -----------------------------------------------------------------------------

void TableFindBar::findNext()
{
    if (m_matchRows.isEmpty()) {
        // Nothing here: go to the first other tab that has a match.
        for (const auto &entry : m_allTables) {
            if (entry.second && entry.second != m_table && countIn(entry.second) > 0) {
                emit switchToTable(entry.second);
                return;
            }
        }
        return;
    }
    int next = m_cursor + 1;
    if (next >= m_matchRows.size()) {
        next = 0;   // wrap
    }
    jumpTo(next, true);
}

void TableFindBar::findPrevious()
{
    if (m_matchRows.isEmpty()) {
        return;
    }
    int previous = m_cursor - 1;
    if (previous < 0) {
        previous = m_matchRows.size() - 1;
    }
    jumpTo(previous, true);
}

// -----------------------------------------------------------------------------
//  Showing, status
// -----------------------------------------------------------------------------

void TableFindBar::open()
{
    m_open = true;
    show();
    m_input->setFocus();
    m_input->selectAll();
    rescan();
}

void TableFindBar::close()
{
    m_open = false;
    hide();
    restyle();          // isActive() is false now: clears tints, unhides rows
    if (m_table) {
        m_table->setFocus();
    }
}

bool TableFindBar::isActive() const
{
    return m_open && !m_terms.isEmpty();
}

void TableFindBar::setQuery(const QString &text)
{
    m_input->setText(text);
}

void TableFindBar::setValuesToo(bool on)
{
    m_valuesToo->setChecked(on);
}

void TableFindBar::setOnlyMatching(bool on)
{
    m_onlyMatching->setChecked(on);
}

QString TableFindBar::statusText() const
{
    return m_status->text();
}

void TableFindBar::updateStatus()
{
    m_status->setStyleSheet(QString());
    if (m_terms.isEmpty()) {
        m_status->clear();
        return;
    }
    if (!m_matchRows.isEmpty()) {
        if (m_cursor >= 0) {
            m_status->setText(tr("%1 of %2").arg(m_cursor + 1).arg(m_matchRows.size()));
        } else {
            m_status->setText(tr("%n match(es)", "", m_matchRows.size()));
        }
        return;
    }
    // Nothing on this tab: say where else it is.
    QStringList elsewhere;
    for (const auto &entry : m_allTables) {
        if (entry.second && entry.second != m_table) {
            const int count = countIn(entry.second);
            if (count > 0) {
                elsewhere.append(QStringLiteral("%1 %2").arg(entry.first).arg(count));
            }
        }
    }
    if (elsewhere.isEmpty()) {
        m_status->setText(tr("no match"));
        m_status->setStyleSheet(UiColor::warningStyle());
    } else {
        m_status->setText(tr("not here — %1 (Enter)").arg(elsewhere.join(QStringLiteral(", "))));
        m_status->setStyleSheet(UiColor::style(UiColor::accent()));
    }
}

bool TableFindBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (key->modifiers() & Qt::ShiftModifier) {
                findPrevious();
            } else {
                findNext();
            }
            return true;
        }
        if (key->key() == Qt::Key_Escape) {
            close();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

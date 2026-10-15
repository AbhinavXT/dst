#include "shortcutsdialog.h"

#include "uicolors.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
void harvestMenu(QMenu *menu, const QString &prefix, QVector<ShortcutRow> &out, QSet<QAction *> &seen)
{
    emit menu->aboutToShow();   // Panels, Open Recent, … build their entries on show
    for (QAction *a : menu->actions()) {
        if (a->isSeparator()) continue;
        const QString label = a->text().remove(QLatin1Char('&'));
        if (label.isEmpty()) continue;
        const QString path = prefix + QStringLiteral(" › ") + label;
        if (a->menu()) {
            harvestMenu(a->menu(), path, out, seen);
            continue;
        }
        seen.insert(a);
        if (a->shortcut().isEmpty()) continue;
        out.append({ a->shortcut().toString(QKeySequence::NativeText), a->shortcut().toString(QKeySequence::PortableText),
                     label, path, a->isEnabled() });
    }
}
}  // namespace

QVector<ShortcutRow> harvestShortcuts(QMenuBar *bar, QWidget *window)
{
    QVector<ShortcutRow> out;
    QSet<QAction *> seen;
    if (bar) {
        for (QAction *top : bar->actions())
            if (top->menu()) harvestMenu(top->menu(), top->text().remove(QLatin1Char('&')), out, seen);
    }
    // Shortcuts on the window that no menu shows (added with addAction so
    // they work with the menu bar hidden, e.g. in presentation mode).
    if (window) {
        for (QAction *a : window->actions()) {
            if (seen.contains(a) || a->shortcut().isEmpty() || a->menu()) continue;
            const QString label = a->text().remove(QLatin1Char('&'));
            out.append({ a->shortcut().toString(QKeySequence::NativeText), a->shortcut().toString(QKeySequence::PortableText),
                         label.isEmpty() ? a->objectName() : label, QStringLiteral("(window)"), a->isEnabled() });
        }
    }
    return out;
}

ShortcutsDialog::ShortcutsDialog(const QVector<ShortcutRow> &rows, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Keyboard Shortcuts"));
    resize(720, 560);
    auto *v = new QVBoxLayout(this);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("shortcutSearch"));
    m_search->setPlaceholderText(tr("Search keys, commands or menus (e.g. \"Ctrl+Shift\", \"bookmark\")"));
    m_search->setClearButtonEnabled(true);
    v->addWidget(m_search);

    m_table = new QTableWidget(rows.size(), 3, this);
    m_table->setHorizontalHeaderLabels({ tr("Keys"), tr("Command"), tr("Menu") });
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setStretchLastSection(true);
    for (int r = 0; r < rows.size(); ++r) {
        const ShortcutRow &s = rows.at(r);
        auto *keys = new QTableWidgetItem(s.keys);
        QFont f = keys->font();
        f.setBold(true);
        keys->setFont(f);
        keys->setData(Qt::UserRole, s.portable);
        m_table->setItem(r, 0, keys);
        m_table->setItem(r, 1, new QTableWidgetItem(s.command));
        m_table->setItem(r, 2, new QTableWidgetItem(s.where));
        if (!s.enabled) {
            // Greyed, not hidden: the key is still what it will do.
            for (int c = 0; c < 3; ++c) {
                m_table->item(r, c)->setForeground(UiColor::muted());
                m_table->item(r, c)->setToolTip(tr("Not available right now"));
            }
        }
    }
    m_table->resizeColumnsToContents();
    v->addWidget(m_table, 1);

    auto *note = new QLabel(tr("%n shortcut(s) of this window. Other windows (Tag Builder, Loco Console, …) "
                               "have their own; Help › Run command… finds any command by name.",
                               "", rows.size()),
                            this);
    note->setWordWrap(true);
    note->setStyleSheet(UiColor::mutedStyle());
    v->addWidget(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, this, &ShortcutsDialog::setFilter);
    m_search->setFocus();
}

void ShortcutsDialog::setFilter(const QString &text)
{
    if (m_search->text() != text) m_search->setText(text);   // re-enters once via textChanged
    const QString needle = text.trimmed();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        bool match = needle.isEmpty()
                     || m_table->item(r, 0)->data(Qt::UserRole).toString().contains(needle, Qt::CaseInsensitive);
        for (int c = 0; c < 3 && !match; ++c)
            match = m_table->item(r, c)->text().contains(needle, Qt::CaseInsensitive);
        m_table->setRowHidden(r, !match);
    }
}

int ShortcutsDialog::visibleRows() const
{
    int n = 0;
    for (int r = 0; r < m_table->rowCount(); ++r) n += m_table->isRowHidden(r) ? 0 : 1;
    return n;
}

#include "commandpalette.h"

#include <QAction>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QVBoxLayout>

namespace {

void harvestMenu(QMenu *menu, const QString &prefix, QVector<Command> &out)
{
    if (!menu) return;
    for (QAction *a : menu->actions()) {
        if (a->isSeparator()) continue;

        const QString label = a->text().remove(QLatin1Char('&'));
        if (label.isEmpty()) continue;

        if (a->menu()) {
            // Submenu: recurse. The submenu's own action is not offered —
            // "Row density" alone does nothing; its children do.
            //
            // aboutToShow() first: Panels builds its contents on that signal
            // (the docks do not exist when the menu bar is constructed), so
            // without this the palette lists an empty submenu and the panel
            // toggles are unsearchable. Now that Tools groups its entries
            // into submenus, the palette is the flat way to reach them, and
            // it has to see all of them.
            emit a->menu()->aboutToShow();
            harvestMenu(a->menu(),
                        prefix.isEmpty() ? label
                                         : prefix + QStringLiteral(" › ") + label,
                        out);
            continue;
        }

        Command c;
        c.action   = a;
        c.path     = prefix.isEmpty() ? label
                                      : prefix + QStringLiteral(" › ") + label;
        c.shortcut = a->shortcut().toString(QKeySequence::NativeText);
        out.append(c);
    }
}

}  // namespace

QVector<Command> harvestCommands(QMenuBar *bar)
{
    QVector<Command> out;
    if (!bar) return out;
    for (QAction *top : bar->actions()) {
        if (!top->menu()) continue;
        harvestMenu(top->menu(), top->text().remove(QLatin1Char('&')), out);
    }
    return out;
}

bool fuzzyMatch(const QString &haystack, const QString &needle, int *score)
{
    if (score) *score = 0;
    if (needle.isEmpty()) return true;

    const QString h = haystack.toLower();
    const QString n = needle.toLower();

    int hi = 0, gaps = 0, firstHit = -1, prev = -1;
    for (int ni = 0; ni < n.size(); ++ni) {
        const QChar want = n.at(ni);
        int found = -1;
        for (; hi < h.size(); ++hi) {
            if (h.at(hi) == want) { found = hi; ++hi; break; }
        }
        if (found < 0) return false;          // not a subsequence

        if (firstHit < 0) firstHit = found;
        // Distance from the PREVIOUS match, measured against `prev` rather
        // than the already-advanced cursor. Using `hi` here made the
        // arithmetic go negative, so scattered matches scored lower —
        // better — than contiguous ones, exactly inverting the ranking the
        // gap penalty exists to produce.
        if (prev >= 0) gaps += found - prev - 1;
        prev = found;
    }
    if (score) *score = gaps * 4 + firstHit;  // earlier match also wins
    return true;
}

CommandPalette::CommandPalette(const QVector<Command> &commands, QWidget *parent)
    : QDialog(parent)
    , m_all(commands)
{
    setWindowTitle(tr("Run command"));
    setModal(true);
    resize(560, 380);

    m_edit = new QLineEdit;
    m_edit->setPlaceholderText(tr("Type a command…"));
    m_edit->setClearButtonEnabled(true);

    m_list = new QListWidget;
    m_list->setAlternatingRowColors(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_edit);
    layout->addWidget(m_list, 1);

    connect(m_edit, &QLineEdit::textChanged, this, &CommandPalette::refilter);
    connect(m_edit, &QLineEdit::returnPressed, this, &CommandPalette::runCurrent);
    connect(m_list, &QListWidget::itemActivated, this, &CommandPalette::runCurrent);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            &CommandPalette::runCurrent);

    // Up/Down must move the selection while the cursor stays in the edit;
    // otherwise choosing a result means taking a hand off the keyboard,
    // which defeats the point.
    m_edit->installEventFilter(this);

    refilter(QString());
    m_edit->setFocus();
}

void CommandPalette::refilter(const QString &text)
{
    m_list->clear();
    m_shown.clear();

    QVector<QPair<int, int>> ranked;   // (score, index)
    for (int i = 0; i < m_all.size(); ++i) {
        int score = 0;
        if (!fuzzyMatch(m_all.at(i).path, text, &score)) continue;
        ranked.append({ score, i });
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const QPair<int,int> &a, const QPair<int,int> &b) {
                         return a.first < b.first;
                     });

    for (const auto &r : ranked) {
        const Command &c = m_all.at(r.second);
        QString label = c.path;
        if (!c.shortcut.isEmpty()) {
            // The shortcut column makes the palette double as the keyboard
            // reference the application otherwise has nowhere to show.
            label += QStringLiteral("\t%1").arg(c.shortcut);
        }
        auto *item = new QListWidgetItem(label);
        if (!c.action->isEnabled()) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
        m_list->addItem(item);
        m_shown.append(r.second);
    }

    if (m_list->count() > 0) m_list->setCurrentRow(0);
}

void CommandPalette::runCurrent()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_shown.size()) return;

    QAction *a = m_all.at(m_shown.at(row)).action;
    if (!a || !a->isEnabled()) return;

    // Close BEFORE triggering. Several commands open a window or a modal
    // dialog of their own, and firing them while this dialog is still up
    // leaves the new window behind a palette nobody asked to keep.
    accept();
    a->trigger();
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up) {
            const int rows = m_list->count();
            if (rows > 0) {
                int row = m_list->currentRow()
                          + (key->key() == Qt::Key_Down ? 1 : -1);
                // Wrap: with a short result list, running off the end and
                // stopping feels broken.
                if (row < 0)      row = rows - 1;
                if (row >= rows)  row = 0;
                m_list->setCurrentRow(row);
            }
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

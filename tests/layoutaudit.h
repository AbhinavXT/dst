#ifndef LAYOUTAUDIT_H
#define LAYOUTAUDIT_H

// Session 122: a widget that is in no layout but still visible is drawn at
// its parent's top-left corner, over whatever is there. A comment slipped
// into the middle of a line of addWidget() calls did exactly that to the
// serial terminal's Reset button, and every functional test still passed —
// they read the widgets, not where they were. This finds such orphans.

#include <QLayout>
#include <QSet>
#include <QStringList>
#include <QWidget>

namespace LayoutAudit {

inline void collect(QLayout *l, QSet<QWidget *> *managed)
{
    if (!l) return;
    for (int i = 0; i < l->count(); ++i) {
        QLayoutItem *it = l->itemAt(i);
        if (QWidget *w = it->widget()) managed->insert(w);
        if (QLayout *sub = it->layout()) collect(sub, managed);
    }
}

// Visible child widgets (of `root` and of every widget under it) that no
// layout manages. Popups, scroll-area internals and the like are skipped.
inline QStringList orphans(QWidget *root)
{
    QStringList out;
    QList<QWidget *> parents{ root };
    parents += root->findChildren<QWidget *>();
    for (QWidget *p : parents) {
        if (!p->layout()) continue;               // a widget without a layout places its children itself
        // A QMainWindow's layout places its menu bar, status bar, docks,
        // toolbars and central widget itself, without exposing them as
        // ordinary items: trust it for its direct children (what is INSIDE
        // them is still checked).
        if (p->inherits("QMainWindow")) continue;
        QSet<QWidget *> managed;
        collect(p->layout(), &managed);
        for (QObject *o : p->children()) {
            auto *w = qobject_cast<QWidget *>(o);
            if (!w || w->isWindow() || !w->isVisibleTo(root) || managed.contains(w)) continue;
            out << QStringLiteral("%1 (%2) in %3").arg(w->objectName(), QString::fromLatin1(w->metaObject()->className()),
                                                       QString::fromLatin1(p->metaObject()->className()));
        }
    }
    return out;
}

}  // namespace LayoutAudit

#endif // LAYOUTAUDIT_H

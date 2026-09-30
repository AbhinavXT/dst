#ifndef EMPTYSTATE_H
#define EMPTYSTATE_H
// =====================================================================
//  emptystate.{h,cpp} -- what a table says when it has no rows.
//
//  An empty QTableView is blank, and blank means three different things
//  in this program: nothing has arrived yet, the search found nothing,
//  or nothing is connected. The operator has to work out which from
//  context — and during acceptance testing "no rows" versus "no traffic"
//  is not a detail, it is the observation.
//
//  So: a centred line over the viewport whenever the model is empty,
//  which the owner sets to say which of the three it is. It is drawn in
//  the muted colour, follows the theme, and is transparent to the mouse
//  so it cannot swallow a click on the view underneath.
//
//  The message is expected to CHANGE over a view's life. A results table
//  should read "type a term and press Enter" before a search and "no
//  matches for 'foo'" after one; a message that stays "nothing here" for
//  both is barely better than the blank it replaced.
// =====================================================================
#include <QString>

#include <functional>

class QAbstractItemView;

namespace EmptyState {

// Attach (or re-message) the overlay. Safe to call repeatedly on the same
// view; the second call just updates the text. An empty message removes
// the overlay entirely.
void attach(QAbstractItemView *view, const QString &message);

// The message a view needs is often not fixed. A log tab with an empty
// model is waiting for traffic; the same tab with a full model and a
// filter that matches nothing is a different sentence entirely, and
// telling an operator "waiting for traffic" while 40,000 rows sit behind
// a filter is worse than saying nothing. This overload asks for the text
// each time the view becomes empty.
void attach(QAbstractItemView *view, std::function<QString()> provider);

// The message currently shown, or empty if the view has no overlay.
// Exists for the tests: the whole point is WHICH sentence appears, and a
// test that could only check "something is there" would not catch a view
// still saying "waiting for traffic" after a search came back empty.
QString message(const QAbstractItemView *view);

// Whether the overlay is visible right now (i.e. the model is empty).
bool isShowing(const QAbstractItemView *view);

}  // namespace EmptyState

#endif  // EMPTYSTATE_H

#ifndef QUERYLINEEDIT_H
#define QUERYLINEEDIT_H

// =============================================================================
//  QueryLineEdit
//  -----------------------------------------------------------------------------
//  A QLineEdit that knows about the LogQuery language: it completes field
//  prefixes and their values, and can point at the character where a parse
//  error occurred.
//
//  WHY
//    The query language exists in three places — the filter bar, cross-tab
//    search, and archive search — and in all three the only way to discover
//    that `hex:` or `after:-15m` exist is to hover a checkbox and read a
//    tooltip. A language nobody can find is a language nobody uses, and the
//    fallback is the plain substring search it was meant to replace.
//
//    Equally, LogQuery has always computed an errorOffset for every parse
//    failure and nothing has ever displayed it. "unexpected ')'" is a very
//    different message from "unexpected ')' at column 34" when the query is
//    long enough to have two of them.
//
//  COMPLETION IS PER TOKEN, NOT PER LINE
//    QCompleter completes the whole widget contents by default, which is
//    wrong here: a query is several space-separated terms, and completing
//    `sev:error sr` should offer `src:` for the last token while leaving
//    the rest alone. So the token under the cursor is extracted, completed
//    against, and spliced back — quoted and /regex/ runs are treated as
//    single tokens so a space inside them does not split the word.
//
//  One widget rather than three copies, so the field list cannot drift
//  between the places the same language is typed.
// =============================================================================

#include <QLineEdit>
#include <QStringList>

class QCompleter;
class QStringListModel;

// Start and length of the token containing `cursorPos`. Exposed for testing:
// the boundary rules (quotes, slashes, parentheses, the empty token after a
// trailing space) are where this goes wrong.
struct TokenRange { int start = 0; int length = 0; };
TokenRange queryTokenAt(const QString &text, int cursorPos);

// Completion candidates offered for a partially typed token.
QStringList queryCompletionsFor(const QString &token);

class QueryLineEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit QueryLineEdit(QWidget *parent = nullptr);

    // Mark the query valid, or invalid with a message and the offset
    // LogQuery reported. `offset` < 0 means "no position known".
    void setQueryError(const QString &message, int offset);
    void clearQueryError();

    bool hasQueryError() const { return m_errorOffset != -2; }

    // Select the character range the last error pointed at, so the user can
    // see it rather than count columns. Safe to call when there is no error.
    void selectErrorRange();

    // Record the current text as recently used. Called by the owner when a
    // query is actually RUN — not on every keystroke, or the history would
    // fill with the prefixes of one query.
    void rememberCurrent();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void onTextEdited(const QString &text);
    void onCompletionChosen(const QString &completion);

private:
    QCompleter       *m_completer = nullptr;
    QStringListModel *m_model     = nullptr;

    // -2 = no error. -1 = error with no known position.
    int     m_errorOffset = -2;
    QString m_errorMessage;
};

#endif // QUERYLINEEDIT_H

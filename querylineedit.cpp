#include "querylineedit.h"

#include "fieldcatalog.h"
#include "logquery.h"
#include "queryhistory.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMenu>
#include <QKeyEvent>
#include <QScrollBar>
#include <QStringListModel>

TokenRange queryTokenAt(const QString &text, int cursorPos)
{
    TokenRange r;
    const int n = text.size();
    const int pos = qBound(0, cursorPos, n);

    // Walk back to the token start. Quote and slash runs are opaque: a
    // space inside "No Error" or /a b/ is part of the term, not a
    // separator, and splitting there would offer completions for a
    // fragment of a phrase.
    int start = pos;
    bool inQuote = false, inRegex = false;

    // Determine whether the cursor sits inside a quoted or regex run by
    // scanning from the beginning — cheap at query lengths, and the only
    // way to know without parsing.
    for (int i = 0; i < pos && i < n; ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('"') && !inRegex) inQuote = !inQuote;
        else if (c == QLatin1Char('/') && !inQuote) inRegex = !inRegex;
    }

    while (start > 0) {
        const QChar c = text.at(start - 1);
        if (!inQuote && !inRegex
            && (c.isSpace() || c == QLatin1Char('(') || c == QLatin1Char(')'))) {
            break;
        }
        // TOGGLE, not set. Walking backwards, crossing the opening quote
        // means we have LEFT the quoted run — setting the flag true here
        // instead kept it latched, so the scan ran straight past the quote
        // and swallowed every earlier token on the line.
        if (c == QLatin1Char('"')) inQuote = !inQuote;
        if (c == QLatin1Char('/')) inRegex = !inRegex;
        --start;
    }

    r.start  = start;
    r.length = pos - start;
    return r;
}

QStringList queryCompletionsFor(const QString &token)
{
    // Value completion once a field prefix is committed: after `sev:` the
    // useful suggestions are the three severities, not the field list
    // again. This is where most of the discoverability comes from — it
    // tells you what a field accepts without any documentation.
    const int colon = token.indexOf(QLatin1Char(':'));
    if (colon > 0) {
        const QString field = token.left(colon).toLower();
        const QString stem  = token.left(colon + 1);
        QStringList values;
        if (field == QLatin1String("sev") || field == QLatin1String("severity")) {
            values = QStringList{ QStringLiteral("info"), QStringLiteral("warn"), QStringLiteral("error") };
        } else if (field == QLatin1String("dir")
                   || field == QLatin1String("direction")) {
            values = QStringList{ QStringLiteral("in"), QStringLiteral("out"), QStringLiteral("none") };
        } else if (field == QLatin1String("after")
                   || field == QLatin1String("before")) {
            // Relative forms first: they are the ones people want and the
            // ones least likely to be guessed.
            values = QStringList{ QStringLiteral("-5m"), QStringLiteral("-15m"), QStringLiteral("-1h"), QStringLiteral("-24h") };
        } else if (field == QLatin1String("time")) {
            // The forms nobody guesses: a range, a midnight wrap, a
            // comparison.
            values = QStringList{ QStringLiteral("14:00..14:10"), QStringLiteral("23:50..00:10"),
                                  QStringLiteral(">=09:00"), QStringLiteral("2026-01-31") };
        } else if (field == QLatin1String("last")) {
            values = QStringList{ QStringLiteral("5m"), QStringLiteral("15m"), QStringLiteral("1h"), QStringLiteral("24h") };
        } else if (field == QLatin1String("len")) {
            values = QStringList{ QStringLiteral(">64"), QStringLiteral("<16"), QStringLiteral("=0"), QStringLiteral("10..64") };
        } else if (field == QLatin1String("field")) {
            // Schema field names, so the language is discoverable without
            // the user having to know kavach.xml. Only the commonly-asserted
            // ones: the full list is 400+ and a popup that long is not a
            // help. The mask form is offered because it is the one nobody
            // guesses.
            static const char *common[] = {
                "SIG_OV=1", "FRAME_NUM", "FRAME_NUM&7=1", "FRAME_NUM&7=3",
                "FRAME_NUM&7=5", "FRAME_NUM&7=7", "Loco_Health~",
                "TRAIN_SPEED>", "LOCO_MODE", "MOVEMENT_DIR",
                "EMERGENCY_STATUS", "PKT_TYPE=", "Info_Ack=",
                "ABS_LOCO_LOC>", "LAST_RFID_TAG=", "TIN=",
            };
            QStringList out;
            // Catalogue aliases first: they are the readable names, and
            // completing "loco_mode=staff_responsible" is the whole point
            // of having the file.
            const QString typed = token.mid(colon + 1);
            for (const QString &c : FieldCatalog::instance().completionsFor(typed)) {
                out << stem + c;
            }
            for (const char *v : common) out << stem + QLatin1String(v);
            return out;
        } else {
            return {};
        }
        QStringList out;
        for (const QString &v : values) out << stem + v;
        return out;
    }

    QStringList out;
    for (const QString &f : LogQuery::knownFields()) out << f + QLatin1Char(':');
    // Operators are worth offering too — "NOT" is not guessable from a
    // language that otherwise looks like a search box.
    out << QStringLiteral("AND") << QStringLiteral("OR") << QStringLiteral("NOT");
    return out;
}

QueryLineEdit::QueryLineEdit(QWidget *parent)
    : QLineEdit(parent)
{
    m_model     = new QStringListModel(this);
    m_completer = new QCompleter(m_model, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    // Unset: the widget must not replace its whole contents with the chosen
    // completion, which is exactly what the default does. We splice the
    // token ourselves in onCompletionChosen().
    m_completer->setWidget(this);

    connect(this, &QLineEdit::textEdited, this, &QueryLineEdit::onTextEdited);
    connect(m_completer, QOverload<const QString &>::of(&QCompleter::activated),
            this, &QueryLineEdit::onCompletionChosen);
}

void QueryLineEdit::onTextEdited(const QString &text)
{
    // Typing invalidates whatever the last error pointed at; leaving the
    // red border up while the user fixes it is just noise.
    if (hasQueryError()) clearQueryError();

    const TokenRange r = queryTokenAt(text, cursorPosition());
    const QString token = text.mid(r.start, r.length);

    if (token.isEmpty()) {
        m_completer->popup()->hide();
        return;
    }

    const QStringList candidates = queryCompletionsFor(token);
    if (candidates.isEmpty()) {
        m_completer->popup()->hide();
        return;
    }

    m_model->setStringList(candidates);
    m_completer->setCompletionPrefix(token);

    if (m_completer->completionCount() == 0) {
        m_completer->popup()->hide();
        return;
    }
    // An exact single match is not worth a popup — it would sit there
    // offering the thing already typed.
    if (m_completer->completionCount() == 1
        && m_completer->currentCompletion().compare(token, Qt::CaseInsensitive) == 0) {
        m_completer->popup()->hide();
        return;
    }

    QRect rect = cursorRect();
    rect.setWidth(m_completer->popup()->sizeHintForColumn(0)
                  + m_completer->popup()->verticalScrollBar()->sizeHint().width());
    m_completer->complete(rect);
}

void QueryLineEdit::onCompletionChosen(const QString &completion)
{
    // Splice into the token under the cursor, leaving the rest of the query
    // untouched.
    const QString current = text();
    const TokenRange r = queryTokenAt(current, cursorPosition());

    QString updated = current;
    updated.replace(r.start, r.length, completion);
    setText(updated);
    setCursorPosition(r.start + completion.size());
}

void QueryLineEdit::rememberCurrent()
{
    QueryHistory::remember(text());
}

void QueryLineEdit::contextMenuEvent(QContextMenuEvent *event)
{
    // Extend the standard edit menu rather than replacing it: cut/copy/
    // paste/select-all must keep working in a text box, and a context menu
    // that loses them to make room for a feature would be a poor trade.
    QMenu *menu = createStandardContextMenu();
    menu->addSeparator();

    const QStringList recent = QueryHistory::recent();
    QMenu *recentMenu = menu->addMenu(tr("Recent queries"));
    recentMenu->setEnabled(!recent.isEmpty());
    for (const QString &q : recent) {
        // Elided in the menu, full text in the tooltip: a long query would
        // otherwise stretch the menu past the screen edge.
        const QString label = q.size() > 60 ? q.left(57) + QStringLiteral("…") : q;
        QAction *a = recentMenu->addAction(label);
        a->setToolTip(q);
        connect(a, &QAction::triggered, this, [this, q]() {
            setText(q);
            emit returnPressed();      // run it; picking one means using it
        });
    }

    const QVector<QueryPreset> presets = QueryHistory::presets();
    QMenu *presetMenu = menu->addMenu(tr("Saved queries"));
    presetMenu->setEnabled(!presets.isEmpty());
    for (const QueryPreset &p : presets) {
        QAction *a = presetMenu->addAction(p.name);
        a->setToolTip(p.query);
        connect(a, &QAction::triggered, this, [this, p]() {
            setText(p.query);
            emit returnPressed();
        });
    }

    QAction *save = menu->addAction(tr("Save this query as…"));
    save->setEnabled(!text().trimmed().isEmpty());
    connect(save, &QAction::triggered, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Save query"), tr("Name:"), QLineEdit::Normal,
            QString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            QueryHistory::savePreset(name, text());
        }
    });

    menu->exec(event->globalPos());
    delete menu;
}

void QueryLineEdit::setQueryError(const QString &message, int offset)
{
    m_errorMessage = message;
    m_errorOffset  = (offset >= 0) ? offset : -1;

    setStyleSheet(QStringLiteral("border: 1px solid #cc3300;"));
    setToolTip(offset >= 0
                   ? tr("%1 (at column %2). Click the error message to "
                        "select it.").arg(message).arg(offset + 1)
                   : message);
}

void QueryLineEdit::clearQueryError()
{
    m_errorOffset = -2;
    m_errorMessage.clear();
    setStyleSheet(QString());
    setToolTip(QString());
}

void QueryLineEdit::selectErrorRange()
{
    if (m_errorOffset < 0) return;

    // Select the whole token at the error, not a single character: one
    // highlighted bracket is easy to miss, and the token is what has to
    // change anyway.
    const TokenRange r = queryTokenAt(text(), m_errorOffset);
    const int start = r.start;
    int end = start;
    const QString t = text();
    while (end < t.size() && !t.at(end).isSpace()) ++end;

    setFocus();
    if (end > start) setSelection(start, end - start);
    else             setCursorPosition(qMin(m_errorOffset, t.size()));
}

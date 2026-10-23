#include "queryhistory.h"

#include "settings.h"

#include <QSettings>

namespace {

QSettings store()
{
    return QSettings(Settings::iniPath(), QSettings::IniFormat);
}

}  // namespace

QStringList QueryHistory::recent()
{
    QSettings s = store();
    return s.value(QStringLiteral("query/recent")).toStringList();
}

void QueryHistory::remember(const QString &query)
{
    const QString q = query.trimmed();
    if (q.isEmpty()) return;

    QStringList list = recent();

    // Move-to-front rather than append: re-running an old query should
    // promote it, not leave two copies with the newer one buried.
    list.removeAll(q);
    list.prepend(q);
    while (list.size() > kMaxRecent) list.removeLast();

    QSettings s = store();
    s.setValue(QStringLiteral("query/recent"), list);
}

void QueryHistory::clearRecent()
{
    QSettings s = store();
    s.remove(QStringLiteral("query/recent"));
}

QVector<QueryPreset> QueryHistory::presets()
{
    QSettings s = store();
    // Stored as two parallel lists rather than a QSettings array: arrays
    // need beginWriteArray/endArray bracketing that is easy to get wrong on
    // partial writes, and the INI stays readable and hand-editable this way,
    // which matters for a file operators are already expected to edit.
    const QStringList names   = s.value(QStringLiteral("query/preset_names")).toStringList();
    const QStringList queries = s.value(QStringLiteral("query/preset_queries")).toStringList();

    QVector<QueryPreset> out;
    // Defensive: a hand-edited INI can leave the two lists different
    // lengths, and pairing past the end would read garbage.
    const int n = qMin(names.size(), queries.size());
    for (int i = 0; i < n; ++i) {
        if (names.at(i).trimmed().isEmpty()) continue;
        out.append({ names.at(i), queries.at(i) });
    }
    return out;
}

void QueryHistory::savePreset(const QString &name, const QString &query)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return;

    QVector<QueryPreset> all = presets();

    bool replaced = false;
    for (QueryPreset &p : all) {
        if (p.name.compare(n, Qt::CaseInsensitive) == 0) {
            p.query = query;
            replaced = true;
            break;
        }
    }
    if (!replaced) all.append({ n, query });

    QStringList names, queries;
    for (const QueryPreset &p : all) { names << p.name; queries << p.query; }

    QSettings s = store();
    s.setValue(QStringLiteral("query/preset_names"), names);
    s.setValue(QStringLiteral("query/preset_queries"), queries);
}

void QueryHistory::removePreset(const QString &name)
{
    QVector<QueryPreset> all = presets();
    QStringList names, queries;
    for (const QueryPreset &p : all) {
        if (p.name.compare(name, Qt::CaseInsensitive) == 0) continue;
        names << p.name;
        queries << p.query;
    }
    QSettings s = store();
    s.setValue(QStringLiteral("query/preset_names"), names);
    s.setValue(QStringLiteral("query/preset_queries"), queries);
}

// Two parallel lists, like the presets: readable and hand-editable in the INI.
QVector<QueryHistory::Find> QueryHistory::recentFinds()
{
    QSettings s = store();
    const QStringList texts = s.value(QStringLiteral("find/recent_text")).toStringList();
    const QList<QVariant> modes = s.value(QStringLiteral("find/recent_mode")).toList();
    QVector<Find> out;
    for (int i = 0; i < texts.size(); ++i)
        out.append({ texts.at(i), i < modes.size() ? modes.at(i).toInt() : 0 });
    return out;
}

void QueryHistory::rememberFind(const QString &text, int mode)
{
    if (text.trimmed().isEmpty()) return;
    QVector<Find> list = recentFinds();
    if (!list.isEmpty() && list.first().text == text && list.first().mode == mode) return;  // F3 held down
    for (int i = list.size() - 1; i >= 0; --i)
        if (list.at(i).text == text && list.at(i).mode == mode) list.remove(i);
    list.prepend({ text, mode });
    while (list.size() > kMaxFinds) list.removeLast();

    QStringList texts;
    QList<QVariant> modes;
    for (const Find &f : list) { texts << f.text; modes << f.mode; }
    QSettings s = store();
    s.setValue(QStringLiteral("find/recent_text"), texts);
    s.setValue(QStringLiteral("find/recent_mode"), modes);
}

void QueryHistory::clearRecentFinds()
{
    QSettings s = store();
    s.remove(QStringLiteral("find/recent_text"));
    s.remove(QStringLiteral("find/recent_mode"));
}

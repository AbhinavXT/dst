#include "bookmarks.h"

#include "logmodel.h"
#include "settings.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

BookmarkStore::BookmarkStore(QObject *parent)
    : QObject(parent)
{
}

bool BookmarkStore::load(const QString &path)
{
    m_path = path.isEmpty()
                 ? QFileInfo(Settings::iniPath()).absolutePath()
                       + QStringLiteral("/bookmarks.json")
                 : path;

    m_marks.clear();

    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) {
        // No file yet is the normal first-run case, not a failure.
        emit changed();
        return false;
    }

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();

    if (perr.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning() << "BookmarkStore: could not parse" << m_path
                   << ":" << perr.errorString();
        emit changed();
        return false;
    }

    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        Bookmark b;
        b.tabKey         = o.value("tab").toString();
        b.epochMs        = qint64(o.value("epoch_ms").toDouble());
        b.note           = o.value("note").toString();
        b.messageSnippet = o.value("message").toString();
        b.createdMs      = qint64(o.value("created_ms").toDouble());
        if (b.tabKey.isEmpty() || b.epochMs <= 0) continue;
        m_marks.append(b);
    }

    emit changed();
    return true;
}

bool BookmarkStore::save() const
{
    if (m_path.isEmpty()) return false;

    QJsonArray arr;
    for (const Bookmark &b : m_marks) {
        QJsonObject o;
        o.insert("tab",        b.tabKey);
        o.insert("epoch_ms",   double(b.epochMs));
        o.insert("note",       b.note);
        o.insert("message",    b.messageSnippet);
        o.insert("created_ms", double(b.createdMs));
        arr.append(o);
    }

    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "BookmarkStore: cannot write" << m_path
                   << ":" << f.errorString();
        return false;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

int BookmarkStore::indexOf(const QString &tabKey, qint64 epochMs) const
{
    for (int i = 0; i < m_marks.size(); ++i) {
        if (m_marks.at(i).tabKey == tabKey && m_marks.at(i).epochMs == epochMs) {
            return i;
        }
    }
    return -1;
}

bool BookmarkStore::has(const QString &tabKey, qint64 epochMs) const
{
    return indexOf(tabKey, epochMs) >= 0;
}

bool BookmarkStore::toggle(const QString &tabKey, qint64 epochMs,
                           const QString &messageSnippet)
{
    const int i = indexOf(tabKey, epochMs);
    if (i >= 0) {
        m_marks.remove(i);
        save();
        emit changed();
        return false;
    }

    Bookmark b;
    b.tabKey    = tabKey;
    b.epochMs   = epochMs;
    b.createdMs = QDateTime::currentMSecsSinceEpoch();
    // Snapshot the text now. Once the entry is evicted from the ring this
    // is the only thing left that says what was marked, and a bookmark
    // reading "33_1 at 14:03:22" with no context is close to useless.
    b.messageSnippet = messageSnippet.left(300);

    // Keep the list in time order so the panel reads as a timeline.
    int at = 0;
    while (at < m_marks.size() && m_marks.at(at).epochMs < epochMs) ++at;
    m_marks.insert(at, b);

    save();
    emit changed();
    return true;
}

void BookmarkStore::remove(int index)
{
    if (index < 0 || index >= m_marks.size()) return;
    m_marks.remove(index);
    save();
    emit changed();
}

void BookmarkStore::setNote(int index, const QString &note)
{
    if (index < 0 || index >= m_marks.size()) return;
    m_marks[index].note = note;
    save();
    emit changed();
}

void BookmarkStore::clear()
{
    if (m_marks.isEmpty()) return;
    m_marks.clear();
    save();
    emit changed();
}

void BookmarkStore::replaceAll(const QVector<Bookmark> &marks)
{
    m_marks = marks;
    save();
    emit changed();
}

void BookmarkStore::applyToModel(LogModel *model, const QString &tabKey) const
{
    if (!model) return;

    // Collect this tab's timestamps first so the scan below is a set
    // membership test rather than a nested loop over every bookmark.
    QSet<qint64> stamps;
    for (const Bookmark &b : m_marks) {
        if (b.tabKey == tabKey) stamps.insert(b.epochMs);
    }
    if (stamps.isEmpty()) return;

    const int n = model->count();
    for (int i = 0; i < n; ++i) {
        LogEntryPtr e = model->entryAt(i);
        if (e && stamps.contains(e->epochMs)) e->bookmarked = true;
    }
}

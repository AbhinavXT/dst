#include "notessidecar.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace NotesSidecar {

QString pathFor(const QString &capturePath)
{
    const QFileInfo fi(capturePath);
    return fi.path() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".notes.json");
}

QVector<Bookmark> marksIn(const QVector<Bookmark> &all, const QString &tabKey, qint64 fromMs, qint64 toMs)
{
    QVector<Bookmark> out;
    for (const Bookmark &b : all)
        if (b.tabKey == tabKey && b.epochMs >= fromMs && b.epochMs <= toMs) out.append(b);
    std::stable_sort(out.begin(), out.end(),
                     [](const Bookmark &a, const Bookmark &b) { return a.epochMs < b.epochMs; });
    return out;
}

bool write(const QString &capturePath, const QVector<Bookmark> &marks, QString *err)
{
    const QString path = pathFor(capturePath);
    if (marks.isEmpty()) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            if (err) *err = QStringLiteral("could not remove %1").arg(path);
            return false;
        }
        return true;
    }
    QJsonArray arr;
    for (const Bookmark &b : marks) {
        QJsonObject o;
        o.insert(QStringLiteral("tab"), b.tabKey);
        o.insert(QStringLiteral("epoch_ms"), double(b.epochMs));
        o.insert(QStringLiteral("time"),
                 QDateTime::fromMSecsSinceEpoch(b.epochMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
        o.insert(QStringLiteral("note"), b.note);
        o.insert(QStringLiteral("message"), b.messageSnippet);
        o.insert(QStringLiteral("created_ms"), double(b.createdMs));
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("dlconsole-notes"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("capture"), QFileInfo(capturePath).fileName());
    root.insert(QStringLiteral("notes"), arr);

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (err) *err = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (err) *err = f.errorString();
        return false;
    }
    return true;
}

QVector<Bookmark> read(const QString &capturePath, QString *err)
{
    QVector<Bookmark> out;
    QFile f(pathFor(capturePath));
    if (!f.exists()) return out;
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return out;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    const QJsonObject root = doc.object();
    if (perr.error != QJsonParseError::NoError || !doc.isObject()
        || root.value(QStringLiteral("format")).toString() != QLatin1String("dlconsole-notes")) {
        if (err) *err = QStringLiteral("%1 is not a notes file").arg(QFileInfo(f.fileName()).fileName());
        return out;
    }
    for (const QJsonValue &v : root.value(QStringLiteral("notes")).toArray()) {
        const QJsonObject o = v.toObject();
        Bookmark b;
        b.tabKey = o.value(QStringLiteral("tab")).toString();
        b.epochMs = qint64(o.value(QStringLiteral("epoch_ms")).toDouble());
        b.note = o.value(QStringLiteral("note")).toString();
        b.messageSnippet = o.value(QStringLiteral("message")).toString();
        b.createdMs = qint64(o.value(QStringLiteral("created_ms")).toDouble());
        if (!b.tabKey.isEmpty() && b.epochMs > 0) out.append(b);
    }
    return out;
}

Merge mergeInto(BookmarkStore *store, const QVector<Bookmark> &marks)
{
    Merge m;
    if (!store) return m;
    for (const Bookmark &b : marks) {
        int at = -1;
        for (int i = 0; i < store->count() && at < 0; ++i)
            if (store->all().at(i).tabKey == b.tabKey && store->all().at(i).epochMs == b.epochMs) at = i;
        if (at < 0) {
            store->toggle(b.tabKey, b.epochMs, b.messageSnippet);
            ++m.added;
            if (b.note.isEmpty()) continue;
            for (int i = 0; i < store->count(); ++i)
                if (store->all().at(i).tabKey == b.tabKey && store->all().at(i).epochMs == b.epochMs)
                    store->setNote(i, b.note);
        } else if (store->all().at(at).note == b.note || b.note.isEmpty()) {
            continue;
        } else if (store->all().at(at).note.isEmpty()) {
            store->setNote(at, b.note);
            ++m.filled;
        } else {
            ++m.differ;
        }
    }
    return m;
}

}  // namespace NotesSidecar

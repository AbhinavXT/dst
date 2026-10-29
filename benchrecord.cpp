#include "benchrecord.h"
#include "settings.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace BenchRecord {

namespace {
const char kFormat[] = "dlconsole-benchrec";
constexpr int kVersion = 1;
}

QStringList kinds()
{
    return { QStringLiteral("speed"), QStringLiteral("tag"), QStringLiteral("health"), QStringLiteral("biu"), QStringLiteral("analog"),
             QStringLiteral("event") };
}

QByteArray headerLine(qint64 createdMs, const QString &note)
{
    QJsonObject o;
    o.insert(QStringLiteral("format"), QLatin1String(kFormat));
    o.insert(QStringLiteral("version"), kVersion);
    o.insert(QStringLiteral("createdMs"), double(createdMs));
    o.insert(QStringLiteral("note"), note);
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}

QByteArray itemLine(const Item &item)
{
    QJsonObject o;
    o.insert(QStringLiteral("t"), double(item.ms));
    o.insert(QStringLiteral("k"), item.kind);
    o.insert(QStringLiteral("d"), item.tx ? QStringLiteral("tx") : QStringLiteral("rx"));
    if (!item.peer.isEmpty()) o.insert(QStringLiteral("p"), item.peer);
    if (!item.bytes.isEmpty()) o.insert(QStringLiteral("b"), QString::fromLatin1(item.bytes.toHex(' ').toUpper()));
    if (!item.says.isEmpty()) o.insert(QStringLiteral("s"), item.says);
    if (!item.note.isEmpty()) o.insert(QStringLiteral("n"), item.note);
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}

bool read(const QString &path, File *out, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return false;
    }
    *out = File();
    const QJsonObject head = QJsonDocument::fromJson(f.readLine()).object();
    if (head.value(QStringLiteral("format")).toString() != QLatin1String(kFormat)) {
        if (err) *err = QStringLiteral("not a test recording (.benchrec)");
        return false;
    }
    out->createdMs = qint64(head.value(QStringLiteral("createdMs")).toDouble());
    out->note = head.value(QStringLiteral("note")).toString();
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            out->torn = true;   // a crash mid-line: what came before stands
            break;
        }
        const QJsonObject o = doc.object();
        Item item;
        item.ms = qint64(o.value(QStringLiteral("t")).toDouble());
        item.kind = o.value(QStringLiteral("k")).toString();
        item.tx = o.value(QStringLiteral("d")).toString() != QLatin1String("rx");
        item.peer = o.value(QStringLiteral("p")).toString();
        item.bytes = QByteArray::fromHex(o.value(QStringLiteral("b")).toString().toLatin1());
        item.says = o.value(QStringLiteral("s")).toString();
        item.note = o.value(QStringLiteral("n")).toString();
        out->items.append(item);
    }
    return true;
}

QString defaultPath(qint64 nowMs)
{
    return QDir(Settings::diskLogRoot()).filePath(
        QStringLiteral("bench/bench_%1.benchrec")
            .arg(QDateTime::fromMSecsSinceEpoch(nowMs).toString(QStringLiteral("yyyyMMdd_HHmmss"))));
}

Recorder::Recorder(QObject *parent) : QObject(parent) {}

Recorder::~Recorder()
{
    if (recording()) stop(QDateTime::currentMSecsSinceEpoch());
}

bool Recorder::start(const QString &path, qint64 nowMs, const QString &note, QString *err)
{
    if (recording()) stop(nowMs);
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = m_file.errorString();
        return false;
    }
    m_path = path;
    m_count = 0;
    m_file.write(headerLine(nowMs, note));
    m_file.flush();
    mark(nowMs, QStringLiteral("Recording started"));
    emit started(path);
    return true;
}

void Recorder::stop(qint64 nowMs)
{
    if (!recording()) return;
    mark(nowMs, QStringLiteral("Recording stopped"));
    m_file.close();
    emit stopped(m_path, m_count);
}

void Recorder::append(const Item &item)
{
    if (!recording()) return;
    m_file.write(itemLine(item));
    m_file.flush();
    ++m_count;
}

void Recorder::mark(qint64 ms, const QString &text)
{
    Item item;
    item.ms = ms;
    item.kind = QStringLiteral("event");
    item.says = text;
    append(item);
}

}  // namespace BenchRecord

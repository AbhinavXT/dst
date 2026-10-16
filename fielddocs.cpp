#include "fielddocs.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

const FieldDocs &FieldDocs::instance()
{
    static const FieldDocs docs = []() {
        FieldDocs loaded;
        QFile file(QStringLiteral(":/schema/fielddocs.json"));
        if (file.open(QIODevice::ReadOnly)) {
            loaded.loadFromJson(file.readAll());
        }
        return loaded;
    }();
    return docs;
}

QString FieldDocs::normalizePacket(const QString &packet)
{
    const QString lower = packet.trimmed().toLower();
    if (lower == QLatin1String("arprecv")) {
        return QStringLiteral("arp");
    }
    return lower;
}

QString FieldDocs::normalizeName(const QString &shown)
{
    QString name = shown.trimmed();
    // "subpkt[2] StaticSpeedProfile @byte 25": the sub-packet header row.
    if (name.startsWith(QLatin1String("subpkt"))) {
        return QStringLiteral("subpkt");
    }
    const int paren = name.indexOf(QLatin1String(" ("));
    if (paren > 0) {
        name = name.left(paren);
    }
    static const QRegularExpression index(QStringLiteral("\\[\\d+\\]$"));
    name.remove(index);
    return name.trimmed();
}

bool FieldDocs::loadFromJson(const QByteArray &json, QString *err)
{
    m_docs.clear();
    m_index.clear();
    m_titles.clear();
    m_documents.clear();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isObject()) {
        if (err) *err = QStringLiteral("fielddocs.json: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    const QJsonObject sources = root.value(QStringLiteral("sources")).toObject();
    for (auto it = sources.constBegin(); it != sources.constEnd(); ++it) {
        const QJsonObject source = it.value().toObject();
        m_titles.insert(it.key(), source.value(QStringLiteral("title")).toString());
        m_documents.insert(it.key(), source.value(QStringLiteral("doc")).toString());
    }
    for (const QJsonValue &value : root.value(QStringLiteral("docs")).toArray()) {
        const QJsonObject object = value.toObject();
        FieldDoc doc;
        for (const QJsonValue &packet : object.value(QStringLiteral("packets")).toArray()) {
            doc.packets << packet.toString();
        }
        for (const QJsonValue &name : object.value(QStringLiteral("names")).toArray()) {
            doc.names << name.toString();
        }
        doc.src = object.value(QStringLiteral("src")).toString();
        doc.clause = object.value(QStringLiteral("clause")).toString();
        doc.page = object.value(QStringLiteral("page")).toInt();
        doc.note = object.value(QStringLiteral("note")).toString();
        for (const QJsonValue &type : object.value(QStringLiteral("tag_types")).toArray()) {
            doc.tagTypes << type.toInt();
        }
        for (const QJsonValue &partValue : object.value(QStringLiteral("parts")).toArray()) {
            const QJsonObject partObject = partValue.toObject();
            FieldDocPart part;
            part.field = partObject.value(QStringLiteral("field")).toString();
            part.bits = partObject.value(QStringLiteral("bits")).toInt();
            part.text = partObject.value(QStringLiteral("text")).toString();
            doc.parts << part;
        }
        if (doc.packets.isEmpty() || doc.names.isEmpty() || doc.parts.isEmpty()) {
            continue;
        }
        const int at = m_docs.size();
        m_docs << doc;
        for (const QString &packet : doc.packets) {
            for (const QString &name : doc.names) {
                const QString key = normalizePacket(packet) + QLatin1Char('|') + name;
                if (!m_index.value(key).contains(at)) {
                    m_index[key] << at;
                }
            }
        }
    }
    if (m_docs.isEmpty()) {
        if (err) *err = QStringLiteral("fielddocs.json holds no descriptions");
        return false;
    }
    return true;
}

const FieldDoc *FieldDocs::find(const QString &packet, const QString &name, int tagType) const
{
    const QVector<int> hits = m_index.value(normalizePacket(packet) + QLatin1Char('|') + normalizeName(name));
    if (hits.isEmpty()) {
        return nullptr;
    }
    const int wanted = tagType > 0 ? tagType : 9;   // unknown: the Normal tag's
    for (int index : hits) {
        const FieldDoc &doc = m_docs.at(index);
        if (doc.tagTypes.isEmpty() || doc.tagTypes.contains(wanted)) {
            return &doc;
        }
    }
    // A field only one tag type has (gate_id on an LC tag) asked about
    // without its type: that type's description is still the right one.
    return tagType > 0 ? nullptr : &m_docs.at(hits.first());
}

QString FieldDocs::sourceTitle(const QString &src) const
{
    return m_titles.value(src);
}

QString FieldDocs::sourceDocument(const QString &src) const
{
    return m_documents.value(src);
}

QString FieldDocs::reference(const FieldDoc &doc) const
{
    return QStringLiteral("%1, %2, page %3").arg(sourceTitle(doc.src), doc.clause).arg(doc.page);
}

QString FieldDocs::tooltipFor(const FieldDoc &doc) const
{
    auto html = [](const QString &text) {
        return text.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br>"));
    };
    QString out = QStringLiteral("<div style='max-width:520px'>");
    for (int index = 0; index < doc.parts.size(); ++index) {
        const FieldDocPart &part = doc.parts.at(index);
        if (index > 0) {
            out += QStringLiteral("<br>");
        }
        out += QStringLiteral("<b>%1</b> &nbsp;·&nbsp; %2 bit%3<br>%4")
                   .arg(part.field.toHtmlEscaped())
                   .arg(part.bits)
                   .arg(part.bits == 1 ? QString() : QStringLiteral("s"))
                   .arg(html(part.text));
    }
    if (!doc.note.isEmpty()) {
        out += QStringLiteral("<br><br><i>DLConsole note: %1</i>").arg(html(doc.note));
    }
    out += QStringLiteral("<br><br><small>%1<br>%2</small></div>")
               .arg(html(reference(doc)), html(sourceDocument(doc.src)));
    return out;
}

QString FieldDocs::tooltip(const QString &packet, const QString &name, int tagType) const
{
    const FieldDoc *doc = find(packet, name, tagType);
    return doc ? tooltipFor(*doc) : QString();
}

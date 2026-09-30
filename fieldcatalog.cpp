#include "fieldcatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

FieldCatalog &FieldCatalog::instance()
{
    static FieldCatalog c;
    return c;
}

void FieldCatalog::clear() { m_byAlias.clear(); }

bool FieldCatalog::load(const QString &path, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("cannot open %1: %2").arg(path, f.errorString());
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();
    return loadFromJson(data, err);
}

bool FieldCatalog::loadFromJson(const QByteArray &json, QString *err)
{
    m_byAlias.clear();

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (perr.error != QJsonParseError::NoError) {
        if (err) *err = QStringLiteral("JSON error at offset %1: %2")
                            .arg(perr.offset).arg(perr.errorString());
        return false;
    }
    if (!doc.isObject()) {
        if (err) *err = QStringLiteral("expected a JSON object with a 'fields' array");
        return false;
    }

    const QJsonArray fields = doc.object().value("fields").toArray();
    for (const QJsonValue &v : fields) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();

        Entry e;
        e.alias  = o.value("name").toString().trimmed();
        e.field  = o.value("field").toString().trimmed();
        e.packet = o.value("packet").toString().trimmed().toLower();
        e.desc   = o.value("desc").toString();

        // A missing "field" means the alias IS the decoder name — common
        // for fields whose spelling is already readable.
        if (e.field.isEmpty()) e.field = e.alias;
        if (e.alias.isEmpty()) continue;

        const QJsonObject vals = o.value("values").toObject();
        for (auto it = vals.begin(); it != vals.end(); ++it) {
            e.values.insert(it.key().toLower(), qint64(it.value().toDouble()));
        }

        m_byAlias.insert(e.alias.toLower(), e);
    }

    if (m_byAlias.isEmpty()) {
        if (err) *err = QStringLiteral("no field definitions found");
        return false;
    }
    return true;
}

const FieldCatalog::Entry *FieldCatalog::find(const QString &alias) const
{
    auto it = m_byAlias.constFind(alias.trimmed().toLower());
    return it == m_byAlias.constEnd() ? nullptr : &it.value();
}

QString FieldCatalog::resolveField(const QString &alias) const
{
    const Entry *e = find(alias);
    // Unknown aliases pass through untouched: the catalogue is additive, and
    // a field it has not been told about must stay queryable by raw name.
    return e ? e->field : alias;
}

QString FieldCatalog::resolvePacket(const QString &alias) const
{
    const Entry *e = find(alias);
    return e ? e->packet : QString();
}

qint64 FieldCatalog::resolveValue(const QString &alias, const QString &symbol,
                                  bool *ok) const
{
    if (ok) *ok = false;
    const Entry *e = find(alias);
    if (!e) return 0;
    auto it = e->values.constFind(symbol.trimmed().toLower());
    if (it == e->values.constEnd()) return 0;
    if (ok) *ok = true;
    return it.value();
}

QString FieldCatalog::symbolFor(const QString &alias, qint64 value) const
{
    const Entry *e = find(alias);
    if (!e) return QString();
    for (auto it = e->values.constBegin(); it != e->values.constEnd(); ++it) {
        if (it.value() == value) return it.key();
    }
    return QString();
}

QStringList FieldCatalog::aliases() const
{
    QStringList out = m_byAlias.keys();
    out.sort();
    return out;
}

QStringList FieldCatalog::completionsFor(const QString &partial) const
{
    const QString p = partial.trimmed().toLower();

    // Once a field and an operator have been typed, offer that field's
    // symbolic values — this is where most of the catalogue's value shows
    // up, because the numbers are exactly what nobody remembers.
    const int op = p.indexOf(QLatin1Char('='));
    if (op > 0) {
        const QString stem = p.left(op + 1);
        const Entry *e = find(p.left(op));
        if (e) {
            QStringList out;
            QStringList syms = e->values.keys();
            syms.sort();
            for (const QString &s : syms) out << stem + s;
            return out;
        }
        return {};
    }

    QStringList out;
    for (auto it = m_byAlias.constBegin(); it != m_byAlias.constEnd(); ++it) {
        if (p.isEmpty() || it.key().startsWith(p)) out << it.value().alias;
    }
    out.sort();
    return out;
}

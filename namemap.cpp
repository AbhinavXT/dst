#include "namemap.h"

#include <QDebug>
#include <QFile>
#include <QStringList>
#include <QTextStream>

bool NameMap::loadFromFile(const QString &path)
{
    m_map.clear();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Missing file is a perfectly normal case (operator hasn't set one
        // up yet). We log it once at debug level but don't warn — warning
        // would create noise in the console for every fresh install.
        qDebug() << "NameMap: no friendly-names file at" << path
                 << "(falling back to raw source_id_kvchId form)";
        return false;
    }

    QTextStream in(&f);
    int lineNo = 0;
    while (!in.atEnd()) {
        ++lineNo;
        QString line = in.readLine().trimmed();

        // Skip blanks and comment lines.
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        // Split on the first comma. Using split(',') with a parts limit of 2
        // means a friendly name that itself contains a comma is preserved.
        const int comma = line.indexOf(',');
        if (comma < 0) {
            qWarning() << "NameMap: line" << lineNo
                       << "in" << path
                       << "has no comma — skipped:" << line;
            continue;
        }

        const QString keyToken  = line.left(comma).trimmed();
        QString nameToken = line.mid(comma + 1).trimmed();

        // Take only up to the NEXT comma. Real friendly_names.csv rows are
        // written by spreadsheets that pad every line to the widest row, so
        // "21_1,L1_V1," is normal and mid(comma+1) swallows the trailing
        // delimiter — producing the name "L1_V1," which then appears in
        // every log line, every tab title and every saved filename.
        const int nextComma = nameToken.indexOf(QLatin1Char(','));
        if (nextComma >= 0) nameToken = nameToken.left(nextComma);
        nameToken = nameToken.trimmed();
        if (keyToken.isEmpty() || nameToken.isEmpty()) {
            qWarning() << "NameMap: line" << lineNo
                       << "has empty key or name — skipped:" << line;
            continue;
        }

        // The key is itself "source_id_kvchId" with the source_id half
        // possibly in hex. Normalise it to the same form lookup() builds:
        //   decimal_source_id "_" decimal_kvchId.
        const int underscore = keyToken.indexOf('_');
        if (underscore < 0) {
            qWarning() << "NameMap: line" << lineNo
                       << "key has no underscore — skipped:" << line;
            continue;
        }
        const QString srcStr  = keyToken.left(underscore).trimmed();
        const QString kvchStr = keyToken.mid(underscore + 1).trimmed();

        const int srcId = parseSourceId(srcStr);
        if (srcId < 0 || srcId > 0xFF) {
            qWarning() << "NameMap: line" << lineNo
                       << "has invalid source_id" << srcStr;
            continue;
        }
        bool kvchOk = false;
        const int kvch = kvchStr.toInt(&kvchOk);
        if (!kvchOk || kvch < 0 || kvch > 0xFFFF) {
            qWarning() << "NameMap: line" << lineNo
                       << "has invalid kvchId" << kvchStr;
            continue;
        }

        // Normalised key in the same shape lookup() uses.
        const QString normalisedKey = QString("%1_%2").arg(srcId).arg(kvch);
        m_map.insert(normalisedKey, nameToken);
    }

    qDebug() << "NameMap: loaded" << m_map.size()
             << "friendly names from" << path;
    return true;
}

QString NameMap::lookup(quint8 sourceId, quint16 kvchId) const
{
    const QString key = QString("%1_%2")
                            .arg(static_cast<int>(sourceId))
                            .arg(static_cast<int>(kvchId));
    return lookupByKey(key);
}

bool NameMap::matchesFilter(const QString &friendlyName,
                            const QString &tabKey,
                            const QString &needle)
{
    const QString n = needle.trimmed();
    if (n.isEmpty()) return true;          // empty filter shows everything
    return friendlyName.contains(n, Qt::CaseInsensitive)
        || tabKey.contains(n, Qt::CaseInsensitive);
}

QString NameMap::lookupByKey(const QString &rawKey) const
{
    auto it = m_map.constFind(rawKey);
    return (it != m_map.constEnd()) ? it.value() : rawKey;
}

int NameMap::parseSourceId(const QString &token)
{
    // Accept "21", "0x21", "21h", and bare hex like "8f" / "FF".
    QString t = token.trimmed().toLower();
    if (t.isEmpty()) return -1;

    bool ok = false;

    // Prefix or suffix forms — explicit hex.
    if (t.startsWith("0x")) {
        const int v = t.mid(2).toInt(&ok, 16);
        return ok ? v : -1;
    }
    if (t.endsWith('h')) {
        const int v = t.left(t.size() - 1).toInt(&ok, 16);
        return ok ? v : -1;
    }

    // Try decimal first.
    int v = t.toInt(&ok, 10);
    if (ok) return v;

    // Fall back to hex if it looks hex-ish (contains a-f). This catches
    // bare-hex entries like "8f" or "FF" without the user having to add a
    // prefix.
    for (QChar c : t) {
        if (c >= 'a' && c <= 'f') {
            v = t.toInt(&ok, 16);
            return ok ? v : -1;
        }
    }
    return -1;
}

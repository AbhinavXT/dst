#include "xlsxbook.h"

#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QXmlStreamReader>

#include <cmath>

namespace XlsxBook {
namespace {

// ---- inflate (RFC 1951), after zlib's puff.c ----------------------------------

struct BitIn {
    const uchar *p = nullptr;
    int len = 0, pos = 0;
    quint32 buf = 0;
    int cnt = 0;
    bool bad = false;
    int bits(int need)
    {
        quint32 v = buf;
        while (cnt < need) {
            if (pos >= len) { bad = true; return 0; }
            v |= quint32(p[pos++]) << cnt;
            cnt += 8;
        }
        buf = v >> need;
        cnt -= need;
        return int(v & ((1u << need) - 1));
    }
};

struct Huff {
    short count[16] = {};
    short symbol[288] = {};
};

// < 0 over-subscribed, 0 complete, > 0 incomplete (accepted, as zlib does
// for a single-code distance table).
int construct(Huff &h, const short *length, int n)
{
    for (short &c : h.count) c = 0;
    for (int s = 0; s < n; ++s) h.count[length[s]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len < 16; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }
    short offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; ++len) offs[len + 1] = short(offs[len] + h.count[len]);
    for (int s = 0; s < n; ++s)
        if (length[s] != 0) h.symbol[offs[length[s]]++] = short(s);
    return left;
}

int decode(BitIn &s, const Huff &h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= s.bits(1);
        if (s.bad) return -1;
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

const short kLenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                             35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const short kLenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                              3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const short kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                              257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                              8193, 12289, 16385, 24577 };
const short kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                               7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

bool codes(BitIn &s, QByteArray &out, const Huff &lens, const Huff &dists)
{
    for (;;) {
        int sym = decode(s, lens);
        if (sym < 0) return false;
        if (sym < 256) { out.append(char(sym)); continue; }
        if (sym == 256) return true;
        sym -= 257;
        if (sym >= 29) return false;
        const int len = kLenBase[sym] + s.bits(kLenExtra[sym]);
        const int ds = decode(s, dists);
        if (ds < 0 || ds >= 30) return false;
        const int dist = kDistBase[ds] + s.bits(kDistExtra[ds]);
        if (s.bad || dist > out.size()) return false;
        const int from = out.size() - dist;
        for (int i = 0; i < len; ++i) out.append(out.at(from + i));   // may overlap: byte by byte
    }
}

bool stored(BitIn &s, QByteArray &out)
{
    s.buf = 0;
    s.cnt = 0;                       // to a byte boundary
    if (s.pos + 4 > s.len) return false;
    const int len = s.p[s.pos] | (s.p[s.pos + 1] << 8);
    const int nlen = s.p[s.pos + 2] | (s.p[s.pos + 3] << 8);
    s.pos += 4;
    if (len != (~nlen & 0xffff) || s.pos + len > s.len) return false;
    out.append(reinterpret_cast<const char *>(s.p + s.pos), len);
    s.pos += len;
    return true;
}

bool fixed(BitIn &s, QByteArray &out)
{
    static Huff lens, dists;
    static bool built = false;
    if (!built) {
        short l[288];
        int i = 0;
        for (; i < 144; ++i) l[i] = 8;
        for (; i < 256; ++i) l[i] = 9;
        for (; i < 280; ++i) l[i] = 7;
        for (; i < 288; ++i) l[i] = 8;
        construct(lens, l, 288);
        for (i = 0; i < 30; ++i) l[i] = 5;
        construct(dists, l, 30);
        built = true;
    }
    return codes(s, out, lens, dists);
}

bool dynamic(BitIn &s, QByteArray &out)
{
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    const int nlen = s.bits(5) + 257, ndist = s.bits(5) + 1, ncode = s.bits(4) + 4;
    if (s.bad || nlen > 286 || ndist > 30) return false;
    short lengths[320] = {};
    for (int i = 0; i < ncode; ++i) lengths[order[i]] = short(s.bits(3));
    Huff lens, dists;
    if (construct(lens, lengths, 19) != 0) return false;   // the code-length code must be complete
    int index = 0;
    while (index < nlen + ndist) {
        int sym = decode(s, lens);
        if (sym < 0) return false;
        if (sym < 16) { lengths[index++] = short(sym); continue; }
        short len = 0;
        if (sym == 16) {
            if (index == 0) return false;
            len = lengths[index - 1];
            sym = 3 + s.bits(2);
        } else if (sym == 17) {
            sym = 3 + s.bits(3);
        } else {
            sym = 11 + s.bits(7);
        }
        if (s.bad || index + sym > nlen + ndist) return false;
        while (sym--) lengths[index++] = len;
    }
    if (lengths[256] == 0) return false;   // no end-of-block code
    if (construct(lens, lengths, nlen) < 0) return false;
    if (construct(dists, lengths + nlen, ndist) < 0) return false;
    return codes(s, out, lens, dists);
}

// ---- zip ------------------------------------------------------------------------

quint32 le32(const QByteArray &b, int at)
{
    const uchar *p = reinterpret_cast<const uchar *>(b.constData()) + at;
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}
quint16 le16(const QByteArray &b, int at)
{
    const uchar *p = reinterpret_cast<const uchar *>(b.constData()) + at;
    return quint16(p[0] | (p[1] << 8));
}
void put16(QByteArray &b, quint16 v) { b.append(char(v & 0xff)); b.append(char(v >> 8)); }
void put32(QByteArray &b, quint32 v) { put16(b, quint16(v & 0xffff)); put16(b, quint16(v >> 16)); }

// name -> contents of every entry. Empty and *err set on failure.
QHash<QString, QByteArray> unzip(const QByteArray &z, QString *err)
{
    QHash<QString, QByteArray> files;
    int eocd = -1;
    for (int i = z.size() - 22; i >= 0 && i >= z.size() - 22 - 65535; --i)
        if (le32(z, i) == 0x06054b50u) { eocd = i; break; }
    if (eocd < 0) { if (err) *err = QStringLiteral("not a zip file (no end of central directory)"); return {}; }
    const int entries = le16(z, eocd + 10);
    int at = int(le32(z, eocd + 16));
    for (int e = 0; e < entries; ++e) {
        if (at < 0 || at + 46 > z.size() || le32(z, at) != 0x02014b50u) {
            if (err) *err = QStringLiteral("damaged zip central directory");
            return {};
        }
        const int method = le16(z, at + 10);
        const quint32 crc = le32(z, at + 16);
        const qint64 csize = le32(z, at + 20), usize = le32(z, at + 24);
        const int nameLen = le16(z, at + 28), extraLen = le16(z, at + 30), commentLen = le16(z, at + 32);
        const qint64 local = le32(z, at + 42);
        const QString name = QString::fromUtf8(z.mid(at + 46, nameLen));
        at += 46 + nameLen + extraLen + commentLen;
        if (csize == 0xffffffffLL || local + 30 > z.size() || le32(z, int(local)) != 0x04034b50u) {
            if (err) *err = QStringLiteral("unsupported or damaged zip entry %1").arg(name);
            return {};
        }
        const qint64 data = local + 30 + le16(z, int(local) + 26) + le16(z, int(local) + 28);
        if (data + csize > z.size()) { if (err) *err = QStringLiteral("zip entry %1 runs past the end").arg(name); return {}; }
        const QByteArray raw = z.mid(int(data), int(csize));
        QByteArray content;
        if (method == 0) content = raw;
        else if (method == 8) {
            bool ok = false;
            content = inflate(raw, &ok);
            if (!ok) { if (err) *err = QStringLiteral("zip entry %1 does not inflate").arg(name); return {}; }
        } else {
            if (err) *err = QStringLiteral("zip entry %1: compression method %2 not supported").arg(name).arg(method);
            return {};
        }
        if (content.size() != usize || crc32(content) != crc) {
            if (err) *err = QStringLiteral("zip entry %1 fails its CRC").arg(name);
            return {};
        }
        files.insert(name, content);
    }
    return files;
}

QByteArray zip(const QVector<QPair<QString, QByteArray>> &files)
{
    QByteArray out, central;
    for (const auto &f : files) {
        const QByteArray name = f.first.toUtf8();
        const quint32 crc = crc32(f.second);
        const quint32 offset = quint32(out.size());
        auto header = [&](QByteArray &b, bool isCentral) {
            put32(b, isCentral ? 0x02014b50u : 0x04034b50u);
            if (isCentral) put16(b, 20);          // version made by
            put16(b, 20);                         // version needed
            put16(b, 0);                          // flags
            put16(b, 0);                          // stored
            put16(b, 0);                          // time 00:00
            put16(b, 0x21);                       // date 1980-01-01: a fixed stamp, so equal input gives equal files
            put32(b, crc);
            put32(b, quint32(f.second.size()));
            put32(b, quint32(f.second.size()));
            put16(b, quint16(name.size()));
            put16(b, 0);                          // extra
            if (isCentral) {
                put16(b, 0); put16(b, 0); put16(b, 0);   // comment, disk, internal attributes
                put32(b, 0);                              // external attributes
                put32(b, offset);
            }
            b.append(name);
        };
        header(out, false);
        out.append(f.second);
        header(central, true);
    }
    const quint32 cdOffset = quint32(out.size());
    out.append(central);
    put32(out, 0x06054b50u);
    put16(out, 0); put16(out, 0);
    put16(out, quint16(files.size())); put16(out, quint16(files.size()));
    put32(out, quint32(central.size()));
    put32(out, cdOffset);
    put16(out, 0);
    return out;
}

// ---- spreadsheet xml ------------------------------------------------------------

QString attr(const QXmlStreamReader &x, const char *localName)
{
    for (const QXmlStreamAttribute &a : x.attributes())
        if (a.name() == QLatin1String(localName)) return a.value().toString();
    return QString();
}

QStringList sharedStrings(const QByteArray &xml)
{
    QStringList out;
    QXmlStreamReader x(xml);
    QString cur;
    bool inSi = false;
    int phonetic = 0;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement()) {
            if (x.name() == QLatin1String("si")) { inSi = true; cur.clear(); }
            else if (x.name() == QLatin1String("rPh")) ++phonetic;
            else if (x.name() == QLatin1String("t") && inSi && phonetic == 0) cur += x.readElementText();
        } else if (x.isEndElement()) {
            if (x.name() == QLatin1String("si")) { out << cur; inSi = false; }
            else if (x.name() == QLatin1String("rPh")) --phonetic;
        }
    }
    return out;
}

// "BC12" -> column 54 (0-based); -1 if there are no letters.
int columnOf(const QString &ref)
{
    int col = 0, n = 0;
    for (const QChar ch : ref) {
        if (ch < QLatin1Char('A') || ch > QLatin1Char('Z')) break;
        col = col * 26 + (ch.unicode() - 'A' + 1);
        ++n;
    }
    return n ? col - 1 : -1;
}

QVariant number(const QString &v)
{
    bool ok = false;
    const double d = v.toDouble(&ok);
    if (!ok) return v;
    if (std::floor(d) == d && std::fabs(d) < 9007199254740992.0) return qlonglong(d);
    return d;
}

QVector<Row> sheetRows(const QByteArray &xml, const QStringList &shared)
{
    QVector<Row> rows;
    QXmlStreamReader x(xml);
    int rowIdx = -1, colIdx = -1;
    QString type, value;
    bool inCell = false, haveValue = false;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement()) {
            if (x.name() == QLatin1String("row")) {
                const int r = attr(x, "r").toInt();
                rowIdx = r > 0 ? r - 1 : rowIdx + 1;
                colIdx = -1;
            } else if (x.name() == QLatin1String("c")) {
                const int c = columnOf(attr(x, "r"));
                colIdx = c >= 0 ? c : colIdx + 1;
                type = attr(x, "t");
                value.clear();
                inCell = true;
                haveValue = false;
            } else if (inCell && x.name() == QLatin1String("v")) {
                value = x.readElementText();
                haveValue = true;
            } else if (inCell && x.name() == QLatin1String("t")) {   // <is><t> inline string
                value += x.readElementText();
                haveValue = true;
            }
        } else if (x.isEndElement() && x.name() == QLatin1String("c")) {
            inCell = false;
            if (!haveValue || rowIdx < 0 || colIdx < 0) continue;
            QVariant cell;
            if (type == QLatin1String("s")) cell = shared.value(value.toInt());
            else if (type == QLatin1String("inlineStr") || type == QLatin1String("str")
                     || type == QLatin1String("e") || type == QLatin1String("d")) cell = value;
            else cell = number(value);   // "n", "b" or none
            if (cell.toString().isEmpty() && cell.userType() == QMetaType::QString) continue;
            if (rows.size() <= rowIdx) rows.resize(rowIdx + 1);
            Row &row = rows[rowIdx];
            if (row.size() <= colIdx) row.resize(colIdx + 1);
            row[colIdx] = cell;
        }
    }
    return rows;
}

QString escape(const QString &s)
{
    QString o;
    o.reserve(s.size());
    for (const QChar ch : s) {
        switch (ch.unicode()) {
        case '&': o += QLatin1String("&amp;"); break;
        case '<': o += QLatin1String("&lt;"); break;
        case '>': o += QLatin1String("&gt;"); break;
        case '"': o += QLatin1String("&quot;"); break;
        default:
            if (ch.unicode() < 0x20 && ch != QLatin1Char('\t') && ch != QLatin1Char('\n') && ch != QLatin1Char('\r')) break;   // not allowed in XML 1.0
            o += ch;
        }
    }
    return o;
}

}  // namespace

QByteArray inflate(const QByteArray &raw, bool *ok)
{
    BitIn s;
    s.p = reinterpret_cast<const uchar *>(raw.constData());
    s.len = raw.size();
    QByteArray out;
    bool good = true;
    int last = 0;
    do {
        last = s.bits(1);
        const int type = s.bits(2);
        if (s.bad) { good = false; break; }
        if (type == 0) good = stored(s, out);
        else if (type == 1) good = fixed(s, out);
        else if (type == 2) good = dynamic(s, out);
        else good = false;
    } while (good && !last);
    if (ok) *ok = good && !s.bad;
    return good ? out : QByteArray();
}

quint32 crc32(const QByteArray &data)
{
    static quint32 table[256];
    static bool built = false;
    if (!built) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        built = true;
    }
    quint32 c = 0xFFFFFFFFu;
    for (const char ch : data) c = table[(c ^ uchar(ch)) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

QString columnName(int index)
{
    QString s;
    for (int n = index + 1; n > 0; n = (n - 1) / 26) s.prepend(QChar('A' + (n - 1) % 26));
    return s;
}

QVector<Sheet> read(const QByteArray &xlsx, QString *err)
{
    const QHash<QString, QByteArray> files = unzip(xlsx, err);
    if (files.isEmpty()) return {};
    if (!files.contains(QStringLiteral("xl/workbook.xml"))) {
        if (err) *err = QStringLiteral("not a spreadsheet (no xl/workbook.xml)");
        return {};
    }
    // relationship id -> part name
    QHash<QString, QString> target;
    {
        QXmlStreamReader x(files.value(QStringLiteral("xl/_rels/workbook.xml.rels")));
        while (!x.atEnd()) {
            x.readNext();
            if (!x.isStartElement() || x.name() != QLatin1String("Relationship")) continue;
            QString t = attr(x, "Target");
            t = t.startsWith(QLatin1Char('/')) ? t.mid(1) : QStringLiteral("xl/") + t;
            target.insert(attr(x, "Id"), t);
        }
    }
    const QStringList shared = sharedStrings(files.value(QStringLiteral("xl/sharedStrings.xml")));
    QVector<Sheet> sheets;
    QXmlStreamReader x(files.value(QStringLiteral("xl/workbook.xml")));
    while (!x.atEnd()) {
        x.readNext();
        if (!x.isStartElement() || x.name() != QLatin1String("sheet")) continue;
        Sheet sh;
        sh.name = attr(x, "name");
        const QString part = target.value(attr(x, "id"));
        sh.rows = sheetRows(files.value(part), shared);
        sheets << sh;
    }
    if (sheets.isEmpty() && err) *err = QStringLiteral("the workbook lists no sheets");
    return sheets;
}

QVector<Sheet> readFile(const QString &path, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("cannot open %1: %2").arg(path, f.errorString());
        return {};
    }
    return read(f.readAll(), err);
}

QByteArray write(const QVector<Sheet> &sheets)
{
    const QString head = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    const QString main = QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main");
    const QString rel = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");
    QVector<QPair<QString, QByteArray>> files;

    QString types = head + QStringLiteral("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>");
    QString book = head + QStringLiteral("<workbook xmlns=\"%1\" xmlns:r=\"%2\"><sheets>").arg(main, rel);
    QString bookRels = head + QStringLiteral("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (int i = 0; i < sheets.size(); ++i) {
        const int n = i + 1;
        types += QStringLiteral("<Override PartName=\"/xl/worksheets/sheet%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>").arg(n);
        book += QStringLiteral("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%2\"/>").arg(escape(sheets.at(i).name)).arg(n);
        bookRels += QStringLiteral("<Relationship Id=\"rId%1\" Type=\"%2/worksheet\" Target=\"worksheets/sheet%1.xml\"/>").arg(n).arg(rel);

        QString xml = head + QStringLiteral("<worksheet xmlns=\"%1\"><sheetData>").arg(main);
        const QVector<Row> &rows = sheets.at(i).rows;
        for (int r = 0; r < rows.size(); ++r) {
            xml += QStringLiteral("<row r=\"%1\">").arg(r + 1);
            for (int c = 0; c < rows.at(r).size(); ++c) {
                const QVariant &v = rows.at(r).at(c);
                if (!v.isValid() || v.isNull()) continue;
                const QString ref = columnName(c) + QString::number(r + 1);
                if (v.userType() == QMetaType::LongLong || v.userType() == QMetaType::Int || v.userType() == QMetaType::UInt
                    || v.userType() == QMetaType::ULongLong) {
                    xml += QStringLiteral("<c r=\"%1\"><v>%2</v></c>").arg(ref, QString::number(v.toLongLong()));
                } else if (v.userType() == QMetaType::Double) {
                    xml += QStringLiteral("<c r=\"%1\"><v>%2</v></c>").arg(ref, QString::number(v.toDouble(), 'g', 17));
                } else {
                    const QString s = v.toString();
                    if (s.isEmpty()) continue;
                    const bool keep = s.at(0).isSpace() || s.at(s.size() - 1).isSpace();
                    xml += QStringLiteral("<c r=\"%1\" t=\"inlineStr\"><is><t%2>%3</t></is></c>")
                               .arg(ref, keep ? QStringLiteral(" xml:space=\"preserve\"") : QString(), escape(s));
                }
            }
            xml += QStringLiteral("</row>");
        }
        xml += QStringLiteral("</sheetData></worksheet>");
        files << qMakePair(QStringLiteral("xl/worksheets/sheet%1.xml").arg(n), xml.toUtf8());
    }
    types += QStringLiteral("</Types>");
    book += QStringLiteral("</sheets></workbook>");
    bookRels += QStringLiteral("</Relationships>");
    const QString rootRels = head + QStringLiteral("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"%1/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>").arg(rel);

    files.prepend(qMakePair(QStringLiteral("xl/_rels/workbook.xml.rels"), bookRels.toUtf8()));
    files.prepend(qMakePair(QStringLiteral("xl/workbook.xml"), book.toUtf8()));
    files.prepend(qMakePair(QStringLiteral("_rels/.rels"), rootRels.toUtf8()));
    files.prepend(qMakePair(QStringLiteral("[Content_Types].xml"), types.toUtf8()));
    return zip(files);
}

bool writeFile(const QString &path, const QVector<Sheet> &sheets, QString *err)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (err) *err = QStringLiteral("cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    f.write(write(sheets));
    if (!f.commit()) {
        if (err) *err = QStringLiteral("cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

}  // namespace XlsxBook

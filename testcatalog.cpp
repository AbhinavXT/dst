#include "testcatalog.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace TestCatalog {

namespace {

const char kFormat[] = "dlconsole-test-catalog";

QVector<int> intList(const QJsonValue &v)
{
    QVector<int> out;
    for (const QJsonValue &x : v.toArray()) out << x.toInt();
    return out;
}

QJsonArray jsonList(const QVector<int> &v)
{
    QJsonArray a;
    for (int x : v) a.append(x);
    return a;
}

void putText(QJsonObject &o, const char *key, const QString &v)
{
    if (!v.isEmpty()) o.insert(QLatin1String(key), v);
}

} // namespace

bool Section::operator==(const Section &o) const
{
    return no == o.no && code == o.code && title == o.title && description == o.description
           && heading == o.heading && notes == o.notes && page == o.page;
}

bool Case::operator==(const Case &o) const
{
    return id == o.id && title == o.title && purpose == o.purpose && methodology == o.methodology
           && setup == o.setup && input == o.input && expected == o.expected && result == o.result
           && observed == o.observed && inputLabel == o.inputLabel && expectedLabel == o.expectedLabel
           && notes == o.notes && page == o.page && pages == o.pages && figurePages == o.figurePages;
}

QString builtinPath()
{
    return QStringLiteral(":/test_catalog/sif0533_v4_22.json");
}

const Catalog &builtin()
{
    static const Catalog c = []() {
        Catalog k;
        load(builtinPath(), &k);
        return k;
    }();
    return c;
}

bool load(const QString &path, Catalog *out, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (err) *err = QStringLiteral("not JSON: %1 at byte %2").arg(pe.errorString()).arg(pe.offset);
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QLatin1String("format")).toString() != QLatin1String(kFormat)) {
        if (err) *err = QStringLiteral("not a DLConsole test case catalog");
        return false;
    }
    Catalog c;
    c.source = root.value(QLatin1String("source")).toString();
    for (const QJsonValue &v : root.value(QLatin1String("sections")).toArray()) {
        const QJsonObject o = v.toObject();
        Section s;
        s.no = o.value(QLatin1String("no")).toInt();
        s.code = o.value(QLatin1String("code")).toString();
        s.title = o.value(QLatin1String("title")).toString();
        s.description = o.value(QLatin1String("description")).toString();
        s.heading = o.value(QLatin1String("heading")).toString();
        s.notes = o.value(QLatin1String("notes")).toString();
        s.page = o.value(QLatin1String("page")).toInt();
        if (s.no > 0) c.sections << s;
    }
    QSet<QString> seen;
    for (const QJsonValue &v : root.value(QLatin1String("cases")).toArray()) {
        const QJsonObject o = v.toObject();
        Case k;
        k.id = o.value(QLatin1String("id")).toString();
        if (!validId(k.id) || seen.contains(k.id)) {
            if (err) *err = QStringLiteral("clause \"%1\": %2").arg(k.id, validId(k.id) ? QStringLiteral("listed twice")
                                                                                        : QStringLiteral("not a clause number"));
            return false;
        }
        seen.insert(k.id);
        k.title = o.value(QLatin1String("title")).toString();
        k.purpose = o.value(QLatin1String("purpose")).toString();
        k.methodology = o.value(QLatin1String("methodology")).toString();
        k.setup = o.value(QLatin1String("setup")).toString();
        k.input = o.value(QLatin1String("input")).toString();
        k.expected = o.value(QLatin1String("expected")).toString();
        k.result = o.value(QLatin1String("result")).toString();
        k.observed = o.value(QLatin1String("observed")).toString();
        k.inputLabel = o.value(QLatin1String("input_label")).toString();
        k.expectedLabel = o.value(QLatin1String("expected_label")).toString();
        k.notes = o.value(QLatin1String("notes")).toString();
        k.page = o.value(QLatin1String("page")).toInt();
        k.pages = intList(o.value(QLatin1String("pages")));
        k.figurePages = intList(o.value(QLatin1String("figure_pages")));
        c.cases << k;
    }
    sort(&c);
    *out = c;
    return true;
}

bool save(const QString &path, const Catalog &c, QString *err)
{
    QJsonArray sections;
    for (const Section &s : c.sections) {
        QJsonObject o;
        o.insert(QLatin1String("no"), s.no);
        putText(o, "code", s.code);
        putText(o, "title", s.title);
        putText(o, "description", s.description);
        putText(o, "heading", s.heading);
        putText(o, "notes", s.notes);
        if (s.page) o.insert(QLatin1String("page"), s.page);
        sections.append(o);
    }
    QJsonArray cases;
    for (const Case &k : c.cases) {
        QJsonObject o;
        o.insert(QLatin1String("id"), k.id);
        if (k.page) o.insert(QLatin1String("page"), k.page);
        putText(o, "title", k.title);
        putText(o, "purpose", k.purpose);
        putText(o, "methodology", k.methodology);
        putText(o, "setup", k.setup);
        putText(o, "input", k.input);
        putText(o, "expected", k.expected);
        putText(o, "result", k.result);
        putText(o, "observed", k.observed);
        putText(o, "input_label", k.inputLabel);
        putText(o, "expected_label", k.expectedLabel);
        putText(o, "notes", k.notes);
        if (!k.pages.isEmpty()) o.insert(QLatin1String("pages"), jsonList(k.pages));
        if (!k.figurePages.isEmpty()) o.insert(QLatin1String("figure_pages"), jsonList(k.figurePages));
        cases.append(o);
    }
    QJsonObject root;
    root.insert(QLatin1String("format"), QLatin1String(kFormat));
    root.insert(QLatin1String("version"), 1);
    root.insert(QLatin1String("source"), c.source);
    root.insert(QLatin1String("sections"), sections);
    root.insert(QLatin1String("cases"), cases);
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

// ---- ids --------------------------------------------------------------------

bool validId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[1-9]\\d{0,2}\\.[1-9]\\d{0,3}(\\.[1-9]\\d{0,3})?$"));
    return re.match(id).hasMatch();
}

QVector<int> idParts(const QString &id)
{
    QVector<int> out;
    for (const QString &p : id.split(QLatin1Char('.'))) out << p.toInt();
    return out;
}

int compareIds(const QString &a, const QString &b)
{
    const QVector<int> x = idParts(a), y = idParts(b);
    for (int i = 0; i < std::min(x.size(), y.size()); ++i)
        if (x.at(i) != y.at(i)) return x.at(i) < y.at(i) ? -1 : 1;
    return x.size() == y.size() ? 0 : (x.size() < y.size() ? -1 : 1);
}

int sectionOf(const QString &id)
{
    return id.section(QLatin1Char('.'), 0, 0).toInt();
}

QString parentOf(const QString &id)
{
    return depth(id) == 2 ? id.section(QLatin1Char('.'), 0, 1) : QString();
}

int depth(const QString &id)
{
    return id.count(QLatin1Char('.'));
}

void sort(Catalog *c)
{
    std::stable_sort(c->cases.begin(), c->cases.end(),
                     [](const Case &a, const Case &b) { return compareIds(a.id, b.id) < 0; });
    std::stable_sort(c->sections.begin(), c->sections.end(),
                     [](const Section &a, const Section &b) { return a.no < b.no; });
}

int indexOf(const Catalog &c, const QString &id)
{
    for (int i = 0; i < c.cases.size(); ++i)
        if (c.cases.at(i).id == id) return i;
    return -1;
}

int sectionIndex(const Catalog &c, int no)
{
    for (int i = 0; i < c.sections.size(); ++i)
        if (c.sections.at(i).no == no) return i;
    return -1;
}

const Section *section(const Catalog &c, int no)
{
    const int i = sectionIndex(c, no);
    return i < 0 ? nullptr : &c.sections.at(i);
}

QStringList childrenOf(const Catalog &c, const QString &id)
{
    QStringList out;
    for (const Case &k : c.cases)
        if (parentOf(k.id) == id) out << k.id;
    return out;
}

QString nextId(const Catalog &c, const QString &under)
{
    const bool inSection = !under.contains(QLatin1Char('.'));
    int top = 0;
    for (const Case &k : c.cases) {
        const QVector<int> p = idParts(k.id);
        if (inSection ? (p.size() == 2 && p.at(0) == under.toInt()) : parentOf(k.id) == under)
            top = std::max(top, p.last());
    }
    return under + QLatin1Char('.') + QString::number(top + 1);
}

bool rename(Catalog *c, const QString &from, const QString &to, QString *why)
{
    auto refuse = [why](const QString &w) { if (why) *why = w; return false; };
    if (from == to) return true;
    if (indexOf(*c, from) < 0) return refuse(QStringLiteral("there is no clause %1").arg(from));
    if (!validId(to)) return refuse(QStringLiteral("\"%1\" is not a clause number (4.93, or 31.6.1 under 31.6)").arg(to));
    const QStringList kids = childrenOf(*c, from);
    if (!kids.isEmpty() && depth(to) == 2)
        return refuse(QStringLiteral("%1 has sub-clauses, so it cannot become a sub-clause itself").arg(from));
    if (to.startsWith(from + QLatin1Char('.'))) return refuse(QStringLiteral("a clause cannot go under itself"));
    QHash<QString, QString> moves { { from, to } };
    for (const QString &k : kids) moves.insert(k, to + k.mid(from.size()));
    for (auto it = moves.cbegin(); it != moves.cend(); ++it)
        if (!moves.contains(it.value()) && indexOf(*c, it.value()) >= 0)
            return refuse(QStringLiteral("%1 is taken").arg(it.value()));
    for (Case &k : c->cases)
        if (moves.contains(k.id)) k.id = moves.value(k.id);
    sort(c);
    return true;
}

// ---- against the SIF ---------------------------------------------------------

Origin originOf(const Case &k)
{
    const Catalog &b = builtin();
    const int i = indexOf(b, k.id);
    if (i < 0) return Origin::Added;
    return b.cases.at(i) == k ? Origin::Sif : Origin::Changed;
}

QVector<Case> deletedFromSif(const Catalog &c)
{
    QSet<QString> have;
    for (const Case &k : c.cases) have.insert(k.id);
    QVector<Case> out;
    for (const Case &k : builtin().cases)
        if (!have.contains(k.id)) out << k;
    return out;
}

QString searchText(const Case &k)
{
    return QStringList { k.id, k.title, k.purpose, k.methodology, k.setup, k.input, k.expected, k.result,
                         k.observed, k.notes }.join(QLatin1Char('\n'));
}

} // namespace TestCatalog

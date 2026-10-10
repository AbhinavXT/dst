#include "rfidexport.h"

#include <QHash>
#include <QRegularExpression>

namespace RfidExport {

TextFiles textFiles(const RfidTag::Route &r)
{
    TextFiles out;
    struct Row { QString id; bool dup; qint64 loc; int place, tinNom, tinRev; };
    QVector<Row> rows;
    for (const RfidTag::Tag &t : r.tags) {
        const RfidTag::Summary s = RfidTag::summary(t.bytes);
        // tags_sim's tag_placement: the field for types 9 and 10, the type
        // number for 11 and 12 (so never a signal foot).
        const int place = s.type == 9 || s.type == 10 ? s.placement : s.type;
        rows.append({ RfidTag::nameOf(t.bytes), s.duplicate, s.absLoc, place, s.tinNom, s.tinRev });
    }

    QHash<QString, QString> sigName, sigId;
    QStringList footOrder;
    for (const RfidTag::Signal &sg : r.signalList) {
        sigName.insert(sg.footTag, sg.name);
        sigId.insert(sg.footTag, sg.sigId);
        footOrder << sg.footTag;
    }
    auto exitOf = [&](const QString &foot) {
        const int i = footOrder.indexOf(foot);
        return i >= 0 && i + 1 < footOrder.size() ? sigId.value(footOrder.at(i + 1)) : QStringLiteral("#");
    };

    QHash<QString, qint64> loc;
    QHash<QString, QString> dupDir;
    QVector<QStringList> groups;
    QStringList group;
    QString direction = QStringLiteral("1"), entry = QStringLiteral("#"), exit = QStringLiteral("#");
    QString first, last;
    for (int i = 0; i < rows.size(); ++i) {
        const Row &x = rows.at(i);
        const QString type = x.place == 1 || x.place == 2 ? QStringLiteral("2") : QStringLiteral("1");
        if (i + 1 < rows.size()) {
            const Row &n = rows.at(i + 1);
            const bool rising = n.loc > x.loc;
            direction = rising ? QStringLiteral("1") : QStringLiteral("2");
            if (!x.dup) {
                loc.insert(x.id, x.loc);
                if (!dupDir.contains(x.id)) dupDir.insert(x.id, rising ? QStringLiteral("0") : QStringLiteral("1"));
            }
            if (x.id == n.id + QLatin1Char('D')) dupDir.insert(n.id, rising ? QStringLiteral("1") : QStringLiteral("0"));
        } else if (!x.dup) {
            dupDir.insert(x.id, direction == QLatin1String("1") ? QStringLiteral("0") : QStringLiteral("1"));
            loc.insert(x.id, x.loc);
        }
        const int tin = direction == QLatin1String("1") ? x.tinNom : x.tinRev;
        const bool foot = (direction == QLatin1String("1") && x.place == 1) || (direction == QLatin1String("2") && x.place == 2);
        if (foot && !x.dup) {
            if (sigId.contains(x.id)) {
                entry = sigId.value(x.id);
                exit = exitOf(x.id);
                out.sigId << QStringLiteral("~%1,%2,%3,%4,#^^").arg(sigName.value(x.id), sigId.value(x.id)).arg(x.loc).arg(x.id);
            } else {
                out.sigId << QStringLiteral("~#,#,%1,%2,#^^").arg(x.loc).arg(x.id);
            }
            if (!group.isEmpty()) {
                group << x.id;
                groups << group;
                group.clear();
            }
        }
        if (!x.dup) {
            out.rfid << QStringLiteral("~%1,%2,%3,%4,%5,%6,1,0,%7^^")
                            .arg(x.id).arg(tin).arg(type).arg(x.loc).arg(entry, direction, exit);
            group << x.id;
            if (first.isEmpty()) first = x.id;
            last = x.id;
        }
    }
    out.stem = first + QLatin1Char('_') + last + QLatin1Char('_');

    QStringList sums;
    QString e2 = QStringLiteral("#"), x2 = QStringLiteral("#");
    for (const QStringList &g : groups) {
        if (sigId.contains(g.first())) {
            e2 = sigId.value(g.first());
            x2 = exitOf(g.first());
        }
        QString line = QStringLiteral("%1,%2 ,#,0,#,0,#,0,#,0,#, %3").arg(e2, x2).arg(g.size());
        qint64 total = 0;
        for (int k = 0; k < g.size(); ++k) {
            if (k == 0) {
                line += QStringLiteral(", 0,%1,%2").arg(g.at(k), dupDir.value(g.at(k)));
            } else {
                const qint64 d = qAbs(loc.value(g.at(k - 1)) - loc.value(g.at(k)));
                total += d;
                line += QStringLiteral(",%1,%2,%3").arg(d).arg(g.at(k), dupDir.value(g.at(k)));
            }
        }
        out.tagLinkInfo << line;
        sums << QStringLiteral("sum dist:%1 b/w signals with tags: %2").arg(total).arg(g.join(QStringLiteral(", ")));
    }
    out.tagLinkInfo << sums;
    return out;
}

namespace {

// [start, end) of the <route_data route_name="name"> element in `text`, or
// {-1, -1}.
QPair<int, int> blockSpan(const QString &text, const QString &name)
{
    const QRegularExpression open(QStringLiteral("<route_data\\b[^>]*\\broute_name\\s*=\\s*\"%1\"[^>]*>")
                                      .arg(QRegularExpression::escape(name.toHtmlEscaped())));
    const QRegularExpressionMatch m = open.match(text);
    if (!m.hasMatch()) return { -1, -1 };
    if (m.captured().endsWith(QLatin1String("/>"))) return { m.capturedStart(), m.capturedEnd() };
    const int close = text.indexOf(QLatin1String("</route_data>"), m.capturedEnd());
    if (close < 0) return { -1, -1 };
    return { m.capturedStart(), close + int(QStringLiteral("</route_data>").size()) };
}

}  // namespace

QByteArray mergeIntoConfiguration(const QByteArray &config, const RfidTag::Route &r, QString *err, QStringList *notes)
{
    QString e;
    const QByteArray xml = RfidTag::toRouteXml(r, &e);
    if (xml.isEmpty()) {
        if (err) *err = e;
        return {};
    }
    const QVector<RfidTag::Route> had = RfidTag::readXml(config, &e);
    if (had.isEmpty()) {
        if (err) *err = QStringLiteral("not a Configuration1.xml with routes (%1)").arg(e);
        return {};
    }

    // Our two blocks as text, and their names.
    const QString ours = QString::fromUtf8(xml);
    const QVector<RfidTag::Route> mine = RfidTag::readXml(xml);
    QString text = QString::fromUtf8(config);
    int added = 0;
    for (const RfidTag::Route &m : mine) {
        const QPair<int, int> src = blockSpan(ours, m.name);
        const QString block = ours.mid(src.first, src.second - src.first);
        const QPair<int, int> at = blockSpan(text, m.name);
        if (at.first >= 0) {
            text.replace(at.first, at.second - at.first, block);
            if (notes) *notes << QStringLiteral("replaced route %1").arg(m.name);
        } else {
            const int lastClose = text.lastIndexOf(QLatin1String("</route_data>"));
            if (lastClose < 0) {
                if (err) *err = QStringLiteral("it has no </route_data> to put the route after");
                return {};
            }
            const int insertAt = lastClose + int(QStringLiteral("</route_data>").size());
            text.insert(insertAt, QStringLiteral("\n") + block);
            ++added;
            if (notes) *notes << QStringLiteral("added route %1").arg(m.name);
        }
    }

    // Read it back: it must parse, keep every other route, and hold ours.
    const QByteArray out = text.toUtf8();
    const QVector<RfidTag::Route> now = RfidTag::readXml(out, &e);
    int found = 0;
    for (const RfidTag::Route &m : mine)
        for (const RfidTag::Route &n : now)
            if (n.name == m.name && n.tags.size() == m.tags.size()) {
                bool same = true;
                for (int i = 0; i < m.tags.size() && same; ++i) same = n.tags.at(i).bytes == m.tags.at(i).bytes;
                found += same ? 1 : 0;
            }
    if (now.size() != had.size() + added || found < mine.size()) {
        if (err) *err = QStringLiteral("the merged file did not read back as expected (%1 routes, %2 of ours); not written")
                            .arg(now.size()).arg(found);
        return {};
    }
    return out;
}

QByteArray fixCrcs(const QByteArray &config, QStringList *changes, QString *err)
{
    QString text = QString::fromUtf8(config);
    static const QRegularExpression rowRe(QStringLiteral("<rfid_data\\b[^>]*>"));
    static const QRegularExpression routeRe(QStringLiteral("<route_data\\b[^>]*\\broute_name\\s*=\\s*\"([^\"]*)\""));
    static const QRegularExpression pxRe(QStringLiteral("\\bpage_x\\s*=\\s*\"([0-9A-Fa-f]*)\""));
    static const QRegularExpression pyRe(QStringLiteral("\\bpage_y\\s*=\\s*\"([0-9A-Fa-f]*)\""));
    static const QRegularExpression idRe(QStringLiteral("\\brfid_id\\s*=\\s*\"([^\"]*)\""));

    // Find the rows first, then edit from the end so earlier offsets hold.
    struct Edit { int at, len; QString value, line; };
    QVector<Edit> edits;
    int rowsSeen = 0;
    QVector<QPair<int, QString>> routes;           // where each route_data opens, its name
    QRegularExpressionMatchIterator ri = routeRe.globalMatch(text);
    while (ri.hasNext()) {
        const QRegularExpressionMatch m = ri.next();
        routes.append({ int(m.capturedStart()), m.captured(1) });
    }
    QRegularExpressionMatchIterator it = rowRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch row = it.next();
        ++rowsSeen;
        const QString el = row.captured();
        const QRegularExpressionMatch px = pxRe.match(el), py = pyRe.match(el);
        if (!px.hasMatch() || !py.hasMatch()) continue;
        const QByteArray tag = RfidTag::fromPages(px.captured(1), py.captured(1));
        if (tag.isEmpty() || RfidTag::summary(tag).crcOk) continue;
        const QString newY = RfidTag::pageY(RfidTag::fixCrc(tag));
        // The route this row is in: the last route_data opened before it.
        QString route;
        for (int k = routes.size() - 1; k >= 0 && route.isEmpty(); --k)
            if (routes.at(k).first < row.capturedStart()) route = routes.at(k).second;
        edits.append({ int(row.capturedStart() + py.capturedStart(1)), int(py.capturedLength(1)), newY,
                       QStringLiteral("%1: %2  page_y %3 -> %4")
                           .arg(route.isEmpty() ? QStringLiteral("(no route)") : route, idRe.match(el).captured(1),
                                py.captured(1), newY) });
    }
    if (rowsSeen == 0) {
        if (err) *err = QStringLiteral("no <rfid_data> rows in it");
        return {};
    }
    for (int i = edits.size() - 1; i >= 0; --i) text.replace(edits.at(i).at, edits.at(i).len, edits.at(i).value);
    if (changes) for (const Edit &e : edits) *changes << e.line;

    // Read back: every row's CRC passes, and the routes are all still there.
    const QByteArray out = text.toUtf8();
    QString e;
    const QVector<RfidTag::Route> before = RfidTag::readXml(config, &e), after = RfidTag::readXml(out, &e);
    bool allPass = !after.isEmpty();
    for (const RfidTag::Route &r : after)
        for (const RfidTag::Tag &t : r.tags) allPass = allPass && RfidTag::summary(t.bytes).crcOk;
    if (!allPass || after.size() != before.size()) {
        if (err) *err = QStringLiteral("the corrected file did not read back with every CRC passing; not written");
        return {};
    }
    return out;
}

}  // namespace RfidExport

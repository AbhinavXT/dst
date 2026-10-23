#include "fieldcrumb.h"

#include "capturedecoder.h"   // FieldRow

namespace {
int indentOf(const QString &s)
{
    int n = 0;
    while (n < s.size() && s.at(n) == QLatin1Char(' ')) ++n;
    return n;
}

bool isSubpacketHeader(const QString &field)
{
    return field.startsWith(QLatin1String("subpkt["));
}
}  // namespace

QVector<int> FieldCrumb::parents(const QVector<FieldRow> &rows, int row)
{
    QVector<int> out;
    if (row < 0 || row >= rows.size()) return out;
    int cur = indentOf(rows.at(row).field);
    for (int i = row - 1; i >= 0 && cur > 0; --i) {
        const QString &f = rows.at(i).field;
        const int ind = indentOf(f);
        if (ind >= cur) continue;
        if (ind == 0 && !isSubpacketHeader(f)) break;   // the envelope, not a parent
        out.prepend(i);
        cur = ind;
    }
    return out;
}

QString FieldCrumb::label(const FieldRow &r)
{
    QString s = r.field.trimmed();
    if (isSubpacketHeader(s)) {
        const int at = s.indexOf(QLatin1String(" @byte "));
        if (at > 0) s.truncate(at);
    }
    return s;
}

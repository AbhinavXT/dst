#include "locoidentity.h"

#include <QObject>

const char *LocoIdentity::kOwnLocoId = "OWN_LOCO_ID";

void LocoIdentity::observe(const QString &sourceKey, const QString &captype,
                           const QHash<QString, qint64> &values)
{
    if (sourceKey.isEmpty()) { return; }

    // Only frames the loco itself sends. `arprecv` is another loco's ARP
    // arriving at ours, so its SOURCE_LOCO_ID is the other loco — learning
    // from it would teach the wrong identity on every loco-to-loco approach,
    // which is when these rules matter most.
    const bool teaches = (captype == QLatin1String("arp")
                          || captype == QLatin1String("lsrp"));
    if (!teaches) { return; }

    auto it = values.constFind(QStringLiteral("SOURCE_LOCO_ID"));
    if (it == values.constEnd()) { return; }

    // Zero is the unassigned value, not a loco. Recording it would make a
    // source that has only ever carried unassigned frames look identified.
    if (it.value() <= 0) { return; }

    m_seen[sourceKey].insert(it.value());
}

LocoIdentity::Id LocoIdentity::idFor(const QString &sourceKey) const
{
    Id out;
    const auto it = m_seen.constFind(sourceKey);
    if (it == m_seen.constEnd() || it->isEmpty()) { return out; }

    out.seen = it->size();
    if (it->size() == 1) {
        out.state = State::Known;
        out.value = *it->cbegin();
    } else {
        out.state = State::Ambiguous;
    }
    return out;
}

QString LocoIdentity::explain(const QString &sourceKey) const
{
    const Id id = idFor(sourceKey);
    switch (id.state) {
    case State::Known:
        return {};
    case State::Unknown:
        // Said, not left silent: silence reads identically to "checked, and
        // fine", which is the opposite of the truth here.
        return QObject::tr("loco not identified yet — no ARP or LSRP seen on "
                           "this source, so rules that depend on which loco "
                           "this is are not being checked");
    case State::Ambiguous:
        return QObject::tr("%1 locos seen on this source, so which one is "
                           "\"ours\" is undecidable — rules that depend on it "
                           "are not being checked").arg(id.seen);
    }
    return {};
}

QHash<QString, qint64> LocoIdentity::contextFor(const QString &sourceKey) const
{
    QHash<QString, qint64> out;
    const Id id = idFor(sourceKey);
    // Nothing at all when it is not known. Absence is what stops the
    // identity-dependent rules from firing; supplying a sentinel would give
    // a rule something to compare against by accident.
    if (id.state == State::Known) {
        out.insert(QString::fromLatin1(kOwnLocoId), id.value);
    }
    return out;
}

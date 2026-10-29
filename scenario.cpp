#include "scenario.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

namespace Scenario {

namespace {

const char kFormat[] = "dlconsole-scenario";

struct DoInfo { Do d; const char *key; const char *name; };
const DoInfo kDo[] = {
    { Do::Speed,      "speed",      "speed (km/h)" },
    { Do::Accelerate, "accelerate", "accelerate (km/h/s)" },
    { Do::Decelerate, "decelerate", "decelerate (km/h/s)" },
    { Do::Hold,       "hold",       "hold" },
    { Do::SkipTag,    "skipTag",    "skip a tag" },
    { Do::TagFault,   "tagFault",   "a tag's fault" },
    { Do::Reader,     "reader",     "a reader on / off" },
    { Do::AirFault,   "airFault",   "an air fault (bench)" },
    { Do::BiuSilent,  "biuSilent",  "BIU silent (bench)" },
    { Do::BiuFault,   "biuFault",   "BIU fault code (bench)" },
    { Do::Note,       "note",       "a note" },
};

QString whenKey(When w)
{
    switch (w) {
    case When::Time:     return QStringLiteral("time");
    case When::Tag:      return QStringLiteral("tag");
    case When::Location: return QStringLiteral("location");
    }
    return QString();
}

QString num(double v) { return QString::number(v, 'g', 10); }

}  // namespace

QStringList whenNames()
{
    return { QObject::tr("at a time"), QObject::tr("at a tag"), QObject::tr("at a location") };
}

QStringList doNames()
{
    QStringList names;
    for (const DoInfo &i : kDo) names << QObject::tr(i.name);
    return names;
}

QString doKey(Do d)
{
    for (const DoInfo &i : kDo)
        if (i.d == d) return QLatin1String(i.key);
    return QString();
}

bool doFromKey(const QString &key, Do *out)
{
    for (const DoInfo &i : kDo)
        if (key == QLatin1String(i.key)) { *out = i.d; return true; }
    return false;
}

bool isBenchAction(Do d)
{
    return d == Do::AirFault || d == Do::BiuSilent || d == Do::BiuFault;
}

QString describe(const Step &s)
{
    QString when;
    switch (s.when) {
    case When::Time:     when = QObject::tr("at %1 s").arg(num(s.at)); break;
    case When::Tag:      when = QObject::tr("at tag %1").arg(s.tag); break;
    case When::Location: when = QObject::tr("at %1 m").arg(num(s.at)); break;
    }
    QString what;
    switch (s.action) {
    case Do::Speed:      what = QObject::tr("speed %1 km/h").arg(num(s.value)); break;
    case Do::Accelerate: what = QObject::tr("accelerate at %1 km/h/s").arg(num(s.value)); break;
    case Do::Decelerate: what = QObject::tr("decelerate at %1 km/h/s").arg(num(s.value)); break;
    case Do::Hold:       what = QObject::tr("hold"); break;
    case Do::SkipTag:    what = s.value != 0 ? QObject::tr("skip tag %1").arg(s.on) : QObject::tr("send tag %1 again").arg(s.on); break;
    case Do::TagFault:
        what = s.kind == QLatin1String("late") ? QObject::tr("tag %1 late by %2 ms").arg(s.on).arg(num(s.value))
                                               : QObject::tr("tag %1 %2").arg(s.on, s.kind);
        break;
    case Do::Reader:
        what = QObject::tr("reader %1 %2").arg(s.on, s.value != 0 ? QObject::tr("on") : QObject::tr("off"))
               + (s.kind.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(s.kind));
        break;
    case Do::AirFault:
        what = s.kind == QLatin1String("none") ? QObject::tr("%1 fault cleared").arg(s.on)
                                               : QObject::tr("%1 %2").arg(s.on, s.kind)
                                                     + (s.kind == QLatin1String("stuck") ? QString() : QStringLiteral(" %1").arg(num(s.value)));
        break;
    case Do::BiuSilent:  what = s.value != 0 ? QObject::tr("BIU silent") : QObject::tr("BIU talking again"); break;
    case Do::BiuFault:   what = QObject::tr("BIU fault code %1").arg(s.text.isEmpty() ? num(s.value) : s.text); break;
    case Do::Note:       what = s.text; break;
    }
    return QStringLiteral("%1: %2").arg(when, what);
}

bool check(const Script &script, QString *why)
{
    const QStringList tagKinds = { QStringLiteral("none"), QStringLiteral("late"), QStringLiteral("twice"), QStringLiteral("crc") };
    const QStringList airKinds = { QStringLiteral("none"), QStringLiteral("stuck"), QStringLiteral("slow"), QStringLiteral("leak") };
    const QStringList air = { QStringLiteral("BP"), QStringLiteral("BC"), QStringLiteral("MR") };
    for (int i = 0; i < script.steps.size(); ++i) {
        const Step &s = script.steps.at(i);
        QString wrong;
        if (s.when == When::Tag && s.tag.trimmed().isEmpty()) wrong = QObject::tr("at a tag, but no tag named");
        else if ((s.action == Do::SkipTag || s.action == Do::TagFault) && s.on.trimmed().isEmpty()) wrong = QObject::tr("no tag to act on");
        else if (s.action == Do::TagFault && !tagKinds.contains(s.kind)) wrong = QObject::tr("a tag's fault is none, late, twice or crc");
        else if (s.action == Do::Reader && s.on != QLatin1String("1") && s.on != QLatin1String("2")) wrong = QObject::tr("reader 1 or 2");
        else if (s.action == Do::Reader && !s.kind.isEmpty() && s.kind != QLatin1String("ok") && s.kind != QLatin1String("fail"))
            wrong = QObject::tr("a reader's kind is ok or fail (or empty)");
        else if (s.action == Do::AirFault && !air.contains(s.on)) wrong = QObject::tr("an air fault is on BP, BC or MR");
        else if (s.action == Do::AirFault && !airKinds.contains(s.kind)) wrong = QObject::tr("an air fault is none, stuck, slow or leak");
        else if (s.action == Do::BiuFault && !s.text.isEmpty()) {
            bool ok = false;
            s.text.toUInt(&ok, 0);
            if (!ok) wrong = QObject::tr("the fault code \"%1\" is not a number").arg(s.text);
        }
        if (!wrong.isEmpty()) {
            if (why) *why = QObject::tr("Step %1: %2").arg(i + 1).arg(wrong);
            return false;
        }
    }
    return true;
}

QByteArray toJson(const Script &script)
{
    QJsonArray steps;
    for (const Step &s : script.steps) {
        QJsonObject o;
        o.insert(QStringLiteral("when"), whenKey(s.when));
        if (s.when == When::Tag) o.insert(QStringLiteral("tag"), s.tag);
        else o.insert(QStringLiteral("at"), s.at);
        o.insert(QStringLiteral("do"), doKey(s.action));
        if (!s.on.isEmpty()) o.insert(QStringLiteral("on"), s.on);
        if (!s.kind.isEmpty()) o.insert(QStringLiteral("kind"), s.kind);
        o.insert(QStringLiteral("value"), s.value);
        if (!s.text.isEmpty()) o.insert(QStringLiteral("text"), s.text);
        steps.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QLatin1String(kFormat));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("name"), script.name);
    root.insert(QStringLiteral("steps"), steps);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool fromJson(const QByteArray &json, Script *out, QString *why)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    const QJsonObject root = doc.object();
    if (pe.error != QJsonParseError::NoError || root.value(QStringLiteral("format")).toString() != QLatin1String(kFormat)) {
        if (why) *why = pe.error != QJsonParseError::NoError ? pe.errorString() : QObject::tr("not a scenario file");
        return false;
    }
    Script script;
    script.name = root.value(QStringLiteral("name")).toString();
    const QJsonArray steps = root.value(QStringLiteral("steps")).toArray();
    for (int i = 0; i < steps.size(); ++i) {
        const QJsonObject o = steps.at(i).toObject();
        Step s;
        const QString when = o.value(QStringLiteral("when")).toString();
        if (when == QLatin1String("time")) s.when = When::Time;
        else if (when == QLatin1String("tag")) s.when = When::Tag;
        else if (when == QLatin1String("location")) s.when = When::Location;
        else {
            if (why) *why = QObject::tr("Step %1: \"%2\" is not a trigger").arg(i + 1).arg(when);
            return false;
        }
        if (!doFromKey(o.value(QStringLiteral("do")).toString(), &s.action)) {
            if (why) *why = QObject::tr("Step %1: \"%2\" is not an action").arg(i + 1).arg(o.value(QStringLiteral("do")).toString());
            return false;
        }
        s.at = o.value(QStringLiteral("at")).toDouble();
        s.tag = o.value(QStringLiteral("tag")).toString();
        s.on = o.value(QStringLiteral("on")).toString();
        s.kind = o.value(QStringLiteral("kind")).toString();
        s.value = o.value(QStringLiteral("value")).toDouble();
        s.text = o.value(QStringLiteral("text")).toString();
        script.steps.append(s);
    }
    *out = script;
    return true;
}

}  // namespace Scenario

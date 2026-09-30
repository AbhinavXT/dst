#include "colorrules.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

bool ColorRules::loadFromFile(const QString &path)
{
    m_rules.clear();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        loadDefaults();
        return false;
    }

    QJsonParseError err;
    const QByteArray bytes = f.readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning() << "ColorRules: failed to parse" << path
                   << ":" << err.errorString()
                   << "— falling back to defaults";
        loadDefaults();
        return false;
    }

    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        const QString patStr = o.value("pattern").toString();
        if (patStr.isEmpty()) {
            continue;   // silently skip malformed rules
        }
        Rule r;
        QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
        if (!o.value("case_sensitive").toBool()) {
            opts |= QRegularExpression::CaseInsensitiveOption;
        }
        r.pattern = QRegularExpression(patStr, opts);
        if (!r.pattern.isValid()) {
            qWarning() << "ColorRules: invalid regex" << patStr
                       << "—" << r.pattern.errorString();
            continue;
        }
        // QColor accepts CSS color names and #RRGGBB. Empty string yields
        // an invalid QColor, which is exactly "no preference".
        r.fgLight   = QColor(o.value("fg")     .toString());
        r.bgLight   = QColor(o.value("bg")     .toString());
        r.fgDark    = QColor(o.value("fg_dark").toString());
        r.bgDark    = QColor(o.value("bg_dark").toString());
        r.severity  = parseSeverity (o.value("severity") .toString(),
                                     &r.hasSeverity);
        r.direction = parseDirection(o.value("direction").toString(),
                                     &r.hasDirection);

        // Convenience: if dark variants are not specified, fall back to
        // the light ones. This keeps legacy color_rules.json files (no
        // fg_dark / bg_dark fields) producing visible output, even if
        // the colors look a bit off in dark mode. The bundled JSON sets
        // both, so this only affects users with hand-written rules.
        if (!r.fgDark.isValid()) r.fgDark = r.fgLight;
        if (!r.bgDark.isValid()) r.bgDark = r.bgLight;

        // Compile + JIT now rather than lazily on the first message. This
        // runs on the classify() hot path for every entry, and the first
        // call would otherwise pay compilation inside drainBatch().
        r.pattern.optimize();

        m_rules.append(r);
    }

    if (m_rules.isEmpty()) {
        loadDefaults();
        return false;
    }
    return true;
}

void ColorRules::loadDefaults()
{
    // Fallback defaults for when no color_rules.json is present. These
    // mirror the bundled file but are baked in so the app still colors
    // sensibly in a fresh install. Each rule gets both light and dark
    // colors so theme switching works out of the box.
    // sev/dir are pointers: nullptr means "this rule declares nothing about
    // that attribute and classification should keep looking".
    auto add = [&](const QString &pat, bool caseSensitive,
                   const QString &fgL, const QString &bgL,
                   const QString &fgD, const QString &bgD,
                   const Severity *sev, const Direction *dir) {
        Rule r;
        r.pattern = QRegularExpression(
            pat, caseSensitive ? QRegularExpression::NoPatternOption
                               : QRegularExpression::CaseInsensitiveOption);
        r.pattern.optimize();
        r.fgLight   = QColor(fgL);
        r.bgLight   = QColor(bgL);
        r.fgDark    = QColor(fgD);
        r.bgDark    = QColor(bgD);
        if (sev) { r.severity  = *sev; r.hasSeverity  = true; }
        if (dir) { r.direction = *dir; r.hasDirection = true; }
        m_rules.append(r);
    };

    static const Severity  kInfo  = Severity::Info;
    static const Severity  kWarn  = Severity::Warn;
    static const Severity  kError = Severity::Error;
    static const Direction kIn    = Direction::In;
    static const Direction kOut   = Direction::Out;

    // Order = priority for COLORS. Most specific first so RAD+IN gets the
    // RAD palette rather than the generic IN palette. These two rules are
    // purely decorative — they pass nullptr for direction so the \bIN\b /
    // \bOUT\b rules below still get to set the direction tag.
    //                                 light fg     light bg     dark fg      dark bg
    add(R"(\bRAD\b.*\b(IN|OUT)\b)", true,
        "darkblue",  "#ffd0e0",   "#9ec5ff",   "#3a2230",  nullptr, nullptr);
    add(R"(\bCAN\b.*\b(IN|OUT)\b)", true,
        "darkgreen", "#fffacd",   "#9ed29e",   "#2a3320",  nullptr, nullptr);

    // Severity rules. "No Error" must precede "Error" so the negation wins.
    add(R"(\bNo\s+Error\b)", false,
        "grey",      "#f0f0f0",   "#a0a0a0",   "#383838",  &kInfo,  nullptr);
    add(R"(\bError\b)", false,
        "darkred",   "#ffe0e0",   "#ff8080",   "#5a2020",  &kError, nullptr);
    add(R"(\bWarn(ing)?\b)", false,
        "darkorange","#fff0d0",   "#ffb060",   "#4a3010",  &kWarn,  nullptr);

    // Direction rules, deliberately CASE-SENSITIVE: the protocol writes
    // "IN"/"OUT" in caps, and a case-insensitive \bIN\b also matches the
    // English preposition ("timeout in link 1"), which produced a steady
    // trickle of messages falsely tagged inbound.
    add(R"(\bIN\b)", true,
        "darkblue",  "#e0f0ff",   "#7fb3ff",   "#1e2a3c",  &kInfo,  &kIn);
    add(R"(\bOUT\b)", true,
        "darkgreen", "#e0ffe0",   "#7fcc7f",   "#1e3a1e",  &kInfo,  &kOut);
}

ColorRules::RuleMatch ColorRules::classify(const QString &text) const
{
    RuleMatch m;
    bool haveColors = false, haveSeverity = false, haveDirection = false;

    int index = -1;
    for (const Rule &r : m_rules) {
        ++index;
        // Early out once every attribute is resolved — with the default
        // rule set that's usually after 2-3 regexes rather than all 7.
        if (haveColors && haveSeverity && haveDirection) break;

        if (!r.pattern.match(text).hasMatch()) continue;

        m.matched = true;

        if (!haveColors) {
            m.fgLight = r.fgLight;
            m.bgLight = r.bgLight;
            m.fgDark  = r.fgDark;
            m.bgDark  = r.bgDark;
            m.colorRule = index;
            haveColors = true;
        }
        if (!haveSeverity && r.hasSeverity) {
            m.severity      = r.severity;
            m.severityRule  = index;
            haveSeverity = true;
        }
        if (!haveDirection && r.hasDirection) {
            m.direction     = r.direction;
            m.directionRule = index;
            haveDirection = true;
        }
    }
    return m;   // matched=false + defaults if nothing hit
}

QString ColorRules::rulePattern(int index) const
{
    if (index < 0 || index >= m_rules.size()) return QString();
    return m_rules.at(index).pattern.pattern();
}

QString ColorRules::explainClassification(const QString &text) const
{
    const RuleMatch m = classify(text);

    auto sevName = [](Severity s) {
        return s == Severity::Error ? QStringLiteral("error")
             : s == Severity::Warn  ? QStringLiteral("warn")
                                    : QStringLiteral("info");
    };
    auto dirName = [](Direction d) {
        return d == Direction::In  ? QStringLiteral("in")
             : d == Direction::Out ? QStringLiteral("out")
                                   : QStringLiteral("none");
    };
    auto attribution = [this](int idx) {
        return idx < 0 ? QStringLiteral("(default — no rule set it)")
                       : QStringLiteral("rule #%1  %2")
                             .arg(idx).arg(rulePattern(idx));
    };

    QStringList out;
    out << QStringLiteral("severity  = %1     from %2")
               .arg(sevName(m.severity), attribution(m.severityRule));
    out << QStringLiteral("direction = %1     from %2")
               .arg(dirName(m.direction), attribution(m.directionRule));
    out << QStringLiteral("colours   =        from %1")
               .arg(attribution(m.colorRule));

    // Rules that matched but supplied nothing are the interesting ones: a
    // decorative rule sitting above the rule that should have set severity
    // looks identical to a correct classification until you can see that it
    // matched and contributed nothing.
    QStringList alsoMatched;
    for (int i = 0; i < m_rules.size(); ++i) {
        if (i == m.colorRule || i == m.severityRule || i == m.directionRule) {
            continue;
        }
        if (m_rules.at(i).pattern.match(text).hasMatch()) {
            alsoMatched << QStringLiteral("  rule #%1  %2")
                               .arg(i).arg(rulePattern(i));
        }
    }
    if (!alsoMatched.isEmpty()) {
        out << QString();
        out << QStringLiteral("Also matched, but a rule earlier in the file "
                              "had already supplied every attribute:");
        out << alsoMatched;
    }
    if (!m.matched) {
        out << QString();
        out << QStringLiteral("No rule matched this text at all.");
    }
    return out.join(QLatin1Char('\n'));
}

Severity ColorRules::parseSeverity(const QString &s, bool *has)
{
    const QString t = s.trimmed().toLower();
    if (has) *has = !t.isEmpty();
    if (t == "error" || t == "err")     return Severity::Error;
    if (t == "warn"  || t == "warning") return Severity::Warn;
    return Severity::Info;
}

Direction ColorRules::parseDirection(const QString &s, bool *has)
{
    const QString t = s.trimmed().toLower();
    if (has) *has = !t.isEmpty();
    if (t == "in")  return Direction::In;
    if (t == "out") return Direction::Out;
    return Direction::None;
}

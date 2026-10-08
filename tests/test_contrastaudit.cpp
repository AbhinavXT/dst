#include "testutil.h"

#include "uicolors.h"
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPalette>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

// =============================================================================
//  App-wide contrast.
//
//  Two halves, because the failures came in two kinds.
//
//  1. THE PALETTE. Every pair of roles that can end up as text on a surface
//     is checked in every theme (ThemeUtil::all()). The per-module contrast suite only covers
//     the five semantic colours; this covers what Qt itself draws with.
//
//  2. THE SOURCE. Every contrast bug found in this program so far was a
//     literal somewhere the suite did not look — "red" and "darkorange" in
//     the log rows at 4.0:1 and 2.1:1, palette Mid used as text in the
//     startup panel, the field plot and the timeline. No arithmetic test
//     can find those, because the numbers are not in the module. So this
//     reads the source and fails when a new one appears.
//
//     A source-scanning test is unusual and worth justifying: the rule it
//     enforces is one a reviewer would otherwise have to remember on every
//     paint routine, and three separate reviews here did not.
// =============================================================================

namespace {

const double kTextFloor     = 4.5;   // WCAG AA, normal text
const double kDisabledFloor = 3.0;   // greyed out, still has to be read

QString sourceDir()
{
#ifdef DL_SRC_DIR
    return QString::fromLatin1(DL_SRC_DIR);
#else
    return QString();
#endif
}

// Files allowed to hold colour literals, each for a stated reason.
bool literalsAllowed(const QString &file)
{
    return file == QLatin1String("uicolors.cpp")        // where they are defined
        || file == QLatin1String("uicolors.h")
        || file == QLatin1String("theme.h")             // the palettes themselves
        || file == QLatin1String("uistyle.cpp")         // derives from the palette
        // Standalone HTML documents: read in a browser or on paper, with
        // their own white background and no palette to follow.
        || file == QLatin1String("testassertions.cpp")
        || file == QLatin1String("faultpanelwindow.cpp")
        || file == QLatin1String("runreport.cpp")          // session 81: the run summary
        || file == QLatin1String("incidentreport.cpp")     // session 97: the incident report pack
        || file == QLatin1String("missionreport.cpp")      // session 171: the mission report
        // A fixed dark plot canvas, deliberately independent of the theme.
        || file == QLatin1String("replaywindow.cpp");
}

// Palette roles that are frames, never text. Drawing a label in one is the
// single most common way this program has produced unreadable text.
bool frameRoleAsInk(const QString &line)
{
    return line.contains(QLatin1String("palette().mid()"))
        || line.contains(QLatin1String("palette().midlight()"))
        || line.contains(QLatin1String("palette().dark()"))
        || line.contains(QLatin1String("palette().shadow()"))
        || line.contains(QLatin1String("palette(mid)"))
        || line.contains(QLatin1String("palette(dark)"));
}

}  // namespace

TEST_SUITE(contrastaudit)
{
    // ---- 0. the theme list itself ---------------------------------------------
    {
        QSet<QString> keys;
        bool roundTrips = true;
        for (Theme t : ThemeUtil::all()) {
            keys.insert(QString::fromLatin1(ThemeUtil::toString(t)));
            if (ThemeUtil::fromString(QString::fromLatin1(ThemeUtil::toString(t))) != t) {
                roundTrips = false;
            }
        }
        CHECK(keys.size() == ThemeUtil::all().size(), "every theme has its own settings key");
        CHECK(roundTrips, "every theme's key reads back as that theme");
        CHECK(ThemeUtil::fromString(QStringLiteral("no-such-theme")) == Theme::Light,
              "an unknown key in dlconsole.ini falls back to Light");
    }

    // ---- 1. the palette, role by role, in every theme --------------------------
    const QPalette saved = qApp->palette();

    for (Theme t : ThemeUtil::all()) {
        ThemeUtil::apply(t);
        const QPalette p = qApp->palette();
        const QString name = QString::fromLatin1(ThemeUtil::toString(t));
        // High Contrast promises more than AA: 7 : 1 (WCAG AAA) for text and
        // every semantic colour. The other themes are held to 4.5 : 1.
        double textFloor = kTextFloor;
        if (t == Theme::HighContrast) {
            textFloor = 7.0;
        }

        struct Pair { const char *what; QPalette::ColorRole ink, on; double floorAt; };
        const Pair pairs[] = {
            { "window text on the window",   QPalette::WindowText,     QPalette::Window,        textFloor },
            { "text on the base",            QPalette::Text,           QPalette::Base,          textFloor },
            { "text on the alternate row",   QPalette::Text,           QPalette::AlternateBase, textFloor },
            { "button text on a button",     QPalette::ButtonText,     QPalette::Button,        textFloor },
            { "selected text on selection",  QPalette::HighlightedText,QPalette::Highlight,     textFloor },
            { "tooltip text on its base",    QPalette::ToolTipText,    QPalette::ToolTipBase,   textFloor },
            { "a link on the base",          QPalette::Link,           QPalette::Base,          textFloor },
            { "a visited link on the base",  QPalette::LinkVisited,    QPalette::Base,          textFloor },
            { "placeholder text on the base",QPalette::PlaceholderText,QPalette::Base,          kDisabledFloor },
        };

        for (const Pair &pr : pairs) {
            const double c = UiColor::contrastRatio(p.color(pr.ink), p.color(pr.on));
            CHECK(c >= pr.floorAt,
                  QStringLiteral("%1: %2 is %3:1")
                      .arg(name, QLatin1String(pr.what))
                      .arg(c, 0, 'f', 2).toUtf8().constData());
        }

        // Disabled text, on both surfaces it can appear on.
        for (auto on : { QPalette::Window, QPalette::Base,
                         QPalette::AlternateBase, QPalette::Button }) {
            const double c = UiColor::contrastRatio(
                p.color(QPalette::Disabled, QPalette::Text), p.color(on));
            CHECK(c >= kDisabledFloor,
                  QStringLiteral("%1: disabled text is %2:1")
                      .arg(name).arg(c, 0, 'f', 2).toUtf8().constData());
        }

        // The semantic colours on every surface they are drawn on, not just
        // the two the uicolors suite checks.
        struct Sem { const char *what; QColor c; };
        const Sem sem[] = {
            { "ok",      UiColor::ok()      },
            { "warning", UiColor::warning() },
            { "error",   UiColor::error()   },
            { "muted",   UiColor::muted()   },
            { "accent",  UiColor::accent()  },
        };
        struct Surface { const char *what; QPalette::ColorRole role; };
        const Surface surfaces[] = {
            { "the window",         QPalette::Window        },
            { "the base",           QPalette::Base          },
            { "an alternating row", QPalette::AlternateBase },
            { "a button",           QPalette::Button        },
        };
        for (const Sem &s : sem) {
            for (const Surface &su : surfaces) {
                const double c = UiColor::contrastRatio(s.c, p.color(su.role));
                CHECK(c >= textFloor,
                      QStringLiteral("%1: %2 on %3 is %4:1")
                          .arg(name, QLatin1String(s.what), QLatin1String(su.what))
                          .arg(c, 0, 'f', 2).toUtf8().constData());
            }
        }

        // A gridline must be visible without competing with the trace drawn
        // over it. Not a text floor — a line only has to be findable.
        const double gridC = UiColor::contrastRatio(UiColor::grid(), p.color(QPalette::Base));
        CHECK(gridC >= 1.05, QStringLiteral("%1: a gridline is not invisible").arg(name)
                                 .toUtf8().constData());
        CHECK(UiColor::frame().alpha() > UiColor::grid().alpha(),
              "a frame is stronger than a gridline");

        // The alternating row stripe: the whole reason it exists is to be
        // seen, and both palettes have had it too faint at some point.
        const double stripe = UiColor::contrastRatio(p.color(QPalette::AlternateBase),
                                                     p.color(QPalette::Base));
        CHECK(stripe >= 1.10,
              QStringLiteral("%1: the alternating row is %2:1 against the base")
                  .arg(name).arg(stripe, 0, 'f', 3).toUtf8().constData());
    }

    qApp->setPalette(saved);

    // ---- 2. the source ------------------------------------------------------
    const QString root = sourceDir();
    CHECK(!root.isEmpty(), "the suite knows where the source is (DL_SRC_DIR)");
    QDir dir(root);
    CHECK(dir.exists(), "and the source tree is there");

    if (dir.exists()) {
        // The root, plus the subdirectories that hold UI code. flasher/ is
        // listed explicitly: when the Firmware Flasher moved into its own
        // folder, a root-only scan would have quietly stopped auditing it.
        QStringList files =
            dir.entryList({ QStringLiteral("*.cpp"), QStringLiteral("*.h") }, QDir::Files);
        const QStringList uiSubdirectories{ QStringLiteral("flasher"), QStringLiteral("lococonfig") };
        for (const QString &subdirectory : uiSubdirectories) {
            QDir sub(dir.filePath(subdirectory));
            CHECK(sub.exists(), "a listed UI subdirectory is really there");
            const QStringList subFiles =
                sub.entryList({ QStringLiteral("*.cpp"), QStringLiteral("*.h") }, QDir::Files);
            for (const QString &f : subFiles) {
                files << subdirectory + QLatin1Char('/') + f;
            }
        }
        CHECK(files.size() > 50, "the scan is looking at the whole tree");

        QStringList literalOffenders;
        QStringList namedOffenders;
        QStringList frameInkOffenders;

        for (const QString &f : files) {
            QFile file(dir.filePath(f));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { continue; }
            QTextStream ts(&file);
            int lineNo = 0;
            while (!ts.atEnd()) {
                const QString line = ts.readLine();
                ++lineNo;
                if (line.trimmed().startsWith(QLatin1String("//"))) { continue; }

                if (frameRoleAsInk(line)) {
                    frameInkOffenders << QStringLiteral("%1:%2").arg(f).arg(lineNo);
                }
                // Note the \\s*: the first version of this check looked for
                // "color:#" exactly, and "color: #cc3300" — with a space,
                // which is how most of them were actually written — walked
                // straight past it.
                static const QRegularExpression hexLit(
                    QStringLiteral("color:\\s*#|QColor\\(\\s*\"#|QColor\\(0x"));
                if (!literalsAllowed(f) && hexLit.match(line).hasMatch()) {
                    literalOffenders << QStringLiteral("%1:%2").arg(f).arg(lineNo);
                }

                // Named CSS colours in stylesheets. This pattern slipped
                // past the first version of the audit, which only looked
                // for hex: "color: gray" appeared in nineteen places and
                // "color: darkblue" in two — 3.5:1 and 1.4:1 respectively
                // on the surfaces they were drawn on.
                if (!literalsAllowed(f)) {
                    static const QRegularExpression named(
                        QStringLiteral("color:\\s*[a-zA-Z]{3,}"));
                    const QRegularExpressionMatch m = named.match(line);
                    if (m.hasMatch()
                        && !line.contains(QLatin1String("color:%"))
                        && !line.contains(QLatin1String("palette("))) {
                        namedOffenders << QStringLiteral("%1:%2 (%3)")
                                              .arg(f).arg(lineNo).arg(m.captured(0));
                    }
                }
            }
        }

        for (const QString &o : frameInkOffenders) {
            printf("      frame role used for drawing: %s\n", qPrintable(o));
        }
        for (const QString &o : literalOffenders) {
            printf("      colour literal: %s\n", qPrintable(o));
        }

        CHECK(frameInkOffenders.isEmpty(),
              "no file draws with palette Mid/Dark/Shadow — those are frame "
              "roles and are unreadable as text; use UiColor::frame() for a "
              "line and UiColor::muted() for a label");
        for (const QString &o : namedOffenders) {
            printf("      named colour: %s\n", qPrintable(o));
        }
        CHECK(namedOffenders.isEmpty(),
              "no named CSS colours in stylesheets — \"gray\" and \"darkblue\" "
              "are fixed values that do not follow the theme");
        CHECK(literalOffenders.isEmpty(),
              "no colour literals outside uicolors/theme/uistyle and the "
              "standalone HTML reports");
    }
}

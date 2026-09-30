#include "flasherstyle.h"

#include "uicolors.h"
#include "uistyle.h"

#include <QApplication>
#include <QLabel>
#include <QPalette>

namespace FlasherStyle {

QColor primary()   { return UiColor::accent(); }
QColor accepted()  { return UiColor::ok(); }
QColor active()    { return UiColor::warning(); }
QColor attention() { return UiColor::warning(); }
QColor danger()    { return UiColor::error(); }
QColor muted()     { return UiColor::muted(); }
QColor border()    { return UiColor::frame(); }

QColor notSent()
{
    // The alternating-row colour: visibly "a cell", visibly not filled in.
    // contrastaudit already holds it to 1.10:1 against Base, which is what a
    // grid of empty cells needs to be countable.
    return qApp->palette().color(QPalette::AlternateBase);
}

QColor tint(const QColor &ink, double strength)
{
    if (strength < 0.0) {
        strength = 0.0;
    }
    if (strength > 1.0) {
        strength = 1.0;
    }
    const QColor base = qApp->palette().color(QPalette::Base);
    const double keep = 1.0 - strength;
    return QColor::fromRgbF(base.redF() * keep + ink.redF() * strength,
                            base.greenF() * keep + ink.greenF() * strength,
                            base.blueF() * keep + ink.blueF() * strength);
}

QString badgeSheet(const QColor &ink)
{
    // The colour values are formatted in at run time from the palette; the
    // source holds no literal, which is what contrastaudit checks for.
    return QStringLiteral("QLabel { color:%1; background-color:%2; border-radius:10px;"
                          " padding:2px 10px; font-weight:600; }")
        .arg(ink.name(), tint(ink).name());
}

QString cardSheet(const QString &objectName)
{
    const QColor base = qApp->palette().color(QPalette::Base);
    return QStringLiteral("QFrame#%1 { background-color:%2; border:1px solid %3; border-radius:10px; }")
        .arg(objectName, base.name(), border().name(QColor::HexArgb));
}

QString tintedPanelSheet(const QString &objectName, const QColor &ink)
{
    return QStringLiteral("QFrame#%1 { background-color:%2; border:1px solid %3; border-radius:10px; }")
        .arg(objectName, tint(ink, 0.10).name(), tint(ink, 0.45).name());
}

void makeSectionLabel(QLabel *label)
{
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * 0.9);
    font.setBold(true);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    label->setFont(font);
    label->setStyleSheet(UiColor::mutedStyle());
}

QFont statValueFont()
{
    QFont font = UiStyle::monoFont();
    font.setPointSizeF(QApplication::font().pointSizeF() * 1.7);
    font.setBold(true);
    return font;
}

QFont scaledFont(const QFont &base, double factor, bool bold)
{
    QFont font = base;
    font.setPointSizeF(base.pointSizeF() * factor);
    font.setBold(bold);
    return font;
}

}  // namespace FlasherStyle

#include "textzoom.h"

#include "settings.h"
#include "uistyle.h"

#include <QApplication>
#include <QHeaderView>
#include <QSettings>
#include <QTableView>
#include <QWidget>

#include <cmath>

namespace TextZoom {

namespace {

const char kSettingKey[]   = "ui/textZoom";
const char kFontBase[]     = "dlZoomBasePt";    // a widget's own font size at 100 %
const char kRowBase[]      = "dlZoomBaseRow";   // a table's row height at 100 %
const char kStyleSheetSavedFont[] = "_q_styleSheetWidgetFont";   // Qt's own (see apply())

bool   s_initialised   = false;
double s_basePt        = 9.0;    // the application font at 100 %
int    s_percent       = 100;
int    s_boost         = 0;
double s_appliedFactor = 1.0;    // what every widget is currently sized for

int stepIndexFor(int percent)
{
    const QVector<int> all = steps();
    int best = 0;
    for (int index = 1; index < all.size(); ++index) {
        if (std::abs(all.at(index) - percent) < std::abs(all.at(best) - percent)) {
            best = index;
        }
    }
    return best;
}

// A widget's size at 100 %: what we stored when we last sized it, unless
// something else has resized it since (then derive it afresh). Keeps
// repeated zooming from drifting by rounding.
double baseFor(QWidget *widget, const char *property, double current)
{
    const QVariant stored = widget->property(property);
    if (stored.isValid()) {
        const double base = stored.toDouble();
        if (std::fabs(base * s_appliedFactor - current) < 0.51) {
            return base;
        }
    }
    return current / s_appliedFactor;
}

}  // namespace

QVector<int> steps()
{
    return { 80, 90, 100, 110, 125, 140, 160, 180, 200 };
}

void initialise()
{
    if (qApp == nullptr) {
        return;
    }
    s_basePt = qApp->font().pointSizeF();
    if (s_basePt <= 0) {
        s_basePt = 9.0;
    }
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    s_percent = steps().at(stepIndexFor(settings.value(QLatin1String(kSettingKey), 100).toInt()));
    s_initialised = true;
    apply();
}

bool isInitialised()
{
    return s_initialised;
}

int percent()
{
    return s_percent;
}

int effectivePercent()
{
    const QVector<int> all = steps();
    int index = stepIndexFor(s_percent) + s_boost;
    if (index >= all.size()) {
        index = all.size() - 1;
    }
    if (index < 0) {
        index = 0;
    }
    return all.at(index);
}

double factor()
{
    if (!s_initialised) {
        return 1.0;
    }
    return effectivePercent() / 100.0;
}

void setPercent(int wanted)
{
    s_percent = steps().at(stepIndexFor(wanted));
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.setValue(QLatin1String(kSettingKey), s_percent);
    apply();
}

void zoomIn()
{
    const QVector<int> all = steps();
    const int index = stepIndexFor(s_percent);
    if (index + 1 < all.size()) {
        setPercent(all.at(index + 1));
    }
}

void zoomOut()
{
    const int index = stepIndexFor(s_percent);
    if (index > 0) {
        setPercent(steps().at(index - 1));
    }
}

void reset()
{
    setPercent(100);
}

void setBoost(int stepsUp)
{
    if (stepsUp < 0) {
        stepsUp = 0;
    }
    if (s_boost == stepsUp) {
        return;
    }
    s_boost = stepsUp;
    apply();
}

int boost()
{
    return s_boost;
}

void apply()
{
    if (qApp == nullptr || !s_initialised) {
        return;
    }
    const double newFactor = factor();

    // 1. Widgets with a font size of their OWN (only the size bit matters:
    //    a font set just to make text bold still inherits its size, and is
    //    carried along by step 2). Sized before the application font
    //    changes, while their current size still reflects the old factor.
    const QList<QWidget *> widgets = QApplication::allWidgets();
    for (QWidget *widget : widgets) {
        if (!widget->testAttribute(Qt::WA_SetFont)) {
            continue;
        }
        QFont font = widget->font();
        if (!(font.resolve() & QFont::SizeResolved) || font.pointSizeF() <= 0) {
            continue;
        }
        const double base = baseFor(widget, kFontBase, font.pointSizeF());
        widget->setProperty(kFontBase, base);
        font.setPointSizeF(base * newFactor);
        widget->setFont(font);
        // Under an application style sheet, Qt remembers each widget's font
        // when it first styles it, and puts that font back whenever the
        // sheet is re-applied -- which step 2 does. Without updating what
        // it remembers, every explicitly-sized widget would snap back to
        // its first size on the next zoom. (Same property in Qt 5 and 6:
        // qstylesheetstyle.cpp.)
        if (widget->property(kStyleSheetSavedFont).isValid()) {
            widget->setProperty(kStyleSheetSavedFont, font);
        }
    }

    // 2. The application font, which everything else inherits; and the
    //    style sheet, whose tab font size is derived from it.
    QFont appFont = qApp->font();
    appFont.setPointSizeF(s_basePt * newFactor);
    qApp->setFont(appFont);
    qApp->setStyleSheet(UiStyle::sheet());

    // 3. Fixed table rows.
    for (QWidget *widget : widgets) {
        auto *table = qobject_cast<QTableView *>(widget);
        if (table == nullptr) {
            continue;
        }
        QHeaderView *rows = table->verticalHeader();
        const double base = baseFor(table, kRowBase, rows->defaultSectionSize());
        table->setProperty(kRowBase, base);
        rows->setDefaultSectionSize(qMax(12, static_cast<int>(std::lround(base * newFactor))));
    }

    s_appliedFactor = newFactor;
    emit notifier()->changed();
}

Notifier *notifier()
{
    static Notifier *instance = new Notifier(qApp);
    return instance;
}

}  // namespace TextZoom

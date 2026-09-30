#include "tabtags.h"

#include "settings.h"
#include "uicolors.h"

#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QSettings>

namespace {

QString settingKey(const QString &key)
{
    // QSettings treats '/' as a group separator; tab keys never contain one,
    // but a stray one must not create a nested group.
    QString safe = key;
    safe.replace(QLatin1Char('/'), QLatin1Char('_'));
    return QStringLiteral("ui/tabTags/") + safe;
}

}  // namespace

TabTags::TabTags(QObject *parent)
    : QObject(parent)
{
    // Theme changes: each window that draws dots redraws them itself
    // (UiColor::onThemeChange needs a widget).
}

TabTags *TabTags::instance()
{
    static TabTags *tags = new TabTags(qApp);
    return tags;
}

TabTag TabTags::tag(const QString &key) const
{
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    const QString stored = settings.value(settingKey(key)).toString();
    TabTag result;
    if (stored.isEmpty()) {
        return result;
    }
    // "<color>|<label>"
    const int bar = stored.indexOf(QLatin1Char('|'));
    bool ok = false;
    const int color = stored.left(bar).toInt(&ok);
    if (ok && color >= 0 && color < UiColor::kTagCount) {
        result.color = color;
    }
    if (bar >= 0) {
        result.label = stored.mid(bar + 1);
    }
    return result;
}

void TabTags::setTag(const QString &key, const TabTag &tag)
{
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    TabTag clean = tag;
    clean.label = clean.label.trimmed().left(kMaxLabelLength);
    clean.label.remove(QLatin1Char('|'));
    if (!clean.isSet()) {
        settings.remove(settingKey(key));
    } else {
        settings.setValue(settingKey(key), QStringLiteral("%1|%2").arg(clean.color).arg(clean.label));
    }
    emit changed(key);
}

void TabTags::setColor(const QString &key, int color)
{
    TabTag current = tag(key);
    current.color = color;
    setTag(key, current);
}

void TabTags::setLabel(const QString &key, const QString &label)
{
    TabTag current = tag(key);
    current.label = label;
    setTag(key, current);
}

QIcon TabTags::dotIconFor(int color)
{
    if (color < 0) {
        return QIcon();
    }
    // Drawn at 2x for high-DPI screens; a dot with a hairline ring in the
    // window colour, so it reads as a shape on any tab background.
    const int size = 32;
    QPixmap pixmap(size, size);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(qApp->palette().color(QPalette::Window), 1.0));
    painter.setBrush(UiColor::tagColor(color));
    painter.drawEllipse(QRectF(2.5, 2.5, 11.0, 11.0));
    painter.end();
    return QIcon(pixmap);
}

QIcon TabTags::dotIcon(const QString &key) const
{
    return dotIconFor(tag(key).color);
}

QString TabTags::decoratedName(const QString &key, const QString &name) const
{
    const TabTag current = tag(key);
    if (current.label.isEmpty()) {
        return name;
    }
    return QStringLiteral("%1 · %2").arg(name, current.label);
}

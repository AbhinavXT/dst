#include "soundalerts.h"

#include "settings.h"

#include <QApplication>
#include <QObject>
#include <QSettings>

namespace {
qint64 g_lastMs = -1;
void (*g_sounder)() = nullptr;
}

QString SoundAlerts::id(Event e)
{
    switch (e) {
    case Event::SourceOffline: return QStringLiteral("source_offline");
    case Event::ClockSkew:     return QStringLiteral("clock_skew");
    case Event::ErrorNote:     return QStringLiteral("error_note");
    }
    return QString();
}

QString SoundAlerts::title(Event e)
{
    switch (e) {
    case Event::SourceOffline: return QObject::tr("A source goes offline");
    case Event::ClockSkew:     return QObject::tr("Loco and station clocks fall out of step");
    case Event::ErrorNote:     return QObject::tr("An error is reported");
    }
    return QString();
}

bool SoundAlerts::enabled(Event e)
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    return s.value(QStringLiteral("sound/") + id(e), false).toBool();
}

void SoundAlerts::setEnabled(Event e, bool on)
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.setValue(QStringLiteral("sound/") + id(e), on);
}

bool SoundAlerts::ring(Event e, qint64 nowMs)
{
    if (!enabled(e)) return false;
    if (g_lastMs >= 0 && nowMs - g_lastMs < kQuietMs) return false;
    g_lastMs = nowMs;
    if (g_sounder) g_sounder(); else QApplication::beep();
    return true;
}

void SoundAlerts::setSounder(void (*sounder)())
{
    g_sounder = sounder;
}

void SoundAlerts::resetQuiet()
{
    g_lastMs = -1;
}

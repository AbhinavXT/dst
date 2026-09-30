#include "statusline.h"

#include "uicolors.h"

#include <QTimer>

namespace {

// Six seconds: long enough to read a sentence after looking back at the
// screen, short enough that nobody reads it as the current state.
const int kTransientMs = 6000;

QString glyphFor(StatusLine::Kind k)
{
    switch (k) {
    case StatusLine::Kind::Ok:   return QStringLiteral("✓ ");
    case StatusLine::Kind::Warn: return QStringLiteral("⚠ ");
    case StatusLine::Kind::Fail: return QStringLiteral("✕ ");
    case StatusLine::Kind::State:
    case StatusLine::Kind::Info:
    case StatusLine::Kind::None: break;
    }
    return QString();
}

}  // namespace

StatusLine::StatusLine(QWidget *parent)
    : QLabel(parent)
{
    setWordWrap(true);
    setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_expiry = new QTimer(this);
    m_expiry->setSingleShot(true);
    connect(m_expiry, &QTimer::timeout, this, &StatusLine::clear);

    UiColor::onThemeChange(this, [this] { applyColour(); });
}

int StatusLine::transientMs() { return kTransientMs; }

void StatusLine::state(const QString &text) { set(Kind::State, text); }
void StatusLine::say(const QString &text)  { set(Kind::Info, text); }
void StatusLine::ok(const QString &text)   { set(Kind::Ok,   text); }
void StatusLine::warn(const QString &text) { set(Kind::Warn, text); }
void StatusLine::fail(const QString &text) { set(Kind::Fail, text); }

void StatusLine::clear()
{
    m_expiry->stop();
    m_kind = Kind::None;
    m_message.clear();
    QLabel::clear();
    setStyleSheet(QString());
}

void StatusLine::set(Kind k, const QString &text)
{
    if (text.isEmpty()) { clear(); return; }

    m_kind    = k;
    m_message = text;
    setText(glyphFor(k) + text);
    applyColour();

    // Sticky kinds stop any timer left running by an earlier transient
    // message: a failure must not be wiped three seconds later by the
    // expiry of the "sending…" that preceded it.
    m_expiry->stop();
    if (!isSticky()) { m_expiry->start(kTransientMs); }
}

void StatusLine::applyColour()
{
    switch (m_kind) {
    case Kind::Ok:   setStyleSheet(UiColor::okStyle());      break;
    case Kind::Warn: setStyleSheet(UiColor::warningStyle()); break;
    case Kind::Fail: setStyleSheet(UiColor::errorStyle());   break;
    case Kind::State:
    case Kind::Info: setStyleSheet(UiColor::mutedStyle());   break;
    case Kind::None: setStyleSheet(QString());               break;
    }
}

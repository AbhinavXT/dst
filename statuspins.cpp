#include "statuspins.h"

#include "settings.h"
#include "uicolors.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QPointer>

#include "undolog.h"
#include <QTimer>
#include <QToolButton>

namespace {

const char kPinsKey[] = "ui/statusPins";
const QChar kSeparator(0x1F);   // unit separator: cannot appear in a key or a label
StatusPins *g_instance = nullptr;

}  // namespace

StatusPins::StatusPins(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 6, 0);
    layout->setSpacing(4);
    g_instance = this;
    m_ageTimer = new QTimer(this);
    m_ageTimer->setInterval(1000);
    connect(m_ageTimer, &QTimer::timeout, this, [this]() {
        for (int index = 0; index < m_pins.size(); ++index) {
            updateChip(index);
        }
    });
    m_ageTimer->start();
    load();
    rebuild();
    UiColor::onThemeChange(this, [this]() { rebuild(); });
}

StatusPins::~StatusPins()
{
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

StatusPins *StatusPins::instance()
{
    return g_instance;
}

bool StatusPins::addPin(const QString &captureKey, const LiveFieldRef &ref, const QString &current)
{
    if (m_pins.size() >= kMaxPins || !ref.isValid()) {
        return false;
    }
    for (const Pin &pin : m_pins) {
        if (pin.key == captureKey && pin.ref.sources == ref.sources) {
            return false;
        }
    }
    Pin pin;
    pin.key = captureKey;
    pin.ref = ref;
    pin.value = current;
    if (!current.isEmpty()) {
        pin.seenMs = QDateTime::currentMSecsSinceEpoch();
    }
    m_pins.append(pin);
    save();
    rebuild();
    return true;
}

void StatusPins::removePin(int index)
{
    if (index < 0 || index >= m_pins.size()) {
        return;
    }
    const Pin removed = m_pins.at(index);
    m_pins.removeAt(index);
    save();
    rebuild();
    if (m_undo) {
        QPointer<StatusPins> self(this);
        m_undo->push(tr("Unpin %1 · %2").arg(removed.key, removed.ref.label),
                     [self, removed, index]() {
            if (!self || self->m_pins.size() >= kMaxPins) {
                return false;
            }
            for (const Pin &p : self->m_pins) {
                if (p.key == removed.key && p.ref.serialise() == removed.ref.serialise()) {
                    return true;   // pinned again meanwhile
                }
            }
            Pin back = removed;
            back.chip = nullptr;
            back.text = nullptr;
            self->m_pins.insert(qMin(index, self->m_pins.size()), back);
            self->save();
            self->rebuild();
            return true;
        });
    }
}

void StatusPins::reload()
{
    m_pins.clear();
    load();
    rebuild();
}

void StatusPins::onEntry(QString tabKey, LogEntryPtr entry)
{
    Q_UNUSED(tabKey);
    if (entry.isNull() || m_pins.isEmpty() || !entry->text.startsWith(QLatin1Char('@'))) {
        return;
    }
    observeLine(entry->text);
}

void StatusPins::observeLine(const QString &line)
{
    if (m_pins.isEmpty()) {
        return;
    }
    const CaptureLine capture = CaptureDecoder::parseLine(line);
    if (!capture.valid) {
        return;
    }
    // describe() only for a type some pin wants, and only once per line.
    QVector<FieldRow> rows;
    bool described = false;
    for (int index = 0; index < m_pins.size(); ++index) {
        Pin &pin = m_pins[index];
        if (pin.key != capture.key()) {
            continue;
        }
        CapType type = CapType::Unknown;
        QString field;
        if (!LiveFields::parseSource(pin.ref.sources.value(0), &type, &field) || type != capture.type) {
            continue;
        }
        if (!described) {
            rows = CaptureDecoder::describe(capture);
            described = true;
        }
        const QString value = LiveFields::valueIn(rows, field);
        if (!value.isNull()) {
            pin.value = value;
            pin.seenMs = QDateTime::currentMSecsSinceEpoch();
            updateChip(index);
        }
    }
}

QString StatusPins::displayValue(const Pin &pin) const
{
    if (pin.value.isEmpty()) {
        return QStringLiteral("—");
    }
    CapType type = CapType::Unknown;
    QString field;
    if (LiveFields::parseSource(pin.ref.sources.value(0), &type, &field) && LiveFields::isFrameNumber(field)) {
        const QString clock = LiveFields::frameClock(pin.value);
        if (!clock.isEmpty()) {
            return clock;
        }
    }
    return pin.value;
}

void StatusPins::rebuild()
{
    for (const Pin &pin : m_pins) {
        if (pin.chip != nullptr) {
            pin.chip->deleteLater();
        }
    }
    auto *layout = static_cast<QHBoxLayout *>(this->layout());
    const QPalette pal = palette();
    const QString sheet =
        QStringLiteral("QFrame#pinChip { background-color:%1; border:1px solid %2; border-radius:10px; }")
            .arg(pal.color(QPalette::AlternateBase).name(), UiColor::frame().name(QColor::HexArgb));
    for (int index = 0; index < m_pins.size(); ++index) {
        Pin &pin = m_pins[index];
        auto *chip = new QFrame(this);
        chip->setObjectName(QStringLiteral("pinChip"));
        chip->setStyleSheet(sheet);
        auto *chipLayout = new QHBoxLayout(chip);
        chipLayout->setContentsMargins(8, 1, 2, 1);
        chipLayout->setSpacing(2);
        pin.text = new QLabel(chip);
        chipLayout->addWidget(pin.text);
        auto *close = new QToolButton(chip);
        close->setText(QStringLiteral("✕"));
        close->setAutoRaise(true);
        close->setToolTip(tr("Unpin"));
        // Find the pin by its chip at click time: indices shift as pins go.
        connect(close, &QToolButton::clicked, this, [this, chip]() {
            for (int i = 0; i < m_pins.size(); ++i) {
                if (m_pins.at(i).chip == chip) {
                    removePin(i);
                    return;
                }
            }
        });
        chipLayout->addWidget(close);
        pin.chip = chip;
        layout->addWidget(chip);
        updateChip(index);
    }
    setVisible(!m_pins.isEmpty());
}

void StatusPins::updateChip(int index)
{
    if (index < 0 || index >= m_pins.size() || m_pins.at(index).text == nullptr) {
        return;
    }
    const Pin &pin = m_pins.at(index);
    pin.text->setText(QStringLiteral("%1 · %2: %3").arg(pin.key, pin.ref.label, displayValue(pin)));
    const qint64 ageMs = QDateTime::currentMSecsSinceEpoch() - pin.seenMs;
    if (pin.seenMs == 0) {
        pin.text->setStyleSheet(UiColor::mutedStyle());
        pin.text->setToolTip(tr("Nothing received from %1 for this field yet").arg(pin.key));
    } else if (ageMs > kStaleMs) {
        pin.text->setStyleSheet(UiColor::mutedStyle());
        pin.text->setToolTip(tr("Last value %1 s ago — not updating").arg(ageMs / 1000));
    } else {
        pin.text->setStyleSheet(QString());
        pin.text->setToolTip(tr("%1 from %2 (live)").arg(pin.ref.sources.value(0), pin.key));
    }
}

QString StatusPins::textAt(int index) const
{
    if (index < 0 || index >= m_pins.size() || m_pins.at(index).text == nullptr) {
        return QString();
    }
    return m_pins.at(index).text->text();
}

bool StatusPins::isStaleAt(int index) const
{
    if (index < 0 || index >= m_pins.size()) {
        return true;
    }
    const Pin &pin = m_pins.at(index);
    return pin.seenMs == 0 || QDateTime::currentMSecsSinceEpoch() - pin.seenMs > kStaleMs;
}

void StatusPins::save() const
{
    QStringList texts;
    for (const Pin &pin : m_pins) {
        texts.append(pin.key + kSeparator + pin.ref.serialise());
    }
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.setValue(QLatin1String(kPinsKey), texts);
}

void StatusPins::load()
{
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    for (const QString &text : settings.value(QLatin1String(kPinsKey)).toStringList()) {
        const int split = text.indexOf(kSeparator);
        if (split <= 0) {
            continue;
        }
        Pin pin;
        pin.key = text.left(split);
        pin.ref = LiveFieldRef::parse(text.mid(split + 1));
        if (pin.ref.isValid() && m_pins.size() < kMaxPins) {
            m_pins.append(pin);
        }
    }
}

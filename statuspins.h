#ifndef STATUSPINS_H
#define STATUSPINS_H
// =============================================================================
//  StatusPins -- live values pinned to the main window's status bar.
//
//  Right-click a field in the Live Loco Console > "Pin to status bar", and
//  "7_1 · LOCO_MODE: 2 (Staff_Responsible)" stays in view whatever window
//  is in front. Each pin is (loco, field); it reads the capture stream
//  itself, so it keeps updating with the Loco Console closed.
//
//  A value not refreshed for kStaleMs is drawn muted, with its age in the
//  tooltip. ✕ removes a pin. Pins are remembered (ui/statusPins).
// =============================================================================
#include <QDateTime>
#include <QVector>
#include <QWidget>

#include "livefields.h"
#include "logentry.h"

class QLabel;
class QTimer;
class UndoLog;

class StatusPins : public QWidget
{
    Q_OBJECT
public:
    static const int kMaxPins = 6;
    static const int kStaleMs = 5000;

    explicit StatusPins(QWidget *parent = nullptr);
    ~StatusPins() override;

    // The main window's pins (null until it has made them).
    static StatusPins *instance();

    // Pin `ref` (one source) for the loco with this capture key. `current`
    // shows at once, before the next frame arrives. False when full or
    // already pinned.
    bool addPin(const QString &captureKey, const LiveFieldRef &ref, const QString &current = QString());
    void removePin(int index);

    // Removing a pin pushes its undo here when set (session 79).
    void setUndoLog(UndoLog *log) { m_undo = log; }
    // Re-read the pins from the INI (after a settings import).
    void reload();

    // For the tests.
    int     count() const { return m_pins.size(); }
    QString textAt(int index) const;
    bool    isStaleAt(int index) const;

public slots:
    // Wired to MessageDispatcher::entryAppended.
    void onEntry(QString tabKey, LogEntryPtr entry);
    // A capture line as text (the tests, and onEntry).
    void observeLine(const QString &line);

private:
    struct Pin {
        QString      key;          // capture key "7_1"
        LiveFieldRef ref;          // one source
        QString      value;
        qint64       seenMs = 0;
        QWidget     *chip = nullptr;
        QLabel      *text = nullptr;
    };

    void rebuild();
    void updateChip(int index);
    void save() const;
    void load();
    QString displayValue(const Pin &pin) const;

    QVector<Pin> m_pins;
    QTimer      *m_ageTimer = nullptr;
    UndoLog     *m_undo = nullptr;
};

#endif // STATUSPINS_H

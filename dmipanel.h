#ifndef DMIPANEL_H
#define DMIPANEL_H

// =============================================================================
//  DMI window (session 83) — Tools ▸ Monitor ▸ DMI (LP-OCIP)…
//  -----------------------------------------------------------------------------
//  The Loco Pilot's Operation-cum-Indication Panel, drawn from the loco's
//  @dmi frames, laid out as RDSO/SPN/196/2020 Annexure-B (Amendment-3)
//  specifies it: an 800 x 600 unit screen divided into regions A to M and the
//  soft-key row K, each at the position and size the annexure gives, with
//  its fonts (in pixels of the 800 x 600 screen) and colour codes.
//
//    A  target distance: type (A4), heading (A2), bar on the annexure's
//       scale (A1), four-digit value (A3)
//    B  speedometer: 0-250 km/h over -149..+149 deg, 125 divisions (B2), the
//       permitted / overspeed / target rings (B3), pointer and speed (B1),
//       loco id (B4), date and time (B5, B6), next lower speed (B7), brake
//       symbol (B8), location (B9), mode symbol (B10), section speed (B11)
//    C  movement authority: heading (C2), bar on its scale (C1), five-digit
//       value (C3)
//    D  signal post with the aspect lit (D1), calling-on / IB / gate / auto
//       disc, junction route or stencil, distance (D2), name (D3, orange)
//    E  deceleration constant    F  train length    G  mode in words
//    H  system messages (the alarm flags; several alternate every 2 s)
//    I  context messages (the context flags; likewise)
//    J  radio: antenna, "RF", five strength bars
//    L  last tag: Tid, direction, distance to the next tag
//    M  the last three tags on a track diagram
//    K  the ten soft keys, as labels (this window is a monitor, not a DMI:
//       nothing is sent back to the loco)
//
//  COLOURS
//    Two palettes. "Theme" (the default) follows the console's theme: the
//    background and text are the theme's, and each Annexure-B colour keeps
//    its hue but is moved to at least 3:1 against that background, so the
//    yellow and light green stay visible on a light theme. "Annexure-B"
//    draws Table B.2's exact RGB on black, as the real panel does.
//    Signal lamps are the railway's colours in both.
//
//  WHAT IS NOT IN THE FRAME IS NOT INVENTED
//    Every region is filled from a named @dmi field. Where the annexure asks
//    for something the frame does not carry, the region is left as the
//    annexure says for "nothing to show" (blank), not filled with a guess.
//    Assumptions that could not be avoided are listed in dmiAssumptions()
//    and shown in the window's status line on request.
// =============================================================================

#include <QDateTime>
#include <QHash>
#include <QPalette>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "dmitimetravel.h"

struct FieldRow;
struct LogEntry;   // a struct: MSVC mangles class and struct differently (session 116)
class MessageDispatcher;
class QCheckBox;
class QPushButton;
class QTableWidget;
class QComboBox;
class QLabel;

// ---- what the panel shows, from one @dmi frame -------------------------------
struct DmiState {
    bool    valid = false;
    qint64  receivedMs = 0;

    // B
    int     speed = 0;
    bool    hasPermitted = false;
    int     permitted = 0;
    bool    hasTargetSpeed = false;
    int     targetSpeed = 0;
    int     sectionSpeed = 0;         // B11 (0 = none)
    QString locoId;                   // B4
    QString date, time;               // B5, B6 (DD-Mmm-YYYY, HH:MM:SS)
    double  locationKm = 0.0;         // B9
    int     brakeType = 0;            // B8: 0 none, 1 overspeed warning, 2 NB, 3 FSB, 4 EB, 5 LE
    int     mode = 0;                 // B10 / G: dmiLocoMode
    QString modeText;                 // G, per Annexure-A1

    // A
    int     targetDistance = 0;       // m; 0 = region A blank
    QString targetType;               // A4

    // C
    int     movementAuthority = 0;    // m (ma_w_r_t_sig)

    // D
    int     aspect = 0;               // current_sig_aspect (0 = no signal: D1 blank)
    QString disc;                     // "C", "IB", "G", "A", "AG" or empty
    int     route = 0;                // 1..6 junction route position, 0 none
    int     stencil = 0;              // 1..32 stencil route, 0 none
    int     signalDistance = 0;       // D2
    QString signalName;               // D3 ("DN MAIN Adv-Str")

    // E F
    QString dc;                       // "DC 0.90"
    int     trainLength = 0;

    // H I
    QStringList systemMessages;       // region H, in flag order
    QStringList contextMessages;      // region I

    // J L M
    int     rfBars = 0;               // 0..5
    int     tagId = 0;
    QString tagDir;                   // "N" / "R" / ""
    int     nextTagDistance = 0;
    int     tagStatus[3] = { 0, 0, 0 };   // current, last, last-to-last (2-bit status)
    int     tagIds[3] = { 0, 0, 0 };
};

DmiState dmiStateFrom(const QVector<FieldRow> &rows, const QHash<QString, qint64> &raw);
DmiState dmiStateFromLine(const QString &captureLine);
// The same from a parsed frame (the replay window holds frames, not lines).
DmiState dmiStateFromCapture(const CaptureLine &cap);

// The Annexure-A1 mode name for a dmiLocoMode value ("On Sight Mode").
QString dmiModeName(int mode);
// Region H wording for an alarm flag, with its XX / YYYY filled from the frame.
QString dmiSystemMessage(const QString &flag, const QHash<QString, qint64> &raw,
                         const QHash<QString, QString> &text);
QString dmiContextMessage(const QString &flag, const QHash<QString, qint64> &raw,
                          const QHash<QString, QString> &text);
// Target-distance scale (A1) and MA scale (C1): value -> y offset in the
// region, from the annexure's figures.
double dmiTargetScaleY(double metres);
double dmiMaScaleY(double metres);
// Which lamps of the four (top YLW, GRN, YLW, bottom RED) an aspect lights.
QVector<int> dmiLitLamps(int aspect);   // indices 0..3, top first
QStringList dmiAssumptions();

// ---- colours ------------------------------------------------------------------
struct DmiColours {
    QColor bg, wht, gry, lgy, mgy, lbl, ylw, lor, org, brd, lgr, grn, dgr;
    static DmiColours annexure();                      // Table B.2 exactly, on black
    static DmiColours forTheme(const QPalette &palette);
};

// ---- the panel ----------------------------------------------------------------
class DmiView : public QWidget
{
    Q_OBJECT
public:
    explicit DmiView(QWidget *parent = nullptr);
    void setState(const DmiState &state);
    const DmiState &state() const { return m_state; }
    void setAnnexureColours(bool on);
    bool annexureColours() const { return m_annexure; }
    void setStale(bool stale) { m_stale = stale; update(); }
    bool isStale() const { return m_stale; }
    // What the panel says when there is no frame to draw (session 84: the
    // time-travel wording differs from the live one).
    void setEmptyText(const QString &text) { m_emptyText = text; update(); }
    // Index into systemMessages / contextMessages shown now (they alternate).
    int  systemMessageIndex() const { return m_msgTick; }
    void advanceMessages() { ++m_msgTick; update(); }
    QSize sizeHint() const override { return QSize(800, 600); }
    bool hasHeightForWidth() const override { return true; }
    int  heightForWidth(int w) const override { return w * 3 / 4; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    DmiState m_state;
    bool     m_annexure = false;
    bool     m_stale = false;
    int      m_msgTick = 0;
    QString  m_emptyText;
};

class DmiWindow : public QWidget
{
    Q_OBJECT
public:
    DmiWindow(MessageDispatcher *dispatcher, QWidget *parent = nullptr);
    DmiView *view() const { return m_view; }
    // Feed one capture line directly (tests; the dispatcher does this live).
    void observeLine(const QString &sourceKey, const QString &line);
    QString selectedSource() const;
    void setSelectedSource(const QString &key);
    bool saveImage(const QString &path) const;
    QString statusText() const;

    // Session 84: time travel. Following, the panel shows each loco's
    // latest @dmi at or before the moment picked elsewhere in the console
    // (a row, a replay cursor) instead of the live frame.
    void setFollowCursor(bool on);
    bool followCursor() const { return m_following; }
    void showMoment(const DmiMoment &moment);
    const DmiMoment &moment() const { return m_moment; }

    // Session 158: "Show on DMI" on a row. The DMI window `owner` already
    // has open (else a new one), set to follow the cursor, brought to the
    // front. Offer the row's moment to the broker first: following picks up
    // the moment pointed at last.
    static DmiWindow *showFollowing(QWidget *owner, MessageDispatcher *dispatcher);

    // Session 160: two locos side by side. Session 167: up to four panels
    // (1-3 in a row, 4 as a 2x2 grid), each with its own loco picker and,
    // when Fields is open, its own decoded-fields table under it. Panel 0 is
    // "A", panel 1 "B". The count is remembered (dmi/panels).
    static const int kMaxPanels = 4;
    void setPanelCount(int n);
    int  panelCount() const { return m_count; }
    DmiView *panelView(int i) const;
    QString panelSource(int i) const;
    void setPanelSource(int i, const QString &key);
    QString panelStatus(int i) const;
    void setTwoLocos(bool on) { setPanelCount(on ? 2 : 1); }
    bool twoLocos() const { return m_count >= 2; }
    DmiView *viewB() const { return panelView(1); }
    QString selectedSourceB() const { return panelSource(1); }
    void setSelectedSourceB(const QString &key) { setPanelSource(1, key); }
    QString statusTextB() const { return panelStatus(1); }

    // Session 92: the decoded fields of the frame each panel is drawing
    // (live or at the moment). Session 167: under each panel.
    void setFieldsVisible(bool on);
    bool fieldsVisible() const;
    QStringList fieldNames(int panel = 0) const;              // for tests
    QString fieldValue(const QString &name, int panel = 0) const;

private:
    struct Panel {
        QWidget      *column = nullptr;
        QComboBox    *source = nullptr;
        DmiView      *view = nullptr;
        QLabel       *status = nullptr;
        QWidget      *fieldsPane = nullptr;
        QLabel       *fieldsTitle = nullptr;
        QTableWidget *fields = nullptr;
        CaptureLine   shown;                    // the frame the panel is drawing
    };
    void buildPanel(int i, bool annexure);
    void layoutPanels();
    void refreshStatus();
    void render();                 // the visible panels, live or at the moment
    // One panel: `key`'s frame (live, or at the moment) into `view`; the
    // frame drawn is returned. And that panel's status line.
    CaptureLine drawPanel(DmiView *view, const QString &key);
    QString     statusFor(DmiView *view, const QString &key);
    void        pickOther(int i);  // panel i on a loco no other panel shows, when there is one
    void addSource(const QString &key);
    void refreshFields(int i);
    MessageDispatcher *m_dispatcher = nullptr;
    QVector<Panel> m_panels;
    int        m_count = 1;
    DmiView   *m_view = nullptr;     // m_panels[0].view
    QComboBox *m_source = nullptr;   // m_panels[0].source
    QLabel    *m_status = nullptr;   // m_panels[0].status
    QCheckBox *m_follow = nullptr;
    QComboBox *m_countBox = nullptr;
    QPushButton *m_fieldsBtn = nullptr;
    bool       m_fieldsOn = false;
    QLabel    *m_fieldNotes = nullptr;
    class QGridLayout *m_grid = nullptr;
    bool       m_following = false;
    DmiMoment  m_moment;
    QHash<QString, QString> m_lastLine;     // source -> last @dmi line
    QHash<QString, qint64>  m_lastMs;
};

#endif // DMIPANEL_H

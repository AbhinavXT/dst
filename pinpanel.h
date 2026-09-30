#ifndef PINPANEL_H
#define PINPANEL_H
// =============================================================================
//  PinPanel — the dock that shows pinned fields and their current values.
//
//  A table rather than a list of labels, because the useful reading is across
//  four columns at once: what it is, what it says now, what it said before,
//  and how long ago that changed. A value with no age beside it cannot be told
//  apart from a value that stopped arriving ten minutes ago, and that
//  distinction is the difference between "the loco is in Stand By" and "the
//  loco stopped talking while in Stand By".
// =============================================================================

#include <QWidget>
#include <QVector>

#include "pinboard.h"

class QComboBox;
class QLineEdit;
class QMenu;
class QPushButton;
class QStringListModel;
class QTableWidget;
class QTimer;
class QToolButton;
class StatusLine;

// One packet's worth of the chooser: what to call it, what a pin narrowed
// to it stores, and what it can carry.
//
// Mirrors Schema::Decoder::PacketFields, deliberately without including the
// decoder here — this panel does not decode anything, and a widget header
// that drags in QDomDocument makes every file that shows a pin board
// depend on the XML parser.
struct PinPacketFields {
    QString     packet;    // "SLRP", "LOCO_SOS"
    QString     captype;   // "slrp", "lsos" — what a pin actually narrows on
    QStringList fields;
};

class PinPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PinPanel(QWidget *parent = nullptr);

    PinBoard &board() { return m_board; }

    // Offered every delivered batch. See PinBoard::observe for why this is the
    // last entry of a batch rather than all of them.
    void observe(const LogEntryPtr &entry, const QString &sourceKey);

    // The field names the operator can choose from and the sources. Supplied
    // by the owner because discovering them means walking a model and reading
    // the schema, which is the window's business.
    //
    // The chooser is NESTED, one level of packet above the field names, for
    // the reason the flat list failed: five hundred names in one alphabetical
    // run is a list nobody reads to the end, and thirty-five of those names
    // occur in more than one packet, so the flat list could not even say
    // which of the five FRAME_NUMs was being offered. Under a packet it is
    // unambiguous, and picking one there narrows the pin to that packet
    // without the operator having to set the narrowing separately.
    //
    // `seenHere` is what the current tab has actually carried and goes in a
    // group of its own at the top: it is a few dozen names out of five
    // hundred, and it is nearly always the one wanted. `byPacket` is
    // everything the schema can produce, because the field an operator most
    // wants to watch is often one that has NOT arrived yet — a chooser built
    // only from observed traffic cannot offer it.
    void setAvailableFields(const QStringList &seenHere,
                            const QVector<PinPacketFields> &byPacket);
    void setAvailableSources(const QStringList &tabKeys);

    // Captypes the schema can decode, for narrowing a pin to one packet.
    void setAvailablePackets(const QVector<PinPacketFields> &packets);

    // Hold the whole board at this instant, naming what caused it. Called by
    // the owner when a watch fires, if the operator asked for that.
    void freezeNow(const QString &why);

    void restore();     // pins from Settings
    void persist() const;

signals:
    // The operator asked to see where a value came from: the source, and the
    // LOG timestamp of the frame that established it. A tab key alone was
    // what this used to carry, which switched tabs and left the operator to
    // find the frame themselves — the question is which packet, not which
    // source.
    void revealRequested(const QString &sourceKey, qint64 epochMs);

    // A pinned field and a plotted field are the same field asked about over
    // two different spans — one now, one over the session. This is the way
    // from the first to the second.
    void plotRequested(const QString &fieldName, const QString &sourceKey);

private slots:
    void addPin();
    void removeSelected();
    void refresh();

private:
    // Accept a field chosen from the menu: it goes in the box, and the
    // packet it was chosen under goes in the narrowing combo.
    //
    // The second half is the point of nesting. An operator who picks
    // FRAME_NUM from under LSRP has already said which of the five they
    // mean; leaving the narrowing on "any packet" after that would pin the
    // one thing they did not ask for — whichever packet arrived last.
    // `captype` is empty for the observed-fields group, which says nothing
    // about packets, and the narrowing is then left alone.
    void pickField(const QString &field, const QString &captype);

    // One packet's submenu, split further when it is too long to read.
    QMenu *buildPacketMenu(const PinPacketFields &pk, QWidget *parent);

    // Fill `menu` with one action per name. Above kBucketLimit names it
    // buckets them into alphabetical sub-menus instead, because a menu
    // taller than the screen scrolls, and a scrolling menu of 167 names is
    // the flat list again with extra steps.
    void addFieldActions(QMenu *menu, const QStringList &fields,
                         const QString &captype);
    static constexpr int kBucketLimit  = 40;
    static constexpr int kBucketTarget = 20;

    PinBoard      m_board;
    QLineEdit    *m_fieldEdit = nullptr;
    QToolButton  *m_browseBtn = nullptr;
    QMenu        *m_fieldMenu = nullptr;
    QStringListModel *m_completions = nullptr;  // flat, for typing
    QComboBox    *m_sourceBox = nullptr;
    QComboBox    *m_packetBox = nullptr;
    QPushButton  *m_addBtn    = nullptr;
    QPushButton  *m_delBtn    = nullptr;
    QPushButton  *m_freezeBtn = nullptr;
    class QCheckBox *m_autoFreeze = nullptr;
    QTableWidget *m_table     = nullptr;
    StatusLine   *m_status    = nullptr;
    QTimer       *m_ageTick   = nullptr;
    bool          m_dirty     = false;
};

#endif  // PINPANEL_H

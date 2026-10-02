#ifndef PACKETMAKERDIALOG_H
#define PACKETMAKERDIALOG_H
// =====================================================================
//  packetmakerdialog.{h,cpp} -- the Packet Maker UI.
//
//  Builds a station/loco packet from the schema: a form generated from
//  the packet's header fields, a list of sub-packets the operator can
//  add / edit / remove, a live byte + decode preview, and the two send
//  modes (once, and every N ms) driven by UdpSender.
//
//  Nothing is sent until the frame has been built AND self-verified by
//  PacketBuilder (round-trips through the decoder, CRC checks). The send
//  buttons stay disabled until that passes.
// =====================================================================
#include <QDialog>
#include <QHash>
#include <QVector>
#include <QByteArray>
#include <QJsonObject>

#include "packetbuilder.h"
#include "udpsender.h"
#include "packetvariation.h"
#include "messageheader.h"
#include "schema/schemaencoder.h"

class SessionKeyStore;
class QComboBox;
class FrameNumberWatch;
class QLineEdit;
class QSpinBox;
class QListWidget;
class QFormLayout;
class QPlainTextEdit;
class QLabel;
class StatusLine;
class QWidget;
class QPushButton;
class QTableWidget;
class QGroupBox;
class QCheckBox;
class QTableWidget;

class SubPacketWindow;

class PacketMakerDialog : public QDialog {
    Q_OBJECT
public:
    // `frameWatch`: the live frame-number watch a new frame is seeded from
    // (MainWindow's). nullptr = no live traffic to follow (session 95).
    explicit PacketMakerDialog(QWidget *parent = nullptr, FrameNumberWatch *frameWatch = nullptr,
                               SessionKeyStore *keys = nullptr);
    ~PacketMakerDialog() override;

    // The field-editor factory, shared with SubPacketWindow so a field looks
    // and parses the same wherever it is edited. Public rather than
    // duplicated: two factories would drift on enums, signedness and hex
    // handling, and the sub-packet editor is exactly where that would bite.
    static QWidget *makeFieldEditor(const Schema::FieldInfo &f,
                                    const Schema::Encoder &enc, QWidget *parent);
    static qint64   readFieldEditor(QWidget *w);
    static void     writeFieldEditor(QWidget *w, qint64 v);

    // Fill from a buffer handed over by another part of the application (a
    // log row, the raw-bytes panel, the Check Buffer page) rather than by a
    // paste. Same text shapes parseBuffer() accepts — bare bytes or a whole
    // @-capture line — and the same code path as the Load button, so a frame
    // loaded this way is loaded exactly as a pasted one is.
    void loadBuffer(const QString &text);

    // True while the interval sender is running. Asked before this dialog is
    // reused for a different frame: rewriting the form under a live send
    // would change what goes on the wire without the operator saying so.
    bool isSending() const { return m_sender.isRunning(); }

    // Pull the byte buffer out of whatever the operator pasted: bare hex,
    // a whole @slrp capture line, 0x-prefixed or comma-separated bytes.
    // Public because it is a pure function with several fiddly cases (the
    // capture preamble in particular) and it is worth testing directly.
    // Seconds elapsed since local midnight, clipped into a field of
    // `bits` bits. This is what FRAME_NUM is seeded with.
    //
    // Public because it is the kind of thing that is silently wrong at one
    // moment of the day and correct at every other: 86,399 fits in the
    // 17 bits FRAME_NUM has, but a narrower field would wrap, and the wrap
    // has to be a stated behaviour rather than an accident of arithmetic.
    // The rules the next interval run will apply. Public because "what will
    // this actually send" is a question worth being able to ask from outside
    // the dialog — the tests ask it of the seeded rule.
    QVector<PacketVary::Rule> varyRules() const { return readVaryRules(); }

    // The header values the next build will use, including the live frame
    // number. Public for the tests: "what would this send right now" is the
    // question the staleness bug turned on.
    QHash<QString, qint64> headerForTest() const { return readHeader(); }

    static qint64 secondsSinceMidnight(int bits);

    // The extra header fields as the table currently holds them, and the
    // exact envelope (header + enabled extras) the next send would put in
    // front of a packet of `packetLen` bytes. Public for the tests.
    QVector<MessageHeader::Extra> extrasForTest(QString *err = nullptr) const
        { return readExtras(err); }
    void setExtrasForTest(const QVector<MessageHeader::Extra> &e) { setExtras(e); }
    QByteArray envelopeForTest(int packetLen) const
        { return currentHeaderPreview(currentCaptype(), packetLen); }

    // What FRAME_NUM should start at: the number the equipment is currently
    // using if ARP or LSRP has been seen, otherwise the clock. `fromLive` is
    // set so the caller can say which it was — a number taken from traffic
    // and a number taken from this laptop's clock are different claims and
    // must not look alike.
    // `watch` may be nullptr (no live traffic): the clock is used then.
    static qint64 seedFrameNumber(const FrameNumberWatch *watch, int bits, bool *fromLive = nullptr);

    static QByteArray parseBuffer(const QString &text, QString *captypeHint);

    // Where the packet body starts inside a pasted buffer, and what was in
    // front of it.
    //
    // This exists because getting it wrong is silent. parseBody() reads from
    // bit 0 of whatever it is handed, so a captured arp/lsrp buffer that is
    // two bytes out of step does not fail — it fills the form with plausible
    // wrong values, and Field Sweep then transmits from them. The schema's
    // body_offset is the authority (10 bytes for arp/lsrp: the 8-byte message
    // header plus two more), NOT MessageHeader::SIZE, which only describes
    // the transport envelope.
    struct BufferSplit {
        QByteArray body;             // what parseBody should be given
        int  stripped  = 0;          // bytes removed from the front
        bool hadHeader = false;      // a real message header was recognised
        int  srcId = -1, destId = -1, seq = -1;   // read from it, when present
        QStringList notes;           // what was assumed, for the status line
    };
    static BufferSplit splitBuffer(const Schema::Encoder &enc,
                                   const QString &captype,
                                   const QByteArray &frame);

protected:
    // Escape and the close box while a send is in flight — see sendguard.h.
    void reject() override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    // Repopulate the captured-key-set picker from the store, keeping the
    // operator's selection if it still exists.
    void refreshKeySets();
    void onPacketChanged();
    void onAddSub();
    void onRemoveSub();
    void onSubSelectionChanged();
    void onSubActivated();
    void onBuild();
    void onSendOnce();
    void onToggleInterval();
    void onSenderError(const QString &what);
    void onLoadBuffer();
    void onClearBuffer();
    void onVaryAdd();
    void onVaryRemove();
    void updateVaryTabTitle();          // "Vary per send (N rules)"
    void onFieldFilterChanged();
    void onShowDiff();
    void onSavePreset();
    void onLoadPreset();
    void onExtraAdd();
    void onExtraRemove();
    void onExtrasEdited();

private:
    FrameNumberWatch *m_frameWatch = nullptr;     // session 95: not owned; may be nullptr
    void rebuildHeaderForm();
    void rebuildSubEditor();
    void commitCurrentSub();
    void applyParsed(const Schema::ParsedPacket &pp);
    QHash<QString, qint64> readHeader() const;
    QByteArray sessionKey() const;
    // sendIndex >= 0 applies the per-send variation rules for that index.
    // -1 builds exactly what the form says, which is what Build and Send
    // Once use.
    PacketBuilder::Result buildNow(int sendIndex = -1);

    // Render a built frame into the preview pane. Split out of onBuild()
    // because Send Once rebuilds at the moment of sending and the preview
    // must show what actually went out, not what Build left behind.
    void refreshPreview(const PacketBuilder::Result &r);

    // Rebuild in place because the frame number moved, keeping Send armed
    // with a frame that is current. Silent on failure beyond the status
    // line: this runs on its own, and a modal or a cleared form would be
    // the program interrupting work nobody asked it to interrupt.
    void rebuildForNewFrameNumber();

    int  headerFieldBits(const QString &name) const;
    QVector<PacketVary::Rule> readVaryRules() const;   // public below too
    void setVaryRules(const QVector<PacketVary::Rule> &rules);
    void rebuildVaryTable();
    void seedDefaultVaryRule();
    void applyFieldFilter();
    void markChangedFields();
    QString varySummary() const;

    // Serialise/restore everything the operator composed. The SESSION KEY is
    // deliberately excluded: it is a shared secret, and a preset is a file
    // that gets mailed around and committed. Everything else round-trips.
    QJsonObject toPreset() const;
    bool applyPreset(const QJsonObject &o, QString *err);
    void setSendEnabled(bool on);
    void refreshArmState();     // Send is live only when built AND armed
    void disarm(const QString &why);
    UdpSender::PrefixFn headerPrefix(const QString &captype, int packetLen);
    QByteArray currentHeaderPreview(const QString &captype, int packetLen) const;
    QString    currentCaptype() const;

    // Extra header fields. readExtras() reports the first enabled row whose
    // value does not parse or fit; disabled rows are read as best they can
    // be and never block anything.
    QVector<MessageHeader::Extra> readExtras(QString *err = nullptr) const;
    void setExtras(const QVector<MessageHeader::Extra> &extras);
    void appendExtraRow(const MessageHeader::Extra &e);
    void refreshExtrasTitle();
    void fitExtrasTable();

    static QWidget *makeEditor(const Schema::FieldInfo &f, const Schema::Encoder &enc,
                               QWidget *parent);
    static qint64 readEditor(QWidget *w);
    static void   writeEditor(QWidget *w, qint64 v);

    PacketBuilder m_builder;
    UdpSender     m_sender;

    QComboBox    *m_packetCombo = nullptr;
    QLineEdit    *m_destEdit    = nullptr;
    QSpinBox     *m_portSpin    = nullptr;
    QLineEdit    *m_keyEdit     = nullptr;
    QComboBox    *m_keySetBox   = nullptr;   // captured session key sets

    // message header (STRUCT_MESSAGE_HEADER) for slrp/lsrp/aap/aep/arp
    QGroupBox    *m_hdrBox      = nullptr;
    QSpinBox     *m_srcSpin     = nullptr;
    QSpinBox     *m_destSpin    = nullptr;
    QSpinBox     *m_seqSpin     = nullptr;
    QLabel       *m_msgIdLabel  = nullptr;

    // Extra header fields: between the 8-byte header and the packet.
    QGroupBox    *m_extraBox    = nullptr;
    QTableWidget *m_extraTable  = nullptr;
    bool          m_writingExtras = false;   // programmatic fill, not an edit

    // Up to four destinations. A station talks to more than one loco, and
    // sending the same frame to four peers is one transmission with four
    // recipients — the send index advances once, so every peer sees the same
    // FRAME_NUM and the same header seq on the same frame.
    static const int kMaxDests = 4;
    QLineEdit    *m_destHost[kMaxDests] = { nullptr, nullptr, nullptr, nullptr };
    class QSpinBox *m_destPort[kMaxDests] = { nullptr, nullptr, nullptr, nullptr };
    class QCheckBox *m_destOn[kMaxDests] = { nullptr, nullptr, nullptr, nullptr };

    // The enabled, non-empty ones, in order.
    QVector<UdpSender::Target> destinations() const;
    QString destinationSummary() const;

    // The arm switch.
    //
    // The confirmation dialogs went because a modal between the click and
    // the datagram is dead time in which the frame number goes stale. That
    // left Send as a single unguarded click, so the guard moved to a place
    // where it costs no time: arming is a separate, earlier act, and after
    // it the send is immediate.
    //
    // Arming refers to the frame in front of the operator, so anything that
    // REPLACES that frame disarms: a different packet type, a buffer, a
    // preset. Rebuilding for a new frame number does not — that is the same
    // frame with the number it should have had.
    class QCheckBox *m_armed = nullptr;
    bool m_builtOk = false;     // a successful build stands behind Send

    class QCheckBox *m_followFrameNum = nullptr;   // track live arp/lsrp
    bool m_writingFrameNum = false;   // a live update, not an operator edit

    // How many sends have gone out against the current observed frame number.
    // A live rule with a step uses this to advance between observations and
    // re-anchor when the equipment moves on — see PacketVary::LiveFrame.
    mutable qint64 m_lastLiveFrameNum   = -1;
    mutable int    m_sendsAtSameLiveNum = 0;

    QWidget      *m_headerHost  = nullptr;
    class QGroupBox *m_subBox   = nullptr;   // hidden for packets with no subs
    class QSplitter *m_mid      = nullptr;
    class QSplitter *m_vsplit   = nullptr;
    QFormLayout  *m_headerForm  = nullptr;
    QHash<QString, QWidget *> m_headerEditors;

    QListWidget  *m_subList     = nullptr;
    QVector<Schema::SubEntry> m_subs;
    SubPacketWindow *m_subWindow = nullptr;   // detached field editor, one instance
    QPushButton  *m_editSubBtn  = nullptr;
    int           m_curSub      = -1;

    QSpinBox     *m_intervalSpin = nullptr;
    QPushButton  *m_buildBtn    = nullptr;
    QPushButton  *m_sendOnceBtn = nullptr;
    QPushButton  *m_intervalBtn = nullptr;
    QPlainTextEdit *m_preview   = nullptr;
    StatusLine   *m_status      = nullptr;

    // "Fill from buffer": paste bytes, get every header and sub-packet field
    // populated so an existing frame can be edited instead of retyped.
    QPlainTextEdit *m_bufEdit   = nullptr;
    QLabel       *m_bufStatus   = nullptr;

    // "Vary per send": header fields that advance from one interval datagram
    // to the next, so a repeated send is a stream and not the same bytes.
    QTableWidget *m_varyTable   = nullptr;
    class QTabWidget *m_lowerTabs = nullptr;  // Output | Vary per send
    QPushButton  *m_varyAddBtn  = nullptr;
    QPushButton  *m_varyDelBtn  = nullptr;

    // Field filter over the header form. linfo has 171 fields, dmi 88,
    // ccsys 56 — without this they are a wall.
    QLineEdit    *m_fieldFilter = nullptr;
    QCheckBox    *m_onlyChanged = nullptr;
    QPushButton  *m_diffBtn     = nullptr;

    // The frame most recently loaded via "Fill from buffer", kept so the
    // form can be diffed against what was pasted.
    QHash<QString, qint64>    m_refHeader;
    QVector<Schema::SubEntry> m_refSubs;
    bool                      m_haveRef = false;

    QByteArray    m_lastFrame;
    SessionKeyStore *m_keys = nullptr;     // session 96: not owned (unless made here)
};

#endif  // PACKETMAKERDIALOG_H

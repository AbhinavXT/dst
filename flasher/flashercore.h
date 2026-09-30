#ifndef FLASHERCORE_H
#define FLASHERCORE_H
// =============================================================================
//  flashercore.{h,cpp} -- everything the Firmware Flasher decides that does
//  not need a widget to decide it.
//
//  WHY A SEPARATE FILE
//    The flasher window is five screens of widgets, and the rules those
//    screens enforce -- which image belongs to which card, when the Flash
//    button is allowed to light up, what happens to the rest of a batch when
//    one card fails, what goes into the audit trail -- are the part that has
//    to be right on a bench with a chassis on it. Keeping them here, as plain
//    functions and small value types, means dltests can check every one of
//    them without building a window, a socket or a board simulator.
//
//    The widgets in flasherwindow.cpp and friends only present these results
//    and route clicks back into them.
//
//  WHAT IS DELIBERATELY NOT HERE
//    The wire protocol. That lives in flash_engine.{h,cpp}, unchanged from
//    the handoff pack, and nothing in this file sends or receives a byte.
// =============================================================================
#include "profileio.h"
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include "flash_engine.h"

namespace Flasher {

// -----------------------------------------------------------------------------
//  Cards
// -----------------------------------------------------------------------------

// The card-type numbers the firmware's updater keys on (META byte 1). They are
// also the "ID n" shown next to every card name in the UI, because that is how
// the chassis documentation refers to them.
enum CardType {
    CardVcc    = 1,
    CardInput  = 2,
    CardOutput = 3,
    CardAnalog = 4
};

// Every card type, in the order a fresh queue lists them. VCC is last on
// purpose: the IOA images travel THROUGH the VCC, which relays them by card
// ID, so the VCC has to still be running its current image while they go.
// Whether the VCC reboots into a new image after COMPLETE is one of the open
// firmware questions in the handoff; flashing it last is correct either way.
QVector<int> defaultCardOrder();

bool    isValidCardType(int cardType);
QString cardName(int cardType);          // "Input card", "VCC (master)"
QString cardShortName(int cardType);     // "Input", "VCC" -- for the route tiles
QString cardIdText(int cardType);        // "ID 2"

// How a card's image gets to it. The VCC is addressed directly; the other
// three are relayed by the VCC. Shown under the card name on the Flashing
// page, because a slow IOA transfer is often a relay question, not a link one.
QString deliveryText(int cardType);      // "ID 3 · via VCC relay"

// -----------------------------------------------------------------------------
//  Image check: does the file name say it is meant for this card?
// -----------------------------------------------------------------------------
//
//  Until the firmware answers the open question of whether an .appimage
//  carries its own card identifier, the file name is the only evidence the
//  PC has. The builds are named after the card (KAVACH_Input_Card_v5,
//  LKAVACH_v1.2.9 for the VCC), so a case-insensitive token match catches
//  the realistic mistake: the Output build dropped on the Input row.
//
//  Three answers, not two. A name that mentions no card at all is not the
//  same as a name that mentions a different card: the first is merely
//  unproven, the second is very probably wrong. Both are shown in the
//  warning colour and both are refused in Operator mode, but the wording
//  differs so the technician knows which one they are looking at.
enum class NameCheck {
    Matches,        // the name mentions this card and no other
    OtherCard,      // the name mentions a different card (or several)
    Unrecognised,   // the name mentions no card at all
    NoImage         // there is no file to check
};

struct NameCheckResult {
    NameCheck kind = NameCheck::NoImage;
    int       namedCard = 0;       // for OtherCard: the card the name points at (first found)
    QString   badgeText;           // short text for the queue's badge column
    QString   explanation;         // one sentence for the pre-flight list / confirm box
};

// The card types whose tokens appear in `fileName` (the name only; callers
// pass QFileInfo::fileName(), never the directory, because a folder called
// "Input_builds" says nothing about the file inside it).
QList<int> cardsNamedIn(const QString &fileName);

NameCheckResult checkImageName(const QString &fileName, int cardType);

// -----------------------------------------------------------------------------
//  An image on disk, read and hashed
// -----------------------------------------------------------------------------
struct ImageInfo {
    QString    path;               // absolute path as chosen
    bool       loaded = false;     // false = could not be read; see error
    QString    error;
    qint64     sizeBytes = 0;
    quint32    blockCount = 0;     // ceil(size / kBlockSize)
    quint32    crc32 = 0;          // kflash::image_crc32 -- same algorithm as the MCU
    QByteArray sha256;             // 32 raw bytes, kflash::image_sha256
    QDateTime  modified;
    QByteArray bytes;              // the whole file: the engine wants it in memory anyway

    QString crcText() const;       // "0x1A2B3C4D"
    QString shaHex() const;        // 64 lower-case hex characters
};

// Reads `path` whole and computes everything the queue shows. Never throws;
// a problem comes back as loaded == false with a sentence in `error`.
ImageInfo loadImage(const QString &path);

quint32 blocksForSize(qint64 sizeBytes);

// The largest image the Kavach updater will take: MAX_IMAGE_SIZE in the
// updater's firmware_update_user.h (800 KB, the size of its RAM buffer). A
// bigger META is dropped by the board WITHOUT a reply, which the engine can
// only report as "META never acknowledged" -- so the queue refuses such an
// image up front with the real reason. Same limit on the VCC and IOA builds.
const qint64 kBoardMaxImageBytes = 800 * 1024;

// How the updater gets a chance to run: it starts only at power-on, listens
// for 5 s, and boots the application if nothing arrives (FW_WATCHDOG_DEFAULT_MS).
// A human cannot press Flash inside that window reliably, so the flasher
// starts sending FIRST and the operator power-cycles the chassis while it
// waits. These bound how long it waits.
const int kUpdaterListenSeconds     = 5;
const int kDefaultUpdaterWaitSeconds = 60;
const int kMinUpdaterWaitSeconds    = 10;
const int kMaxUpdaterWaitSeconds    = 600;

// -----------------------------------------------------------------------------
//  Profiles: one bench or site, with its address, default images and tuning
// -----------------------------------------------------------------------------
struct FlashProfile {
    QString name = QStringLiteral("Bench");
    QString vccIp = QStringLiteral("192.168.25.168");
    quint16 port = 50001;

    // Default image per card type (1..4). Empty = no default.
    QHash<int, QString> defaultImages;

    // "Pin by SHA-256": when on, the SHA of each default image is recorded
    // when the profile is saved, and the queue warns if the file on disk no
    // longer matches -- the build folder was rebuilt under the profile.
    bool                pinBySha = false;
    QHash<int, QString> pinnedSha;       // card type -> 64-hex SHA-256

    // Transfer tuning. Maps one-to-one onto kflash::TransferTuning.
    kflash::TransferTuning tuning;

    // "Start from this bench's last settled rate". The rate the adaptive
    // pacer settled at on the last accepted card is saved here and, when
    // the box is ticked, used as the next run's starting rate.
    bool   startFromLastSettled = false;
    double lastSettledRateKBps = 0.0;

    // How long to keep offering META while the operator power-cycles the
    // chassis (see kDefaultUpdaterWaitSeconds). Clamped to [min, max].
    int    updaterWaitSeconds = kDefaultUpdaterWaitSeconds;

    QJsonObject toJson() const;
    static FlashProfile fromJson(const QJsonObject &object);
};

// The tuning actually handed to the engine for a card: the profile's tuning,
// with the starting rate replaced by the last settled rate when the profile
// asks for that and one is known. Clamped into [min, max] so a settled rate
// from before the ceiling was lowered cannot start a run above it.
//
// Also turns updaterWaitSeconds into the engine's handshake budget: the
// engine re-sends META every poll_give_up_ms for meta_handshake_tries tries,
// so tries = ceil(wait / give_up). This is an existing TransferTuning field;
// the engine is not changed.
kflash::TransferTuning effectiveTuning(const FlashProfile &profile);

// Profiles live in one JSON file beside the application, next to
// dlconsole.ini. One file rather than QSettings groups because a profile is
// a small nested document (a map of images, a tuning block) and QSettings
// flattens that into keys nobody can read or hand-edit.
class ProfileStore {
public:
    explicit ProfileStore(const QString &filePath);

    // Loads the file. A missing file is not an error: the store then holds
    // one default profile. Returns false only for a file that exists and
    // cannot be parsed; the store still holds the default in that case, and
    // lastError() says why, so the window can warn instead of silently
    // dropping someone's profiles.
    bool load();
    bool save() const;
    QString lastError() const { return m_lastError; }

    QStringList names() const;
    bool        contains(const QString &name) const;
    FlashProfile profile(const QString &name) const;   // default-constructed if absent

    // Insert or replace. If `previousName` is given and differs from the
    // profile's name, the old entry is removed: this is how a rename works.
    void upsert(const FlashProfile &profile, const QString &previousName = QString());
    bool remove(const QString &name);                  // refuses to remove the last one

    QString activeName() const;
    void    setActiveName(const QString &name);

    QString filePath() const { return m_filePath; }

    // Session 94: every profile, in order (for export), and adding an
    // imported one: returns the name it was stored under, empty if skipped.
    QList<FlashProfile> all() const { return m_profiles; }
    QString importProfile(const FlashProfile &profile, ImportClash clash);

private:
    QString               m_filePath;
    QList<FlashProfile>   m_profiles;
    QString               m_activeName;
    mutable QString       m_lastError;
};

// -----------------------------------------------------------------------------
//  Batch outcome for one card
// -----------------------------------------------------------------------------
enum class CardOutcome {
    Queued,         // in the batch, not started yet
    Running,
    Accepted,       // the board reported COMPLETE: shown as "Verified" (see outcomeText)
    AlreadyHeld,    // COMPLETE at the handshake: the board already had it
    Failed,
    Cancelled,      // aborted by the operator while this card was running
    NotRun          // the batch stopped before reaching it
};

QString outcomeText(CardOutcome outcome);              // "Verified", "Not run", ...

// What COMPLETE does and does not prove, per card, for the Summary. The
// updater sends COMPLETE once its RAM copy passes CRC-32 + SHA-256 -- before
// the VCC writes flash, and for an IOA card before the VCC forwards it over
// CAN. Nothing further comes back to the PC.
QString successExplanation(int cardType);
bool    outcomeIsSuccess(CardOutcome outcome);         // Accepted or AlreadyHeld

// The engine reports "board already holds this exact image" only as a log
// line; FlashResult has no field for it. The window watches the log for this
// text so the Summary can say "nothing sent" instead of "accepted".
bool logLineMeansAlreadyHeld(const QString &line);

// A plain-language reason for a failed or cancelled card, for the Summary.
// The engine's own message is technical and written for the log; this is
// written for the technician deciding what to do next. The most important
// case is IMAGE_FAIL: the board received every block and rejected the image,
// so resending the same file will fail the same way.
QString failureExplanation(const kflash::FlashResult &result);

// Was this failure an IMAGE_FAIL? (Drives the explanation above and the
// "Last status" row on the Summary card.)
bool isImageFail(const kflash::FlashResult &result);

// -----------------------------------------------------------------------------
//  The batch: which card runs next, and when the batch stops
// -----------------------------------------------------------------------------
//
//  The engine flashes one card per call. The batch is the window's job, and
//  its rules live here so they can be tested without threads:
//
//    * Cards run in queue order.
//    * After a failure, the batch stops if "Stop batch on first failure" is
//      on; everything not yet started becomes Not run.
//    * "Stop after this card" lets the running card finish (success or not)
//      and then stops the same way.
//    * Abort cancels the running card; the batch stops.
//
struct BatchEntry {
    int          cardType = 0;
    ImageInfo    image;
    CardOutcome  outcome = CardOutcome::Queued;
    kflash::FlashResult result;
    QStringList  log;             // this card's session log, timestamped, for history
    QDateTime    startedAt;
};

class BatchPlan {
public:
    BatchPlan() = default;
    BatchPlan(const QVector<BatchEntry> &entries, bool stopOnFirstFailure);

    // Index of the next card to run, marking it Running; -1 when the batch is
    // over (all run, or stopped). When the batch is stopping, every entry
    // still Queued is turned into NotRun here, so the Summary never shows a
    // card as "queued" after the batch has ended.
    int  startNext();

    // Record the engine's result for the card currently Running.
    void finishCurrent(const kflash::FlashResult &result, bool alreadyHeld);

    void requestStopAfterCurrent() { m_stopAfterCurrent = true; }
    void requestAbort()            { m_aborted = true; }

    bool stopAfterCurrentRequested() const { return m_stopAfterCurrent; }
    bool abortRequested() const            { return m_aborted; }
    bool isFinished() const                { return m_finished; }

    int  currentIndex() const { return m_current; }
    int  size() const         { return m_entries.size(); }

    const QVector<BatchEntry> &entries() const { return m_entries; }
    BatchEntry &entry(int index)               { return m_entries[index]; }

    int succeededCount() const;
    int failedCount() const;      // Failed + Cancelled
    int notRunCount() const;

    // Why the batch ended short, for the Summary banner. Empty when every
    // card was reached.
    QString stopReason() const { return m_stopReason; }

private:
    QVector<BatchEntry> m_entries;
    bool    m_stopOnFirstFailure = true;
    bool    m_stopAfterCurrent = false;
    bool    m_aborted = false;
    bool    m_stopping = false;
    bool    m_finished = false;
    int     m_current = -1;
    QString m_stopReason;
};

// The Summary's verdict banner. Written for one card per run (the flasher's
// mode since the updater only runs after a power cycle); the multi-card
// wording is kept for BatchPlan's own tests.
struct Verdict {
    enum class Tone { Success, Partial, Failure };
    Tone    tone = Tone::Success;
    QString headline;       // "Batch stopped — 1 of 3 cards updated"
    QString explanation;    // one or two sentences under it
};
Verdict batchVerdict(const BatchPlan &plan);

// -----------------------------------------------------------------------------
//  Pre-flight checklist
// -----------------------------------------------------------------------------
enum class Mode { Engineer, Operator };

struct PreflightRow {           // one queue row as the checklist sees it
    int       cardType = 0;
    bool      ticked = false;
    bool      hasPath = false;
    bool      loaded = false;
    qint64    sizeBytes = 0;
    QString   loadError;
    QString   fileName;
    NameCheckResult nameCheck;
    bool      shaPinMismatch = false;   // profile pinned a SHA and the file no longer matches
    QString   engineProblem;            // non-empty = FlashEngine::validate refused it
};

struct PreflightInput {
    Mode    mode = Mode::Engineer;
    QString vccIp;
    quint16 port = 0;
    bool    adapterOnSubnet = false;
    QString adapterDescription;     // "Ethernet 2 · 192.168.25.10"
    QString subnetText;             // "192.168.25.x"
    int     updaterWaitSeconds = kDefaultUpdaterWaitSeconds;
    QVector<PreflightRow> rows;     // queue order
};

struct PreflightItem {
    enum class State { Ok, Warning };
    State   state = State::Ok;
    QString text;
    bool    blocksFlash = false;    // a Warning that keeps the Flash button disabled
};

struct PreflightReport {
    QVector<PreflightItem> items;
    bool    ready = false;          // no blocking item
    int     tickedCount = 0;
    QString firstBlocker;           // shown inline in the action bar
    bool    needsNameConfirmation = false;   // Engineer mode, some name did not match
};

PreflightReport evaluatePreflight(const PreflightInput &input);

// Is `adapterIp` on the same IPv4 subnet as `targetIp`, given the adapter's
// netmask? All three in host byte order. A zero netmask is treated as "no
// information" and answers false rather than matching everything.
bool sameSubnet(quint32 adapterIp, quint32 netmask, quint32 targetIp);

// "192.168.25.x" for a /24, "10.1.x.x" for a /16, "a.b.c.d/n" otherwise.
QString subnetDescription(quint32 ip, quint32 netmask);

// -----------------------------------------------------------------------------
//  History: an append-only audit trail of every card a batch touched
// -----------------------------------------------------------------------------
struct HistoryRecord {
    QDateTime   when;               // UTC; shown in local time
    QString     batchId;
    QString     operatorName;
    QString     chassis;            // the profile name: "Bench — Chassis A"
    QString     vccIp;
    int         cardType = 0;
    QString     imagePath;
    QString     crcText;
    QString     sha256;
    QString     result;             // outcomeText()
    QString     message;            // engine message
    qint64      totalMs = 0;
    qint64      handshakeMs = 0;
    qint64      sendMs = 0;
    qint64      repairMs = 0;
    int         repairRounds = 0;
    quint32     totalBlocks = 0;
    quint32     blocksResent = 0;
    quint64     bytesOnWire = 0;
    double      avgKBps = 0.0;
    double      settledRateKBps = 0.0;
    QStringList log;

    QJsonObject toJson() const;
    static HistoryRecord fromJson(const QJsonObject &object);
    QString fileName() const;
};

// The record for one batch entry.
HistoryRecord makeHistoryRecord(const BatchEntry &entry, const QString &batchId,
                                const QString &operatorName, const QString &chassis,
                                const QString &vccIp);

// JSON Lines, one record per line, only ever appended to. Appending a line
// is the one write that cannot damage what is already there, which is the
// property an audit trail needs; a half-written last line (power cut
// mid-write) is skipped on read instead of losing the file.
class HistoryLog {
public:
    explicit HistoryLog(const QString &filePath);

    bool append(const HistoryRecord &record);
    bool appendAll(const QList<HistoryRecord> &records);

    // Every readable record, oldest first. `skipped` (optional) receives the
    // number of lines that could not be parsed.
    QList<HistoryRecord> readAll(int *skipped = nullptr) const;

    QString filePath() const { return m_filePath; }
    QString lastError() const { return m_lastError; }

private:
    QString         m_filePath;
    mutable QString m_lastError;
};

struct HistoryFilter {
    QString text;                   // matched against file, chassis, operator, CRC, message
    int     cardType = 0;           // 0 = all
    QString result;                 // empty = all, else outcomeText()
    int     lastDays = 0;           // 0 = all time
};
bool historyMatches(const HistoryRecord &record, const HistoryFilter &filter,
                    const QDateTime &nowUtc);

// CSV for "Export CSV": header row plus one row per record, RFC 4180 quoting.
QString historyToCsv(const QList<HistoryRecord> &records);

// -----------------------------------------------------------------------------
//  Small formatting helpers shared by the pages
// -----------------------------------------------------------------------------
QString formatBytes(quint64 bytes);            // "592.6 KB"
QString formatDuration(qint64 milliseconds);   // "0.4 s", "1 min 12 s"
QString formatClock(qint64 milliseconds);      // "00:02", "01:12:05"
QString formatRate(double kBps);               // "1 101 KB/s"
QString newBatchId(const QDateTime &nowUtc);   // "20260924-1403-7Q2F"
QString currentOperatorName();                 // Windows user name, or $USER

// The text report written by "Export report" on the Summary page.
QString batchReportText(const BatchPlan &plan, const QString &batchId,
                        const QString &operatorName, const QString &chassis,
                        const QString &vccIp, quint16 port);

// ---- export / import to another PC (session 94) -------------------------------
// A .dlflash file: {"format":"dlconsole-flasher-profiles","version":1,
// "profiles":[...]}, each exactly as flasher_profiles.json stores it. The
// default images are PATHS on the exporting PC; importedImageProblems()
// names the ones this PC does not have, so they can be re-pointed.
extern const char *kFlasherProfilesFormat;
QByteArray exportProfiles(const QList<FlashProfile> &profiles);

struct ImportedProfiles {
    bool                ok = false;
    QString             error;
    QList<FlashProfile> profiles;
};
ImportedProfiles importProfiles(const QByteArray &json);

// "VCC: D:/builds/vcc.appimage is not on this PC", one per default image
// that does not exist here.
QStringList importedImageProblems(const FlashProfile &profile);

}  // namespace Flasher

#endif  // FLASHERCORE_H

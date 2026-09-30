#include "flashercore.h"
#include <QSet>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace Flasher {

// =============================================================================
//  Cards
// =============================================================================

QVector<int> defaultCardOrder()
{
    // IOA cards first, VCC last -- see the header for why.
    return QVector<int>{ CardInput, CardOutput, CardAnalog, CardVcc };
}

bool isValidCardType(int cardType)
{
    return cardType >= CardVcc && cardType <= CardAnalog;
}

QString cardName(int cardType)
{
    switch (cardType) {
    case CardVcc:    return QStringLiteral("VCC (master)");
    case CardInput:  return QStringLiteral("Input card");
    case CardOutput: return QStringLiteral("Output card");
    case CardAnalog: return QStringLiteral("Analog card");
    default:         return QStringLiteral("Unknown card");
    }
}

QString cardShortName(int cardType)
{
    switch (cardType) {
    case CardVcc:    return QStringLiteral("VCC");
    case CardInput:  return QStringLiteral("Input");
    case CardOutput: return QStringLiteral("Output");
    case CardAnalog: return QStringLiteral("Analog");
    default:         return QStringLiteral("?");
    }
}

QString cardIdText(int cardType)
{
    return QStringLiteral("ID %1").arg(cardType);
}

QString deliveryText(int cardType)
{
    if (cardType == CardVcc) {
        return QStringLiteral("%1 · direct").arg(cardIdText(cardType));
    }
    return QStringLiteral("%1 · via VCC relay").arg(cardIdText(cardType));
}

// =============================================================================
//  Image name check
// =============================================================================

QList<int> cardsNamedIn(const QString &fileName)
{
    // Upper-cased once so every comparison below is case-insensitive.
    const QString upperName = fileName.toUpper();
    QList<int> named;

    // VCC answers to two tokens: "VCC" and "LKAVACH" (the VCC build is the
    // loco Kavach application). Note that the IOA builds are called
    // KAVACH_..., which does NOT contain "LKAVACH", so they are not mistaken
    // for the VCC.
    if (upperName.contains(QLatin1String("VCC")) || upperName.contains(QLatin1String("LKAVACH"))) {
        named.append(CardVcc);
    }
    if (upperName.contains(QLatin1String("INPUT"))) {
        named.append(CardInput);
    }
    if (upperName.contains(QLatin1String("OUTPUT"))) {
        named.append(CardOutput);
    }
    if (upperName.contains(QLatin1String("ANALOG"))) {
        named.append(CardAnalog);
    }
    return named;
}

NameCheckResult checkImageName(const QString &fileName, int cardType)
{
    NameCheckResult check;
    if (fileName.isEmpty()) {
        check.kind = NameCheck::NoImage;
        check.badgeText = QStringLiteral("No image");
        check.explanation = QStringLiteral("%1: no image chosen").arg(cardName(cardType));
        return check;
    }

    const QList<int> named = cardsNamedIn(fileName);

    if (named.size() == 1 && named.first() == cardType) {
        check.kind = NameCheck::Matches;
        check.namedCard = cardType;
        check.badgeText = QStringLiteral("Name matches %1").arg(cardShortName(cardType));
        check.explanation = QStringLiteral("%1: file name matches the card").arg(cardName(cardType));
        return check;
    }

    if (named.isEmpty()) {
        check.kind = NameCheck::Unrecognised;
        check.badgeText = QStringLiteral("Name doesn't say");
        check.explanation = QStringLiteral("%1: \"%2\" does not name any card, so it cannot be "
                                           "checked against this slot")
                                .arg(cardName(cardType), fileName);
        return check;
    }

    // One or more cards are named, and it is not exactly this one. Report the
    // first card named that is NOT this one: that is the likely mix-up.
    int otherCard = named.first();
    for (int candidate : named) {
        if (candidate != cardType) {
            otherCard = candidate;
            break;
        }
    }
    check.kind = NameCheck::OtherCard;
    check.namedCard = otherCard;
    check.badgeText = QStringLiteral("Looks like %1").arg(cardShortName(otherCard));
    check.explanation = QStringLiteral("%1: \"%2\" looks like the %3 image")
                            .arg(cardName(cardType), fileName, cardShortName(otherCard));
    return check;
}

// =============================================================================
//  Images
// =============================================================================

quint32 blocksForSize(qint64 sizeBytes)
{
    if (sizeBytes <= 0) {
        return 0;
    }
    const qint64 blockSize = static_cast<qint64>(kflash::kBlockSize);
    return static_cast<quint32>((sizeBytes + blockSize - 1) / blockSize);
}

QString ImageInfo::crcText() const
{
    // Lower-case "0x", upper-case digits: how the engine's log and the
    // firmware team write CRCs, so the two can be compared by eye.
    return QStringLiteral("0x") + QStringLiteral("%1").arg(crc32, 8, 16, QLatin1Char('0')).toUpper();
}

QString ImageInfo::shaHex() const
{
    return QString::fromLatin1(sha256.toHex());
}

ImageInfo loadImage(const QString &path)
{
    ImageInfo info;
    info.path = path;

    QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        info.error = QStringLiteral("File not found");
        return info;
    }
    if (!fileInfo.isFile()) {
        info.error = QStringLiteral("Not a file");
        return info;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        info.error = QStringLiteral("Cannot read: %1").arg(file.errorString());
        return info;
    }
    info.bytes = file.readAll();
    file.close();

    if (info.bytes.isEmpty()) {
        info.error = QStringLiteral("File is empty");
        return info;
    }

    // Both digests come from the engine's own implementations, not from
    // QCryptographicHash: they must be byte-for-byte what the engine will
    // put into META, and the firmware checks against exactly those.
    const uint8_t *data = reinterpret_cast<const uint8_t *>(info.bytes.constData());
    const size_t length = static_cast<size_t>(info.bytes.size());
    info.crc32 = kflash::image_crc32(data, length);

    uint8_t digest[32];
    kflash::image_sha256(data, length, digest);
    info.sha256 = QByteArray(reinterpret_cast<const char *>(digest), 32);

    info.sizeBytes = info.bytes.size();
    info.blockCount = blocksForSize(info.sizeBytes);
    info.modified = fileInfo.lastModified();
    info.loaded = true;
    return info;
}

// =============================================================================
//  Profiles
// =============================================================================

namespace {

QJsonObject tuningToJson(const kflash::TransferTuning &tuning)
{
    QJsonObject object;
    object[QStringLiteral("adaptive_rate")]           = tuning.adaptive_rate;
    object[QStringLiteral("start_rate_kBps")]         = static_cast<double>(tuning.start_rate_kBps);
    object[QStringLiteral("min_rate_kBps")]           = static_cast<double>(tuning.min_rate_kBps);
    object[QStringLiteral("max_rate_kBps")]           = static_cast<double>(tuning.max_rate_kBps);
    object[QStringLiteral("burst_packets")]           = static_cast<double>(tuning.burst_packets);
    object[QStringLiteral("checkpoint_every_blocks")] = static_cast<double>(tuning.checkpoint_every_blocks);
    object[QStringLiteral("poll_timeout_min_ms")]     = static_cast<double>(tuning.poll_timeout_min_ms);
    object[QStringLiteral("poll_timeout_max_ms")]     = static_cast<double>(tuning.poll_timeout_max_ms);
    object[QStringLiteral("poll_give_up_ms")]         = static_cast<double>(tuning.poll_give_up_ms);
    object[QStringLiteral("max_rounds")]              = tuning.max_rounds;
    object[QStringLiteral("meta_repeats")]            = tuning.meta_repeats;
    object[QStringLiteral("meta_handshake_tries")]    = tuning.meta_handshake_tries;
    object[QStringLiteral("max_image_fail_restarts")] = tuning.max_image_fail_restarts;
    return object;
}

// Read an unsigned field, keeping the default when the key is missing or
// holds something that is not a non-negative number. A hand-edited profile
// with a typo must not turn into a zero rate.
uint32_t readUnsigned(const QJsonObject &object, const char *key, uint32_t fallback)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble()) {
        return fallback;
    }
    const double number = value.toDouble();
    if (number < 0.0 || number > 4294967295.0) {
        return fallback;
    }
    return static_cast<uint32_t>(number);
}

int readInt(const QJsonObject &object, const char *key, int fallback)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble()) {
        return fallback;
    }
    return value.toInt(fallback);
}

kflash::TransferTuning tuningFromJson(const QJsonObject &object)
{
    kflash::TransferTuning tuning;   // defaults for anything missing
    if (object.contains(QStringLiteral("adaptive_rate"))) {
        tuning.adaptive_rate = object.value(QStringLiteral("adaptive_rate")).toBool(true);
    }
    tuning.start_rate_kBps         = readUnsigned(object, "start_rate_kBps", tuning.start_rate_kBps);
    tuning.min_rate_kBps           = readUnsigned(object, "min_rate_kBps", tuning.min_rate_kBps);
    tuning.max_rate_kBps           = readUnsigned(object, "max_rate_kBps", tuning.max_rate_kBps);
    tuning.burst_packets           = readUnsigned(object, "burst_packets", tuning.burst_packets);
    tuning.checkpoint_every_blocks = readUnsigned(object, "checkpoint_every_blocks", tuning.checkpoint_every_blocks);
    tuning.poll_timeout_min_ms     = readUnsigned(object, "poll_timeout_min_ms", tuning.poll_timeout_min_ms);
    tuning.poll_timeout_max_ms     = readUnsigned(object, "poll_timeout_max_ms", tuning.poll_timeout_max_ms);
    tuning.poll_give_up_ms         = readUnsigned(object, "poll_give_up_ms", tuning.poll_give_up_ms);
    tuning.max_rounds              = readInt(object, "max_rounds", tuning.max_rounds);
    tuning.meta_repeats            = readInt(object, "meta_repeats", tuning.meta_repeats);
    tuning.meta_handshake_tries    = readInt(object, "meta_handshake_tries", tuning.meta_handshake_tries);
    tuning.max_image_fail_restarts = readInt(object, "max_image_fail_restarts", tuning.max_image_fail_restarts);
    return tuning;
}

}  // namespace

QJsonObject FlashProfile::toJson() const
{
    QJsonObject object;
    object[QStringLiteral("name")] = name;
    object[QStringLiteral("vcc_ip")] = vccIp;
    object[QStringLiteral("port")] = static_cast<int>(port);

    // Card types are written as their number so the file does not depend on
    // how the UI happens to spell a card's name.
    QJsonObject images;
    for (auto iterator = defaultImages.constBegin(); iterator != defaultImages.constEnd(); ++iterator) {
        if (!iterator.value().isEmpty()) {
            images[QString::number(iterator.key())] = iterator.value();
        }
    }
    object[QStringLiteral("default_images")] = images;

    object[QStringLiteral("pin_by_sha")] = pinBySha;
    QJsonObject pins;
    for (auto iterator = pinnedSha.constBegin(); iterator != pinnedSha.constEnd(); ++iterator) {
        if (!iterator.value().isEmpty()) {
            pins[QString::number(iterator.key())] = iterator.value();
        }
    }
    object[QStringLiteral("pinned_sha256")] = pins;

    object[QStringLiteral("tuning")] = tuningToJson(tuning);
    object[QStringLiteral("start_from_last_settled")] = startFromLastSettled;
    object[QStringLiteral("last_settled_rate_kBps")] = lastSettledRateKBps;
    object[QStringLiteral("updater_wait_s")] = updaterWaitSeconds;
    return object;
}

FlashProfile FlashProfile::fromJson(const QJsonObject &object)
{
    FlashProfile profile;
    const QString storedName = object.value(QStringLiteral("name")).toString();
    if (!storedName.trimmed().isEmpty()) {
        profile.name = storedName.trimmed();
    }
    const QString storedIp = object.value(QStringLiteral("vcc_ip")).toString();
    if (!storedIp.isEmpty()) {
        profile.vccIp = storedIp;
    }
    const int storedPort = object.value(QStringLiteral("port")).toInt(0);
    if (storedPort > 0 && storedPort <= 65535) {
        profile.port = static_cast<quint16>(storedPort);
    }

    const QJsonObject images = object.value(QStringLiteral("default_images")).toObject();
    for (auto iterator = images.constBegin(); iterator != images.constEnd(); ++iterator) {
        const int cardType = iterator.key().toInt();
        if (isValidCardType(cardType)) {
            profile.defaultImages.insert(cardType, iterator.value().toString());
        }
    }

    profile.pinBySha = object.value(QStringLiteral("pin_by_sha")).toBool(false);
    const QJsonObject pins = object.value(QStringLiteral("pinned_sha256")).toObject();
    for (auto iterator = pins.constBegin(); iterator != pins.constEnd(); ++iterator) {
        const int cardType = iterator.key().toInt();
        if (isValidCardType(cardType)) {
            profile.pinnedSha.insert(cardType, iterator.value().toString().toLower());
        }
    }

    profile.tuning = tuningFromJson(object.value(QStringLiteral("tuning")).toObject());
    profile.startFromLastSettled = object.value(QStringLiteral("start_from_last_settled")).toBool(false);
    profile.lastSettledRateKBps = object.value(QStringLiteral("last_settled_rate_kBps")).toDouble(0.0);

    int waitSeconds = object.value(QStringLiteral("updater_wait_s")).toInt(kDefaultUpdaterWaitSeconds);
    if (waitSeconds < kMinUpdaterWaitSeconds) {
        waitSeconds = kMinUpdaterWaitSeconds;
    }
    if (waitSeconds > kMaxUpdaterWaitSeconds) {
        waitSeconds = kMaxUpdaterWaitSeconds;
    }
    profile.updaterWaitSeconds = waitSeconds;
    return profile;
}

kflash::TransferTuning effectiveTuning(const FlashProfile &profile)
{
    kflash::TransferTuning tuning = profile.tuning;
    if (profile.startFromLastSettled && profile.lastSettledRateKBps >= 1.0) {
        double startRate = profile.lastSettledRateKBps;
        if (startRate < static_cast<double>(tuning.min_rate_kBps)) {
            startRate = static_cast<double>(tuning.min_rate_kBps);
        }
        if (startRate > static_cast<double>(tuning.max_rate_kBps)) {
            startRate = static_cast<double>(tuning.max_rate_kBps);
        }
        tuning.start_rate_kBps = static_cast<uint32_t>(std::lround(startRate));
    }

    // Handshake budget: enough META attempts to cover the whole wait. Never
    // fewer than the profile's own setting.
    int waitSeconds = profile.updaterWaitSeconds;
    if (waitSeconds < kMinUpdaterWaitSeconds) {
        waitSeconds = kMinUpdaterWaitSeconds;
    }
    if (waitSeconds > kMaxUpdaterWaitSeconds) {
        waitSeconds = kMaxUpdaterWaitSeconds;
    }
    uint32_t giveUpMs = tuning.poll_give_up_ms;
    if (giveUpMs == 0) {
        giveUpMs = 1;
    }
    const uint32_t waitMs = static_cast<uint32_t>(waitSeconds) * 1000u;
    const int triesForWait = static_cast<int>((waitMs + giveUpMs - 1) / giveUpMs);
    if (triesForWait > tuning.meta_handshake_tries) {
        tuning.meta_handshake_tries = triesForWait;
    }
    return tuning;
}

ProfileStore::ProfileStore(const QString &filePath)
    : m_filePath(filePath)
{
    // Never empty: every consumer can assume at least one profile exists.
    m_profiles.append(FlashProfile());
    m_activeName = m_profiles.first().name;
}

bool ProfileStore::load()
{
    m_lastError.clear();
    QFile file(m_filePath);
    if (!file.exists()) {
        return true;    // first run: keep the default profile
    }
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QStringLiteral("Cannot read %1: %2").arg(m_filePath, file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_lastError = QStringLiteral("%1 is not valid JSON (%2)").arg(m_filePath, parseError.errorString());
        return false;
    }

    QList<FlashProfile> loaded;
    const QJsonArray array = document.object().value(QStringLiteral("profiles")).toArray();
    for (const QJsonValue &value : array) {
        FlashProfile profile = FlashProfile::fromJson(value.toObject());
        // Duplicate names would make the combo box ambiguous; the first wins.
        bool duplicate = false;
        for (const FlashProfile &existing : loaded) {
            if (existing.name == profile.name) {
                duplicate = true;
            }
        }
        if (!duplicate) {
            loaded.append(profile);
        }
    }
    if (loaded.isEmpty()) {
        return true;    // an empty file behaves like a missing one
    }
    m_profiles = loaded;

    const QString active = document.object().value(QStringLiteral("active")).toString();
    if (contains(active)) {
        m_activeName = active;
    } else {
        m_activeName = m_profiles.first().name;
    }
    return true;
}

bool ProfileStore::save() const
{
    QJsonArray array;
    for (const FlashProfile &profile : m_profiles) {
        array.append(profile.toJson());
    }
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("active")] = m_activeName;
    root[QStringLiteral("profiles")] = array;

    // QSaveFile writes to a temporary and renames on commit, so a crash
    // mid-save leaves the previous profiles intact instead of a truncated file.
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = QStringLiteral("Cannot write %1: %2").arg(m_filePath, file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        m_lastError = QStringLiteral("Cannot save %1: %2").arg(m_filePath, file.errorString());
        return false;
    }
    return true;
}

QStringList ProfileStore::names() const
{
    QStringList list;
    for (const FlashProfile &profile : m_profiles) {
        list.append(profile.name);
    }
    return list;
}

bool ProfileStore::contains(const QString &name) const
{
    for (const FlashProfile &profile : m_profiles) {
        if (profile.name == name) {
            return true;
        }
    }
    return false;
}

FlashProfile ProfileStore::profile(const QString &name) const
{
    for (const FlashProfile &profile : m_profiles) {
        if (profile.name == name) {
            return profile;
        }
    }
    return FlashProfile();
}

void ProfileStore::upsert(const FlashProfile &profile, const QString &previousName)
{
    // A rename: drop the entry under its old name first, remembering where it
    // was so the renamed profile keeps its place in the combo box.
    int insertAt = -1;
    if (!previousName.isEmpty() && previousName != profile.name) {
        for (int index = 0; index < m_profiles.size(); ++index) {
            if (m_profiles[index].name == previousName) {
                m_profiles.removeAt(index);
                insertAt = index;
                break;
            }
        }
        if (m_activeName == previousName) {
            m_activeName = profile.name;
        }
    }

    for (int index = 0; index < m_profiles.size(); ++index) {
        if (m_profiles[index].name == profile.name) {
            m_profiles[index] = profile;
            return;
        }
    }
    if (insertAt >= 0 && insertAt <= m_profiles.size()) {
        m_profiles.insert(insertAt, profile);
    } else {
        m_profiles.append(profile);
    }
}

bool ProfileStore::remove(const QString &name)
{
    if (m_profiles.size() <= 1) {
        return false;
    }
    for (int index = 0; index < m_profiles.size(); ++index) {
        if (m_profiles[index].name == name) {
            m_profiles.removeAt(index);
            if (m_activeName == name) {
                m_activeName = m_profiles.first().name;
            }
            return true;
        }
    }
    return false;
}

QString ProfileStore::activeName() const
{
    return m_activeName;
}

void ProfileStore::setActiveName(const QString &name)
{
    if (contains(name)) {
        m_activeName = name;
    }
}

// =============================================================================
//  Outcomes and explanations
// =============================================================================

QString outcomeText(CardOutcome outcome)
{
    switch (outcome) {
    case CardOutcome::Queued:      return QStringLiteral("Queued");
    case CardOutcome::Running:     return QStringLiteral("Running");
    // "Verified", not "Accepted" or "Updated": COMPLETE proves the image
    // arrived intact, not that it was written or is running (see
    // successExplanation).
    case CardOutcome::Accepted:    return QStringLiteral("Verified");
    case CardOutcome::AlreadyHeld: return QStringLiteral("Already held");
    case CardOutcome::Failed:      return QStringLiteral("Failed");
    case CardOutcome::Cancelled:   return QStringLiteral("Cancelled");
    case CardOutcome::NotRun:      return QStringLiteral("Not run");
    }
    return QStringLiteral("?");
}

QString successExplanation(int cardType)
{
    if (cardType == CardVcc) {
        return QStringLiteral("The VCC verified the image (CRC-32 + SHA-256). It now writes it to its "
                              "spare flash bank and boots it. Keep the chassis powered until the VCC "
                              "is back up.");
    }
    return QStringLiteral("The VCC verified the image (CRC-32 + SHA-256) and is forwarding it to "
                          "the %1 over CAN; the card then checks it again and writes it. The PC gets "
                          "no confirmation from the card itself. Keep the chassis powered until the "
                          "card is back up.").arg(cardName(cardType));
}

bool outcomeIsSuccess(CardOutcome outcome)
{
    return outcome == CardOutcome::Accepted || outcome == CardOutcome::AlreadyHeld;
}

bool logLineMeansAlreadyHeld(const QString &line)
{
    // The exact wording of flash_engine.cpp's handshake branch. If the engine's
    // wording ever changes, the only consequence is that such a card shows as
    // "Accepted" instead of "Already held" -- the success verdict itself
    // comes from FlashResult::success, never from this text.
    return line.contains(QLatin1String("already holds this exact image"), Qt::CaseInsensitive);
}

bool isImageFail(const kflash::FlashResult &result)
{
    return QString::fromStdString(result.message).contains(QLatin1String("IMAGE_FAIL"));
}

QString failureExplanation(const kflash::FlashResult &result)
{
    const QString message = QString::fromStdString(result.message);

    if (result.cancelled) {
        return QStringLiteral("Stopped by the operator while this card was being flashed. "
                              "The board is left part-way through a transfer; a fresh run "
                              "starts it again from the beginning.");
    }
    if (isImageFail(result)) {
        return QStringLiteral("IMAGE_FAIL — the board received every block but rejected the "
                              "image. Resending the same file won't help — check it's the "
                              "right build for this card.");
    }
    if (message.contains(QLatin1String("META never acknowledged"))) {
        return QStringLiteral("The board never answered. The updater only listens for 5 s after "
                              "power-on: press Flash first, then power-cycle the chassis while the "
                              "flasher is waiting. If that was done, check the cable, the IP "
                              "address and the port.");
    }
    if (message.contains(QLatin1String("No STATUS"))) {
        return QStringLiteral("The board stopped answering part-way through. Check power and "
                              "the link, then retry — the transfer restarts from the beginning.");
    }
    if (message.contains(QLatin1String("max repair rounds"))) {
        return QStringLiteral("Too many blocks kept going missing. The link is losing packets; "
                              "try a lower start or maximum rate in the profile.");
    }
    if (message.contains(QLatin1String("Socket"))) {
        return QStringLiteral("This PC could not open a network socket. Check the adapter is up.");
    }
    if (message.isEmpty()) {
        return QStringLiteral("The flash did not complete.");
    }
    return message;
}

// =============================================================================
//  Batch
// =============================================================================

BatchPlan::BatchPlan(const QVector<BatchEntry> &entries, bool stopOnFirstFailure)
    : m_entries(entries)
    , m_stopOnFirstFailure(stopOnFirstFailure)
{
}

int BatchPlan::startNext()
{
    if (m_finished) {
        return -1;
    }

    // Find the next card still waiting, unless the batch has been told to stop.
    int next = -1;
    if (!m_stopping) {
        for (int index = m_current + 1; index < m_entries.size(); ++index) {
            if (m_entries[index].outcome == CardOutcome::Queued) {
                next = index;
                break;
            }
        }
    }

    if (next < 0) {
        // The batch is over. Anything not reached is Not run, never Queued.
        for (BatchEntry &entry : m_entries) {
            if (entry.outcome == CardOutcome::Queued) {
                entry.outcome = CardOutcome::NotRun;
            }
        }
        m_finished = true;
        m_current = -1;
        return -1;
    }

    m_current = next;
    m_entries[next].outcome = CardOutcome::Running;
    m_entries[next].startedAt = QDateTime::currentDateTimeUtc();
    return next;
}

void BatchPlan::finishCurrent(const kflash::FlashResult &result, bool alreadyHeld)
{
    if (m_current < 0 || m_current >= m_entries.size()) {
        return;
    }
    BatchEntry &entry = m_entries[m_current];
    entry.result = result;

    if (result.success) {
        if (alreadyHeld) {
            entry.outcome = CardOutcome::AlreadyHeld;
        } else {
            entry.outcome = CardOutcome::Accepted;
        }
    } else if (result.cancelled || m_aborted) {
        entry.outcome = CardOutcome::Cancelled;
    } else {
        entry.outcome = CardOutcome::Failed;
    }

    // Decide whether anything else runs. The order of these tests is the
    // order of precedence for the reason shown on the Summary banner.
    if (entry.outcome == CardOutcome::Cancelled) {
        m_stopping = true;
        m_stopReason = QStringLiteral("the batch was aborted");
    } else if (entry.outcome == CardOutcome::Failed && m_stopOnFirstFailure) {
        m_stopping = true;
        m_stopReason = QStringLiteral("“Stop batch on first failure” was on");
    } else if (m_stopAfterCurrent) {
        m_stopping = true;
        m_stopReason = QStringLiteral("“Stop after this card” was pressed");
    }
}

int BatchPlan::succeededCount() const
{
    int count = 0;
    for (const BatchEntry &entry : m_entries) {
        if (outcomeIsSuccess(entry.outcome)) {
            ++count;
        }
    }
    return count;
}

int BatchPlan::failedCount() const
{
    int count = 0;
    for (const BatchEntry &entry : m_entries) {
        if (entry.outcome == CardOutcome::Failed || entry.outcome == CardOutcome::Cancelled) {
            ++count;
        }
    }
    return count;
}

int BatchPlan::notRunCount() const
{
    int count = 0;
    for (const BatchEntry &entry : m_entries) {
        if (entry.outcome == CardOutcome::NotRun) {
            ++count;
        }
    }
    return count;
}

Verdict batchVerdict(const BatchPlan &plan)
{
    Verdict verdict;
    const int total = plan.size();
    const int succeeded = plan.succeededCount();
    const int failed = plan.failedCount();
    const int notRun = plan.notRunCount();

    QString cardWord = QStringLiteral("cards");
    if (total == 1) {
        cardWord = QStringLiteral("card");
    }

    if (failed == 0 && notRun == 0) {
        verdict.tone = Verdict::Tone::Success;
        if (total == 1) {
            const BatchEntry &only = plan.entries().first();
            if (only.outcome == CardOutcome::AlreadyHeld) {
                verdict.headline = QStringLiteral("%1 already had this image").arg(cardName(only.cardType));
                verdict.explanation = QStringLiteral("Nothing was sent.");
            } else {
                verdict.headline = QStringLiteral("%1 — image verified").arg(cardName(only.cardType));
                verdict.explanation = successExplanation(only.cardType);
            }
        } else {
            verdict.headline = QStringLiteral("All %1 cards updated").arg(total);
            verdict.explanation = QStringLiteral("Every card in the batch reported COMPLETE for its image.");
        }
        return verdict;
    }

    if (succeeded == 0) {
        verdict.tone = Verdict::Tone::Failure;
    } else {
        verdict.tone = Verdict::Tone::Partial;
    }

    // One card: say what happened to it, in the Summary's own words.
    if (total == 1) {
        const BatchEntry &only = plan.entries().first();
        if (only.outcome == CardOutcome::Cancelled) {
            verdict.headline = QStringLiteral("%1 — aborted").arg(cardName(only.cardType));
        } else {
            verdict.headline = QStringLiteral("%1 — not updated").arg(cardName(only.cardType));
        }
        verdict.explanation = failureExplanation(only.result);
        return verdict;
    }

    if (notRun > 0) {
        verdict.headline = QStringLiteral("Batch stopped — %1 of %2 %3 updated")
                               .arg(succeeded).arg(total).arg(cardWord);
    } else {
        verdict.headline = QStringLiteral("Batch finished — %1 of %2 %3 updated")
                               .arg(succeeded).arg(total).arg(cardWord);
    }

    // Name the cards that did not make it, then say why the rest were not run.
    QStringList failedNames;
    QStringList notRunNames;
    for (const BatchEntry &entry : plan.entries()) {
        if (entry.outcome == CardOutcome::Failed) {
            if (isImageFail(entry.result)) {
                failedNames.append(QStringLiteral("%1 failed image verification").arg(cardName(entry.cardType)));
            } else {
                failedNames.append(QStringLiteral("%1 failed").arg(cardName(entry.cardType)));
            }
        } else if (entry.outcome == CardOutcome::Cancelled) {
            failedNames.append(QStringLiteral("%1 was aborted").arg(cardName(entry.cardType)));
        } else if (entry.outcome == CardOutcome::NotRun) {
            notRunNames.append(cardName(entry.cardType));
        }
    }

    QString text;
    if (!failedNames.isEmpty()) {
        text = failedNames.join(QStringLiteral("; ")) + QStringLiteral(".");
    }
    if (!notRunNames.isEmpty()) {
        QString verb = QStringLiteral("were");
        if (notRunNames.size() == 1) {
            verb = QStringLiteral("was");
        }
        QString sentence = QStringLiteral("%1 %2 not flashed")
                               .arg(notRunNames.join(QStringLiteral(", ")), verb);
        if (!plan.stopReason().isEmpty()) {
            sentence += QStringLiteral(" because %1").arg(plan.stopReason());
        }
        sentence += QStringLiteral(".");
        if (!text.isEmpty()) {
            text += QLatin1Char(' ');
        }
        text += sentence;
    }
    verdict.explanation = text;
    return verdict;
}

// =============================================================================
//  Pre-flight
// =============================================================================

bool sameSubnet(quint32 adapterIp, quint32 netmask, quint32 targetIp)
{
    if (netmask == 0 || adapterIp == 0 || targetIp == 0) {
        return false;
    }
    return (adapterIp & netmask) == (targetIp & netmask);
}

QString subnetDescription(quint32 ip, quint32 netmask)
{
    const quint32 network = ip & netmask;
    const int octet1 = static_cast<int>((network >> 24) & 0xFF);
    const int octet2 = static_cast<int>((network >> 16) & 0xFF);
    const int octet3 = static_cast<int>((network >> 8) & 0xFF);
    const int octet4 = static_cast<int>(network & 0xFF);

    if (netmask == 0xFFFFFF00u) {
        return QStringLiteral("%1.%2.%3.x").arg(octet1).arg(octet2).arg(octet3);
    }
    if (netmask == 0xFFFF0000u) {
        return QStringLiteral("%1.%2.x.x").arg(octet1).arg(octet2);
    }
    if (netmask == 0xFF000000u) {
        return QStringLiteral("%1.x.x.x").arg(octet1);
    }
    // Any other mask: CIDR notation, counting the leading one-bits.
    int prefixLength = 0;
    quint32 mask = netmask;
    while ((mask & 0x80000000u) != 0) {
        ++prefixLength;
        mask <<= 1;
    }
    return QStringLiteral("%1.%2.%3.%4/%5").arg(octet1).arg(octet2).arg(octet3).arg(octet4).arg(prefixLength);
}

PreflightReport evaluatePreflight(const PreflightInput &input)
{
    PreflightReport report;

    // Helper: add an item and keep the report's summary fields in step.
    auto add = [&report](PreflightItem::State state, const QString &text, bool blocks) {
        PreflightItem item;
        item.state = state;
        item.text = text;
        item.blocksFlash = blocks;
        report.items.append(item);
        if (blocks && report.firstBlocker.isEmpty()) {
            report.firstBlocker = text;
        }
    };

    // ---- the target ------------------------------------------------------
    uint32_t targetIp = 0;
    const bool ipValid = kflash::parse_ipv4(input.vccIp.toStdString(), &targetIp)
                         && targetIp != 0 && targetIp != 0xFFFFFFFFu;
    if (!ipValid) {
        add(PreflightItem::State::Warning, QStringLiteral("VCC IP address is not a valid IPv4 address"), true);
    } else if (input.port == 0) {
        add(PreflightItem::State::Warning, QStringLiteral("Port must be 1–65535"), true);
    }

    // ---- the adapter -----------------------------------------------------
    // In Operator mode an adapter off the board's subnet blocks the run: on a
    // production bench that is always a wrong cable or a wrong IP. An
    // engineer may legitimately be routing to a board elsewhere, so there it
    // is only a warning.
    if (ipValid) {
        if (input.adapterOnSubnet) {
            add(PreflightItem::State::Ok,
                QStringLiteral("Adapter on the %1 subnet").arg(input.subnetText), false);
        } else {
            QString text = QStringLiteral("No adapter on the board's subnet");
            if (!input.subnetText.isEmpty()) {
                text = QStringLiteral("No adapter on the %1 subnet").arg(input.subnetText);
            }
            add(PreflightItem::State::Warning, text, input.mode == Mode::Operator);
        }
    }

    // ---- the queue -------------------------------------------------------
    QStringList missing;
    QStringList unreadable;
    QStringList refusedByEngine;
    QStringList nameProblems;
    QStringList pinProblems;
    QStringList tooLarge;
    int loadedCount = 0;

    for (const PreflightRow &row : input.rows) {
        if (!row.ticked) {
            continue;
        }
        ++report.tickedCount;

        if (!row.hasPath) {
            missing.append(cardName(row.cardType));
            continue;
        }
        if (!row.loaded) {
            unreadable.append(QStringLiteral("%1 (%2)").arg(cardName(row.cardType), row.loadError));
            continue;
        }
        ++loadedCount;
        if (row.sizeBytes > kBoardMaxImageBytes) {
            tooLarge.append(QStringLiteral("%1: image is %2; the updater takes at most %3")
                                .arg(cardName(row.cardType), formatBytes(static_cast<quint64>(row.sizeBytes)),
                                     formatBytes(static_cast<quint64>(kBoardMaxImageBytes))));
        }
        if (!row.engineProblem.isEmpty()) {
            refusedByEngine.append(QStringLiteral("%1: %2").arg(cardName(row.cardType), row.engineProblem));
        }
        if (row.nameCheck.kind != NameCheck::Matches) {
            nameProblems.append(row.nameCheck.explanation);
        }
        if (row.shaPinMismatch) {
            pinProblems.append(cardName(row.cardType));
        }
    }

    if (report.tickedCount == 0) {
        add(PreflightItem::State::Warning, QStringLiteral("No card selected — pick the card to flash"), true);
    } else if (report.tickedCount > 1) {
        // The queue only lets one row be selected; this guards the logic.
        add(PreflightItem::State::Warning,
            QStringLiteral("One card per run — the updater only runs after a power cycle"), true);
    }

    if (unreadable.isEmpty() && refusedByEngine.isEmpty() && tooLarge.isEmpty() && loadedCount > 0) {
        add(PreflightItem::State::Ok, QStringLiteral("Images readable, CRC + SHA-256 computed"), false);
    }
    for (const QString &text : unreadable) {
        add(PreflightItem::State::Warning, QStringLiteral("Cannot read %1").arg(text), true);
    }
    for (const QString &text : tooLarge) {
        add(PreflightItem::State::Warning, text, true);
    }
    for (const QString &text : refusedByEngine) {
        add(PreflightItem::State::Warning, text, true);
    }

    if (loadedCount > 0) {
        if (nameProblems.isEmpty()) {
            add(PreflightItem::State::Ok, QStringLiteral("Card IDs match image names"), false);
        } else {
            // Operator: refused outright. Engineer: allowed, with a confirm box
            // at start that lists exactly these lines.
            const bool blocks = (input.mode == Mode::Operator);
            for (const QString &text : nameProblems) {
                add(PreflightItem::State::Warning, text, blocks);
            }
            if (!blocks) {
                report.needsNameConfirmation = true;
            }
        }
    }

    for (const QString &name : missing) {
        add(PreflightItem::State::Warning,
            QStringLiteral("%1: choose an image or pick another card").arg(name), true);
    }

    for (const QString &name : pinProblems) {
        add(PreflightItem::State::Warning,
            QStringLiteral("%1: file changed on disk since the profile pinned it").arg(name), false);
    }

    // Always last, never blocking: the procedure. The PC cannot power the
    // chassis, so the checklist states what the operator does next.
    add(PreflightItem::State::Warning,
        QStringLiteral("After pressing Flash, power-cycle the chassis — the flasher waits up to "
                       "%1 s for the updater").arg(input.updaterWaitSeconds), false);

    report.ready = report.firstBlocker.isEmpty();
    return report;
}

// =============================================================================
//  History
// =============================================================================

QJsonObject HistoryRecord::toJson() const
{
    QJsonObject object;
    object[QStringLiteral("when")] = when.toUTC().toString(Qt::ISODateWithMs);
    object[QStringLiteral("batch")] = batchId;
    object[QStringLiteral("operator")] = operatorName;
    object[QStringLiteral("chassis")] = chassis;
    object[QStringLiteral("vcc_ip")] = vccIp;
    object[QStringLiteral("card_type")] = cardType;
    object[QStringLiteral("image")] = imagePath;
    object[QStringLiteral("crc32")] = crcText;
    object[QStringLiteral("sha256")] = sha256;
    object[QStringLiteral("result")] = result;
    object[QStringLiteral("message")] = message;
    object[QStringLiteral("total_ms")] = static_cast<double>(totalMs);
    object[QStringLiteral("handshake_ms")] = static_cast<double>(handshakeMs);
    object[QStringLiteral("send_ms")] = static_cast<double>(sendMs);
    object[QStringLiteral("repair_ms")] = static_cast<double>(repairMs);
    object[QStringLiteral("repair_rounds")] = repairRounds;
    object[QStringLiteral("total_blocks")] = static_cast<double>(totalBlocks);
    object[QStringLiteral("blocks_resent")] = static_cast<double>(blocksResent);
    object[QStringLiteral("bytes_on_wire")] = static_cast<double>(bytesOnWire);
    object[QStringLiteral("avg_kBps")] = avgKBps;
    object[QStringLiteral("settled_rate_kBps")] = settledRateKBps;
    object[QStringLiteral("log")] = QJsonArray::fromStringList(log);
    return object;
}

HistoryRecord HistoryRecord::fromJson(const QJsonObject &object)
{
    HistoryRecord record;
    record.when = QDateTime::fromString(object.value(QStringLiteral("when")).toString(), Qt::ISODateWithMs);
    record.when.setTimeSpec(Qt::UTC);
    record.batchId = object.value(QStringLiteral("batch")).toString();
    record.operatorName = object.value(QStringLiteral("operator")).toString();
    record.chassis = object.value(QStringLiteral("chassis")).toString();
    record.vccIp = object.value(QStringLiteral("vcc_ip")).toString();
    record.cardType = object.value(QStringLiteral("card_type")).toInt();
    record.imagePath = object.value(QStringLiteral("image")).toString();
    record.crcText = object.value(QStringLiteral("crc32")).toString();
    record.sha256 = object.value(QStringLiteral("sha256")).toString();
    record.result = object.value(QStringLiteral("result")).toString();
    record.message = object.value(QStringLiteral("message")).toString();
    record.totalMs = static_cast<qint64>(object.value(QStringLiteral("total_ms")).toDouble());
    record.handshakeMs = static_cast<qint64>(object.value(QStringLiteral("handshake_ms")).toDouble());
    record.sendMs = static_cast<qint64>(object.value(QStringLiteral("send_ms")).toDouble());
    record.repairMs = static_cast<qint64>(object.value(QStringLiteral("repair_ms")).toDouble());
    record.repairRounds = object.value(QStringLiteral("repair_rounds")).toInt();
    record.totalBlocks = static_cast<quint32>(object.value(QStringLiteral("total_blocks")).toDouble());
    record.blocksResent = static_cast<quint32>(object.value(QStringLiteral("blocks_resent")).toDouble());
    record.bytesOnWire = static_cast<quint64>(object.value(QStringLiteral("bytes_on_wire")).toDouble());
    record.avgKBps = object.value(QStringLiteral("avg_kBps")).toDouble();
    record.settledRateKBps = object.value(QStringLiteral("settled_rate_kBps")).toDouble();
    const QJsonArray lines = object.value(QStringLiteral("log")).toArray();
    for (const QJsonValue &line : lines) {
        record.log.append(line.toString());
    }
    return record;
}

QString HistoryRecord::fileName() const
{
    return QFileInfo(imagePath).fileName();
}

HistoryRecord makeHistoryRecord(const BatchEntry &entry, const QString &batchId,
                                const QString &operatorName, const QString &chassis,
                                const QString &vccIp)
{
    HistoryRecord record;
    if (entry.startedAt.isValid()) {
        record.when = entry.startedAt;
    } else {
        // Not-run cards never started; stamp them with the batch's end so
        // they sort beside the cards that did run.
        record.when = QDateTime::currentDateTimeUtc();
    }
    record.batchId = batchId;
    record.operatorName = operatorName;
    record.chassis = chassis;
    record.vccIp = vccIp;
    record.cardType = entry.cardType;
    record.imagePath = entry.image.path;
    // The CRC is always the PC's own, computed when the file was loaded:
    // FlashResult::image_crc is not filled in by the engine.
    if (entry.image.loaded) {
        record.crcText = entry.image.crcText();
        record.sha256 = entry.image.shaHex();
    }
    record.result = outcomeText(entry.outcome);
    record.message = QString::fromStdString(entry.result.message);
    if (entry.outcome == CardOutcome::NotRun) {
        record.message = QStringLiteral("Batch stopped before this card");
    }
    record.totalMs = entry.result.total_ms;
    record.handshakeMs = entry.result.handshake_ms;
    record.sendMs = entry.result.initial_send_ms;
    record.repairMs = entry.result.repair_ms;
    record.repairRounds = entry.result.repair_rounds;
    record.totalBlocks = entry.result.total_blocks;
    record.blocksResent = entry.result.blocks_resent;
    record.bytesOnWire = entry.result.bytes_on_wire;
    record.avgKBps = entry.result.avg_kBps;
    record.settledRateKBps = entry.result.final_rate_kBps;
    record.log = entry.log;
    return record;
}

HistoryLog::HistoryLog(const QString &filePath)
    : m_filePath(filePath)
{
}

bool HistoryLog::append(const HistoryRecord &record)
{
    QList<HistoryRecord> one;
    one.append(record);
    return appendAll(one);
}

bool HistoryLog::appendAll(const QList<HistoryRecord> &records)
{
    m_lastError.clear();
    QFile file(m_filePath);
    // Append-only by construction: QIODevice::Append never seeks back into
    // what is already there.
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        m_lastError = QStringLiteral("Cannot open %1 for appending: %2").arg(m_filePath, file.errorString());
        return false;
    }
    for (const HistoryRecord &record : records) {
        QByteArray line = QJsonDocument(record.toJson()).toJson(QJsonDocument::Compact);
        line.append('\n');
        if (file.write(line) != line.size()) {
            m_lastError = QStringLiteral("Write to %1 failed: %2").arg(m_filePath, file.errorString());
            return false;
        }
    }
    file.flush();
    return true;
}

QList<HistoryRecord> HistoryLog::readAll(int *skipped) const
{
    QList<HistoryRecord> records;
    int badLines = 0;
    QFile file(m_filePath);
    if (file.open(QIODevice::ReadOnly)) {
        while (!file.atEnd()) {
            const QByteArray line = file.readLine().trimmed();
            if (line.isEmpty()) {
                continue;
            }
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                ++badLines;
                continue;
            }
            records.append(HistoryRecord::fromJson(document.object()));
        }
    }
    if (skipped != nullptr) {
        *skipped = badLines;
    }
    return records;
}

bool historyMatches(const HistoryRecord &record, const HistoryFilter &filter, const QDateTime &nowUtc)
{
    if (filter.cardType != 0 && record.cardType != filter.cardType) {
        return false;
    }
    if (!filter.result.isEmpty() && record.result != filter.result) {
        return false;
    }
    if (filter.lastDays > 0) {
        const QDateTime cutoff = nowUtc.addDays(-filter.lastDays);
        if (record.when < cutoff) {
            return false;
        }
    }
    const QString needle = filter.text.trimmed();
    if (!needle.isEmpty()) {
        const QStringList haystack{
            record.fileName(), record.chassis, record.operatorName, record.crcText,
            record.message, record.batchId, cardName(record.cardType), record.vccIp
        };
        bool found = false;
        for (const QString &text : haystack) {
            if (text.contains(needle, Qt::CaseInsensitive)) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

namespace {

// RFC 4180: quote a field when it contains a comma, a quote or a line break,
// and double every quote inside it.
QString csvField(const QString &value)
{
    if (value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"'))
        || value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))) {
        QString escaped = value;
        escaped.replace(QLatin1String("\""), QLatin1String("\"\""));
        return QStringLiteral("\"%1\"").arg(escaped);
    }
    return value;
}

}  // namespace

QString historyToCsv(const QList<HistoryRecord> &records)
{
    QString csv;
    QTextStream out(&csv);
    out << "when_utc,batch,operator,chassis,vcc_ip,card_type,card,image,crc32,sha256,result,"
           "total_ms,repair_rounds,blocks_resent,total_blocks,bytes_on_wire,avg_kBps,message\r\n";
    for (const HistoryRecord &record : records) {
        const QStringList fields{
            record.when.toUTC().toString(Qt::ISODate),
            record.batchId,
            record.operatorName,
            record.chassis,
            record.vccIp,
            QString::number(record.cardType),
            cardName(record.cardType),
            record.imagePath,
            record.crcText,
            record.sha256,
            record.result,
            QString::number(record.totalMs),
            QString::number(record.repairRounds),
            QString::number(record.blocksResent),
            QString::number(record.totalBlocks),
            QString::number(record.bytesOnWire),
            QString::number(record.avgKBps, 'f', 1),
            record.message
        };
        QStringList escaped;
        for (const QString &field : fields) {
            escaped.append(csvField(field));
        }
        out << escaped.join(QLatin1Char(',')) << "\r\n";
    }
    out.flush();
    return csv;
}

// =============================================================================
//  Formatting
// =============================================================================

QString formatBytes(quint64 bytes)
{
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    const double kilobytes = static_cast<double>(bytes) / 1024.0;
    if (kilobytes < 1024.0) {
        return QStringLiteral("%1 KB").arg(kilobytes, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB").arg(kilobytes / 1024.0, 0, 'f', 2);
}

QString formatDuration(qint64 milliseconds)
{
    if (milliseconds < 0) {
        milliseconds = 0;
    }
    if (milliseconds < 60000) {
        return QStringLiteral("%1 s").arg(static_cast<double>(milliseconds) / 1000.0, 0, 'f', 1);
    }
    const qint64 totalSeconds = milliseconds / 1000;
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1 min %2 s").arg(minutes).arg(seconds);
}

QString formatClock(qint64 milliseconds)
{
    if (milliseconds < 0) {
        milliseconds = 0;
    }
    const qint64 totalSeconds = milliseconds / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString formatRate(double kBps)
{
    return QStringLiteral("%1 KB/s").arg(std::lround(kBps));
}

QString newBatchId(const QDateTime &nowUtc)
{
    // Timestamp for sorting and reading aloud, plus four random characters so
    // two benches flashing in the same minute still get distinct IDs.
    static const char kAlphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";  // no 0/O, 1/I
    QString suffix;
    for (int index = 0; index < 4; ++index) {
        const int pick = static_cast<int>(QRandomGenerator::global()->bounded(32));
        suffix.append(QLatin1Char(kAlphabet[pick]));
    }
    return nowUtc.toLocalTime().toString(QStringLiteral("yyyyMMdd-HHmm")) + QLatin1Char('-') + suffix;
}

QString currentOperatorName()
{
    // USERNAME on Windows, USER on Linux/macOS. Both are what the OS login
    // says, which is what an audit trail wants -- not a name typed into a box.
    QString name = qEnvironmentVariable("USERNAME");
    if (name.isEmpty()) {
        name = qEnvironmentVariable("USER");
    }
    if (name.isEmpty()) {
        name = QStringLiteral("unknown");
    }
    return name;
}

QString batchReportText(const BatchPlan &plan, const QString &batchId,
                        const QString &operatorName, const QString &chassis,
                        const QString &vccIp, quint16 port)
{
    const Verdict verdict = batchVerdict(plan);
    QString text;
    QTextStream out(&text);
    out << "Kavach firmware flash report\n";
    out << "============================\n\n";
    out << "Batch     : " << batchId << "\n";
    out << "Operator  : " << operatorName << "\n";
    out << "Chassis   : " << chassis << "\n";
    out << "Target    : " << vccIp << ":" << port << "\n";
    out << "Written   : " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n\n";
    out << verdict.headline << "\n";
    if (!verdict.explanation.isEmpty()) {
        out << verdict.explanation << "\n";
    }
    out << "\n";

    for (const BatchEntry &entry : plan.entries()) {
        out << "---- " << cardName(entry.cardType) << " (" << cardIdText(entry.cardType) << ") ----\n";
        out << "Result          : " << outcomeText(entry.outcome) << "\n";
        out << "Image           : " << entry.image.path << "\n";
        if (entry.image.loaded) {
            out << "CRC-32          : " << entry.image.crcText() << "\n";
            out << "SHA-256         : " << entry.image.shaHex() << "\n";
            out << "Size            : " << formatBytes(static_cast<quint64>(entry.image.sizeBytes))
                << " (" << entry.image.blockCount << " blocks)\n";
        }
        const bool ran = entry.outcome != CardOutcome::NotRun && entry.outcome != CardOutcome::Queued;
        if (ran) {
            const kflash::FlashResult &result = entry.result;
            out << "Engine message  : " << QString::fromStdString(result.message) << "\n";
            if (!outcomeIsSuccess(entry.outcome)) {
                out << "What it means   : " << failureExplanation(result) << "\n";
            }
            out << "Repair rounds   : " << result.repair_rounds << "\n";
            out << "Blocks resent   : " << result.blocks_resent << " / " << result.total_blocks << "\n";
            out << "Bytes on wire   : " << formatBytes(result.bytes_on_wire) << "\n";
            out << "Avg throughput  : " << formatRate(result.avg_kBps) << "\n";
            out << "Settled rate    : " << formatRate(result.final_rate_kBps) << "\n";
            out << "Phases          : handshake " << formatDuration(result.handshake_ms)
                << ", send " << formatDuration(result.initial_send_ms)
                << ", repair " << formatDuration(result.repair_ms) << "\n";
            out << "Total           : " << formatDuration(result.total_ms) << "\n";
        }
        out << "\n";
    }
    out.flush();
    return text;
}

// =============================================================================
//  Session 94: export / import to another PC
// =============================================================================

const char *kFlasherProfilesFormat = "dlconsole-flasher-profiles";

QByteArray exportProfiles(const QList<FlashProfile> &profiles)
{
    QJsonArray array;
    for (const FlashProfile &p : profiles) array.append(p.toJson());
    QJsonObject root;
    root.insert(QStringLiteral("format"), QLatin1String(kFlasherProfilesFormat));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("exported_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("profiles"), array);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

ImportedProfiles importProfiles(const QByteArray &json)
{
    ImportedProfiles out;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isObject()) {
        out.error = QStringLiteral("not valid JSON: %1").arg(parseError.errorString());
        return out;
    }
    const QJsonObject root = document.object();
    // flasher_profiles.json itself (no "format") is accepted too.
    const QString format = root.value(QStringLiteral("format")).toString();
    if (!format.isEmpty() && format != QLatin1String(kFlasherProfilesFormat)) {
        out.error = QStringLiteral("this is a \"%1\" file, not flasher profiles").arg(format);
        return out;
    }
    if (root.value(QStringLiteral("version")).toInt(1) > 1) {
        out.error = QStringLiteral("written by a newer DLConsole (version %1)").arg(root.value(QStringLiteral("version")).toInt());
        return out;
    }
    QSet<QString> names;
    for (const QJsonValue &value : root.value(QStringLiteral("profiles")).toArray()) {
        FlashProfile p = FlashProfile::fromJson(value.toObject());
        p.name = p.name.trimmed();
        if (p.name.isEmpty() || names.contains(p.name)) continue;
        names.insert(p.name);
        out.profiles.append(p);
    }
    if (out.profiles.isEmpty()) {
        out.error = QStringLiteral("the file holds no profiles");
        return out;
    }
    out.ok = true;
    return out;
}

QStringList importedImageProblems(const FlashProfile &profile)
{
    QStringList out;
    QList<int> types = profile.defaultImages.keys();
    std::sort(types.begin(), types.end());
    for (int type : types) {
        const QString path = profile.defaultImages.value(type);
        if (!path.isEmpty() && !QFileInfo::exists(path))
            out << QStringLiteral("%1: %2 is not on this PC").arg(cardShortName(type), QDir::toNativeSeparators(path));
    }
    return out;
}

QString ProfileStore::importProfile(const FlashProfile &profile, ImportClash clash)
{
    if (!contains(profile.name)) {
        m_profiles.append(profile);
        return profile.name;
    }
    switch (clash) {
    case ImportClash::Skip:
        return QString();
    case ImportClash::Replace:
        for (FlashProfile &p : m_profiles) {
            if (p.name == profile.name) { p = profile; break; }
        }
        return profile.name;
    case ImportClash::KeepBoth: {
        FlashProfile renamed = profile;
        renamed.name = uniqueImportName(profile.name, [this](const QString &n) { return contains(n); });
        m_profiles.append(renamed);
        return renamed.name;
    }
    }
    return QString();
}

}  // namespace Flasher

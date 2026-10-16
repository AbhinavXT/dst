/* =========================================================================
 * flash_engine.h  -  In-field firmware update engine (UDP, bitmap-NAK)
 *
 * The protocol logic from udp_file_v5.cpp, pulled out of main() so that the
 * CLI and the GUI drive the exact same code. No printing happens in here:
 * everything the user sees goes out through the Callbacks struct.
 *
 * Wire format is UNCHANGED from v5 (META / DATA / POLL / STATUS, 1450-byte
 * blocks, same CRC-32 and SHA-256). Only host-side behaviour changed:
 *   - precise, rate-based pacing instead of Sleep(1)
 *   - adaptive send rate (backs off on loss, speeds up when clean)
 *   - checkpoint polls during the first pass, so losses are repaired early
 *   - adaptive poll timeout with backoff, time-budgeted instead of count-based
 *   - stale STATUS draining, source-address check, bitmap bounds checks
 *   - IMAGE_FAIL restarts capped
 *   - cancellable from another thread
 *
 * Session 296: two updaters. Classic is the protocol above, unchanged. The
 * 2.6 updater (updater/guide in the firmware repo) adds packets the engine
 * uses only when FlashConfig::updater says so:
 *   - HOLD (0x07): keep the board in the updater (2 min idle instead of
 *     5 s). Sent with a POLL while waiting for the power cycle; the board
 *     answers HOLD with INFO (0x05), an updater before 2.2 answers the POLL
 *     only.
 *   - BOOT (0x08): leave the updater now. Sent to a held VCC after it
 *     accepted an Input/Output/Analog image (it relays it over CAN and
 *     would otherwise wait for BOOT until its 2 min run out).
 *   - STATUS REJECTED (0x04) + a reason byte: the image is intact but the
 *     board refuses it (wrong card, downgrade, trailer). Final: no resend.
 *   - Silence after the session was up is "result unknown", not a failure:
 *     the board sends COMPLETE three times and then flashes without
 *     answering, so all three may have been lost.
 * ========================================================================= */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kflash {

/* Block size MUST equal the MCU's FW_BLOCK_SIZE. */
constexpr uint32_t kBlockSize = 1450;

enum class Phase {
    Idle,
    Handshake,
    InitialSend,
    Repair,
    CardResults,   /* session 299: 2.6, a card image: collecting the cards' own results */
    Done
};

const char* phase_name(Phase phase);

/* Which updater the board runs (session 296). Classic: the protocol as it
 * was before. V26: updater 2.2+ packets (HOLD, BOOT, REJECTED, INFO). */
enum class Updater { Classic, V26 };

const char* updater_name(Updater updater);   /* "Classic", "2.6" */

/* The board's reason byte after STATUS REJECTED (FW_REJECT_* in the updater). */
const char* reject_reason_text(uint8_t reason);
const char* card_type_name(int card_type);

/* Everything an engineer might want to tune. The defaults are what the GUI
 * ships with; the profile screen exposes them in Engineer mode. */
struct TransferTuning {
    /* ---- send rate ---- */
    bool     adaptive_rate          = true;   /* false = hold start_rate_kBps  */
    uint32_t start_rate_kBps        = 1000;   /* first-pass starting rate      */
    uint32_t min_rate_kBps          = 60;     /* adaptive floor                */
    uint32_t max_rate_kBps          = 8000;   /* adaptive ceiling              */
    uint32_t burst_packets          = 4;      /* how far pacing may catch up   */

    /* ---- checkpoint polls during the first pass (0 = off) ---- */
    uint32_t checkpoint_every_blocks = 64;

    /* ---- polling ---- */
    uint32_t poll_timeout_min_ms    = 60;     /* adaptive timeout floor        */
    uint32_t poll_timeout_max_ms    = 400;    /* adaptive timeout ceiling      */
    uint32_t poll_give_up_ms        = 2500;   /* total silence before abort    */

    /* ---- session ---- */
    int      max_rounds             = 100;
    int      meta_repeats           = 3;
    int      meta_handshake_tries   = 2;
    int      max_image_fail_restarts = 1;

    /* ---- session 299: card results after a card image (2.6) ---- */
    uint32_t card_wait_total_ms     = 20000;  /* udp_file_v5 CARD_WAIT_TOTAL_MS  */
    uint32_t card_wait_quiet_ms     = 4000;   /* stop once reports stop coming  */
    uint32_t card_wait_poll_ms      = 500;
};

struct FlashConfig {
    std::string          receiver_ip;
    uint16_t             port      = 50001;
    int                  card_type = 1;     /* VCC(1) Input(2) Output(3) Analog(4) */
    std::vector<uint8_t> image;             /* whole .appimage in memory            */
    TransferTuning       tuning;
    Updater              updater   = Updater::Classic;
};

/* Live state for progress bars and the stats column. */
struct Progress {
    Phase    phase             = Phase::Idle;
    uint32_t total_blocks      = 0;
    uint32_t acked_blocks      = 0;   /* from the latest STATUS bitmap          */
    uint32_t sent_cursor       = 0;   /* first pass: next block to send         */
    int      round             = 0;
    double   target_rate_kBps  = 0.0; /* what the pacer is aiming for           */
    double   wire_rate_kBps    = 0.0; /* what actually left the PC recently     */
    double   last_loss_pct     = 0.0; /* loss in the last evaluated window      */
    uint32_t blocks_resent     = 0;
    uint32_t poll_timeouts     = 0;
    uint32_t poll_timeout_ms   = 0;   /* current adaptive timeout               */
    double   rtt_ms            = 0.0; /* smoothed POLL -> STATUS round trip     */
};

struct FlashResult {
    bool        success          = false;
    bool        cancelled        = false;
    std::string message;
    int         repair_rounds    = 0;
    uint32_t    total_blocks     = 0;
    uint32_t    blocks_resent    = 0;
    uint32_t    send_failures    = 0;
    uint64_t    bytes_on_wire    = 0;
    long long   handshake_ms     = 0;
    long long   initial_send_ms  = 0;
    long long   repair_ms        = 0;
    long long   total_ms         = 0;
    double      avg_kBps         = 0.0;
    double      final_rate_kBps  = 0.0; /* worth saving per profile as next start */
    uint32_t    image_crc        = 0;

    /* ---- session 296: the 2.6 updater (all false / 0 with Classic) ---- */
    Updater     updater          = Updater::Classic;
    bool        board_held       = false; /* answered HOLD: stays 2 min idle      */
    bool        older_updater    = false; /* answered POLL but not HOLD (< 2.2)   */
    bool        rejected         = false; /* STATUS REJECTED: final               */
    uint8_t     reject_reason    = 0;
    bool        result_unknown   = false; /* silence after the session was up     */
    bool        boot_sent        = false; /* BOOT sent to release a held VCC      */
    bool        boot_acknowledged = false;

    /* ---- session 299: what each card controller reported (2.6, card images) ----
     * Read from the VCC's INFO (v2+): only results for this image's CRC.
     * result: 1 OK, 2 VERIFY_FAIL, 3 FLASH_FAIL, 4 INCOMPLETE, 5 REJECTED. */
    struct CardReport { int controller_id; int result; };
    bool        card_results_asked = false;   /* the VCC was asked for them        */
    bool        card_results_supported = false; /* its INFO carries them (2.1+)    */
    std::vector<CardReport> card_reports;     /* in controller-ID order             */
    bool        card_failed      = false;     /* a controller reported a failure:
                                                 then success is false              */
};

enum class LogLevel { Info, Warn, Error };

struct Callbacks {
    std::function<void(LogLevel, const std::string&)>               log;
    std::function<void(const Progress&)>                            progress;
    /* Raw received-bitmap (bit set = block held by board), for the block map. */
    std::function<void(const std::vector<uint8_t>&, uint32_t)>      bitmap;
};

class FlashEngine {
public:
    FlashEngine();
    ~FlashEngine();

    /* Blocking. Run it on a worker thread (QThread in the GUI). */
    FlashResult run(const FlashConfig& config, const Callbacks& callbacks);

    /* Safe to call from any thread; run() returns at the next check. */
    void cancel();

    /* Returns an empty string when the config is usable, else the reason. */
    static std::string validate(const FlashConfig& config);

private:
    std::atomic<bool> cancel_requested_;
};

/* ---- image helpers (same algorithms as the firmware) ---- */
uint32_t image_crc32(const uint8_t* data, size_t length);
void     image_sha256(const uint8_t* data, size_t length, uint8_t out32[32]);
bool     parse_ipv4(const std::string& text, uint32_t* out_host_order);

} // namespace kflash

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
    Done
};

const char* phase_name(Phase phase);
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
};

struct FlashConfig {
    std::string          receiver_ip;
    uint16_t             port      = 50001;
    int                  card_type = 1;     /* VCC(1) Input(2) Output(3) Analog(4) */
    std::vector<uint8_t> image;             /* whole .appimage in memory            */
    TransferTuning       tuning;
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

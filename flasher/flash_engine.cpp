/* =========================================================================
 * flash_engine.cpp  -  see flash_engine.h for the overview.
 *
 * Portable: builds with MinGW g++ / MSVC on Windows and g++/clang on
 * Linux/macOS (the non-Windows build is what the board simulator tests run
 * against).
 * ========================================================================= */
#include "flash_engine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <thread>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <mmsystem.h>
    #ifdef _MSC_VER
        #pragma comment(lib, "Ws2_32.lib")
        #pragma comment(lib, "winmm.lib")
    #endif
    typedef SOCKET socket_handle;
    static const socket_handle kInvalidSocket = INVALID_SOCKET;
    static int  last_socket_error() { return WSAGetLastError(); }
    static void close_socket(socket_handle s) { closesocket(s); }
#else
    #include <arpa/inet.h>
    #include <errno.h>
    #include <netinet/in.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <unistd.h>
    typedef int socket_handle;
    static const socket_handle kInvalidSocket = -1;
    static int  last_socket_error() { return errno; }
    static void close_socket(socket_handle s) { close(s); }
#endif

namespace kflash {

/* ---- Packet types (must match firmware_update_user.h) ---- */
static const uint8_t PKT_TYPE_META   = 0x00;
static const uint8_t PKT_TYPE_DATA   = 0x01;
static const uint8_t PKT_TYPE_POLL   = 0x02;
static const uint8_t PKT_TYPE_STATUS = 0x03;

/* ---- STATUS status codes ---- */
static const uint8_t FW_STATUS_COMPLETE   = 0x00;
static const uint8_t FW_STATUS_INCOMPLETE = 0x01;
static const uint8_t FW_STATUS_NEED_META  = 0x02;
static const uint8_t FW_STATUS_IMAGE_FAIL = 0x03;

#pragma pack(push, 1)
struct MetaHeader {
    uint8_t  type;
    uint8_t  card_type;
    uint32_t totalSize;
    uint32_t image_crc;
    uint8_t  image_sha256[32];
};                                  /* 42 bytes */

struct ChunkHeader {
    uint8_t  type;
    uint32_t offset;
    uint32_t size;
    uint32_t chunk_crc;
};                                  /* 13 bytes */

struct PollPacket {
    uint8_t  type;
    uint32_t image_crc;
};                                  /* 5 bytes */

struct StatusHeader {
    uint8_t  type;
    uint8_t  status;
    uint32_t image_crc;
    uint32_t total_blocks;
    uint32_t blocks_received;
    uint16_t bitmap_bytes;
};                                  /* 16 bytes */
#pragma pack(pop)

static_assert(sizeof(MetaHeader)   == 42, "MetaHeader layout changed");
static_assert(sizeof(ChunkHeader)  == 13, "ChunkHeader layout changed");
static_assert(sizeof(PollPacket)   == 5,  "PollPacket layout changed");
static_assert(sizeof(StatusHeader) == 16, "StatusHeader layout changed");

typedef std::chrono::steady_clock Clock;

static long long ms_since(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

const char* phase_name(Phase phase)
{
    switch (phase) {
        case Phase::Idle:        return "Idle";
        case Phase::Handshake:   return "Session";
        case Phase::InitialSend: return "Send";
        case Phase::Repair:      return "Repair";
        case Phase::Done:        return "Done";
    }
    return "?";
}

const char* card_type_name(int card_type)
{
    switch (card_type) {
        case 1: return "VCC (master)";
        case 2: return "Input card";
        case 3: return "Output card";
        case 4: return "Analog card";
        default: return "UNKNOWN";
    }
}

/* =========================================================================
 * CRC-32 (reflected, poly 0x04C11DB7, init 0, xorout 0) - identical output
 * to crcFast() in v5, table built once at static-init time.
 * ========================================================================= */
namespace {

uint32_t reflect_bits(uint32_t data, int bit_count)
{
    uint32_t reflection = 0;
    for (int bit = 0; bit < bit_count; ++bit) {
        if (data & 1u) {
            reflection |= (1u << ((bit_count - 1) - bit));
        }
        data >>= 1;
    }
    return reflection;
}

struct CrcTable {
    uint32_t entries[256];
    CrcTable()
    {
        for (uint32_t dividend = 0; dividend < 256; ++dividend) {
            uint32_t remainder = dividend << 24;
            for (int bit = 0; bit < 8; ++bit) {
                if (remainder & 0x80000000u) {
                    remainder = (remainder << 1) ^ 0x04C11DB7u;
                } else {
                    remainder = (remainder << 1);
                }
            }
            entries[dividend] = remainder;
        }
    }
};

const CrcTable g_crc_table;

} // namespace

uint32_t image_crc32(const uint8_t* data, size_t length)
{
    uint32_t remainder = 0x00000000u;
    for (size_t i = 0; i < length; ++i) {
        uint8_t index = static_cast<uint8_t>(reflect_bits(data[i], 8) ^ (remainder >> 24));
        remainder = g_crc_table.entries[index] ^ (remainder << 8);
    }
    return reflect_bits(remainder, 32) ^ 0x00000000u;
}

/* =========================================================================
 * SHA-256 - same implementation as v5
 * ========================================================================= */
namespace {

#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32 - (b))))
#define CH(x,y,z)     (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z)    (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)        (ROTRIGHT(x,2)  ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x)        (ROTRIGHT(x,6)  ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x)       (ROTRIGHT(x,7)  ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x)       (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

const uint32_t kSha256K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

struct Sha256Context {
    uint8_t  data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
};

void sha256_transform(Sha256Context* ctx, const uint8_t data[64])
{
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    uint32_t i, j;

    for (i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
    }
    for (; i < 64; ++i) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e, f, g) + kSha256K[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

} // namespace

void image_sha256(const uint8_t* data, size_t length, uint8_t out32[32])
{
    Sha256Context ctx;
    ctx.datalen  = 0;
    ctx.bitlen   = 0;
    ctx.state[0] = 0x6a09e667; ctx.state[1] = 0xbb67ae85;
    ctx.state[2] = 0x3c6ef372; ctx.state[3] = 0xa54ff53a;
    ctx.state[4] = 0x510e527f; ctx.state[5] = 0x9b05688c;
    ctx.state[6] = 0x1f83d9ab; ctx.state[7] = 0x5be0cd19;

    for (size_t i = 0; i < length; ++i) {
        ctx.data[ctx.datalen] = data[i];
        ctx.datalen++;
        if (ctx.datalen == 64) {
            sha256_transform(&ctx, ctx.data);
            ctx.bitlen += 512;
            ctx.datalen = 0;
        }
    }

    uint32_t i = ctx.datalen;
    if (ctx.datalen < 56) {
        ctx.data[i++] = 0x80;
        while (i < 56) ctx.data[i++] = 0x00;
    } else {
        ctx.data[i++] = 0x80;
        while (i < 64) ctx.data[i++] = 0x00;
        sha256_transform(&ctx, ctx.data);
        memset(ctx.data, 0, 56);
    }

    ctx.bitlen += (uint64_t)ctx.datalen * 8;
    for (int k = 0; k < 8; ++k) {
        ctx.data[63 - k] = (uint8_t)(ctx.bitlen >> (8 * k));
    }
    sha256_transform(&ctx, ctx.data);

    for (int k = 0; k < 4; ++k) {
        for (int word = 0; word < 8; ++word) {
            out32[k + word * 4] = (uint8_t)((ctx.state[word] >> (24 - k * 8)) & 0xff);
        }
    }
}

/* Strict dotted-quad parser. inet_addr() silently turns a typo into
 * 255.255.255.255 (broadcast); this refuses anything that isn't a.b.c.d. */
bool parse_ipv4(const std::string& text, uint32_t* out_host_order)
{
    uint32_t value = 0;
    int      octet_count = 0;
    size_t   pos = 0;

    while (octet_count < 4) {
        if (pos >= text.size() || text[pos] < '0' || text[pos] > '9') {
            return false;
        }
        uint32_t octet = 0;
        int digits = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            octet = octet * 10 + static_cast<uint32_t>(text[pos] - '0');
            ++digits;
            ++pos;
            if (digits > 3 || octet > 255) {
                return false;
            }
        }
        value = (value << 8) | octet;
        ++octet_count;
        if (octet_count < 4) {
            if (pos >= text.size() || text[pos] != '.') {
                return false;
            }
            ++pos;
        }
    }
    if (pos != text.size()) {
        return false;
    }
    *out_host_order = value;
    return true;
}

/* =========================================================================
 * Platform helpers
 * ========================================================================= */
namespace {

/* Winsock + 1 ms timer resolution for the life of one run().
 * Without timeBeginPeriod(1), Windows sleeps in ~15.6 ms ticks, which is
 * what capped v5's first pass (Sleep(1) was really Sleep(15.6)). */
class PlatformScope {
public:
    PlatformScope() : ok_(true)
    {
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            ok_ = false;
        }
        timeBeginPeriod(1);
#endif
    }
    ~PlatformScope()
    {
#ifdef _WIN32
        timeEndPeriod(1);
        if (ok_) {
            WSACleanup();
        }
#endif
    }
    bool ok() const { return ok_; }
private:
    bool ok_;
};

/* Waits up to timeout_ms for a datagram. Returns bytes read, 0 on timeout,
 * -1 on error. Uses select() so the timeout can change per call. */
int receive_with_timeout(socket_handle sock, uint8_t* buffer, int buffer_len,
                         int timeout_ms, sockaddr_in* from)
{
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);

    timeval tv;
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ready = select(static_cast<int>(sock) + 1, &read_set, nullptr, nullptr, &tv);
    if (ready <= 0) {
        return ready; /* 0 = timeout, -1 = error */
    }

    socklen_t from_len = sizeof(*from);
    int received = recvfrom(sock, reinterpret_cast<char*>(buffer), buffer_len, 0,
                            reinterpret_cast<sockaddr*>(from), &from_len);
    if (received < 0) {
        return -1;
    }
    return received;
}

/* Rate-based pacer. Each packet "costs" bytes / rate of time. If the thread
 * falls behind (scheduler hiccup) it may catch up by at most burst_packets
 * worth of time, so a stall never turns into a flood that overruns the
 * MCU's receive ring. Sleeps for the coarse part, yields for the last ~2 ms. */
class Pacer {
public:
    Pacer() : bytes_per_second_(1.0e6), burst_packets_(4), started_(false) {}

    void set_rate_kBps(double rate_kBps)
    {
        bytes_per_second_ = rate_kBps * 1024.0;
    }

    void set_burst(uint32_t packets) { burst_packets_ = packets; }

    void restart() { started_ = false; }

    /* Blocks until this packet may go out. Returns false if cancelled. */
    bool wait_for_slot(size_t packet_bytes, const std::atomic<bool>& cancel)
    {
        const double seconds_per_packet = static_cast<double>(packet_bytes) / bytes_per_second_;
        const auto packet_cost = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(seconds_per_packet));

        Clock::time_point now = Clock::now();
        if (!started_) {
            next_slot_ = now;
            started_   = true;
        }

        const Clock::time_point earliest_allowed = now - packet_cost * burst_packets_;
        if (next_slot_ < earliest_allowed) {
            next_slot_ = earliest_allowed;
        }

        while (true) {
            if (cancel.load()) {
                return false;
            }
            now = Clock::now();
            if (now >= next_slot_) {
                break;
            }
            const auto remaining = next_slot_ - now;
            /* Only sleep for long gaps: an OS sleep can overshoot by a whole
             * scheduler tick (15.6 ms on stock Windows). Short gaps spin with
             * yield, which is exact on every OS without needing timeBeginPeriod. */
            if (remaining > std::chrono::milliseconds(20)) {
                std::this_thread::sleep_for(remaining - std::chrono::milliseconds(17));
            } else {
                std::this_thread::yield();
            }
        }

        next_slot_ += packet_cost;
        return true;
    }

private:
    double            bytes_per_second_;
    uint32_t          burst_packets_;
    bool              started_;
    Clock::time_point next_slot_;
};

enum class PollOutcome { Reply, Timeout, Cancelled };

enum class PassOutcome { Finished, NeedMeta, ImageFail, Complete, Cancelled };

} // namespace

/* =========================================================================
 * Session - one run() of the engine
 * ========================================================================= */
namespace {

class Session {
public:
    Session(const FlashConfig& config, const Callbacks& callbacks,
            const std::atomic<bool>& cancel)
        : config_(config), tuning_(config.tuning), callbacks_(callbacks), cancel_(cancel),
          sock_(kInvalidSocket), image_(config.image.data()),
          total_bytes_(static_cast<uint32_t>(config.image.size())),
          block_count_((total_bytes_ + kBlockSize - 1) / kBlockSize),
          image_crc_(0), receive_buffer_(4096),
          rate_kBps_(config.tuning.start_rate_kBps),
          srtt_ms_(0.0), have_rtt_(false), rate_changed_since_window_(false),
          last_progress_emit_(Clock::now()), bytes_at_last_emit_(0)
    {
        memset(image_sha_, 0, sizeof(image_sha_));
        memset(&receiver_, 0, sizeof(receiver_));
    }

    ~Session()
    {
        if (sock_ != kInvalidSocket) {
            close_socket(sock_);
        }
    }

    FlashResult run();

private:
    /* ---- plumbing ---- */
    void log(LogLevel level, const std::string& text)
    {
        if (callbacks_.log) {
            callbacks_.log(level, text);
        }
    }
    void emit_progress(bool force);
    void emit_bitmap(const uint8_t* bitmap, uint32_t bitmap_bytes);
    bool open_socket();

    /* ---- protocol ---- */
    bool        send_block(uint32_t block_index);
    void        send_meta_burst();
    void        drain_stale_datagrams();
    PollOutcome poll_for_status(uint32_t budget_ms, bool single_attempt, int* out_len);
    bool        status_bitmap_is_valid(const StatusHeader& header, int received_len);
    PassOutcome send_pass(bool with_checkpoints);
    PassOutcome handle_checkpoint_reply(int received_len, uint32_t eval_from, uint32_t eval_to);

    /* ---- adaptation ---- */
    uint32_t current_poll_timeout_ms(int attempt) const;
    void     record_rtt_sample(double sample_ms);
    void     adapt_rate(uint32_t lost, uint32_t sent);

    static bool block_is_held(const uint8_t* bitmap, uint32_t block_index)
    {
        return ((bitmap[block_index >> 3] >> (block_index & 7)) & 1) != 0;
    }

    const FlashConfig&       config_;
    const TransferTuning&    tuning_;
    const Callbacks&         callbacks_;
    const std::atomic<bool>& cancel_;

    socket_handle  sock_;
    sockaddr_in    receiver_;
    const uint8_t* image_;
    uint32_t       total_bytes_;
    uint32_t       block_count_;
    uint32_t       image_crc_;
    uint8_t        image_sha_[32];
    std::vector<uint32_t> block_crcs_;    /* computed once, reused on resends */
    std::vector<uint8_t>  receive_buffer_;
    std::vector<uint8_t>  packet_buffer_;

    Pacer    pacer_;
    double   rate_kBps_;
    double   srtt_ms_;
    bool     have_rtt_;
    bool     rate_changed_since_window_; /* next checkpoint window was sent at the OLD rate */

    Progress          progress_;
    FlashResult       result_;
    Clock::time_point last_progress_emit_;
    uint64_t          bytes_at_last_emit_;
};

bool Session::open_socket()
{
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == kInvalidSocket) {
        return false;
    }

    /* A bigger send buffer lets the pacer, not the OS, decide timing. */
    int send_buffer_bytes = 1 << 20;
    setsockopt(sock_, SOL_SOCKET, SO_SNDBUF,
               reinterpret_cast<const char*>(&send_buffer_bytes), sizeof(send_buffer_bytes));

    uint32_t ip_host_order = 0;
    parse_ipv4(config_.receiver_ip, &ip_host_order); /* validated earlier */
    receiver_.sin_family      = AF_INET;
    receiver_.sin_port        = htons(config_.port);
    receiver_.sin_addr.s_addr = htonl(ip_host_order);
    return true;
}

void Session::emit_progress(bool force)
{
    const Clock::time_point now = Clock::now();
    const double elapsed_s = std::chrono::duration<double>(now - last_progress_emit_).count();
    if (!force && elapsed_s < 0.05) {
        return;
    }
    if (elapsed_s > 0.0) {
        const double new_bytes = static_cast<double>(result_.bytes_on_wire - bytes_at_last_emit_);
        const double instant_kBps = (new_bytes / 1024.0) / elapsed_s;
        /* light smoothing so the number is readable */
        progress_.wire_rate_kBps = 0.6 * progress_.wire_rate_kBps + 0.4 * instant_kBps;
    }
    last_progress_emit_ = now;
    bytes_at_last_emit_ = result_.bytes_on_wire;

    progress_.total_blocks     = block_count_;
    progress_.target_rate_kBps = rate_kBps_;
    progress_.blocks_resent    = result_.blocks_resent;
    progress_.rtt_ms           = srtt_ms_;
    progress_.poll_timeout_ms  = current_poll_timeout_ms(0);
    if (callbacks_.progress) {
        callbacks_.progress(progress_);
    }
}

void Session::emit_bitmap(const uint8_t* bitmap, uint32_t bitmap_bytes)
{
    uint32_t held = 0;
    for (uint32_t blk = 0; blk < block_count_; ++blk) {
        if (block_is_held(bitmap, blk)) {
            ++held;
        }
    }
    progress_.acked_blocks = held;

    if (callbacks_.bitmap) {
        std::vector<uint8_t> copy(bitmap, bitmap + bitmap_bytes);
        callbacks_.bitmap(copy, block_count_);
    }
}

/* ---------------------------------------------------------------- send --- */

bool Session::send_block(uint32_t block_index)
{
    /* Guard added after review: v5 trusted the board's total_blocks, and an
     * out-of-range index made (total - offset) underflow and read past the
     * image buffer. */
    if (block_index >= block_count_) {
        return false;
    }

    const uint32_t offset = block_index * kBlockSize;
    uint32_t size = total_bytes_ - offset;
    if (size > kBlockSize) {
        size = kBlockSize;
    }

    ChunkHeader header;
    header.type      = PKT_TYPE_DATA;
    header.offset    = offset;
    header.size      = size;
    header.chunk_crc = block_crcs_[block_index];

    const size_t packet_bytes = sizeof(header) + size;
    if (!pacer_.wait_for_slot(packet_bytes, cancel_)) {
        return false;
    }

    memcpy(packet_buffer_.data(), &header, sizeof(header));
    memcpy(packet_buffer_.data() + sizeof(header), image_ + offset, size);

    const int sent = sendto(sock_, reinterpret_cast<const char*>(packet_buffer_.data()),
                            static_cast<int>(packet_bytes), 0,
                            reinterpret_cast<const sockaddr*>(&receiver_), sizeof(receiver_));
    if (sent < 0) {
        ++result_.send_failures;
        return false;
    }
    result_.bytes_on_wire += packet_bytes;
    return true;
}

/* META is a single un-retransmitted datagram and the first packet to a fresh
 * destination is often lost to ARP resolution, so it goes out as a burst. */
void Session::send_meta_burst()
{
    MetaHeader meta;
    meta.type      = PKT_TYPE_META;
    meta.card_type = static_cast<uint8_t>(config_.card_type);
    meta.totalSize = total_bytes_;
    meta.image_crc = image_crc_;
    memcpy(meta.image_sha256, image_sha_, 32);

    for (int copy = 0; copy < tuning_.meta_repeats; ++copy) {
        sendto(sock_, reinterpret_cast<const char*>(&meta), sizeof(meta), 0,
               reinterpret_cast<const sockaddr*>(&receiver_), sizeof(receiver_));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    pacer_.restart();
}

/* --------------------------------------------------------------- poll --- */

/* A STATUS that arrives after we gave up on its POLL stays queued in the
 * socket; v5 would read it as the answer to the NEXT poll. Throw those away
 * before every poll so each reply reflects the board's current state. */
void Session::drain_stale_datagrams()
{
    sockaddr_in from;
    int drained = 0;
    while (receive_with_timeout(sock_, receive_buffer_.data(),
                                static_cast<int>(receive_buffer_.size()), 0, &from) > 0) {
        ++drained;
    }
    if (drained > 0) {
        std::ostringstream text;
        text << "[poll] discarded " << drained << " stale datagram(s)";
        log(LogLevel::Info, text.str());
    }
}

uint32_t Session::current_poll_timeout_ms(int attempt) const
{
    double base_ms = static_cast<double>(tuning_.poll_timeout_max_ms);
    if (have_rtt_) {
        /* 4x smoothed RTT plus slack for the MCU finishing queued blocks */
        base_ms = srtt_ms_ * 4.0 + 30.0;
    }
    /* exponential backoff on consecutive misses */
    for (int step = 0; step < attempt && step < 4; ++step) {
        base_ms *= 2.0;
    }
    if (base_ms < tuning_.poll_timeout_min_ms) {
        base_ms = tuning_.poll_timeout_min_ms;
    }
    if (base_ms > tuning_.poll_timeout_max_ms) {
        base_ms = tuning_.poll_timeout_max_ms;
    }
    return static_cast<uint32_t>(base_ms);
}

void Session::record_rtt_sample(double sample_ms)
{
    if (!have_rtt_) {
        srtt_ms_  = sample_ms;
        have_rtt_ = true;
    } else {
        srtt_ms_ = 0.875 * srtt_ms_ + 0.125 * sample_ms;
    }
}

/* Sends POLL until a STATUS for this session arrives or budget_ms of silence
 * has passed. Stale / foreign replies no longer burn an attempt: we keep
 * listening until that attempt's deadline. */
PollOutcome Session::poll_for_status(uint32_t budget_ms, bool single_attempt, int* out_len)
{
    PollPacket poll;
    poll.type      = PKT_TYPE_POLL;
    poll.image_crc = image_crc_;

    drain_stale_datagrams();

    const Clock::time_point budget_start = Clock::now();
    int attempt = 0;

    while (true) {
        if (cancel_.load()) {
            return PollOutcome::Cancelled;
        }

        const uint32_t timeout_ms = current_poll_timeout_ms(attempt);
        const Clock::time_point sent_at = Clock::now();
        sendto(sock_, reinterpret_cast<const char*>(&poll), sizeof(poll), 0,
               reinterpret_cast<const sockaddr*>(&receiver_), sizeof(receiver_));
        const Clock::time_point deadline = sent_at + std::chrono::milliseconds(timeout_ms);

        while (true) {
            const long long remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - Clock::now()).count();
            if (remaining_ms <= 0) {
                break;
            }

            sockaddr_in from;
            memset(&from, 0, sizeof(from));
            const int received = receive_with_timeout(
                sock_, receive_buffer_.data(), static_cast<int>(receive_buffer_.size()),
                static_cast<int>(remaining_ms), &from);

            if (received <= 0) {
                if (received < 0) {
                    std::ostringstream text;
                    text << "[poll] receive error " << last_socket_error();
                    log(LogLevel::Warn, text.str());
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                continue;
            }

            /* Only the board we are flashing may answer. */
            if (from.sin_addr.s_addr != receiver_.sin_addr.s_addr) {
                continue;
            }
            if (received < static_cast<int>(sizeof(StatusHeader))) {
                continue;
            }

            StatusHeader header;
            memcpy(&header, receive_buffer_.data(), sizeof(header));
            if (header.type != PKT_TYPE_STATUS) {
                continue;
            }

            /* NEED_META cannot carry our session id (board has no META yet). */
            if (header.image_crc == image_crc_ || header.status == FW_STATUS_NEED_META) {
                /* Karn's rule: only time replies to a first, unambiguous poll. */
                if (attempt == 0) {
                    record_rtt_sample(std::chrono::duration<double, std::milli>(
                        Clock::now() - sent_at).count());
                }
                *out_len = received;
                return PollOutcome::Reply;
            }
            /* stale session: keep waiting within this attempt */
        }

        ++progress_.poll_timeouts;
        ++attempt;
        std::ostringstream text;
        text << "[poll] no reply in " << timeout_ms << " ms (attempt " << attempt << ")";
        log(LogLevel::Warn, text.str());

        if (single_attempt) {
            return PollOutcome::Timeout;
        }
        if (static_cast<uint32_t>(ms_since(budget_start)) >= budget_ms) {
            return PollOutcome::Timeout;
        }
    }
}

bool Session::status_bitmap_is_valid(const StatusHeader& header, int received_len)
{
    const uint32_t needed_bitmap_bytes = (block_count_ + 7) / 8;

    if (header.total_blocks != block_count_) {
        std::ostringstream text;
        text << "[status] board reports " << header.total_blocks << " blocks, image has "
             << block_count_ << " - ignoring this STATUS";
        log(LogLevel::Warn, text.str());
        return false;
    }
    if (header.bitmap_bytes < needed_bitmap_bytes) {
        log(LogLevel::Warn, "[status] bitmap shorter than block count - ignoring");
        return false;
    }
    if (received_len < static_cast<int>(sizeof(StatusHeader) + header.bitmap_bytes)) {
        log(LogLevel::Warn, "[status] truncated bitmap - ignoring");
        return false;
    }
    return true;
}

/* ------------------------------------------------------- rate control --- */

/* AIMD-style: speed up gently while the board keeps up, back off hard when
 * it clearly can't. The board's receive ring + flash write speed is the real
 * ceiling; this finds it instead of guessing.
 *
 * Three zones, so random link noise doesn't drag the rate down (slowing down
 * can't fix a noisy cable, and the repair rounds mop that up anyway):
 *   loss  < 2%   -> clean: +20%
 *   2% .. 10%    -> hold (could be noise or mild overload)
 *   loss > 10%   -> overrun: x0.7   (> 25%: x0.5)
 * A ring overrun at even 10% too fast shows up as 10%+ loss; 2% random loss
 * almost never puts 7 of 64 blocks in one window. */
void Session::adapt_rate(uint32_t lost, uint32_t sent)
{
    if (sent == 0) {
        return;
    }
    const double loss_pct = 100.0 * static_cast<double>(lost) / static_cast<double>(sent);
    progress_.last_loss_pct = loss_pct;

    if (!tuning_.adaptive_rate) {
        return;
    }

    const double old_rate = rate_kBps_;
    if (loss_pct > 25.0) {
        rate_kBps_ *= 0.5;
    } else if (loss_pct > 10.0) {
        rate_kBps_ *= 0.7;
    } else if (loss_pct < 2.0) {
        rate_kBps_ *= 1.2;
    }

    if (rate_kBps_ < tuning_.min_rate_kBps) {
        rate_kBps_ = tuning_.min_rate_kBps;
    }
    if (rate_kBps_ > tuning_.max_rate_kBps) {
        rate_kBps_ = tuning_.max_rate_kBps;
    }
    pacer_.set_rate_kBps(rate_kBps_);

    if (static_cast<int>(old_rate) != static_cast<int>(rate_kBps_)) {
        rate_changed_since_window_ = true;
        std::ostringstream text;
        text.setf(std::ios::fixed);
        text.precision(1);
        text << "[rate] loss " << loss_pct << "% over " << sent << " blocks -> "
             << static_cast<int>(old_rate) << " -> " << static_cast<int>(rate_kBps_) << " KB/s";
        log(LogLevel::Info, text.str());
    }
}

/* ------------------------------------------------------- first pass --- */

/* Handles a STATUS received at a checkpoint. Loss is judged on the window
 * [eval_from, eval_to), which ends one checkpoint BEHIND the cursor: blocks
 * sent just before the POLL may still be queued in the MCU (control and image
 * traffic may not share a FIFO), so judging them would count them as lost. */
PassOutcome Session::handle_checkpoint_reply(int received_len, uint32_t eval_from, uint32_t eval_to)
{
    StatusHeader header;
    memcpy(&header, receive_buffer_.data(), sizeof(header));

    if (header.status == FW_STATUS_NEED_META) {
        return PassOutcome::NeedMeta;
    }
    if (header.status == FW_STATUS_IMAGE_FAIL) {
        return PassOutcome::ImageFail;
    }
    if (header.status == FW_STATUS_COMPLETE) {
        return PassOutcome::Complete;
    }
    if (!status_bitmap_is_valid(header, received_len)) {
        return PassOutcome::Finished; /* ignore this checkpoint, keep sending */
    }

    /* Copy the bitmap out: resends below reuse the receive buffer. */
    const uint8_t* raw_bitmap = receive_buffer_.data() + sizeof(StatusHeader);
    std::vector<uint8_t> bitmap(raw_bitmap, raw_bitmap + header.bitmap_bytes);
    emit_bitmap(bitmap.data(), header.bitmap_bytes);

    std::vector<uint32_t> missing;
    for (uint32_t blk = eval_from; blk < eval_to; ++blk) {
        if (!block_is_held(bitmap.data(), blk)) {
            missing.push_back(blk);
        }
    }
    /* The window we judge was sent before the last rate change took effect;
     * judging it would punish (or reward) the new rate for the old one. */
    if (rate_changed_since_window_) {
        rate_changed_since_window_ = false;
        if (eval_to > eval_from) {
            progress_.last_loss_pct = 100.0 * static_cast<double>(missing.size())
                                    / static_cast<double>(eval_to - eval_from);
        }
    } else {
        adapt_rate(static_cast<uint32_t>(missing.size()), eval_to - eval_from);
    }

    /* Repair early, while we are streaming anyway. */
    for (size_t i = 0; i < missing.size(); ++i) {
        if (send_block(missing[i])) {
            ++result_.blocks_resent;
        }
        if (cancel_.load()) {
            return PassOutcome::Cancelled;
        }
    }
    return PassOutcome::Finished;
}

PassOutcome Session::send_pass(bool with_checkpoints)
{
    pacer_.set_rate_kBps(rate_kBps_);

    uint32_t last_checkpoint_cursor     = 0;
    uint32_t previous_checkpoint_cursor = 0;
    uint32_t evaluated_up_to            = 0;
    const uint32_t checkpoint_every     = tuning_.checkpoint_every_blocks;

    for (uint32_t blk = 0; blk < block_count_; ++blk) {
        if (cancel_.load()) {
            return PassOutcome::Cancelled;
        }
        send_block(blk);
        progress_.sent_cursor = blk + 1;
        emit_progress(false);

        const bool checkpoint_due = with_checkpoints && checkpoint_every > 0
            && (blk + 1) - last_checkpoint_cursor >= checkpoint_every
            && (blk + 1) < block_count_;
        if (!checkpoint_due) {
            continue;
        }

        int received_len = 0;
        const PollOutcome outcome = poll_for_status(0, true, &received_len);
        if (outcome == PollOutcome::Cancelled) {
            return PassOutcome::Cancelled;
        }
        if (outcome == PollOutcome::Reply) {
            const PassOutcome pass = handle_checkpoint_reply(
                received_len, evaluated_up_to, previous_checkpoint_cursor);
            if (pass != PassOutcome::Finished) {
                return pass;
            }
            evaluated_up_to = previous_checkpoint_cursor;
        }
        /* a lost checkpoint poll is harmless: keep streaming */
        previous_checkpoint_cursor = blk + 1;
        last_checkpoint_cursor     = blk + 1;
    }
    emit_progress(true);
    return PassOutcome::Finished;
}

/* --------------------------------------------------------------- run --- */

FlashResult Session::run()
{
    const Clock::time_point run_start = Clock::now();
    result_.total_blocks = block_count_;
    progress_.total_blocks = block_count_;

    /* ---- hash once; per-block CRCs once (v5 recomputed on every resend) ---- */
    image_crc_ = image_crc32(image_, total_bytes_);
    image_sha256(image_, total_bytes_, image_sha_);
    result_.image_crc = image_crc_;
    block_crcs_.resize(block_count_);
    for (uint32_t blk = 0; blk < block_count_; ++blk) {
        const uint32_t offset = blk * kBlockSize;
        uint32_t size = total_bytes_ - offset;
        if (size > kBlockSize) {
            size = kBlockSize;
        }
        block_crcs_[blk] = image_crc32(image_ + offset, size);
    }
    packet_buffer_.resize(sizeof(ChunkHeader) + kBlockSize);
    pacer_.set_burst(tuning_.burst_packets);
    pacer_.set_rate_kBps(rate_kBps_);

    if (!open_socket()) {
        result_.message = "Socket creation failed";
        return result_;
    }

    {
        std::ostringstream text;
        char crc_hex[16];
        snprintf(crc_hex, sizeof(crc_hex), "0x%08x", image_crc_);
        text << "META x" << tuning_.meta_repeats << " -> " << config_.receiver_ip << ":"
             << config_.port << "  card=" << config_.card_type << " ("
             << card_type_name(config_.card_type) << ") size=" << total_bytes_
             << " blocks=" << block_count_ << " crc=" << crc_hex;
        log(LogLevel::Info, text.str());
    }

    /* ==== 1. Handshake ==================================================== */
    progress_.phase = Phase::Handshake;
    emit_progress(true);
    Clock::time_point phase_start = Clock::now();

    bool session_up    = false;
    bool already_there = false;
    for (int attempt = 0; attempt < tuning_.meta_handshake_tries && !session_up; ++attempt) {
        send_meta_burst();
        int received_len = 0;
        const PollOutcome outcome = poll_for_status(tuning_.poll_give_up_ms, false, &received_len);
        if (outcome == PollOutcome::Cancelled) {
            result_.cancelled = true;
            result_.message   = "Cancelled during handshake";
            return result_;
        }
        if (outcome == PollOutcome::Timeout) {
            log(LogLevel::Warn, "[handshake] no reply, re-sending META");
            continue;
        }
        StatusHeader header;
        memcpy(&header, receive_buffer_.data(), sizeof(header));
        if (header.status == FW_STATUS_NEED_META) {
            log(LogLevel::Warn, "[handshake] META not yet received, re-sending");
            continue;
        }
        session_up = true;
        if (header.status == FW_STATUS_COMPLETE) {
            already_there = true;
        }
        std::ostringstream text;
        text.setf(std::ios::fixed);
        text.precision(1);
        text << "Session established (RTT " << srtt_ms_ << " ms)";
        log(LogLevel::Info, text.str());
    }
    result_.handshake_ms = ms_since(phase_start);

    if (!session_up) {
        result_.message = "META never acknowledged - check link / IP / board is in updater";
        result_.total_ms = ms_since(run_start);
        return result_;
    }

    bool success = already_there;
    if (already_there) {
        log(LogLevel::Info, "Board already holds this exact image - nothing to send");
    }

    /* ==== 2. First pass =================================================== */
    int image_fail_restarts = 0;
    if (!success) {
        progress_.phase = Phase::InitialSend;
        emit_progress(true);
        phase_start = Clock::now();
        const PassOutcome pass = send_pass(true);
        result_.initial_send_ms = ms_since(phase_start);

        if (pass == PassOutcome::Cancelled) {
            result_.cancelled = true;
            result_.message   = "Cancelled during send";
            result_.total_ms  = ms_since(run_start);
            return result_;
        }
        if (pass == PassOutcome::Complete) {
            success = true;
        }
        if (pass == PassOutcome::NeedMeta || pass == PassOutcome::ImageFail) {
            /* Fall through: the repair loop's next poll sees the same status
             * and applies the normal recovery (with its restart cap). */
            log(LogLevel::Warn, "[send] board lost the session mid-pass - recovering");
        }
        std::ostringstream text;
        text << "First pass done: " << block_count_ << " blocks, "
             << progress_.acked_blocks << " confirmed so far";
        log(LogLevel::Info, text.str());
    }

    /* ==== 3. Poll / repair ================================================ */
    progress_.phase = Phase::Repair;
    phase_start = Clock::now();
    std::vector<uint32_t> resent_last_round;
    int round = 0;

    for (round = 0; round < tuning_.max_rounds && !success; ++round) {
        progress_.round = round;
        emit_progress(true);

        int received_len = 0;
        const PollOutcome outcome = poll_for_status(tuning_.poll_give_up_ms, false, &received_len);
        if (outcome == PollOutcome::Cancelled) {
            result_.cancelled = true;
            result_.message   = "Cancelled during repair";
            break;
        }
        if (outcome == PollOutcome::Timeout) {
            std::ostringstream text;
            text << "No STATUS for " << tuning_.poll_give_up_ms << " ms - aborting";
            result_.message = text.str();
            log(LogLevel::Error, text.str());
            break;
        }

        StatusHeader header;
        memcpy(&header, receive_buffer_.data(), sizeof(header));

        if (header.status == FW_STATUS_COMPLETE) {
            std::ostringstream text;
            text << "[round " << round << "] COMPLETE - image accepted";
            log(LogLevel::Info, text.str());
            success = true;
            break;
        }

        if (header.status == FW_STATUS_NEED_META || header.status == FW_STATUS_IMAGE_FAIL) {
            if (header.status == FW_STATUS_IMAGE_FAIL) {
                if (image_fail_restarts >= tuning_.max_image_fail_restarts) {
                    result_.message = "Board rejected the image again (IMAGE_FAIL) - stopping. "
                                      "Check the card type and that the file is the right build.";
                    log(LogLevel::Error, result_.message);
                    break;
                }
                ++image_fail_restarts;
                log(LogLevel::Warn, "[round] IMAGE_FAIL - restarting transfer once");
            } else {
                log(LogLevel::Warn, "[round] NEED_META - resending META + image");
            }
            send_meta_burst();
            const PassOutcome pass = send_pass(true);
            if (pass == PassOutcome::Cancelled) {
                result_.cancelled = true;
                result_.message   = "Cancelled during resend";
                break;
            }
            resent_last_round.clear();
            continue;
        }

        /* INCOMPLETE */
        if (!status_bitmap_is_valid(header, received_len)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        const uint8_t* raw_bitmap = receive_buffer_.data() + sizeof(StatusHeader);
        std::vector<uint8_t> bitmap(raw_bitmap, raw_bitmap + header.bitmap_bytes);
        emit_bitmap(bitmap.data(), header.bitmap_bytes);

        /* Loss signal for the repair rounds: of what we resent last round,
         * how much is still missing now. */
        if (!resent_last_round.empty()) {
            uint32_t still_missing = 0;
            for (size_t i = 0; i < resent_last_round.size(); ++i) {
                if (!block_is_held(bitmap.data(), resent_last_round[i])) {
                    ++still_missing;
                }
            }
            adapt_rate(still_missing, static_cast<uint32_t>(resent_last_round.size()));
        }

        resent_last_round.clear();
        for (uint32_t blk = 0; blk < block_count_; ++blk) {
            if (!block_is_held(bitmap.data(), blk)) {
                resent_last_round.push_back(blk);
            }
        }

        std::ostringstream text;
        text << "[round " << round << "] " << header.blocks_received << "/" << block_count_
             << " - resending " << resent_last_round.size();
        log(LogLevel::Info, text.str());

        if (resent_last_round.empty()) {
            /* bitmap full but not COMPLETE yet: board is still verifying */
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        pacer_.restart();
        for (size_t i = 0; i < resent_last_round.size(); ++i) {
            if (send_block(resent_last_round[i])) {
                ++result_.blocks_resent;
            }
            emit_progress(false);
            if (cancel_.load()) {
                break;
            }
        }
    }

    result_.repair_ms       = ms_since(phase_start);
    result_.repair_rounds   = round;
    result_.success         = success;
    result_.total_ms        = ms_since(run_start);
    result_.final_rate_kBps = rate_kBps_;
    if (result_.total_ms > 0) {
        result_.avg_kBps = (result_.bytes_on_wire / 1024.0) / (result_.total_ms / 1000.0);
    }
    if (success) {
        result_.message = "Image accepted";
    } else if (result_.message.empty()) {
        result_.message = "Gave up after max repair rounds";
    }

    progress_.phase = Phase::Done;
    emit_progress(true);
    return result_;
}

} // namespace

/* =========================================================================
 * FlashEngine
 * ========================================================================= */
FlashEngine::FlashEngine() : cancel_requested_(false) {}
FlashEngine::~FlashEngine() {}

void FlashEngine::cancel()
{
    cancel_requested_.store(true);
}

std::string FlashEngine::validate(const FlashConfig& config)
{
    uint32_t ip = 0;
    if (!parse_ipv4(config.receiver_ip, &ip)) {
        return "Receiver IP is not a valid IPv4 address";
    }
    if (ip == 0xFFFFFFFFu || ip == 0) {
        return "Receiver IP cannot be 0.0.0.0 or broadcast";
    }
    if (config.port == 0) {
        return "Port must be 1-65535";
    }
    if (config.card_type < 1 || config.card_type > 4) {
        return "Card type must be VCC(1), Input(2), Output(3) or Analog(4)";
    }
    if (config.image.empty()) {
        return "Image file is empty";
    }
    if (config.image.size() > 0x7FFFFFFFu) {
        return "Image file is too large";
    }
    /* The STATUS bitmap has to fit in one datagram. */
    const uint64_t blocks = (config.image.size() + kBlockSize - 1) / kBlockSize;
    if ((blocks + 7) / 8 + sizeof(StatusHeader) > 1400) {
        return "Image too large for a single-datagram STATUS bitmap";
    }
    if (config.tuning.start_rate_kBps == 0 || config.tuning.min_rate_kBps == 0
        || config.tuning.max_rate_kBps < config.tuning.min_rate_kBps) {
        return "Rate settings are inconsistent";
    }
    return std::string();
}

FlashResult FlashEngine::run(const FlashConfig& config, const Callbacks& callbacks)
{
    cancel_requested_.store(false);

    FlashResult result;
    const std::string problem = validate(config);
    if (!problem.empty()) {
        result.message = problem;
        return result;
    }

    PlatformScope platform;
    if (!platform.ok()) {
        result.message = "WSAStartup failed";
        return result;
    }

    Session session(config, callbacks, cancel_requested_);
    return session.run();
}

} // namespace kflash

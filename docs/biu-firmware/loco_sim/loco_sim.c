/* Loco simulator: plays the VCC's side against DLConsole's Brake Test Bench.
 *
 * It runs the real firmware file (../brake_pressure_interface_user.c) on the
 * UDP link, as the VCC does on the bench:
 *   - sends the boot-up, the heartbeat and the 0x190 command every 250 ms to
 *     the bench (UDP 51234), and takes the bench's NMT, heartbeat and status
 *     frames on the reply port (UDP 50001);
 *   - takes the analog card's 34-byte frames (UDP 20000) and reads BP, BC and
 *     MR through current_to_pressure(), as the VCC does;
 *   - once a second prints what it commanded, what the BIU reported, what the
 *     analog card reads, and what check_brake_malfunction() says of each.
 *
 * Type a brake type (0-5) and Enter to change the brake; i toggles BIU
 * isolate, q quits. See README.md for the options.
 *
 * shortcut: POSIX sockets only (Mac, Linux); add Winsock if it is needed on Windows. */
#include "common_include.h"
#include "../brake_pressure_interface_user.h"
#include "enet_user.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

LocoParams loco_params;

static int      s_biu_socket = -1;        /* bound to the reply port: the bench answers the sender */
static int      s_analog_socket = -1;
static uint32_t s_bench_override_ip;      /* host order; 0 = use the firmware's BIU_SIM_IP0..3 */
static uint16_t s_bench_port = 51234;

/* ---- the firmware's network calls, on real sockets ------------------------------- */
ip_addr_t set_pc_ipAddr(ipAddress address)
{
    ip_addr_t packed = { ((uint32_t)address.ip_addr0 << 24) | ((uint32_t)address.ip_addr1 << 16)
                         | ((uint32_t)address.ip_addr2 << 8) | address.ip_addr3 };
    return packed;
}

void UDP_Send(const char *buffer, ip_addr_t *ip_address, uint16_t port, uint16_t length)
{
    struct sockaddr_in destination;
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_addr.s_addr = htonl(s_bench_override_ip ? s_bench_override_ip : ip_address->addr);
    destination.sin_port = htons(s_bench_port);
    (void)port;   /* the firmware's BIU_SIM_PORT; --bench-port sets it here */
    if (sendto(s_biu_socket, buffer, length, 0, (struct sockaddr *)&destination, sizeof(destination)) < 0)
        fprintf(stderr, "loco_sim: send to the bench failed: %s\n", strerror(errno));
}

/* ---- the analog card's frame (DLConsole cabio.h, session 346-347) -------------------
 *  [length][src 15/16][dest 1][0xF1][seq lo][seq hi][6 x uint32 big-endian][CRC le32]
 *  mA = raw / 2^31 * 75; CRC: JAMCRC init 0 over the 30 bytes before it. */
#define ANALOG_FRAME_LEN    34
#define ANALOG_MESSAGE_ID   0xF1U
#define ANALOG_CHANNELS     6

typedef struct {
    uint8_t  card_id;              /* 15 top, 16 bottom */
    uint16_t sequence;
    float    current_mA[ANALOG_CHANNELS];
    uint8_t  crc_matches;
    uint32_t received_ms;          /* 0 = never */
} AnalogFrame;

static AnalogFrame s_last_analog;

static uint32_t jamcrc_init0(const uint8_t *bytes, size_t length)
{
    uint32_t crc = 0;
    for (size_t byte_index = 0; byte_index < length; ++byte_index) {
        crc ^= bytes[byte_index];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1U) ? (crc >> 1) ^ 0xEDB88320U : crc >> 1;
    }
    return crc;
}

static int parse_analog_frame(const uint8_t *bytes, size_t length, AnalogFrame *out)
{
    if (length != ANALOG_FRAME_LEN || bytes[3] != ANALOG_MESSAGE_ID) return 0;
    out->card_id  = bytes[1];
    out->sequence = (uint16_t)(bytes[4] | (bytes[5] << 8));
    for (int channel = 0; channel < ANALOG_CHANNELS; ++channel) {
        const uint8_t *word = bytes + 6 + 4 * channel;
        const uint32_t raw = ((uint32_t)word[0] << 24) | ((uint32_t)word[1] << 16)
                             | ((uint32_t)word[2] << 8) | word[3];
        out->current_mA[channel] = (float)((double)raw / 2147483648.0 * 75.0);
    }
    const uint32_t stored_crc = (uint32_t)bytes[30] | ((uint32_t)bytes[31] << 8)
                                | ((uint32_t)bytes[32] << 16) | ((uint32_t)bytes[33] << 24);
    out->crc_matches = (uint8_t)(jamcrc_init0(bytes, 30) == stored_crc);
    return 1;
}

/* ---- the report ------------------------------------------------------------------- */
static const char *brake_name(uint8_t brake_type)
{
    static const char *const names[] = { "NO BRAKE", "OVERSPEED", "NB", "FSB", "EB", "LE" };
    return brake_type < 6 ? names[brake_type] : "?";
}

static const char *check_name(BrakeCheck check)
{
    switch (check) {
    case BRAKE_CHECK_WAIT:        return "wait";
    case BRAKE_CHECK_OK:          return "ok";
    case BRAKE_CHECK_MALFUNCTION: return "MALFUNCTION";
    default:                      return "NO SPEC";
    }
}

static const char *nmt_name(uint8_t state)
{
    switch (state) {
    case BIU_NMT_BOOT_UP:         return "boot-up";
    case BIU_NMT_STOPPED:         return "stopped";
    case BIU_NMT_OPERATIONAL:     return "operational";
    case BIU_NMT_PRE_OPERATIONAL: return "pre-operational";
    default:                      return "?";
    }
}

static void print_report(uint8_t brake_type, float analog_full_scale_bar)
{
    const BiuStatus *status = biu_status_get();
    const uint32_t now_ms = (uint32_t)(ClockP_getTimeUsec() / 1000U);
    printf("\n[%s] us %s, BIU link %s, isolate %u\n", brake_name(brake_type),
           nmt_name(biu_nmt_state()), biu_link_up() ? "up" : "DOWN",
           (unsigned)loco_params.dmi_context_values.biu_isolated);

    static const struct { const char *name; BiuPressureId biu_index; PressureCmd which; } kRows[3] = {
        { "BP", BIU_P_BP, CMD_BP }, { "BC", BIU_P_BC, CMD_BC }, { "MR", BIU_P_MR, CMD_MR },
    };
    const int analog_fresh = s_last_analog.received_ms != 0U && now_ms - s_last_analog.received_ms < 2000U;
    for (int row = 0; row < 3; ++row) {
        const BiuPressure *reported = &status->pressure[kRows[row].biu_index];
        printf("  %s  BIU ", kRows[row].name);
        if (status->rx_ms[0] == 0U)   printf("  --        ");
        else if (!reported->valid)    printf("  not valid ");
        else printf("%5.2f %-5s ", (double)reported->kgcm2,
                    check_name(check_brake_malfunction(kRows[row].which, reported->kgcm2)));

        printf("  analog ");
        if (!analog_fresh) { printf("  --\n"); continue; }
        const float current_mA = s_last_analog.current_mA[row];
        const PressureReading reading = current_to_pressure(current_mA, analog_full_scale_bar);
        if (reading.status == SENSE_FAULT_OPEN)       printf("%5.2f mA OPEN\n", (double)current_mA);
        else if (reading.status == SENSE_FAULT_SHORT) printf("%5.2f mA SHORT\n", (double)current_mA);
        else {
            printf("%5.2f mA = %5.2f %-5s", (double)current_mA, (double)reading.value_kgcm2,
                   check_name(check_brake_malfunction(kRows[row].which, reading.value_kgcm2)));
            if (status->rx_ms[0] != 0U && reported->valid)
                printf("  analog - BIU %+.2f", (double)(reading.value_kgcm2 - reported->kgcm2));
            printf("\n");
        }
    }
    if (analog_fresh)
        printf("  analog card %u seq %u, CRC %s\n", (unsigned)s_last_analog.card_id,
               (unsigned)s_last_analog.sequence, s_last_analog.crc_matches ? "ok" : "does not match JAMCRC/0");
    if (status->fault_code || status->display_code)
        printf("  BIU fault code 0x%04X, display code 0x%04X\n", (unsigned)status->fault_code, (unsigned)status->display_code);
}

/* ---- set-up ----------------------------------------------------------------------- */
static int open_udp_socket(uint16_t port)
{
    const int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) return -1;
    const int reuse = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    if (bind(socket_fd, (struct sockaddr *)&local, sizeof(local)) < 0) { close(socket_fd); return -1; }
    return socket_fd;
}

static void usage(void)
{
    printf("loco_sim [--bench a.b.c.d] [--bench-port 51234] [--reply-port 50001] [--analog-port 20000]\n"
           "         [--loco wap7|wag9|wap5] [--mode formation|lem] [--brake 0-5] [--speed m/s]\n"
           "         [--analog-bar 10] [--can]\n"
           "  While running: 0-5 + Enter sets the brake (0 no brake, 1 overspeed, 2 NB, 3 FSB, 4 EB, 5 LE),\n"
           "  i toggles BIU isolate, s toggles speed valid, q quits.\n");
}

int main(int argument_count, char **arguments)
{
    uint16_t reply_port = 50001, analog_port = 20000;
    uint8_t  loco_type = LOCO_WAP7, train_mode = TRAIN_MODE_FORMATION, brake_type = NO_BRAKE_T;
    float    analog_full_scale_bar = 10.0f;
    int      use_can_link = 0;

    loco_params.sensor_speed = 0.0f;
    loco_params.speed_ok = 1;
    loco_params.loco_health_status = 1;
    loco_params.train_type = FORMATION_TYPE;

    for (int index = 1; index < argument_count; ++index) {
        const char *option = arguments[index];
        const char *value = index + 1 < argument_count ? arguments[index + 1] : "";
        if (!strcmp(option, "--bench")) {
            struct in_addr parsed;
            if (inet_pton(AF_INET, value, &parsed) != 1) { fprintf(stderr, "not an IPv4 address: %s\n", value); return 2; }
            s_bench_override_ip = ntohl(parsed.s_addr); ++index;
        }
        else if (!strcmp(option, "--bench-port"))  { s_bench_port = (uint16_t)atoi(value); ++index; }
        else if (!strcmp(option, "--reply-port"))  { reply_port = (uint16_t)atoi(value); ++index; }
        else if (!strcmp(option, "--analog-port")) { analog_port = (uint16_t)atoi(value); ++index; }
        else if (!strcmp(option, "--loco")) {
            loco_type = !strcmp(value, "wag9") ? LOCO_WAG9 : !strcmp(value, "wap5") ? LOCO_WAP5 : LOCO_WAP7; ++index;
        }
        else if (!strcmp(option, "--mode")) {
            train_mode = !strcmp(value, "lem") ? TRAIN_MODE_LEM : TRAIN_MODE_FORMATION; ++index;
        }
        else if (!strcmp(option, "--brake"))      { brake_type = (uint8_t)atoi(value); ++index; }
        else if (!strcmp(option, "--speed"))      { loco_params.sensor_speed = (float)atof(value); ++index; }
        else if (!strcmp(option, "--analog-bar")) { analog_full_scale_bar = (float)atof(value); ++index; }
        else if (!strcmp(option, "--can"))        { use_can_link = 1; }
        else { usage(); return !strcmp(option, "--help") ? 0 : 2; }
    }
    loco_params.train_type = train_mode == TRAIN_MODE_LEM ? LIGHT_ENGINE_TYPE : FORMATION_TYPE;

    s_biu_socket = open_udp_socket(reply_port);
    s_analog_socket = open_udp_socket(analog_port);
    if (s_biu_socket < 0 || s_analog_socket < 0) {
        fprintf(stderr, "loco_sim: cannot bind UDP %u / %u: %s\n", (unsigned)reply_port, (unsigned)analog_port, strerror(errno));
        return 1;
    }

    /* --can runs the CAN link with no driver: nothing goes out. It shows the
     * firmware refusing the bench's UDP frames when CAN is selected. */
    biu_link_select(use_can_link ? BIU_LINK_CAN : BIU_LINK_UDP);
    biu_link_init();
    printf("loco_sim: VCC as loco %u mode %u, BIU frames to %s:%u, listening on %u (BIU) and %u (analog)\n",
           (unsigned)loco_type, (unsigned)train_mode,
           s_bench_override_ip ? "--bench" : "BIU_SIM_IP", (unsigned)s_bench_port, (unsigned)reply_port, (unsigned)analog_port);
    usage();

    int      stdin_open = 1;   /* run without a terminal: stop reading at end of input */
    uint64_t next_command_us = ClockP_getTimeUsec();
    uint64_t next_report_us = next_command_us + 1000000U;
    for (;;) {
        const uint64_t now_us = ClockP_getTimeUsec();
        if (now_us >= next_command_us) {
            biu_link_tick();
            send_biu_command(loco_type, train_mode, brake_type);
            next_command_us += 250000U;                /* Annexure-1 4: every 250 ms */
        }
        if (now_us >= next_report_us) {
            print_report(brake_type, analog_full_scale_bar);
            fflush(stdout);
            next_report_us += 1000000U;
        }

        fd_set readable;
        FD_ZERO(&readable);
        if (stdin_open) FD_SET(STDIN_FILENO, &readable);
        FD_SET(s_biu_socket, &readable);
        FD_SET(s_analog_socket, &readable);
        const int highest_fd = s_biu_socket > s_analog_socket ? s_biu_socket : s_analog_socket;
        const uint64_t wait_us = next_command_us > now_us ? next_command_us - now_us : 0U;
        struct timeval timeout = { (time_t)(wait_us / 1000000U), (suseconds_t)(wait_us % 1000000U) };
        if (select(highest_fd + 1, &readable, NULL, NULL, &timeout) < 0) {
            if (errno == EINTR) continue;
            perror("select");
            return 1;
        }

        uint8_t datagram[256];
        if (FD_ISSET(s_biu_socket, &readable)) {
            const ssize_t length = recv(s_biu_socket, datagram, sizeof(datagram), 0);
            if (length > 0 && !process_biu_udp_frame(datagram, (uint16_t)length))
                printf("loco_sim: BIU datagram of %zd bytes not taken (COB-ID 0x%03X)\n", length,
                       length >= 2 ? (unsigned)(datagram[0] | (datagram[1] << 8)) : 0U);
        }
        if (FD_ISSET(s_analog_socket, &readable)) {
            const ssize_t length = recv(s_analog_socket, datagram, sizeof(datagram), 0);
            AnalogFrame frame;
            if (length > 0 && parse_analog_frame(datagram, (size_t)length, &frame)) {
                frame.received_ms = (uint32_t)(ClockP_getTimeUsec() / 1000U);
                if (frame.received_ms == 0U) frame.received_ms = 1U;
                s_last_analog = frame;
            } else if (length > 0) {
                printf("loco_sim: analog datagram of %zd bytes is not a 34-byte 0xF1 frame\n", length);
            }
        }
        if (stdin_open && FD_ISSET(STDIN_FILENO, &readable)) {
            char line[64];
            if (!fgets(line, sizeof(line), stdin)) { stdin_open = 0; continue; }
            if (line[0] == 'q') break;
            if (line[0] >= '0' && line[0] <= '5') brake_type = (uint8_t)(line[0] - '0');
            else if (line[0] == 'i') loco_params.dmi_context_values.biu_isolated ^= 1U;
            else if (line[0] == 's') loco_params.speed_ok ^= 1U;
            printf("loco_sim: brake %s, isolate %u, speed valid %u\n", brake_name(brake_type),
                   (unsigned)loco_params.dmi_context_values.biu_isolated, (unsigned)loco_params.speed_ok);
        }
    }
    close(s_biu_socket);
    close(s_analog_socket);
    return 0;
}

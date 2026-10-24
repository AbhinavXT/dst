#include "common_include.h"
#include "brake_pressure_interface_user.h"
#include "enet_user.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ---- Build hooks (see brake_pressure_interface_user.h) ------------------------ */
#ifndef BIU_SPEED_TO_CM_PER_S
/* loco_params.sensor_speed is m/s: 0.01 m/s on the wire. */
#define BIU_SPEED_TO_CM_PER_S(v)  ((float)(v) * 100.0f)
#endif
#ifndef BIU_SPEED_VALID
#error "Define BIU_SPEED_VALID(): 1 while the speed sensor is healthy"
#endif
#ifndef BIU_DYNAMIC_BRAKING
#define BIU_DYNAMIC_BRAKING()  0U
#endif
#ifndef BIU_NOW_MS
#define BIU_NOW_MS()           ((uint32_t)(ClockP_getTimeUsec() / 1000U))
#endif
#ifndef BIU_SIM_IP0
#define BIU_SIM_IP0 192U
#define BIU_SIM_IP1 168U
#define BIU_SIM_IP2 25U
#define BIU_SIM_IP3 126U
#endif
#ifndef BIU_SIM_PORT
#define BIU_SIM_PORT 51234U
#endif
#ifndef BIU_HEARTBEAT_PERIOD_MS
#define BIU_HEARTBEAT_PERIOD_MS   500U      /* Annexure-1 4 */
#endif
#ifndef BIU_HEARTBEAT_TIMEOUT_MS
#define BIU_HEARTBEAT_TIMEOUT_MS  1500U     /* three BIU heartbeats missed */
#endif

/* The VCC's brake types (DMI brake type 0-5) are the rows of kBrakeTable. */
_Static_assert((int)EMERGENCY_BRAKE == (int)CMD_EB, "VCC brake types must match the Command rows of kBrakeTable");
_Static_assert((int)LIGHT_ENGINE_BRAKE == (int)CMD_LE, "LIGHT_ENGINE_BRAKE must be the CMD_LE row");

/*
 * Pressures and times per loco, mode and command, to Annexure-4:
 *   BP  no brake 5.0 +/- 0.1; NB 0.6 drop = 4.4; FSB 1.5 drop = 3.5; EB to 0.
 *   BC  NB 1.2 +/- 0.1 (8.1, 9.1); FSB / EB: light engine 3.5 +/- 0.1 (8.6 note),
 *       formation 2.4 +/- 0.1 (9.8).
 *   LE  the light engine brake: the independent brake (SA9) in full application,
 *       BC 3.5 +/- 0.1 with BP left charged (4.2, 2.4). The BIU spec has no LE
 *       command of its own; this is its SA9 full application.
 * The spec's BC values are the same for every loco; the times are per loco.
 */
#define BP_RELEASED  RANGE_TOL(5.0f, 0.1f)
#define BP_NB        RANGE_TOL(4.4f, 0.1f)
#define BP_FSB       RANGE_TOL(3.5f, 0.2f)
#define BP_EB        RANGE_UPPER(0.3f)
#define BC_RELEASED  RANGE_TOL(0.0f, 0.1f)
#define BC_NB        RANGE_TOL(1.2f, 0.1f)
#define BC_LEM_FULL  RANGE_TOL(3.5f, 0.1f)
#define BC_FORMATION RANGE_TOL(2.4f, 0.1f)
#define MR_STD       RANGE_BETWEEN(8.0f, 10.0f)

#define RELEASED_ROWS \
   [CMD_NO_BRAKE]  = { MR_STD, BP_RELEASED, BC_RELEASED, RANGE_UPPER(5), RANGE_UPPER(5) }, \
   [CMD_OVERSPEED] = { MR_STD, BP_RELEASED, BC_RELEASED, RANGE_UPPER(5), RANGE_UPPER(5) }

static const BrakeSpec kBrakeTable[LOCO_TYPE_COUNT][TRAIN_MODE_COUNT][BRAKE_CMD_COUNT] =
{
 /* WAP7 */
 {
  /* LEM */
  {
   RELEASED_ROWS,
   [CMD_NB]  = { MR_STD, BP_NB,       BC_NB,       RANGE_UPPER(8), RANGE_UPPER(15) },
   [CMD_FSB] = { MR_STD, BP_FSB,      BC_LEM_FULL, RANGE_UPPER(8), RANGE_UPPER(20) },
   [CMD_EB]  = { MR_STD, BP_EB,       BC_LEM_FULL, RANGE_UPPER(8), RANGE_UPPER(25) },
   [CMD_LE]  = { MR_STD, BP_RELEASED, BC_LEM_FULL, RANGE_UPPER(8), RANGE_UPPER(25) },
  },
  /* FORMATION */
  {
   RELEASED_ROWS,
   [CMD_NB]  = { MR_STD, BP_NB,       BC_NB,        RANGE_UPPER(9), RANGE_UPPER(15) },
   [CMD_FSB] = { MR_STD, BP_FSB,      BC_FORMATION, RANGE_UPPER(9), RANGE_UPPER(20) },
   [CMD_EB]  = { MR_STD, BP_EB,       BC_FORMATION, RANGE_UPPER(9), RANGE_UPPER(20) },
   [CMD_LE]  = { MR_STD, BP_RELEASED, BC_LEM_FULL,  RANGE_UPPER(9), RANGE_UPPER(20) },
  },
 },
 /* WAG9 */
 {
  /* LEM */
  {
   RELEASED_ROWS,
   [CMD_NB]  = { MR_STD, BP_NB,       BC_NB,       RANGE_UPPER(8), RANGE_UPPER(45)      },
   [CMD_FSB] = { MR_STD, BP_FSB,      BC_LEM_FULL, RANGE_UPPER(8), RANGE_UPPER(60)      },
   [CMD_EB]  = { MR_STD, BP_EB,       BC_LEM_FULL, RANGE_UPPER(8), RANGE_BETWEEN(45,60) },
   [CMD_LE]  = { MR_STD, BP_RELEASED, BC_LEM_FULL, RANGE_UPPER(8), RANGE_BETWEEN(45,60) },
  },
  /* FORMATION */
  {
   RELEASED_ROWS,
   [CMD_NB]  = { MR_STD, BP_NB,       BC_NB,        RANGE_UPPER(18),      RANGE_UPPER(45)      },
   [CMD_FSB] = { MR_STD, BP_FSB,      BC_FORMATION, RANGE_BETWEEN(18,24), RANGE_BETWEEN(45,60) },
   [CMD_EB]  = { MR_STD, BP_EB,       BC_FORMATION, RANGE_BETWEEN(18,24), RANGE_BETWEEN(45,60) },
   [CMD_LE]  = { MR_STD, BP_RELEASED, BC_LEM_FULL,  RANGE_BETWEEN(18,24), RANGE_BETWEEN(45,60) },
  },
 },
 /* WAP5 */
 {
  /* LEM */
  {
   RELEASED_ROWS,
   [CMD_NB]  = { MR_STD, BP_NB,       BC_NB,       RANGE_UPPER(8), RANGE_UPPER(15) },
   [CMD_FSB] = { MR_STD, BP_FSB,      BC_LEM_FULL, RANGE_UPPER(8), RANGE_UPPER(20) },
   [CMD_EB]  = { MR_STD, BP_EB,       BC_LEM_FULL, RANGE_UPPER(8), RANGE_UPPER(20) },
   [CMD_LE]  = { MR_STD, BP_RELEASED, BC_LEM_FULL, RANGE_UPPER(8), RANGE_UPPER(20) },
  },
  /* FORMATION */
  {
   RELEASED_ROWS,
   [CMD_NB]  = { MR_STD, BP_NB,       BC_NB,        RANGE_UPPER(9), RANGE_UPPER(15) },
   [CMD_FSB] = { MR_STD, BP_FSB,      BC_FORMATION, RANGE_UPPER(9), RANGE_UPPER(20) },
   [CMD_EB]  = { MR_STD, BP_EB,       BC_FORMATION, RANGE_UPPER(9), RANGE_UPPER(20) },
   [CMD_LE]  = { MR_STD, BP_RELEASED, BC_LEM_FULL,  RANGE_UPPER(9), RANGE_UPPER(20) },
  },
 },
};

/* ---- Helpers ----------------------------------------------------------------- */
static inline float   range_nominal(Range r)           { return (r.min + r.max) * 0.5f; }
static inline uint8_t range_contains(Range r, float v) { return (uint8_t)((v >= r.min) && (v <= r.max)); }

static inline const BrakeSpec *brake_spec_get(uint8_t loco, uint8_t mode, int cmd)
{
    if (loco >= LOCO_TYPE_COUNT || mode >= TRAIN_MODE_COUNT || cmd < 0 || cmd >= BRAKE_CMD_COUNT) return NULL;
    return &kBrakeTable[loco][mode][cmd];
}

static int brake_cmd_normalise(uint8_t brake_type)
{
    return brake_type < BRAKE_CMD_COUNT ? (int)brake_type : -1;
}

static const Range *spec_range(const BrakeSpec *s, PressureCmd which)
{
    switch (which) {
    case CMD_MR: return &s->mr;
    case CMD_BP: return &s->bp_pressure;
    case CMD_BC: return &s->bc_pressure;
    default:     return NULL;
    }
}

/* The BP target to command: the middle of the range, except a range from 0
 * (EB: BP "below 0.3") is commanded as 0, an empty brake pipe. */
static float bp_target(const BrakeSpec *s)
{
    return s->bp_pressure.min <= 0.0f ? 0.0f : range_nominal(s->bp_pressure);
}

static float target_of(const BrakeSpec *s, PressureCmd which)
{
    switch (which) {
    case CMD_BP: return bp_target(s);
    case CMD_BC: return range_nominal(s->bc_pressure);
    default:     return range_nominal(s->mr);
    }
}

/* ---- Frame codec --------------------------------------------------------------- */
uint8_t biu_kgcm2_to_byte(float kgcm2)
{
    if (!(kgcm2 > 0.0f)) return 0U;                       /* also NaN */
    const float raw = kgcm2 / BIU_KGCM2_PER_BIT + 0.5f;   /* round, do not truncate */
    return raw >= 255.0f ? 255U : (uint8_t)raw;
}

float biu_byte_to_kgcm2(uint8_t raw) { return (float)raw * BIU_KGCM2_PER_BIT; }

void biu_encode_command(const BiuCommand *c, uint8_t out[BIU_FRAME_LEN])
{
    uint8_t d1 = 0U, d2 = 0U;
    if (c->brake_test_request) d1 |= BIU_CMD_D1_BRAKE_TEST_REQ;
    if (c->health)             d1 |= BIU_CMD_D1_HEALTH;
    if (c->bp_charging_cutout) d1 |= BIU_CMD_D1_BP_CHARGING_CUTOUT;
    if (c->biu_isolate)        d1 |= BIU_CMD_D1_BIU_ISOLATE;
    if (c->speed_valid)        d1 |= BIU_CMD_D1_SPEED_VALID;
    if (c->light_engine)       d1 |= BIU_CMD_D1_LIGHT_ENGINE;
    if (c->remote_loco)        d1 |= BIU_CMD_D1_REMOTE_LOCO;
    if (c->dynamic_braking)    d1 |= BIU_CMD_D1_DYNAMIC_BRAKING;
    if (c->fault_code_ack)     d2 |= BIU_CMD_D2_FAULT_CODE_ACK;
    if (c->display_code_ack)   d2 |= BIU_CMD_D2_DISPLAY_CODE_ACK;
    if (c->a9_valid)           d2 |= BIU_CMD_D2_A9_VALID;
    if (c->sa9_valid)          d2 |= BIU_CMD_D2_SA9_VALID;

    out[0] = (uint8_t)(c->speed_cm_per_s & 0xFFU);
    out[1] = (uint8_t)(c->speed_cm_per_s >> 8);
    out[2] = d1;
    out[3] = d2;
    out[4] = biu_kgcm2_to_byte(c->a9_kgcm2);
    out[5] = biu_kgcm2_to_byte(c->sa9_kgcm2);
    out[6] = 0U;
    out[7] = 0U;
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

/* One CAN frame through the UDP gateway: [COB-ID lo][COB-ID hi][3 + n][n data bytes]. */
static void biu_send_frame(uint16_t cob, const uint8_t *data, uint8_t n)
{
    uint8_t buffer[3U + BIU_FRAME_LEN];
    if (n > BIU_FRAME_LEN) n = BIU_FRAME_LEN;
    buffer[0] = (uint8_t)(cob & 0xFFU);
    buffer[1] = (uint8_t)(cob >> 8);
    buffer[2] = (uint8_t)(3U + n);
    if (n > 0U) memcpy(buffer + 3, data, n);

    ipAddress biu_sim_addr;
    biu_sim_addr.ip_addr0 = BIU_SIM_IP0;
    biu_sim_addr.ip_addr1 = BIU_SIM_IP1;
    biu_sim_addr.ip_addr2 = BIU_SIM_IP2;
    biu_sim_addr.ip_addr3 = BIU_SIM_IP3;
    ip_addr_t biu_sim_ip_address = set_pc_ipAddr(biu_sim_addr);
    UDP_Send((const char *)buffer, &biu_sim_ip_address, BIU_SIM_PORT, (uint16_t)(3U + n));
}

/* ---- Module state ------------------------------------------------------------- */
static BiuStatus s_status;

/* Network management (Annexure-1 2.1, 4): KAVACH boots, says so, waits in
 * pre-operational for the BIU's "start remote node", and sends data frames
 * only while operational. Its heartbeat goes out every 500 ms throughout. */
static uint8_t  s_nmt_state = BIU_NMT_BOOT_UP;
static uint8_t  s_nmt_started;
static uint32_t s_heartbeat_due_ms;

/* The command in force and the one before it, for the settling time. */
static uint8_t  s_loco, s_mode;
static int      s_cmd = -1, s_prev_cmd = -1;
static uint32_t s_cmd_since_ms;

/* Fault / display code acknowledge (Annexure-1 3): high for one cycle after a
 * new code arrives. A code is new when it differs from the last one acked, or
 * the BIU has sent 0 since. */
static uint8_t  s_fault_ack_pending, s_display_ack_pending;
static uint16_t s_fault_acked, s_display_acked;

/* Log a refusal once per (loco, mode, brake), not every 250 ms. */
static uint32_t s_last_logged = 0xFFFFFFFFU;
static void log_once(uint32_t key, const char *what, uint8_t loco, uint8_t mode, uint8_t brake)
{
    if (key == s_last_logged) return;
    s_last_logged = key;
    SERIAL_PRINTF("BIU: %s (loco %u, mode %u, brake %u)\n", what, (unsigned)loco, (unsigned)mode, (unsigned)brake);
}

static void note_command(uint8_t loco, uint8_t mode, int cmd, uint32_t now)
{
    if (cmd == s_cmd && loco == s_loco && mode == s_mode) return;
    if (cmd != s_cmd) {
        s_prev_cmd = s_cmd;
        s_cmd = cmd;
    }
    s_loco = loco;
    s_mode = mode;
    s_cmd_since_ms = now;
}

static void send_heartbeat(uint8_t state)
{
    biu_send_frame(BIU_COB_HEARTBEAT_KAVACH, &state, 1U);
}

/* Boot-up: the boot-up message, then pre-operational until the BIU starts us. */
static void nmt_boot(uint32_t now)
{
    s_nmt_state = BIU_NMT_BOOT_UP;
    send_heartbeat(BIU_NMT_BOOT_UP);
    s_nmt_state = BIU_NMT_PRE_OPERATIONAL;
    s_heartbeat_due_ms = now + BIU_HEARTBEAT_PERIOD_MS;
    s_fault_ack_pending = s_display_ack_pending = 0U;
}

/* ---- Public ------------------------------------------------------------------- */

/**
 * Convert a 4-20 mA loop current to pressure in kg/cm^2.
 *
 * @param current_mA     Measured loop current in milliamps.
 * @param sensor_fs_bar  Sensor full-scale rating (e.g. 10.0 for a 10-bar sensor).
 * @return PressureReading containing converted value, raw input, and status.
 */
PressureReading current_to_pressure(float current_mA, float sensor_fs_bar)
{
    PressureReading r = { .raw_mA = current_mA };

    /* Fault detection - reject readings outside the valid loop range */
    if (current_mA < 3.5f)  { r.status = SENSE_FAULT_OPEN;  return r; }
    if (current_mA > 20.5f) { r.status = SENSE_FAULT_SHORT; return r; }

    float fraction = (current_mA - 4.0f) / 16.0f;   /* 16 mA between 4 and 20 */
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;

    /* Scale to the sensor's range; 1 bar = 1.02 kg/cm2 */
    r.value_kgcm2 = fraction * sensor_fs_bar * 1.02f;
    r.status      = SENSE_OK;
    return r;
}

void biu_link_init(void)
{
    nmt_boot(BIU_NOW_MS());
    s_nmt_started = 1U;
}

void biu_link_tick(void)
{
    const uint32_t now = BIU_NOW_MS();
    if (!s_nmt_started) { biu_link_init(); return; }
    if ((int32_t)(now - s_heartbeat_due_ms) >= 0) {
        send_heartbeat(s_nmt_state);
        s_heartbeat_due_ms = now + BIU_HEARTBEAT_PERIOD_MS;
    }
}

uint8_t biu_nmt_state(void) { return s_nmt_state; }

uint8_t biu_link_up(void)
{
    if (s_status.biu_heartbeat_ms == 0U) return 0U;
    return (uint8_t)((uint32_t)(BIU_NOW_MS() - s_status.biu_heartbeat_ms) <= BIU_HEARTBEAT_TIMEOUT_MS
                     && s_status.biu_nmt_state == BIU_NMT_OPERATIONAL);
}

void send_biu_command(uint8_t loco_type, uint8_t train_mode, uint8_t brake_type)
{
    const uint32_t now = BIU_NOW_MS();
    const int cmd = brake_cmd_normalise(brake_type);
    const BrakeSpec *spec = brake_spec_get(loco_type, train_mode, cmd);
    note_command(loco_type, train_mode, spec ? cmd : -1, now);

    /* Pre-operational or stopped: heartbeat only, no data frames (Annexure-1 4). */
    if (s_nmt_state != BIU_NMT_OPERATIONAL) return;

    BiuCommand c;
    memset(&c, 0, sizeof(c));

    const float cms = BIU_SPEED_TO_CM_PER_S(loco_params.sensor_speed);
    c.speed_cm_per_s = cms <= 0.0f ? 0U : cms >= 65535.0f ? 0xFFFFU : (uint16_t)(cms + 0.5f);
    c.speed_valid    = (uint8_t)(BIU_SPEED_VALID() ? 1U : 0U);

    if (spec != NULL) {
        c.a9_kgcm2  = bp_target(spec);
        c.sa9_kgcm2 = range_nominal(spec->bc_pressure);
        c.a9_valid  = (uint8_t)(c.a9_kgcm2  <= BIU_A9_VALID_MAX_KGCM2);
        c.sa9_valid = (uint8_t)(c.sa9_kgcm2 <= BIU_SA9_VALID_MAX_KGCM2);
        if (!c.a9_valid || !c.sa9_valid)
            log_once(((uint32_t)loco_type << 16) | ((uint32_t)train_mode << 8) | brake_type,
                     "reference pressure above the spec's valid range, sent as not valid",
                     loco_type, train_mode, brake_type);
    } else {
        /* No row for this loco / mode / brake: brake rather than guess, as the
         * BIU itself does when it loses KAVACH (Annexure-1 4 c). An empty brake
         * pipe, said on purpose, and no BC command. */
        c.a9_kgcm2  = 0.0f;
        c.a9_valid  = 1U;
        c.sa9_valid = 0U;
        log_once(0x80000000U | ((uint32_t)loco_type << 16) | ((uint32_t)train_mode << 8) | brake_type,
                 "no brake table row: commanding BP 0", loco_type, train_mode, brake_type);
    }

    c.light_engine       = (uint8_t)(loco_params.train_type == LIGHT_ENGINE_TYPE);
    c.brake_test_request = (uint8_t)(loco_params.mbt == 1);
    c.biu_isolate        = (uint8_t)(loco_params.dmi_context_values.biu_isolated == 1);
    c.health             = (uint8_t)(loco_params.loco_health_status ? 1U : 0U);
    c.dynamic_braking    = (uint8_t)(BIU_DYNAMIC_BRAKING() ? 1U : 0U);
    /* Set by DPCS / DPWCS only (A1 2.2.2 Discrete Byte1 bits 2, 6): KAVACH sends 0. */
    c.bp_charging_cutout = 0U;
    c.remote_loco        = 0U;

    c.fault_code_ack   = s_fault_ack_pending;
    c.display_code_ack = s_display_ack_pending;
    s_fault_ack_pending = 0U;
    s_display_ack_pending = 0U;

    uint8_t data[BIU_FRAME_LEN];
    biu_encode_command(&c, data);
    biu_send_frame(BIU_COB_COMMAND, data, BIU_FRAME_LEN);
}

uint8_t process_biu_reply(const uint8_t *buffer, uint16_t len)
{
    if (buffer == NULL || len < 3U) return 0U;
    const uint16_t cob = le16(buffer);
    const uint8_t total = buffer[2];
    if (total < 3U || total > 3U + BIU_FRAME_LEN || len < total) return 0U;
    const uint8_t n = (uint8_t)(total - 3U);
    const uint8_t *d = buffer + 3;
    uint32_t now = BIU_NOW_MS();
    if (now == 0U) now = 1U;   /* 0 means "never" in rx_ms */

    switch (cob) {
    case BIU_COB_NMT:
        /* [command specifier][node: ours, or 0 for all] */
        if (n < 2U || (d[1] != KAVACH_NODE_ID && d[1] != 0U)) return 0U;
        switch (d[0]) {
        case BIU_NMT_CS_START:         s_nmt_state = BIU_NMT_OPERATIONAL;     break;
        case BIU_NMT_CS_STOP:          s_nmt_state = BIU_NMT_STOPPED;         break;
        case BIU_NMT_CS_PRE_OP:        s_nmt_state = BIU_NMT_PRE_OPERATIONAL; break;
        case BIU_NMT_CS_RESET_NODE:
        case BIU_NMT_CS_RESET_COMM:    nmt_boot(now);                         break;
        default:                       return 0U;
        }
        return 1U;
    case BIU_COB_HEARTBEAT_BIU:
        if (n < 1U) return 0U;
        s_status.biu_nmt_state = d[0];
        s_status.biu_heartbeat_ms = now;
        return 1U;
    case BIU_COB_STATUS_PRESSURES:
        if (n < 8U) return 0U;
        for (int i = 0; i < BIU_P_COUNT; ++i) {
            s_status.pressure[i].kgcm2 = biu_byte_to_kgcm2(d[i]);
            s_status.pressure[i].valid = (uint8_t)((d[7] >> i) & 1U);
        }
        s_status.rx_ms[0] = now;
        return 1U;
    case BIU_COB_STATUS_CODES:
        if (n < 6U) return 0U;
        s_status.isolation_counter = le16(d);
        s_status.fault_code        = le16(d + 2);
        s_status.display_code      = le16(d + 4);
        if (s_status.fault_code == 0U)                         s_fault_acked = 0U;
        else if (s_status.fault_code != s_fault_acked)         { s_fault_acked = s_status.fault_code; s_fault_ack_pending = 1U; }
        if (s_status.display_code == 0U)                       s_display_acked = 0U;
        else if (s_status.display_code != s_display_acked)     { s_display_acked = s_status.display_code; s_display_ack_pending = 1U; }
        s_status.rx_ms[1] = now;
        return 1U;
    case BIU_COB_STATUS_DISCRETES:
        if (n < 3U) return 0U;
        s_status.discrete[0] = d[0];
        s_status.discrete[1] = d[1];
        s_status.discrete[2] = d[2];
        s_status.rx_ms[2] = now;
        return 1U;
    default:
        return 0U;
    }
}

const BiuStatus *biu_status_get(void) { return &s_status; }

uint8_t validate_brake_pressure(uint8_t loco_type, uint8_t train_mode, uint8_t brake_type,
                                PressureCmd which, float value)
{
    const BrakeSpec *spec = brake_spec_get(loco_type, train_mode, brake_cmd_normalise(brake_type));
    const Range *r = spec ? spec_range(spec, which) : NULL;
    if (r == NULL) {
        SERIAL_PRINTF("BIU: no range for loco %u mode %u brake %u pressure %d\n",
                      (unsigned)loco_type, (unsigned)train_mode, (unsigned)brake_type, (int)which);
        return 0U;
    }
    return range_contains(*r, value);
}

/*
 * A pressure takes time to settle after a command changes, so it is judged
 * only after it could have arrived, pressure by pressure: moving toward more
 * braking (BP down, BC up) takes the new command's apply time; moving back
 * takes the previous command's release time. LE -> NB, say, waits NB's apply
 * time for BP (down to 4.4) and LE's release time for BC (down to 1.2).
 */
BrakeCheck check_brake_malfunction(PressureCmd which, float value)
{
    const BrakeSpec *spec = brake_spec_get(s_loco, s_mode, s_cmd);
    const Range *r = spec ? spec_range(spec, which) : NULL;
    if (r == NULL) return BRAKE_CHECK_NO_SPEC;

    float settle_s = spec->t_apply.max;
    const BrakeSpec *prev = brake_spec_get(s_loco, s_mode, s_prev_cmd);
    if (prev != NULL && which != CMD_MR) {
        const float from = target_of(prev, which), to = target_of(spec, which);
        const int releasing = which == CMD_BP ? (to > from) : (to < from);
        if (releasing) settle_s = prev->t_release.max;
    }
    if ((uint32_t)(BIU_NOW_MS() - s_cmd_since_ms) < (uint32_t)(settle_s * 1000.0f))
        return BRAKE_CHECK_WAIT;

    if (range_contains(*r, value)) return BRAKE_CHECK_OK;
    SERIAL_PRINTF("BIU: brake %d pressure %d at %.2f, expected %.2f-%.2f\n",
                  s_cmd, (int)which, (double)value, (double)r->min, (double)r->max);
    return BRAKE_CHECK_MALFUNCTION;
}

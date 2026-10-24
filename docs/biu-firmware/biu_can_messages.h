#ifndef BIU_CAN_MESSAGES_H
#define BIU_CAN_MESSAGES_H

/*
 * KAVACH <-> BIU CAN messages, RDSO MP.0.01.00.31 Rev.05 Annexure-1.
 *
 * Replaces the STRUCT_LKAVACH_BIU_* / STRUCT_BIU_LKAVACH_* bit-field structs.
 * Those were memcpy'd to and from the wire, which depends on the compiler's
 * bit-field order and struct padding: the feedback structs had no PACK, so
 * each one-byte discrete struct took 4 bytes and the feedback message was
 * 28 bytes instead of the 19 on the wire. The frames are now built and read
 * byte by byte with explicit shifts (biu_encode_command / biu_decode_frame in
 * brake_pressure_interface_user.c), so the layout is the spec's on any
 * compiler and any endianness.
 *
 * CAN carries its own CRC: the protocol defines no application CRC, so the
 * old LKAVACH_BIU_COMMAND_MSG { data; CRC crc; } wrapper is gone.
 */

#include <stdint.h>

/* ---- Node IDs and COB-IDs (Annexure-1 1.2, 2.2) ------------------------------ */
#define BIU_NODE_ID_KAVACH        0x10U   /* BIU Node 1: the BIU's KAVACH side */
#define KAVACH_NODE_ID            0x20U

#define BIU_COB_COMMAND           (0x180U + BIU_NODE_ID_KAVACH)  /* KAVACH -> BIU, 0x190 */
#define BIU_COB_STATUS_PRESSURES  (0x200U + KAVACH_NODE_ID)      /* BIU -> KAVACH, 0x220 */
#define BIU_COB_STATUS_CODES      (0x300U + KAVACH_NODE_ID)      /* BIU -> KAVACH, 0x320 */
#define BIU_COB_STATUS_DISCRETES  (0x400U + KAVACH_NODE_ID)      /* BIU -> KAVACH, 0x420 */
#define BIU_COB_HEARTBEAT_KAVACH  (0x700U + KAVACH_NODE_ID)      /* KAVACH heartbeat, 500 ms, 0x720 */
#define BIU_COB_HEARTBEAT_BIU     (0x700U + BIU_NODE_ID_KAVACH)  /* BIU heartbeat, 500 ms, 0x710 */
#define BIU_COB_NMT               0x000U                         /* BIU -> nodes: [command][node id] */

/* NMT command specifiers (sent by the BIU, the network manager) */
#define BIU_NMT_CS_START          0x01U
#define BIU_NMT_CS_STOP           0x02U
#define BIU_NMT_CS_PRE_OP         0x80U
#define BIU_NMT_CS_RESET_NODE     0x81U
#define BIU_NMT_CS_RESET_COMM     0x82U

/* Heartbeat states (byte 0 of a heartbeat) */
#define BIU_NMT_BOOT_UP           0x00U
#define BIU_NMT_STOPPED           0x04U
#define BIU_NMT_OPERATIONAL       0x05U
#define BIU_NMT_PRE_OPERATIONAL   0x7FU

#define BIU_FRAME_LEN             8U

/* Every pressure byte, both directions: 0.05 kg/cm2 per bit. */
#define BIU_KGCM2_PER_BIT         0.05f

/* A9 / SA9 control reference pressure is valid only up to these (2.2.2 Discrete Byte2). */
#define BIU_A9_VALID_MAX_KGCM2    6.0f
#define BIU_SA9_VALID_MAX_KGCM2   4.0f

/* ---- KAVACH -> BIU command, COB-ID 0x190 ----------------------------------------
 *  byte 0-1  locomotive speed, 0.01 m/s, lower byte first
 *  byte 2    discrete byte 1 (bits below)
 *  byte 3    discrete byte 2 (bits below)
 *  byte 4    A9 control reference pressure (BP target), 0.05 kg/cm2
 *  byte 5    SA9 control reference pressure (BC target), 0.05 kg/cm2
 *  byte 6-7  spare, 0
 */
#define BIU_CMD_D1_BRAKE_TEST_REQ     (1U << 0)
#define BIU_CMD_D1_HEALTH             (1U << 1)
#define BIU_CMD_D1_BP_CHARGING_CUTOUT (1U << 2)
#define BIU_CMD_D1_BIU_ISOLATE        (1U << 3)
#define BIU_CMD_D1_SPEED_VALID        (1U << 4)
#define BIU_CMD_D1_LIGHT_ENGINE       (1U << 5)
#define BIU_CMD_D1_REMOTE_LOCO        (1U << 6)
#define BIU_CMD_D1_DYNAMIC_BRAKING    (1U << 7)

#define BIU_CMD_D2_FAULT_CODE_ACK     (1U << 0)
#define BIU_CMD_D2_DISPLAY_CODE_ACK   (1U << 1)
#define BIU_CMD_D2_A9_VALID           (1U << 2)
#define BIU_CMD_D2_SA9_VALID          (1U << 3)

typedef struct {
    uint16_t speed_cm_per_s;         /* 0.01 m/s */
    uint8_t  brake_test_request;
    uint8_t  health;
    uint8_t  bp_charging_cutout;
    uint8_t  biu_isolate;
    uint8_t  speed_valid;
    uint8_t  light_engine;
    uint8_t  remote_loco;
    uint8_t  dynamic_braking;
    uint8_t  fault_code_ack;
    uint8_t  display_code_ack;
    float    a9_kgcm2;               /* BP target */
    uint8_t  a9_valid;
    float    sa9_kgcm2;              /* BC target */
    uint8_t  sa9_valid;
} BiuCommand;

/* ---- BIU -> KAVACH status, three frames ------------------------------------------
 *  0x220: byte 0-6 BP, BC, MR, A9 ref, SA9 ref, air flow, feed pipe (0.05 kg/cm2);
 *         byte 7 validity (bit n = byte n valid)
 *  0x320: byte 0-1 isolation counter, 2-3 fault code, 4-5 display-only
 *         message code (all lower byte first); 6-7 spare
 *  0x420: byte 0-2 discrete bytes 1-3 (bits below); 3-7 spare
 */
typedef enum {
    BIU_P_BP = 0,
    BIU_P_BC,
    BIU_P_MR,
    BIU_P_A9_REF,
    BIU_P_SA9_REF,
    BIU_P_AIR_FLOW,
    BIU_P_FEEDPIPE,
    BIU_P_COUNT
} BiuPressureId;

#define BIU_ST_D1_HEALTHY                 (1U << 0)
#define BIU_ST_D1_CMD_OVERRIDE_BY_PILOT   (1U << 1)
#define BIU_ST_D1_PILOT_OVERRIDE_BY_BIU   (1U << 2)
#define BIU_ST_D1_MANUAL_ISOLATE          (1U << 3)
#define BIU_ST_D1_BP_CHARGING_CUTOUT      (1U << 4)   /* 1 = cut out */
#define BIU_ST_D1_EMERGENCY_VALVE         (1U << 5)   /* 1 = cut in */
#define BIU_ST_D1_POWER_CUTOFF_RELAY      (1U << 6)

#define BIU_ST_D2_PANEL_TEST_SUCCESS      (1U << 0)
#define BIU_ST_D2_PANEL_TEST_FAIL         (1U << 1)
#define BIU_ST_D2_PANEL_TEST_PROGRESS     (1U << 2)

#define BIU_ST_D3_BP_CUTOUT_VALVE_ON      (1U << 0)
#define BIU_ST_D3_BP_SELECT_VALVE_ON      (1U << 1)
#define BIU_ST_D3_BC_SELECT_VALVE_ON      (1U << 2)
#define BIU_ST_D3_EMERGENCY_VALVE_ON      (1U << 3)
#define BIU_ST_D3_BP_CUTOUT_VALVE_OK      (1U << 4)
#define BIU_ST_D3_BP_SELECT_VALVE_OK      (1U << 5)
#define BIU_ST_D3_BC_SELECT_VALVE_OK      (1U << 6)
#define BIU_ST_D3_EMERGENCY_VALVE_OK      (1U << 7)

typedef struct {
    float   kgcm2;
    uint8_t valid;                  /* use kgcm2 only when 1 (Annexure-1 4) */
} BiuPressure;

typedef struct {
    BiuPressure pressure[BIU_P_COUNT];
    uint16_t    isolation_counter;
    uint16_t    fault_code;          /* 0x1001.. (Annexure-1 5), 0 = none */
    uint16_t    display_code;        /* 0x2001.. (Annexure-1 6), 0 = none */
    uint8_t     discrete[3];         /* BIU_ST_D1_* / D2_* / D3_* */
    uint32_t    rx_ms[3];            /* when 0x220 / 0x320 / 0x420 last arrived; 0 = never */
    uint8_t     biu_nmt_state;       /* from the BIU's heartbeat (BIU_NMT_*) */
    uint32_t    biu_heartbeat_ms;    /* when it last arrived; 0 = never */
} BiuStatus;

#endif /* BIU_CAN_MESSAGES_H */

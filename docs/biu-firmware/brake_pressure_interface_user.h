#ifndef BRAKE_PRESSURE_INTERFACE_USER_H
#define BRAKE_PRESSURE_INTERFACE_USER_H

#include <stdint.h>
#include "biu_can_messages.h"

#define RANGE_TOL(nom, t)   { (float)((nom) - (t)), (float)((nom) + (t)) }
#define RANGE_UPPER(m)      { 0.0f, (float)(m) }
#define RANGE_BETWEEN(a, b) { (float)(a), (float)(b) }

#define LOCO_TYPE_COUNT     3
#define TRAIN_MODE_COUNT    2
#define BRAKE_CMD_COUNT     6

typedef enum { LOCO_WAP7 = 0, LOCO_WAG9, LOCO_WAP5 } LocoType;
typedef enum { TRAIN_MODE_LEM = 0, TRAIN_MODE_FORMATION } Mode;
/* Rows of kBrakeTable: the VCC's brake types (DMI brake type 0-5). */
typedef enum { CMD_NO_BRAKE, CMD_OVERSPEED, CMD_NB, CMD_FSB, CMD_EB, CMD_LE } Command;
/* Which pressure a check is about. */
typedef enum { CMD_MR, CMD_BP, CMD_BC } PressureCmd;

typedef struct
{
    float min;
    float max;
} Range;

/* Everything needed to validate any checkpoint for a (loco, mode, command). */
typedef struct {
    Range mr;            /* Main Reservoir baseline                          */
    Range bp_pressure;   /* BP target after this command                     */
    Range bc_pressure;   /* BC target after this command                     */
    Range t_apply;       /* s: to reach this command's pressures from weaker  */
    Range t_release;     /* s: to leave them for a weaker command             */
} BrakeSpec;

/* Return type: carries value, original input, and validity status */
typedef enum {
    SENSE_OK,
    SENSE_FAULT_OPEN,    /* < 3.5 mA - broken wire, sensor unpowered */
    SENSE_FAULT_SHORT    /* > 20.5 mA - shorted loop, transmitter failed */
} SenseStatus;

typedef struct {
    float       value_kgcm2;   /* converted pressure (valid only if status == SENSE_OK) */
    float       raw_mA;        /* original current input (always set, useful for logs) */
    SenseStatus status;        /* validity of the reading */
} PressureReading;

typedef enum {
    BRAKE_CHECK_WAIT = 0,       /* the pressure may still be moving: not judged yet  */
    BRAKE_CHECK_OK,             /* in the table's range                               */
    BRAKE_CHECK_MALFUNCTION,    /* settled time passed and out of range               */
    BRAKE_CHECK_NO_SPEC         /* no table row (unknown loco / mode / command, or no
                                   command sent yet): treat as a malfunction          */
} BrakeCheck;

/* ---- Build hooks ---------------------------------------------------------------
 * BIU_SPEED_VALID() must be defined: 1 while the speed sensor is healthy, e.g.
 *   #define BIU_SPEED_VALID()  (loco_params.speed_sensor_healthy == 1)
 * BIU_SPEED_TO_CM_PER_S(v) defaults to v * 100: loco_params.sensor_speed in m/s,
 *   0.01 m/s on the wire (Annexure-1 2.2.2).
 * BIU_DYNAMIC_BRAKING() defaults to 0 (no dynamic-braking input known).
 * BIU_NOW_MS() defaults to the TI DPL clock in milliseconds.
 * BIU_SIM_IP0..3 / BIU_SIM_PORT: where the CAN frames go over UDP, the gateway
 *   or the DLConsole BIU simulator (default 192.168.25.126:51234).
 * BIU_HEARTBEAT_PERIOD_MS (500) / BIU_HEARTBEAT_TIMEOUT_MS (1500).
 */

/* Network management (Annexure-1 2.1, 4). biu_link_init once after power-on:
 * sends the boot-up message and waits pre-operational. biu_link_tick at least
 * every 250 ms: the heartbeat (0x720, our state) every 500 ms. The BIU starts
 * us with NMT "start remote node"; until then send_biu_command sends nothing. */
void    biu_link_init(void);
void    biu_link_tick(void);
uint8_t biu_nmt_state(void);     /* ours: BIU_NMT_PRE_OPERATIONAL, _OPERATIONAL, _STOPPED */
uint8_t biu_link_up(void);       /* the BIU's heartbeat is fresh and says operational */

/* Send the command frame for this brake. Call it every 250 ms (Annexure-1 4),
 * not only on a change: the fault / display acks ride on it.
 * brake_type: a Command (the VCC's brake type 0-5). */
void send_biu_command(uint8_t loco_type, uint8_t train_mode, uint8_t brake_type);

/* Read one gateway frame from the BIU: [COB-ID lo][COB-ID hi][3 + n][n data bytes].
 * Takes the NMT command, the BIU's heartbeat and its three status frames.
 * Returns 1 if it took the frame. */
uint8_t process_biu_reply(const uint8_t *buffer, uint16_t len);

/* The BIU's last status (pressures with validity, codes, discretes). */
const BiuStatus *biu_status_get(void);

PressureReading current_to_pressure(float current_mA, float sensor_fs_bar);

/* Is `value` inside the table's range for this pressure under this command?
 * 0 when there is no row. Stateless: no settling time. */
uint8_t validate_brake_pressure(uint8_t loco_type, uint8_t train_mode, uint8_t brake_type,
                                PressureCmd which, float value);

/* Judge a measured pressure against the command last sent by send_biu_command,
 * once it has had time to settle: the command's apply time when it is at least
 * as strong as the one before, the previous command's release time when it is
 * weaker (EB -> no brake waits for EB's t_release, not no-brake's t_apply). */
BrakeCheck check_brake_malfunction(PressureCmd which, float value);

/* Frame codec, exposed for tests and the DLConsole BIU simulator. */
void    biu_encode_command(const BiuCommand *c, uint8_t out[BIU_FRAME_LEN]);
uint8_t biu_kgcm2_to_byte(float kgcm2);
float   biu_byte_to_kgcm2(uint8_t raw);

#endif

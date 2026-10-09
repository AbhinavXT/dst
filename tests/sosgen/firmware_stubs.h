#ifndef FIRMWARE_STUBS_H
#define FIRMWARE_STUBS_H

/* =============================================================================
 *  The least of LKAVACH v1.2.9 that sos_log_impl.c needs, so the logging code
 *  from SOS_handoff/01_SOS_LOGGING_PACKETS.md compiles and runs on a PC.
 *  Field names and types are the firmware's (loco_manager.h, idd_struct.h,
 *  sos_table_manager.h + the README's new fields); values are scripted by
 *  sosgen.c. Nothing here is firmware logic.
 * ============================================================================= */

#include <math.h>
#include <stdint.h>
#include <string.h>

#define MAX_SOS_SOURCE_LOCOS 8
#define MAX_SOS_STATIONS     2

typedef enum { UNDEFINED_DIR = 0, NOMINAL_DIR = 1, REVERSE_DIR = 2 } MOVEMENT_DIRECTION_E;
typedef enum { STATION_SECTION, ABSOLUTE_BLOCK_SECTION, AUTO_BLOCK_SECTION, RESERVED_SECTION } SECT_TYPE_E;
typedef enum {
    LOG_PKT_SOS, LOG_PKT_SOS_SRC, LOG_PKT_SOS_EV, LOG_PKT_TYPE_COUNT
} LOG_PKT_TYPE;

typedef uint16_t STATION_ID;

typedef struct {
    unsigned sos_self_loco_manual : 1;
    unsigned sos_self_loco_stopped_in_block_section : 1;
    unsigned sos_self_loco_train_parted : 1;
    unsigned sos_other_loco_manual : 1;
    unsigned sos_other_loco_stopped_in_block_section : 1;
    unsigned sos_other_loco_train_parted : 1;
    unsigned sos_station_all_locos : 1;
    unsigned sos_station_this_loco : 1;
} STRUCT_DMI_CONTEXT_VALUES;

typedef struct {
    unsigned headon_collision_with_loco : 1;
    unsigned rearend_collision_with_loco : 1;
    unsigned brake_applied_station_general_sos : 1;
    unsigned ack_block_stop_sos_generate : 1;
} STRUCT_ALARM_CODE;

typedef struct {
    uint32_t loco_id;
    uint8_t  movement_dir;
    double   abs_location;
    uint16_t tin;
    uint8_t  loco_curr_mode;
    uint16_t train_length;
    uint16_t rfid_tag_id;
    int32_t  ma;
    uint8_t  emergency_status;
    STATION_ID station_id;
    float    sensor_speed;
    uint8_t  unusual_sos_acknowledged;
    STRUCT_ALARM_CODE dmi_alarm_codes;
    STRUCT_DMI_CONTEXT_VALUES dmi_context_values;
    uint8_t  is_access_sos_recvd, is_dest_loco_sos_recvd, is_add_em_sos_recvd,
             is_unusual_stop_recvd, is_train_parted_recvd, is_rear_end_collision_recvd,
             is_head_on_collision_recvd, is_stn_head_on_collision_recvd,
             is_stn_rear_end_collision_recvd, is_stn_shuntig_sos_recvd;
    uint8_t  unusual_stoppage_sos_alarm;
    double   sos_distance;
    double   collision_distance;
    uint32_t collision_loco_id;
    uint16_t sos_station_id;
} LOCO_INIT_PARAMS;

typedef struct {
    uint16_t sos_trigger_distance;
    uint16_t sos_cancellation_distance;
    uint16_t collision_trigger_distance;
} LOCO_INFO;

extern LOCO_INIT_PARAMS loco_params;
extern LOCO_INFO        loco_info;

typedef struct {
    uint8_t sect_type_in_nom_dir;
    uint8_t sect_type_in_rev_dir;
} COMMON_RFID_TAG;

typedef enum {
    THREAT_NONE, THREAT_HEAD_ON, THREAT_REAR_END, THREAT_UNUSUAL_STOP, THREAT_MANUAL_SOS, THREAT_TRAIN_PARTED
} SOS_ThreatType_t;

typedef struct {
    uint8_t   in_use;
    uint32_t  source_loco_id;
    uint32_t  last_update_ms;
    int32_t   abs_loc;
    uint8_t   movement_dir;
    uint16_t  tin;
    uint16_t  train_length;
    double    distance;
    uint8_t   is_access_sos_recvd;
    uint8_t   is_unusual_stop_recvd;
    uint8_t   is_head_on_collision_recvd;
    uint8_t   is_rear_end_collision_recvd;
    uint8_t   is_train_parted_recvd;
    double    sos_distance;
    double    collision_distance;
    /* README 01 4.3 */
    int32_t   raw_abs_loc;
    uint8_t   raw_dir;
    uint8_t   last_emergency_sts;
    uint8_t   other_mode;
    uint16_t  other_speed;
    uint32_t  last_frame_no;
    uint16_t  last_rfid_id;
    uint16_t  approaching_station_id;
} SOS_SourceRecord_t;

typedef struct {
    uint8_t   in_use;
    uint16_t  station_id;
    uint32_t  last_update_ms;
    uint32_t  station_abs_loc;
    double    distance;
    uint8_t   is_add_em_sos_recvd;
    double    sos_distance;
    /* README 01 4.3 */
    uint8_t   last_gen_sos_call;
    uint32_t  last_frame_no;
} SOS_StationRecord_t;

/* Firmware functions the logging code calls; sosgen.c answers them. */
uint8_t  GetRearElement(COMMON_RFID_TAG *last_element);
uint32_t ClockP_getTicks(void);
uint8_t  TrainStandstill(void);
uint8_t  CheckifLocoEnteredInBlockSection(void);
void     UDP_SendCapturePkt(LOG_PKT_TYPE type, const uint8_t *buff, uint16_t len);
uint8_t  IsAdjacentLineInfringing(uint16_t other_loco_tin);
uint8_t  IsSOSTargetDistanceRemove(uint32_t other_abs_loc, uint8_t other_dir);
uint8_t  IsManualSOSToBeProcessed(uint32_t other_abs_loc, uint8_t other_dir, uint16_t other_loco_train_length);
uint8_t  IsUnusualStopOfOtherLocoToBeProcessed(uint32_t other_abs_loc, uint8_t other_dir, uint16_t other_tin, uint16_t other_loco_train_length);
uint8_t  SOS_IsClosestActiveThreat(const SOS_SourceRecord_t *slot, SOS_ThreatType_t threat);
uint8_t  SOS_IsClosestActiveStation(const SOS_StationRecord_t *slot);

/* The tables live in sos_table_manager.c, where the logging block is pasted. */
static SOS_SourceRecord_t  g_sos_sources[MAX_SOS_SOURCE_LOCOS];
static SOS_StationRecord_t g_sos_stations[MAX_SOS_STATIONS];

#endif

#ifndef SOS_LOG_H
#define SOS_LOG_H

#include <stdint.h>

/* SoS logging for DLConsole.
 * Three capture lines, all little-endian, sent with UDP_SendCapturePkt():
 *   @sos     one snapshot of the whole SoS state (own loco, aggregate, stations)
 *   @sossrc  one line per in-use source-loco slot, same snap_id as its @sos
 *   @sosev   one line per decision / transition
 * Layouts are in 01_SOS_LOGGING_PACKETS.md. Bump SOS_LOG_VERSION on any change. */

#define SOS_LOG_VERSION         1U
#define SOS_LOG_MAX_BYTES       160U    /* well under the ~440 B a capture line holds */

/* Why a snapshot was sent (@sos byte 1). */
#define SOS_SNAP_PERIODIC       0U
#define SOS_SNAP_AFTER_ARP      1U
#define SOS_SNAP_AFTER_AEP      2U
#define SOS_SNAP_AFTER_DEST_SOS 3U

/* Threat / source codes used in @sos agg_threat and @sosev aux1.
 * 0..5 are SOS_ThreatType_t unchanged. */
#define SOS_LOG_THREAT_NONE             0U
#define SOS_LOG_THREAT_HEAD_ON          1U
#define SOS_LOG_THREAT_REAR_END         2U
#define SOS_LOG_THREAT_UNUSUAL_STOP     3U
#define SOS_LOG_THREAT_MANUAL_SOS       4U
#define SOS_LOG_THREAT_TRAIN_PARTED     5U
#define SOS_LOG_THREAT_STATION_GENERAL  6U   /* additional emergency (AEP) */
#define SOS_LOG_THREAT_DEST_GENERAL     7U   /* SLRP DEST_LOCO_SOS = station general */
#define SOS_LOG_THREAT_STN_HEAD_ON      8U
#define SOS_LOG_THREAT_STN_REAR_END     9U
#define SOS_LOG_THREAT_SPAD             10U
#define SOS_LOG_THREAT_SHUNTING         11U

/* @sosev event codes (byte 9). */
#define SOS_EV_SRC_ADDED            1U
#define SOS_EV_SRC_EVICTED          2U   /* id = evicted loco */
#define SOS_EV_SRC_DROPPED_FULL     3U   /* id = loco that was not tracked */
#define SOS_EV_SRC_QUIET_RELEASED   4U
#define SOS_EV_SRC_TIMEOUT          5U   /* aux1 = threat bit mask that timed out */
#define SOS_EV_ARP_REJECTED         6U   /* aux1 = SOS_REJ_* */
#define SOS_EV_THREAT_START         7U   /* aux1 = threat */
#define SOS_EV_THREAT_END           8U   /* aux1 = threat, aux2 = SOS_END_* */
#define SOS_EV_TARGET_REMOVED       9U   /* passed the source: sos_distance -> 0 */
#define SOS_EV_BRAKE_APPLIED        10U  /* aux1 = threat, id = LB_* code */
#define SOS_EV_BRAKE_SKIPPED        11U  /* aux1 = threat, aux2 = SOS_SKIP_* */
#define SOS_EV_STN_ADDED            12U
#define SOS_EV_STN_EVICTED          13U
#define SOS_EV_STN_DROPPED_FULL     14U
#define SOS_EV_STN_QUIET_RELEASED   15U
#define SOS_EV_STN_SOS_START        16U
#define SOS_EV_STN_SOS_END          17U  /* aux2 = SOS_END_* */
#define SOS_EV_DEST_LOCO_SOS        18U  /* aux1 = sos_type from SLRP, id = station */
#define SOS_EV_TABLE_RESET          19U  /* aux1 = SOS_RESET_* */
#define SOS_EV_SELF_UNUSUAL_ALARM   20U
#define SOS_EV_SELF_UNUSUAL_END     21U
#define SOS_EV_SELF_UNUSUAL_ACK     22U

#define SOS_REJ_SAME_LOCO           1U
#define SOS_REJ_UNDEFINED_DIR       2U

#define SOS_END_STATUS_CLEARED      1U   /* source stopped sending that status */
#define SOS_END_NOT_PROCESSED       2U   /* range / direction / adjacency check now fails */
#define SOS_END_GEOMETRY            3U   /* collision condition no longer true */
#define SOS_END_TIMEOUT             4U
#define SOS_END_OUT_OF_RANGE        5U   /* station */
#define SOS_END_CANCELLED           6U   /* station gen_sos_call = 0 */

#define SOS_SKIP_STANDSTILL         1U
#define SOS_SKIP_NOT_CLOSEST        2U

#define SOS_RESET_UNDEFINED_DIR     1U
#define SOS_RESET_NON_LEADING       2U
#define SOS_RESET_ISOLATION         3U

void    SOS_LogSnapshot(uint8_t why);
void    SOS_LogEvent(uint8_t event, uint8_t aux1, uint8_t aux2, uint32_t id, double distance_m);
void    SOS_LogNoteDestSos(uint8_t sos_type);
void    SOS_LogNoteAggregate(uint8_t threat, uint8_t station_won, uint16_t station_id);
uint8_t SOS_AdjacencyInfoSource(void);

#endif

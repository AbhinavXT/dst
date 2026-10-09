/* =============================================================================
 *  sosgen -- writes SYNTHETIC @sos / @sossrc / @sosev captures.
 *
 *  WHY
 *    No LKAVACH build logs SoS yet. SOS_handoff/01_SOS_LOGGING_PACKETS.md
 *    gives Abhinav the logging code to add; until a capture from a build with
 *    it exists, DLConsole's SoS window is built and tested against frames made
 *    by THAT SAME CODE: sos_log.h and sos_log_impl.c here are the README's,
 *    byte for byte. Change one, change the other (the README is not in this
 *    repository, so no test can hold them together).
 *
 *  WHAT IT PROVES, AND WHAT IT DOES NOT
 *    The layout: every byte comes from the README's writer. NOT the firmware's
 *    decisions: the scenario below scripts what LKAVACH would decide (a
 *    simplified ProcessAccessRequest / SOS_Periodic_Maintenance written here,
 *    not the firmware's code). Bugs #1, #2, #6, #8 and #10 of
 *    02_SOS_POSSIBLE_BUGS.md are scripted in on purpose, the way the code
 *    reads, so the views have something to show.
 *
 *  THE SCENARIO (2026-10-09, 10:00:00 + t seconds; loco 1 logs, loco 2 too)
 *    loco 1  own loco, TIN 101, nominal, 20 m/s from 10 000 m; stops 150-169
 *    loco 2  TIN 102 (adjacent), standing at 11 010 m; manual SoS t 9..112
 *    loco 3  TIN 103 (adjacent), standing at 13 500 m, reverse (ARP said
 *            nominal at 13 480: an adjustment tag in between); unusual stop
 *    loco 4  TIN 101, reverse, closing from 14 400 m at 15 m/s: head-on
 *            under 1000 m; takes TIN 104 at t 158
 *    loco 5  TIN 101, nominal, front at 15 000 m, 15 m/s, 500 m long:
 *            rear-end; TIN 105 at t 225
 *    loco 6  TIN 106, 4 km behind: train parted t 215..235
 *    stn 4501 at 16 000 m: additional emergency (general SoS) t 228..240;
 *            DEST_LOCO_SOS 8 on SLRPs t 245..249, then 0
 *    t 270   loco 1 goes Non-Leading: the table is reset
 *
 *  Trigger distances are the field's (sos 3000 m, collision 1000 m, cancel
 *  500 m), and each log starts with loco 1's REAL @linfo frame of
 *  2026-10-08 (replay/2026-10-08, re-tagged and re-timed), so the views
 *  read them from the log the way they will from a real one.
 *
 *  Build and run: ./make_fixture.sh  (writes schema/fixtures/sos_synthetic_*.log)
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>

#include "firmware_stubs.h"
#include "sos_log.h"

uint16_t access_req_recvd_stn_id = 0U;

#include "sos_log_impl.c"

LOCO_INIT_PARAMS loco_params;
LOCO_INFO        loco_info;

/* ---- the capture writer ---------------------------------------------------- */
static FILE    *g_out;
static unsigned g_seq = 4000U;
static int      g_t   = 0;          /* seconds since 10:00:00 */
static uint32_t g_tick = 0U;        /* ClockP_getTicks */

static const char *const kToken[LOG_PKT_TYPE_COUNT] = { "sos", "sossrc", "sosev" };

void UDP_SendCapturePkt(LOG_PKT_TYPE type, const uint8_t *buff, uint16_t len)
{
    const int hh = 10 + g_t / 3600, mm = (g_t / 60) % 60, ss = g_t % 60;
    fprintf(g_out, "@%s_%u_1 2026-10-09T%02d:%02d:%02d %u", kToken[type],
            (unsigned)loco_params.loco_id, hh, mm, ss, g_seq++);
    for (uint16_t i = 0; i < len; i++) fprintf(g_out, " %02X", buff[i]);
    fputc('\n', g_out);
}

uint32_t ClockP_getTicks(void) { return g_tick; }
uint8_t  TrainStandstill(void) { return loco_params.sensor_speed < 0.01f ? 1U : 0U; }
uint8_t  CheckifLocoEnteredInBlockSection(void) { return 1U; }
uint8_t  GetRearElement(COMMON_RFID_TAG *t)
{
    t->sect_type_in_nom_dir = ABSOLUTE_BLOCK_SECTION;
    t->sect_type_in_rev_dir = ABSOLUTE_BLOCK_SECTION;
    return 1U;
}

/* Adjacency from an RFID tag: lines 102 and 103 are adjacent to 101. */
uint8_t SOS_AdjacencyInfoSource(void) { return 0x01U; }
uint8_t IsAdjacentLineInfringing(uint16_t tin) { return (tin == 102U || tin == 103U) ? 1U : 0U; }

static double gap_to(int32_t other) { return fabs(loco_params.abs_location - (double)other); }

uint8_t IsSOSTargetDistanceRemove(uint32_t other, uint8_t dir)
{
    (void)dir;
    if (!loco_params.dmi_context_values.sos_other_loco_manual) return 0U;
    if (loco_params.movement_dir == NOMINAL_DIR) return loco_params.abs_location > (double)other ? 1U : 0U;
    return loco_params.abs_location < (double)other ? 1U : 0U;
}

uint8_t IsManualSOSToBeProcessed(uint32_t other, uint8_t dir, uint16_t len)
{
    (void)dir; (void)len;
    const double own = loco_params.abs_location;
    const double hold = (double)loco_info.sos_cancellation_distance + loco_params.train_length;
    if (gap_to((int32_t)other) >= loco_info.sos_trigger_distance) return 0U;
    if (own < (double)other) return 1U;
    if (own < (double)other + hold) return loco_params.is_access_sos_recvd ? 1U : 0U;
    return 0U;
}

/* Bug #6 as the firmware reads: no hold once passed. */
uint8_t IsUnusualStopOfOtherLocoToBeProcessed(uint32_t other, uint8_t dir, uint16_t tin, uint16_t len)
{
    (void)dir; (void)len;
    if (tin == loco_params.tin) return 0U;
    if (gap_to((int32_t)other) >= loco_info.sos_trigger_distance) return 0U;
    if (!IsAdjacentLineInfringing(tin)) return 0U;
    return loco_params.abs_location < (double)other ? 1U : 0U;
}

static double threat_dist(const SOS_SourceRecord_t *s, SOS_ThreatType_t t, uint8_t *has)
{
    switch (t) {
    case THREAT_HEAD_ON:      *has = s->is_head_on_collision_recvd;  return s->collision_distance;
    case THREAT_REAR_END:     *has = s->is_rear_end_collision_recvd; return s->collision_distance;
    case THREAT_UNUSUAL_STOP: *has = s->is_unusual_stop_recvd;       return s->sos_distance;
    case THREAT_MANUAL_SOS:   *has = s->is_access_sos_recvd;         return s->sos_distance;
    case THREAT_TRAIN_PARTED: *has = s->is_train_parted_recvd;       return s->sos_distance;
    default:                  *has = 0;                              return 0.0;
    }
}

uint8_t SOS_IsClosestActiveThreat(const SOS_SourceRecord_t *slot, SOS_ThreatType_t threat)
{
    uint8_t has = 0;
    double mine = threat_dist(slot, threat, &has);
    if (!has || mine == 0.0) mine = slot->distance;
    for (int i = 0; i < MAX_SOS_SOURCE_LOCOS; i++) {
        const SOS_SourceRecord_t *o = &g_sos_sources[i];
        if (!o->in_use || o == slot) continue;
        uint8_t oh = 0;
        const double od = threat_dist(o, threat, &oh);
        if (threat == THREAT_TRAIN_PARTED) oh = 0;      /* bug #11 */
        if (oh && od > 0.0 && od < mine) return 0U;
    }
    return 1U;
}

uint8_t SOS_IsClosestActiveStation(const SOS_StationRecord_t *slot) { (void)slot; return 1U; }

/* ---- the other locos --------------------------------------------------------- */
typedef struct {
    uint32_t id;
    uint16_t tin;
    uint8_t  dir;
    uint16_t len;
    double   x0, v;             /* position at t_from, signed m/s */
    int      t_from, t_to;      /* sends ARPs from..to */
    int32_t  adjust;            /* adjustment-tag correction: adjusted = raw + adjust */
    uint8_t  flip;              /* adjustment flips the direction */
} Other;

static Other g_other[] = {
    { 2, 102, NOMINAL_DIR, 450, 11010.0,   0.0,   1, 130,  0, 0 },
    { 3, 103, NOMINAL_DIR, 520, 13480.0,   0.0,  80, 275, 20, 1 },
    { 4, 101, REVERSE_DIR, 600, 14400.0, -15.0, 120, 165,  0, 0 },
    { 5, 101, NOMINAL_DIR, 500, 15000.0,  15.0, 198, 232,  0, 0 },
    { 6, 106, NOMINAL_DIR, 700,  9600.0,  12.0, 213, 240,  0, 0 },
};
#define N_OTHER ((int)(sizeof(g_other) / sizeof(g_other[0])))

static uint8_t other_emergency(const Other *o, int t)
{
    if (o->id == 2 && t >= 9 && t < 112)  return 2U;   /* manual SoS */
    if (o->id == 3 && t >= 90)            return 1U;   /* unusual stop */
    if (o->id == 6 && t >= 215 && t < 235) return 6U;  /* parting */
    return 0U;
}

static uint16_t other_tin(const Other *o, int t)
{
    if (o->id == 4 && t >= 158) return 104U;
    if (o->id == 5 && t >= 225) return 105U;
    return o->tin;
}

static double other_pos(const Other *o, int t) { return o->x0 + o->v * (double)(t - o->t_from); }

/* ---- own loco ---------------------------------------------------------------- */
static double own_pos(int t)
{
    if (t <= 150) return 10000.0 + 20.0 * t;
    if (t < 170)  return 13000.0;
    return 13000.0 + 20.0 * (t - 170);
}

static float own_speed(int t) { return (t >= 150 && t < 170) ? 0.0f : 20.0f; }

/* ---- aggregate (SOS_RecomputeAggregates, as it reads) ------------------------- */
static void recompute(void)
{
    SOS_SourceRecord_t *ho = NULL, *re = NULL, *best = NULL;
    double best_d = 0.0;
    SOS_ThreatType_t best_t = THREAT_NONE;

    for (int i = 0; i < MAX_SOS_SOURCE_LOCOS; i++) {
        SOS_SourceRecord_t *s = &g_sos_sources[i];
        if (!s->in_use) continue;
        if (s->is_head_on_collision_recvd && (!ho || s->collision_distance < ho->collision_distance)) ho = s;
        if (s->is_rear_end_collision_recvd && (!re || s->collision_distance < re->collision_distance)) re = s;
        double d = 0.0; SOS_ThreatType_t t = THREAT_NONE;
        if (s->is_access_sos_recvd)   { d = s->sos_distance; t = THREAT_MANUAL_SOS; }
        if (s->is_unusual_stop_recvd && (t == THREAT_NONE || s->sos_distance < d)) { d = s->sos_distance; t = THREAT_UNUSUAL_STOP; }
        if (s->is_train_parted_recvd && (t == THREAT_NONE || s->sos_distance < d)) { d = s->sos_distance; t = THREAT_TRAIN_PARTED; }
        if (t == THREAT_NONE) continue;
        if (!best || d < best_d) { best = s; best_d = d; best_t = t; }     /* 0 wins: bug #8 */
    }

    loco_params.is_access_sos_recvd = loco_params.is_unusual_stop_recvd = loco_params.is_train_parted_recvd = 0;
    loco_params.is_head_on_collision_recvd = loco_params.is_rear_end_collision_recvd = 0;
    loco_params.dmi_context_values.sos_other_loco_manual = 0;
    loco_params.dmi_context_values.sos_other_loco_stopped_in_block_section = 0;
    loco_params.dmi_context_values.sos_other_loco_train_parted = 0;
    loco_params.dmi_alarm_codes.headon_collision_with_loco = 0;
    loco_params.dmi_alarm_codes.rearend_collision_with_loco = 0;
    loco_params.collision_distance = loco_params.sos_distance = 0.0;
    loco_params.collision_loco_id = 0;

    uint8_t threat = SOS_LOG_THREAT_NONE, stn_won = 0;
    uint16_t stn_id = 0;
    if (ho || re) {
        SOS_SourceRecord_t *tg = ho ? ho : re;
        if (ho) { loco_params.is_head_on_collision_recvd = 1; loco_params.dmi_alarm_codes.headon_collision_with_loco = 1; loco_params.emergency_status = 4; threat = SOS_LOG_THREAT_HEAD_ON; }
        else    { loco_params.is_rear_end_collision_recvd = 1; loco_params.dmi_alarm_codes.rearend_collision_with_loco = 1; loco_params.emergency_status = 5; threat = SOS_LOG_THREAT_REAR_END; }
        /* bug #1: emergency_status is never set back to 0 */
        loco_params.collision_distance = tg->collision_distance;
        loco_params.collision_loco_id = tg->source_loco_id;
    } else {
        if (best) {
            threat = (uint8_t)best_t;
            if (best_t == THREAT_MANUAL_SOS)   { loco_params.is_access_sos_recvd = 1; loco_params.dmi_context_values.sos_other_loco_manual = 1; }
            if (best_t == THREAT_UNUSUAL_STOP) { loco_params.is_unusual_stop_recvd = 1; loco_params.dmi_context_values.sos_other_loco_stopped_in_block_section = 1; }
            if (best_t == THREAT_TRAIN_PARTED) { loco_params.is_train_parted_recvd = 1; loco_params.dmi_context_values.sos_other_loco_train_parted = 1; }
            loco_params.sos_distance = best_d;
            loco_params.collision_loco_id = best->source_loco_id;
        }
        double sd = 0.0;
        for (int i = 0; i < MAX_SOS_STATIONS; i++) {
            const SOS_StationRecord_t *st = &g_sos_stations[i];
            if (st->in_use && st->is_add_em_sos_recvd && st->sos_distance > 0.0 && (sd == 0.0 || st->sos_distance < sd)) { sd = st->sos_distance; stn_id = st->station_id; }
        }
        if (sd > 0.0 && (loco_params.sos_distance == 0.0 || sd < loco_params.sos_distance)) {
            loco_params.is_access_sos_recvd = loco_params.is_unusual_stop_recvd = loco_params.is_train_parted_recvd = 0;
            loco_params.dmi_context_values.sos_other_loco_manual = 0;
            loco_params.dmi_context_values.sos_other_loco_stopped_in_block_section = 0;
            loco_params.dmi_context_values.sos_other_loco_train_parted = 0;
            loco_params.sos_distance = sd;
            loco_params.collision_loco_id = 0;
            threat = SOS_LOG_THREAT_STATION_GENERAL;
            stn_won = 1;
        } else {
            stn_id = 0;
        }
    }

    /* SOS_RecomputeStationAggregates */
    uint8_t any = 0; uint16_t closest = 0; double cd = 0.0;
    for (int i = 0; i < MAX_SOS_STATIONS; i++) {
        const SOS_StationRecord_t *st = &g_sos_stations[i];
        if (st->in_use && st->is_add_em_sos_recvd) {
            any = 1;
            if (cd == 0.0 || st->sos_distance < cd) { cd = st->sos_distance; closest = st->station_id; }
        }
    }
    loco_params.is_add_em_sos_recvd = any;
    loco_params.dmi_context_values.sos_station_all_locos = any;
    loco_params.sos_station_id = closest;
    if (!any) loco_params.dmi_alarm_codes.brake_applied_station_general_sos = 0;

    SOS_LogNoteAggregate(threat, stn_won, stn_id);
}

/* ---- the table ------------------------------------------------------------------- */
static SOS_SourceRecord_t *acquire(uint32_t id, double dist)
{
    for (int i = 0; i < MAX_SOS_SOURCE_LOCOS; i++)
        if (g_sos_sources[i].in_use && g_sos_sources[i].source_loco_id == id) return &g_sos_sources[i];
    for (int i = 0; i < MAX_SOS_SOURCE_LOCOS; i++)
        if (!g_sos_sources[i].in_use) {
            memset(&g_sos_sources[i], 0, sizeof(g_sos_sources[i]));
            g_sos_sources[i].in_use = 1;
            g_sos_sources[i].source_loco_id = id;
            SOS_LogEvent(SOS_EV_SRC_ADDED, 0U, 0U, id, dist);
            return &g_sos_sources[i];
        }
    SOS_LogEvent(SOS_EV_SRC_DROPPED_FULL, 0U, 0U, id, dist);
    return NULL;
}

static uint8_t any_threat(const SOS_SourceRecord_t *s)
{
    return (uint8_t)(s->is_access_sos_recvd | s->is_unusual_stop_recvd | s->is_head_on_collision_recvd |
                     s->is_rear_end_collision_recvd | s->is_train_parted_recvd);
}

#define LB_MANUAL_SOS_RECVD                   21U   /* placeholder numbers: the  */
#define LB_BRAKE_UNUSUAL_STOP_DETECTED        22U   /* real LB_* values are not  */
#define LB_SOS_STN_HEADON_COLLISION_DETECTED  23U   /* in hand yet               */
#define LB_BRAKE_STATION_GENERAL_SOS_RECEIVED 24U

static void collisions(SOS_SourceRecord_t *s)
{
    const double own = loco_params.abs_location;
    const uint8_t same_tin = (loco_params.tin != 0 && s->tin == loco_params.tin);

    /* rear-end */
    uint8_t rear = 0; double rear_d = 0.0;
    if (same_tin && s->movement_dir == loco_params.movement_dir && own < s->abs_loc) {
        rear_d = fabs((double)(s->abs_loc - s->train_length) - own);
        rear = rear_d < loco_info.collision_trigger_distance;
    }
    if (rear) {
        s->collision_distance = rear_d;
        if (!s->is_rear_end_collision_recvd)
            SOS_LogEvent(SOS_EV_THREAT_START, SOS_LOG_THREAT_REAR_END, 0U, s->source_loco_id, rear_d);
        s->is_rear_end_collision_recvd = 1;
    } else if (s->is_rear_end_collision_recvd) {
        SOS_LogEvent(SOS_EV_THREAT_END, SOS_LOG_THREAT_REAR_END, SOS_END_GEOMETRY, s->source_loco_id, s->collision_distance);
        s->is_rear_end_collision_recvd = 0;
        s->collision_distance = 0.0;
    }

    /* head-on */
    const double gap = gap_to(s->abs_loc);
    if (same_tin && s->movement_dir != loco_params.movement_dir && gap < loco_info.collision_trigger_distance) {
        s->collision_distance = gap;
        if (!s->is_head_on_collision_recvd) {
            SOS_LogEvent(SOS_EV_THREAT_START, SOS_LOG_THREAT_HEAD_ON, 0U, s->source_loco_id, gap);
            s->is_head_on_collision_recvd = 1;
            if (SOS_IsClosestActiveThreat(s, THREAT_HEAD_ON))
                SOS_LogEvent(SOS_EV_BRAKE_APPLIED, SOS_LOG_THREAT_HEAD_ON, 0U, LB_SOS_STN_HEADON_COLLISION_DETECTED, gap);
        }
        s->is_head_on_collision_recvd = 1;
    } else if (s->is_head_on_collision_recvd) {
        SOS_LogEvent(SOS_EV_THREAT_END, SOS_LOG_THREAT_HEAD_ON, SOS_END_GEOMETRY, s->source_loco_id, s->collision_distance);
        s->is_head_on_collision_recvd = 0;
        s->collision_distance = 0.0;
    }
}

static void brake_or_skip(SOS_SourceRecord_t *s, SOS_ThreatType_t t, uint8_t threat, uint32_t lb, double d)
{
    if (loco_params.sensor_speed > 0.001f && SOS_IsClosestActiveThreat(s, t))
        SOS_LogEvent(SOS_EV_BRAKE_APPLIED, threat, 0U, lb, d);
    else
        SOS_LogEvent(SOS_EV_BRAKE_SKIPPED, threat,
                     loco_params.sensor_speed > 0.001f ? SOS_SKIP_NOT_CLOSEST : SOS_SKIP_STANDSTILL,
                     s->source_loco_id, d);
}

/* ProcessAccessRequest, as it reads (simplified). */
static void process_arp(const Other *o, int t)
{
    const int32_t raw = (int32_t)other_pos(o, t);
    const uint8_t sts = other_emergency(o, t);
    int32_t adj = raw + o->adjust;
    uint8_t dir = o->dir;
    if (o->flip) dir = (dir == NOMINAL_DIR) ? REVERSE_DIR : NOMINAL_DIR;

    access_req_recvd_stn_id = 0U;
    const double dist = gap_to(adj);
    SOS_SourceRecord_t *s = acquire(o->id, dist);
    if (!s) return;
    s->last_update_ms = g_tick;
    s->abs_loc = adj;
    s->movement_dir = dir;
    s->tin = other_tin(o, t);
    s->train_length = o->len;
    s->distance = dist;
    s->raw_abs_loc = raw;
    s->raw_dir = o->dir;
    s->last_emergency_sts = sts;
    s->other_mode = 4U;
    s->other_speed = (uint16_t)fabs(o->v * 3.6);
    s->last_frame_no = 36001U + (uint32_t)t;
    s->last_rfid_id = (uint16_t)(200 + o->id);
    s->approaching_station_id = 0U;
    const uint8_t had = any_threat(s);

    collisions(s);

    /* parted: no range check (bug #10) */
    if (sts == 6U) {
        s->sos_distance = dist;
        if (!s->is_train_parted_recvd)
            SOS_LogEvent(SOS_EV_THREAT_START, SOS_LOG_THREAT_TRAIN_PARTED, 0U, s->source_loco_id, dist);
        s->is_train_parted_recvd = 1;
    } else if (s->is_train_parted_recvd) {
        SOS_LogEvent(SOS_EV_THREAT_END, SOS_LOG_THREAT_TRAIN_PARTED, SOS_END_STATUS_CLEARED, s->source_loco_id, s->sos_distance);
        s->is_train_parted_recvd = 0; s->sos_distance = 0.0;
    }

    if (sts == 1U) {
        if (IsUnusualStopOfOtherLocoToBeProcessed((uint32_t)s->abs_loc, s->movement_dir, s->tin, s->train_length)) {
            s->sos_distance = dist;
            if (!s->is_unusual_stop_recvd) {
                SOS_LogEvent(SOS_EV_THREAT_START, SOS_LOG_THREAT_UNUSUAL_STOP, 0U, s->source_loco_id, dist);
                s->is_unusual_stop_recvd = 1;
                brake_or_skip(s, THREAT_UNUSUAL_STOP, SOS_LOG_THREAT_UNUSUAL_STOP, LB_BRAKE_UNUSUAL_STOP_DETECTED, dist);
            }
            s->is_unusual_stop_recvd = 1;
        } else {
            if (s->is_unusual_stop_recvd)
                SOS_LogEvent(SOS_EV_THREAT_END, SOS_LOG_THREAT_UNUSUAL_STOP, SOS_END_NOT_PROCESSED, s->source_loco_id, s->sos_distance);
            s->is_unusual_stop_recvd = 0; s->sos_distance = 0.0;
        }
    } else if (s->is_unusual_stop_recvd) {
        SOS_LogEvent(SOS_EV_THREAT_END, SOS_LOG_THREAT_UNUSUAL_STOP, SOS_END_STATUS_CLEARED, s->source_loco_id, s->sos_distance);
        s->is_unusual_stop_recvd = 0; s->sos_distance = 0.0;
    }

    if (sts == 2U) {
        if (IsManualSOSToBeProcessed((uint32_t)s->abs_loc, s->movement_dir, s->train_length)) {
            if (!s->is_access_sos_recvd) {
                s->sos_distance = dist;
                SOS_LogEvent(SOS_EV_THREAT_START, SOS_LOG_THREAT_MANUAL_SOS, 0U, s->source_loco_id, dist);
                s->is_access_sos_recvd = 1;
                brake_or_skip(s, THREAT_MANUAL_SOS, SOS_LOG_THREAT_MANUAL_SOS, LB_MANUAL_SOS_RECVD, dist);
            } else if (s->sos_distance > 0.0) {
                s->sos_distance = dist;
            }
            s->is_access_sos_recvd = 1;
        } else {
            if (s->is_access_sos_recvd)
                SOS_LogEvent(SOS_EV_THREAT_END, SOS_LOG_THREAT_MANUAL_SOS, SOS_END_NOT_PROCESSED, s->source_loco_id, s->sos_distance);
            s->is_access_sos_recvd = 0; s->sos_distance = 0.0;
        }
    } else if (s->is_access_sos_recvd) {
        SOS_LogEvent(SOS_EV_THREAT_END, SOS_LOG_THREAT_MANUAL_SOS, SOS_END_STATUS_CLEARED, s->source_loco_id, s->sos_distance);
        s->is_access_sos_recvd = 0; s->sos_distance = 0.0;
    }

    if (s->sos_distance > 0.0 && IsSOSTargetDistanceRemove((uint32_t)s->abs_loc, s->movement_dir)) {
        SOS_LogEvent(SOS_EV_TARGET_REMOVED, 0U, 0U, s->source_loco_id, s->sos_distance);
        s->sos_distance = 0.0;
    }

    recompute();
    if (sts != 0U || any_threat(s) || had) SOS_LogSnapshot(SOS_SNAP_AFTER_ARP);
}

static void process_aep(uint16_t id, uint32_t loc, uint8_t gen)
{
    const double dist = gap_to((int32_t)loc);
    if (dist >= loco_info.sos_trigger_distance) return;
    SOS_StationRecord_t *s = NULL;
    for (int i = 0; i < MAX_SOS_STATIONS; i++)
        if (g_sos_stations[i].in_use && g_sos_stations[i].station_id == id) s = &g_sos_stations[i];
    if (!s) {
        for (int i = 0; i < MAX_SOS_STATIONS && !s; i++)
            if (!g_sos_stations[i].in_use) {
                s = &g_sos_stations[i];
                memset(s, 0, sizeof(*s));
                s->in_use = 1; s->station_id = id;
                SOS_LogEvent(SOS_EV_STN_ADDED, 0U, 0U, id, dist);
            }
    }
    if (!s) return;
    s->last_update_ms = g_tick;
    s->station_abs_loc = loc;
    s->distance = dist;
    s->last_gen_sos_call = gen;
    s->last_frame_no = 36001U + (uint32_t)g_t;
    if (gen) {
        s->sos_distance = dist;
        if (!s->is_add_em_sos_recvd) {
            SOS_LogEvent(SOS_EV_STN_SOS_START, 0U, 0U, id, dist);
            loco_params.dmi_alarm_codes.brake_applied_station_general_sos = 1;
            SOS_LogEvent(SOS_EV_BRAKE_APPLIED, SOS_LOG_THREAT_STATION_GENERAL, 0U, LB_BRAKE_STATION_GENERAL_SOS_RECEIVED, dist);
        }
        s->is_add_em_sos_recvd = 1;
    } else if (s->is_add_em_sos_recvd) {
        SOS_LogEvent(SOS_EV_STN_SOS_END, 0U, SOS_END_CANCELLED, id, s->sos_distance);
        s->is_add_em_sos_recvd = 0; s->sos_distance = 0.0;
    }
    recompute();
    SOS_LogSnapshot(SOS_SNAP_AFTER_AEP);
}

/* SOS_Periodic_Maintenance, as it reads (simplified). */
static void periodic(void)
{
    if (loco_params.loco_curr_mode == 11U) {
        uint8_t had = 0;
        for (int i = 0; i < MAX_SOS_SOURCE_LOCOS; i++) if (g_sos_sources[i].in_use) had = 1;
        for (int i = 0; i < MAX_SOS_STATIONS; i++) if (g_sos_stations[i].in_use) had = 1;
        if (had) SOS_LogEvent(SOS_EV_TABLE_RESET, SOS_RESET_NON_LEADING, 0U, 0U, 0.0);
        memset(g_sos_sources, 0, sizeof(g_sos_sources));
        memset(g_sos_stations, 0, sizeof(g_sos_stations));
        loco_params.is_dest_loco_sos_recvd = 0;
    }
    for (int i = 0; i < MAX_SOS_SOURCE_LOCOS; i++) {
        SOS_SourceRecord_t *s = &g_sos_sources[i];
        if (!s->in_use) continue;
        const uint8_t coll = s->is_head_on_collision_recvd | s->is_rear_end_collision_recvd;
        const uint8_t sos = s->is_access_sos_recvd | s->is_unusual_stop_recvd | s->is_train_parted_recvd;
        if (coll || sos) {
            s->distance = gap_to(s->abs_loc);
            if (sos && IsSOSTargetDistanceRemove((uint32_t)s->abs_loc, s->movement_dir)) {
                if (s->sos_distance > 0.0)
                    SOS_LogEvent(SOS_EV_TARGET_REMOVED, 0U, 0U, s->source_loco_id, s->sos_distance);
                s->distance = 0.0;
            }
            if (coll) collisions(s);
            if (sos) s->sos_distance = s->distance;
        }
    }
    for (int i = 0; i < MAX_SOS_SOURCE_LOCOS; i++) {
        SOS_SourceRecord_t *s = &g_sos_sources[i];
        if (!s->in_use) continue;
        const uint32_t el = g_tick - s->last_update_ms;
        if (!any_threat(s)) {
            if (el >= 5000U) { SOS_LogEvent(SOS_EV_SRC_QUIET_RELEASED, 0U, 0U, s->source_loco_id, s->distance); memset(s, 0, sizeof(*s)); }
        } else if (el >= 10000U) {
            SOS_LogEvent(SOS_EV_SRC_TIMEOUT, SOS_LogThreatBits(s), 0U, s->source_loco_id, s->distance);
            memset(s, 0, sizeof(*s));
        }
    }
    for (int i = 0; i < MAX_SOS_STATIONS; i++) {
        SOS_StationRecord_t *s = &g_sos_stations[i];
        if (!s->in_use) continue;
        if (s->is_add_em_sos_recvd) { s->distance = gap_to((int32_t)s->station_abs_loc); s->sos_distance = s->distance; }
        if (!s->is_add_em_sos_recvd && g_tick - s->last_update_ms >= 5000U) {
            SOS_LogEvent(SOS_EV_STN_QUIET_RELEASED, 0U, 0U, s->station_id, s->distance);
            memset(s, 0, sizeof(*s));
        }
    }
    recompute();
    SOS_LogSnapshot(SOS_SNAP_PERIODIC);
}

/* The real @linfo frame's hex, from the capture make_fixture.sh hands in. */
static char g_linfo_hex[2048];

static void load_linfo(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[4096];
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "@linfo_", 7) != 0) continue;
        /* skip tag, timestamp, seq */
        char *p = line;
        for (int k = 0; k < 3 && p; k++) { p = strchr(p, ' '); if (p) p++; }
        if (p) {
            strncpy(g_linfo_hex, p, sizeof g_linfo_hex - 1);
            g_linfo_hex[strcspn(g_linfo_hex, "\r\n")] = 0;
        }
        break;
    }
    fclose(f);
}

static void emit_linfo(void)
{
    if (!g_linfo_hex[0]) return;
    fprintf(g_out, "@linfo_%u_1 2026-10-09T10:00:00 %u %s\n", (unsigned)loco_params.loco_id, g_seq++, g_linfo_hex);
}

static void reset_world(uint32_t loco, uint16_t tin)
{
    memset(&loco_params, 0, sizeof(loco_params));
    memset(g_sos_sources, 0, sizeof(g_sos_sources));
    memset(g_sos_stations, 0, sizeof(g_sos_stations));
    loco_params.loco_id = loco;
    loco_params.tin = tin;
    loco_params.movement_dir = NOMINAL_DIR;
    loco_params.loco_curr_mode = 4U;     /* Full_Supervision */
    loco_params.train_length = 600U;
    loco_params.ma = 2500;
    loco_params.station_id = 0U;
    /* As loco 1's real @linfo of 2026-10-08 has them (written into the log). */
    loco_info.sos_trigger_distance = 3000U;
    loco_info.sos_cancellation_distance = 500U;
    loco_info.collision_trigger_distance = 1000U;
    s_sos_snap_id = 0U; s_last_dest_sos = 0U;
    s_agg_threat = 0U; s_agg_station_won = 0U; s_agg_station_id = 0U;
}

/* ---- loco 1: the loco that sees it all ------------------------------------------ */
static void run_loco1(void)
{
    reset_world(1U, 101U);
    emit_linfo();
    for (g_t = 0; g_t <= 280; g_t++) {
        g_tick = 500000U + (uint32_t)g_t * 1000U;
        loco_params.abs_location = own_pos(g_t);
        loco_params.sensor_speed = own_speed(g_t);
        loco_params.rfid_tag_id = (uint16_t)(100 + (int)(loco_params.abs_location / 1000.0));
        if (g_t >= 270) loco_params.loco_curr_mode = 11U;

        /* own unusual stoppage while standing 150..169 */
        if (g_t == 155) {
            loco_params.dmi_alarm_codes.ack_block_stop_sos_generate = 1;
            loco_params.unusual_stoppage_sos_alarm = 1;
            g_tick += 200U; SOS_LogEvent(SOS_EV_SELF_UNUSUAL_ALARM, 0U, 0U, 1U, 0.0);
        }
        if (g_t == 160) {
            g_tick += 200U; SOS_LogEvent(SOS_EV_SELF_UNUSUAL_ACK, 0U, 0U, 1U, 0.0);
            loco_params.unusual_sos_acknowledged = 1;
            loco_params.unusual_stoppage_sos_alarm = 0;
            loco_params.dmi_alarm_codes.ack_block_stop_sos_generate = 0;
        }
        if (g_t == 170) loco_params.unusual_sos_acknowledged = 0;

        /* ARPs: each loco every 2 s, odd/even by id */
        g_tick = 500000U + (uint32_t)g_t * 1000U + 300U;
        if (loco_params.loco_curr_mode != 11U)
            for (int i = 0; i < N_OTHER; i++) {
                const Other *o = &g_other[i];
                if (g_t >= o->t_from && g_t <= o->t_to && ((g_t + (int)o->id) % 2) == 0) process_arp(o, g_t);
            }

        /* station 4501: additional emergency 228..239, cancel at 240 */
        g_tick = 500000U + (uint32_t)g_t * 1000U + 500U;
        if (g_t >= 228 && g_t <= 240 && (g_t % 2) == 0) process_aep(4501U, 16000U, g_t < 240 ? 1U : 0U);

        /* SLRP DEST_LOCO_SOS: 8 (Station General) 245..249, then 0; the flag latches (bug #2) */
        if (g_t >= 240 && g_t < 270 && (g_t % 2) == 1) {
            loco_params.station_id = 4501U;
            const uint8_t d = (g_t >= 245 && g_t < 250) ? 8U : 0U;
            SOS_LogNoteDestSos(d);
            if (d) { loco_params.is_dest_loco_sos_recvd = 1; SOS_LogSnapshot(SOS_SNAP_AFTER_DEST_SOS); }
        }

        g_tick = 500000U + (uint32_t)g_t * 1000U + 900U;
        periodic();
    }
}

/* ---- loco 2: the loco that sent the manual SoS ------------------------------------ */
static void run_loco2(void)
{
    reset_world(2U, 102U);
    emit_linfo();
    loco_params.abs_location = 11010.0;
    loco_params.sensor_speed = 0.0f;
    loco_params.train_length = 450U;
    loco_params.ma = 0;
    Other heard = { 1, 101, NOMINAL_DIR, 600, 10000.0, 20.0, 0, 130, 0, 0 };
    for (g_t = 0; g_t <= 130; g_t++) {
        g_tick = 800000U + (uint32_t)g_t * 1000U;
        /* the pilot holds SoS 9..111 */
        if (g_t == 9) {
            loco_params.emergency_status = 2U;
            loco_params.dmi_context_values.sos_self_loco_manual = 1;
        }
        if (g_t == 112) {
            loco_params.emergency_status = 0U;
            loco_params.dmi_context_values.sos_self_loco_manual = 0;
        }
        g_tick += 400U;
        if ((g_t % 2) == 1) {
            /* loco 1's quiet ARPs: tracked, never a threat (different line, no status) */
            const double x = own_pos(g_t);
            SOS_SourceRecord_t *s = acquire(1U, fabs(x - 11010.0));
            if (s) {
                s->last_update_ms = g_tick;
                s->abs_loc = s->raw_abs_loc = (int32_t)x;
                s->movement_dir = s->raw_dir = heard.dir;
                s->tin = heard.tin; s->train_length = heard.len;
                s->distance = fabs(x - 11010.0);
                s->other_mode = 4U; s->other_speed = (uint16_t)(own_speed(g_t) * 3.6f);
                s->last_frame_no = 36001U + (uint32_t)g_t;
                s->last_rfid_id = (uint16_t)(100 + (int)(x / 1000.0));
            }
        }
        g_tick = 800000U + (uint32_t)g_t * 1000U + 900U;
        periodic();
    }
}

int main(int argc, char **argv)
{
    if (argc != 4) { fprintf(stderr, "usage: sosgen <loco1.log> <loco2.log> <capture with @linfo>\n"); return 2; }
    load_linfo(argv[3]);
    g_out = fopen(argv[1], "w");
    if (!g_out) return 1;
    run_loco1();
    fclose(g_out);
    g_out = fopen(argv[2], "w");
    if (!g_out) return 1;
    g_seq = 9000U;
    run_loco2();
    fclose(g_out);
    return 0;
}

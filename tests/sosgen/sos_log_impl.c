/* ======================================================================
 * SoS logging for DLConsole.
 * Paste this whole block into sos_table_manager.c directly AFTER
 *     static SOS_StationRecord_t g_sos_stations[MAX_SOS_STATIONS];
 * (it reads g_sos_sources / g_sos_stations, which are static there, and
 * the functions below it in that file call into it).
 * ====================================================================== */

extern uint16_t access_req_recvd_stn_id;    /* sos_manager.c */

typedef struct
{
    uint8_t  buf[SOS_LOG_MAX_BYTES];
    uint16_t len;
} SOS_LogBuf_t;

static uint32_t s_sos_snap_id      = 0U;
static uint8_t  s_last_dest_sos    = 0U;
static uint8_t  s_agg_threat       = SOS_LOG_THREAT_NONE;
static uint8_t  s_agg_station_won  = 0U;
static uint16_t s_agg_station_id   = 0U;

static void SOS_LogPutU8(SOS_LogBuf_t *b, uint8_t v)
{
    if (b->len < SOS_LOG_MAX_BYTES)
    {
        b->buf[b->len] = v;
        b->len++;
    }
}

static void SOS_LogPutU16(SOS_LogBuf_t *b, uint16_t v)
{
    SOS_LogPutU8(b, (uint8_t)(v & 0xFFU));
    SOS_LogPutU8(b, (uint8_t)((v >> 8) & 0xFFU));
}

static void SOS_LogPutU32(SOS_LogBuf_t *b, uint32_t v)
{
    SOS_LogPutU8(b, (uint8_t)(v & 0xFFU));
    SOS_LogPutU8(b, (uint8_t)((v >> 8) & 0xFFU));
    SOS_LogPutU8(b, (uint8_t)((v >> 16) & 0xFFU));
    SOS_LogPutU8(b, (uint8_t)((v >> 24) & 0xFFU));
}

static void SOS_LogPutI32(SOS_LogBuf_t *b, int32_t v)
{
    SOS_LogPutU32(b, (uint32_t)v);
}

/* metres (double) -> decimetres (int32), rounded, clamped */
static int32_t SOS_LogDm(double metres)
{
    double  dm = metres * 10.0;
    int32_t r;

    if (dm >= 2147483647.0)
    {
        r = INT32_MAX;
    }
    else if (dm <= -2147483648.0)
    {
        r = INT32_MIN;
    }
    else
    {
        r = (int32_t)((dm >= 0.0) ? (dm + 0.5) : (dm - 0.5));
    }

    return r;
}

/* sensor_speed x 100, same unit as sensor_speed, clamped to 0..65535 */
static uint16_t SOS_LogSpeed(float speed)
{
    float    s = speed * 100.0f;
    uint16_t r;

    if (s <= 0.0f)
    {
        r = 0U;
    }
    else if (s >= 65535.0f)
    {
        r = 65535U;
    }
    else
    {
        r = (uint16_t)(s + 0.5f);
    }

    return r;
}

static uint8_t SOS_LogThreatBits(const SOS_SourceRecord_t *s)
{
    uint8_t t = 0U;

    if (s->is_access_sos_recvd)         { t |= 0x01U; }
    if (s->is_unusual_stop_recvd)       { t |= 0x02U; }
    if (s->is_head_on_collision_recvd)  { t |= 0x04U; }
    if (s->is_rear_end_collision_recvd) { t |= 0x08U; }
    if (s->is_train_parted_recvd)       { t |= 0x10U; }

    return t;
}

/* Called on every SLRP (loco_manager.c) with DEST_LOCO_SOS, including 0,
 * so the console sees the station's request both start and stop. */
void SOS_LogNoteDestSos(uint8_t sos_type)
{
    if (sos_type != s_last_dest_sos)
    {
        s_last_dest_sos = sos_type;
        SOS_LogEvent(SOS_EV_DEST_LOCO_SOS, sos_type, 0U, (uint32_t)loco_params.station_id, 0.0);
    }
}

void SOS_LogNoteAggregate(uint8_t threat, uint8_t station_won, uint16_t station_id)
{
    s_agg_threat      = threat;
    s_agg_station_won = station_won;
    s_agg_station_id  = station_id;
}

/* One @sossrc line for one in-use slot. station_sec = own rear tag is a
 * station section (same test CheckLocoCollisionConditions_PerSource uses). */
static void SOS_LogSource(uint8_t idx, const SOS_SourceRecord_t *s,
                          uint32_t now_ms, uint8_t station_sec)
{
    SOS_LogBuf_t b;
    uint8_t      threat  = SOS_LogThreatBits(s);
    uint8_t      closest = 0U;
    uint16_t     eval    = 0U;
    uint8_t      adj_src = SOS_AdjacencyInfoSource();
    double       gap     = fabs(loco_params.abs_location - (double)s->abs_loc);

    (void)memset(&b, 0, sizeof(b));

    /* Evaluated at snapshot time with the same predicates the firmware uses. */
    if ((s->raw_abs_loc != s->abs_loc) || (s->raw_dir != s->movement_dir))
    {
        eval |= 0x0001U;                                    /* SOSWithAdjustment changed it */
    }
    if (IsAdjacentLineInfringing(s->tin))                   { eval |= 0x0002U; }
    if (adj_src & 0x01U)                                    { eval |= 0x0004U; }
    if (adj_src & 0x02U)                                    { eval |= 0x0008U; }
    if (adj_src == 0U)                                      { eval |= 0x0010U; }   /* fail-open */
    if ((station_sec == 0U) ||
        (loco_params.station_id == access_req_recvd_stn_id)) { eval |= 0x0020U; }  /* as firmware decides */
    if (IsSOSTargetDistanceRemove((uint32_t)s->abs_loc, s->movement_dir)) { eval |= 0x0040U; }
    if (IsManualSOSToBeProcessed((uint32_t)s->abs_loc, s->movement_dir, s->train_length)) { eval |= 0x0080U; }
    if (IsUnusualStopOfOtherLocoToBeProcessed((uint32_t)s->abs_loc, s->movement_dir,
                                              s->tin, s->train_length)) { eval |= 0x0100U; }
    if ((loco_params.tin != 0U) && (loco_params.tin == s->tin)) { eval |= 0x0200U; }
    if (gap < (double)loco_info.sos_trigger_distance)       { eval |= 0x0400U; }
    if (gap < (double)loco_info.collision_trigger_distance) { eval |= 0x0800U; }
    if ((station_sec == 0U) ||
        (loco_params.station_id == s->approaching_station_id)) { eval |= 0x1000U; } /* per-slot answer */

    if ((threat & 0x01U) && SOS_IsClosestActiveThreat(s, THREAT_MANUAL_SOS))   { closest |= 0x01U; }
    if ((threat & 0x02U) && SOS_IsClosestActiveThreat(s, THREAT_UNUSUAL_STOP)) { closest |= 0x02U; }
    if ((threat & 0x04U) && SOS_IsClosestActiveThreat(s, THREAT_HEAD_ON))      { closest |= 0x04U; }
    if ((threat & 0x08U) && SOS_IsClosestActiveThreat(s, THREAT_REAR_END))     { closest |= 0x08U; }
    if ((threat & 0x10U) && SOS_IsClosestActiveThreat(s, THREAT_TRAIN_PARTED)) { closest |= 0x10U; }

    SOS_LogPutU8 (&b, (uint8_t)SOS_LOG_VERSION);            /*  0 */
    SOS_LogPutU32(&b, s_sos_snap_id);                       /*  1 */
    SOS_LogPutU8 (&b, idx);                                 /*  5 */
    SOS_LogPutU32(&b, s->source_loco_id);                   /*  6 */
    SOS_LogPutU32(&b, now_ms - s->last_update_ms);          /* 10 */
    SOS_LogPutU32(&b, s->last_frame_no);                    /* 14 */
    SOS_LogPutU8 (&b, s->last_emergency_sts);               /* 18 */
    SOS_LogPutU8 (&b, s->other_mode);                       /* 19 */
    SOS_LogPutU16(&b, s->other_speed);                      /* 20 */
    SOS_LogPutI32(&b, s->raw_abs_loc);                      /* 22 */
    SOS_LogPutU8 (&b, s->raw_dir);                          /* 26 */
    SOS_LogPutU16(&b, s->last_rfid_id);                     /* 27 */
    SOS_LogPutU16(&b, s->approaching_station_id);           /* 29 */
    SOS_LogPutI32(&b, s->abs_loc);                          /* 31 */
    SOS_LogPutU8 (&b, s->movement_dir);                     /* 35 */
    SOS_LogPutU16(&b, s->tin);                              /* 36 */
    SOS_LogPutU16(&b, s->train_length);                     /* 38 */
    SOS_LogPutI32(&b, SOS_LogDm(s->distance));              /* 40 */
    SOS_LogPutI32(&b, SOS_LogDm(s->sos_distance));          /* 44 */
    SOS_LogPutI32(&b, SOS_LogDm(s->collision_distance));    /* 48 */
    SOS_LogPutU8 (&b, threat);                              /* 52 */
    SOS_LogPutU8 (&b, closest);                             /* 53 */
    SOS_LogPutU16(&b, eval);                                /* 54 */
                                                            /* 56 bytes */
    UDP_SendCapturePkt(LOG_PKT_SOS_SRC, b.buf, b.len);
}

void SOS_LogSnapshot(uint8_t why)
{
    SOS_LogBuf_t    b;
    COMMON_RFID_TAG rear;
    uint8_t         rear_found;
    uint8_t         rear_sect   = 0xFFU;
    uint8_t         station_sec = 0U;
    uint16_t        src_in_use  = 0U;
    uint16_t        src_active  = 0U;
    uint16_t        lp          = 0U;
    uint16_t        dmi         = 0U;
    uint8_t         self        = 0U;
    uint32_t        now_ms      = ClockP_getTicks();
    uint8_t         i;

    (void)memset(&b, 0, sizeof(b));
    (void)memset(&rear, 0, sizeof(rear));

    rear_found = GetRearElement(&rear);
    if (loco_params.movement_dir == NOMINAL_DIR)
    {
        rear_sect = rear.sect_type_in_nom_dir;
    }
    else if (loco_params.movement_dir == REVERSE_DIR)
    {
        rear_sect = rear.sect_type_in_rev_dir;
    }
    else
    {
        ;
    }
    station_sec = (rear_sect == (uint8_t)STATION_SECTION) ? 1U : 0U;

    s_sos_snap_id++;

    for (i = 0U; i < MAX_SOS_SOURCE_LOCOS; i++)
    {
        if (g_sos_sources[i].in_use)
        {
            src_in_use |= (uint16_t)(1U << i);
            if (SOS_LogThreatBits(&g_sos_sources[i]) != 0U)
            {
                src_active |= (uint16_t)(1U << i);
            }
        }
    }

    if (loco_params.is_access_sos_recvd)             { lp |= 0x0001U; }
    if (loco_params.is_unusual_stop_recvd)           { lp |= 0x0002U; }
    if (loco_params.is_train_parted_recvd)           { lp |= 0x0004U; }
    if (loco_params.is_head_on_collision_recvd)      { lp |= 0x0008U; }
    if (loco_params.is_rear_end_collision_recvd)     { lp |= 0x0010U; }
    if (loco_params.is_add_em_sos_recvd)             { lp |= 0x0020U; }
    if (loco_params.is_dest_loco_sos_recvd)          { lp |= 0x0040U; }
    if (loco_params.is_stn_head_on_collision_recvd)  { lp |= 0x0080U; }
    if (loco_params.is_stn_rear_end_collision_recvd) { lp |= 0x0100U; }
    if (loco_params.is_stn_shuntig_sos_recvd)        { lp |= 0x0200U; }

    if (loco_params.dmi_context_values.sos_self_loco_manual)                    { dmi |= 0x0001U; }
    if (loco_params.dmi_context_values.sos_self_loco_stopped_in_block_section)  { dmi |= 0x0002U; }
    if (loco_params.dmi_context_values.sos_self_loco_train_parted)              { dmi |= 0x0004U; }
    if (loco_params.dmi_context_values.sos_other_loco_manual)                   { dmi |= 0x0008U; }
    if (loco_params.dmi_context_values.sos_other_loco_stopped_in_block_section) { dmi |= 0x0010U; }
    if (loco_params.dmi_context_values.sos_other_loco_train_parted)             { dmi |= 0x0020U; }
    if (loco_params.dmi_context_values.sos_station_all_locos)                   { dmi |= 0x0040U; }
    if (loco_params.dmi_context_values.sos_station_this_loco)                   { dmi |= 0x0080U; }
    if (loco_params.dmi_alarm_codes.headon_collision_with_loco)                 { dmi |= 0x0100U; }
    if (loco_params.dmi_alarm_codes.rearend_collision_with_loco)                { dmi |= 0x0200U; }
    if (loco_params.dmi_alarm_codes.brake_applied_station_general_sos)          { dmi |= 0x0400U; }
    if (loco_params.dmi_alarm_codes.ack_block_stop_sos_generate)                { dmi |= 0x0800U; }

    if (loco_params.unusual_stoppage_sos_alarm)  { self |= 0x01U; }
    if (loco_params.unusual_sos_acknowledged)    { self |= 0x02U; }
    if (TrainStandstill())                       { self |= 0x04U; }
    if (CheckifLocoEnteredInBlockSection())      { self |= 0x08U; }

    SOS_LogPutU8 (&b, (uint8_t)SOS_LOG_VERSION);                    /*  0 */
    SOS_LogPutU8 (&b, why);                                         /*  1 */
    SOS_LogPutU32(&b, s_sos_snap_id);                               /*  2 */
    SOS_LogPutU32(&b, now_ms);                                      /*  6 */
    SOS_LogPutI32(&b, SOS_LogDm(loco_params.abs_location));         /* 10 */
    SOS_LogPutU8 (&b, loco_params.movement_dir);                    /* 14 */
    SOS_LogPutU8 (&b, loco_params.loco_curr_mode);                  /* 15 */
    SOS_LogPutU16(&b, loco_params.tin);                             /* 16 */
    SOS_LogPutU16(&b, loco_params.train_length);                    /* 18 */
    SOS_LogPutU16(&b, SOS_LogSpeed(loco_params.sensor_speed));      /* 20 */
    SOS_LogPutI32(&b, loco_params.ma);                              /* 22 */
    SOS_LogPutU16(&b, (uint16_t)loco_params.station_id);            /* 26 */
    SOS_LogPutU16(&b, loco_params.rfid_tag_id);                     /* 28 */
    SOS_LogPutU8 (&b, rear_found);                                  /* 30 */
    SOS_LogPutU8 (&b, rear_sect);                                   /* 31 */
    SOS_LogPutU8 (&b, loco_params.emergency_status);                /* 32 */
    SOS_LogPutU16(&b, access_req_recvd_stn_id);                     /* 33 */
    SOS_LogPutU8 (&b, (uint8_t)MAX_SOS_SOURCE_LOCOS);               /* 35 */
    SOS_LogPutU16(&b, src_in_use);                                  /* 36 */
    SOS_LogPutU16(&b, src_active);                                  /* 38 */
    SOS_LogPutU8 (&b, s_agg_threat);                                /* 40 */
    SOS_LogPutU8 (&b, s_agg_station_won);                           /* 41 */
    SOS_LogPutU32(&b, loco_params.collision_loco_id);               /* 42 */
    SOS_LogPutU16(&b, s_agg_station_id);                            /* 46 */
    SOS_LogPutI32(&b, SOS_LogDm(loco_params.collision_distance));   /* 48 */
    SOS_LogPutI32(&b, SOS_LogDm(loco_params.sos_distance));         /* 52 */
    SOS_LogPutU16(&b, loco_params.sos_station_id);                  /* 56 */
    SOS_LogPutU16(&b, lp);                                          /* 58 */
    SOS_LogPutU16(&b, dmi);                                         /* 60 */
    SOS_LogPutU8 (&b, s_last_dest_sos);                             /* 62 */
    SOS_LogPutU8 (&b, self);                                        /* 63 */
    SOS_LogPutU8 (&b, (uint8_t)MAX_SOS_STATIONS);                   /* 64 */

    for (i = 0U; i < MAX_SOS_STATIONS; i++)                         /* 65 + 20*i */
    {
        const SOS_StationRecord_t *st = &g_sos_stations[i];
        uint8_t sflags = 0U;

        if (st->is_add_em_sos_recvd)                        { sflags |= 0x01U; }
        if (st->last_gen_sos_call)                          { sflags |= 0x02U; }
        if (st->in_use && SOS_IsClosestActiveStation(st))   { sflags |= 0x04U; }

        SOS_LogPutU8 (&b, st->in_use);                                       /* +0  */
        SOS_LogPutU8 (&b, sflags);                                           /* +1  */
        SOS_LogPutU16(&b, st->station_id);                                   /* +2  */
        SOS_LogPutU32(&b, st->in_use ? (now_ms - st->last_update_ms) : 0U);  /* +4  */
        SOS_LogPutU32(&b, st->station_abs_loc);                              /* +8  */
        SOS_LogPutI32(&b, SOS_LogDm(st->distance));                          /* +12 */
        SOS_LogPutI32(&b, SOS_LogDm(st->sos_distance));                      /* +16 */
    }

    UDP_SendCapturePkt(LOG_PKT_SOS, b.buf, b.len);

    for (i = 0U; i < MAX_SOS_SOURCE_LOCOS; i++)
    {
        if (g_sos_sources[i].in_use)
        {
            SOS_LogSource(i, &g_sos_sources[i], now_ms, station_sec);
        }
    }
}

void SOS_LogEvent(uint8_t event, uint8_t aux1, uint8_t aux2, uint32_t id, double distance_m)
{
    SOS_LogBuf_t b;

    (void)memset(&b, 0, sizeof(b));

    SOS_LogPutU8 (&b, (uint8_t)SOS_LOG_VERSION);                    /*  0 */
    SOS_LogPutU32(&b, ClockP_getTicks());                           /*  1 */
    SOS_LogPutU32(&b, s_sos_snap_id);                               /*  5  last snapshot sent */
    SOS_LogPutU8 (&b, event);                                       /*  9 */
    SOS_LogPutU8 (&b, aux1);                                        /* 10 */
    SOS_LogPutU8 (&b, aux2);                                        /* 11 */
    SOS_LogPutU32(&b, id);                                          /* 12 */
    SOS_LogPutI32(&b, SOS_LogDm(distance_m));                       /* 16 */
    SOS_LogPutI32(&b, SOS_LogDm(loco_params.abs_location));         /* 20 */
    SOS_LogPutU16(&b, SOS_LogSpeed(loco_params.sensor_speed));      /* 24 */
    SOS_LogPutU8 (&b, loco_params.loco_curr_mode);                  /* 26 */
                                                                    /* 27 bytes */
    UDP_SendCapturePkt(LOG_PKT_SOS_EV, b.buf, b.len);
}

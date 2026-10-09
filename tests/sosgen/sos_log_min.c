/* ===== DLConsole SoS log (@sos, layout version 2) ==========================
 * Paste into sos_table_manager.c ABOVE SOS_Periodic_Maintenance().
 * Everything little-endian, written byte by byte. Max 255 bytes. */
#define SOS_LOG_MAX 260U

static uint16_t sos_put(uint8_t *b, uint16_t n, uint32_t v, uint8_t bytes)
{
    for (uint8_t i = 0U; (i < bytes) && (n < SOS_LOG_MAX); i++)
    {
        b[n] = (uint8_t)(v >> (8U * i));
        n++;
    }
    return n;
}

static int32_t sos_dm(double m)       /* metres -> decimetres */
{
    return (int32_t)((m >= 0.0) ? (m * 10.0 + 0.5) : (m * 10.0 - 0.5));
}

static void SOS_LogSnapshot(void)
{
    uint8_t  b[SOS_LOG_MAX];
    uint16_t n = 0U;
    uint32_t now = ClockP_getTicks();
    uint16_t lp = 0U, dmi = 0U;
    uint8_t  nsrc = 0U, nstn = 0U, i;

    for (i = 0U; i < MAX_SOS_SOURCE_LOCOS; i++) { if (g_sos_sources[i].in_use)  { nsrc++; } }
    for (i = 0U; i < MAX_SOS_STATIONS; i++)     { if (g_sos_stations[i].in_use) { nstn++; } }

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

    n = sos_put(b, n, 2U, 1U);                                              /*  0 version */
    n = sos_put(b, n, now, 4U);                                             /*  1 tick_ms */
    n = sos_put(b, n, (uint32_t)sos_dm(loco_params.abs_location), 4U);      /*  5 own_abs_loc_dm */
    n = sos_put(b, n, loco_params.movement_dir, 1U);                        /*  9 */
    n = sos_put(b, n, loco_params.loco_curr_mode, 1U);                      /* 10 */
    n = sos_put(b, n, loco_params.tin, 2U);                                 /* 11 */
    n = sos_put(b, n, (uint32_t)(loco_params.sensor_speed * 100.0f), 2U);   /* 13 speed_x100 */
    n = sos_put(b, n, loco_params.emergency_status, 1U);                    /* 15 */
    n = sos_put(b, n, loco_params.collision_loco_id, 4U);                   /* 16 target loco */
    n = sos_put(b, n, (uint32_t)sos_dm(loco_params.collision_distance), 4U);/* 20 */
    n = sos_put(b, n, (uint32_t)sos_dm(loco_params.sos_distance), 4U);      /* 24 */
    n = sos_put(b, n, loco_params.sos_station_id, 2U);                      /* 28 */
    n = sos_put(b, n, lp, 2U);                                              /* 30 */
    n = sos_put(b, n, dmi, 2U);                                             /* 32 */
    n = sos_put(b, n, (uint32_t)((loco_params.unusual_stoppage_sos_alarm ? 1U : 0U)
                               | (loco_params.unusual_sos_acknowledged ? 2U : 0U)), 1U); /* 34 */
    n = sos_put(b, n, nsrc, 1U);                                            /* 35 */
    n = sos_put(b, n, nstn, 1U);                                            /* 36 */

    for (i = 0U; i < MAX_SOS_SOURCE_LOCOS; i++)                             /* 24 B each */
    {
        const SOS_SourceRecord_t *s = &g_sos_sources[i];
        if (s->in_use)
        {
            uint8_t t = (uint8_t)((s->is_access_sos_recvd         ? 0x01U : 0U)
                                | (s->is_unusual_stop_recvd       ? 0x02U : 0U)
                                | (s->is_head_on_collision_recvd  ? 0x04U : 0U)
                                | (s->is_rear_end_collision_recvd ? 0x08U : 0U)
                                | (s->is_train_parted_recvd       ? 0x10U : 0U));
            uint32_t age = (now - s->last_update_ms) / 100U;
            n = sos_put(b, n, s->source_loco_id, 4U);                       /* +0 */
            n = sos_put(b, n, (age > 0xFFFFU) ? 0xFFFFU : age, 2U);         /* +4 age, 0.1 s */
            n = sos_put(b, n, (uint32_t)s->abs_loc, 4U);                    /* +6 m */
            n = sos_put(b, n, s->movement_dir, 1U);                         /* +10 */
            n = sos_put(b, n, s->tin, 2U);                                  /* +11 */
            n = sos_put(b, n, s->train_length, 2U);                         /* +13 */
            n = sos_put(b, n, (uint32_t)sos_dm(s->sos_distance), 4U);       /* +15 */
            n = sos_put(b, n, (uint32_t)sos_dm(s->collision_distance), 4U); /* +19 */
            n = sos_put(b, n, t, 1U);                                       /* +23 */
        }
    }
    for (i = 0U; i < MAX_SOS_STATIONS; i++)                                 /* 13 B each */
    {
        const SOS_StationRecord_t *s = &g_sos_stations[i];
        if (s->in_use)
        {
            uint32_t age = (now - s->last_update_ms) / 100U;
            n = sos_put(b, n, s->station_id, 2U);                           /* +0 */
            n = sos_put(b, n, s->is_add_em_sos_recvd ? 1U : 0U, 1U);        /* +2 */
            n = sos_put(b, n, (age > 0xFFFFU) ? 0xFFFFU : age, 2U);         /* +3 */
            n = sos_put(b, n, s->station_abs_loc, 4U);                      /* +5 */
            n = sos_put(b, n, (uint32_t)sos_dm(s->sos_distance), 4U);       /* +9 */
        }
    }

    UDP_SendCapturePkt(LOG_PKT_SOS, b, n);
}

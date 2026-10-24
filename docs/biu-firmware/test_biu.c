/* Host test for brake_pressure_interface_user.c: build with stand-in
 * common_include.h / enet_user.h. Frame bytes are worked from RDSO
 * MP.0.01.00.31 Rev.05 Annexure-1 and the values from Annexure-4. */
#include "common_include.h"
#include "brake_pressure_interface_user.h"
#include "enet_user.h"
#include <string.h>
#include <math.h>

LocoParams loco_params;
uint64_t fake_usec = 1000000;
static uint8_t sent[64]; static uint16_t sent_len, sent_port; static uint32_t sent_ip; static int sends;
static uint8_t last_hb[16]; static int heartbeats;
ip_addr_t set_pc_ipAddr(ipAddress a) { ip_addr_t r = { ((uint32_t)a.ip_addr0 << 24) | ((uint32_t)a.ip_addr1 << 16) | ((uint32_t)a.ip_addr2 << 8) | a.ip_addr3 }; return r; }
void UDP_Send(const char *b, ip_addr_t *ip, uint16_t port, uint16_t len)
{
    if ((uint8_t)b[0] == 0x20 && (uint8_t)b[1] == 0x07) { memcpy(last_hb, b, len); ++heartbeats; return; }
    memcpy(sent, b, len); sent_len = len; sent_port = port; sent_ip = ip->addr; ++sends;
}

static int fails, checks;
#define CHECK(c, what) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s (line %d)\n", what, __LINE__); } } while (0)
static void advance_s(double s) { fake_usec += (uint64_t)(s * 1e6); }
static void frame(uint16_t cob, const uint8_t *d, uint8_t n) { uint8_t b[11] = { (uint8_t)cob, (uint8_t)(cob >> 8), (uint8_t)(3 + n) }; memcpy(b + 3, d, n); process_biu_reply(b, (uint16_t)(3 + n)); }
static void nmt(uint8_t cs, uint8_t node) { const uint8_t d[2] = { cs, node }; frame(0x000, d, 2); }

int main(void)
{
    loco_params.sensor_speed = 10.0f; loco_params.speed_ok = 1; loco_params.loco_health_status = 1;

    /* ---- network management ---- */
    biu_link_init();
    CHECK(heartbeats == 1 && last_hb[2] == 4 && last_hb[3] == 0x00, "boot-up: 0x720 [00], 1 data byte, length byte 4");
    CHECK(biu_nmt_state() == 0x7F, "then pre-operational");
    send_biu_command(LOCO_WAP7, TRAIN_MODE_FORMATION, NORMAL_BRAKE);
    CHECK(sends == 0, "pre-operational: no command frame");
    advance_s(0.3); biu_link_tick();
    CHECK(heartbeats == 1, "no heartbeat before 500 ms");
    advance_s(0.25); biu_link_tick();
    CHECK(heartbeats == 2 && last_hb[3] == 0x7F, "heartbeat at 500 ms says pre-operational (7F)");
    nmt(0x01, 0x30);
    CHECK(biu_nmt_state() == 0x7F, "start for another node (0x30) ignored");
    nmt(0x01, 0x20);
    CHECK(biu_nmt_state() == 0x05, "start remote node 0x20: operational");
    advance_s(0.5); biu_link_tick();
    CHECK(last_hb[3] == 0x05, "heartbeat says operational (05)");
    CHECK(!biu_link_up(), "BIU link not up before its heartbeat");
    { const uint8_t hb = 0x05; frame(0x710, &hb, 1); }
    CHECK(biu_link_up(), "BIU heartbeat operational: link up");
    advance_s(1.6);
    CHECK(!biu_link_up(), "BIU heartbeat 1.6 s old: link down");
    { const uint8_t hb = 0x05; frame(0x710, &hb, 1); }

    /* ---- command frame ---- */
    send_biu_command(LOCO_WAP7, TRAIN_MODE_FORMATION, NORMAL_BRAKE);
    CHECK(sends == 1 && sent_len == 11 && sent[2] == 11, "operational: 11-byte gateway frame, length byte 11");
    CHECK(sent[0] == 0x90 && sent[1] == 0x01, "COB-ID 0x190 = 0x180 + BIU node 0x10");
    CHECK(sent_port == 51234 && sent_ip == 0xC0A8197Eu, "to 192.168.25.126:51234");
    CHECK(sent[3] == 0xE8 && sent[4] == 0x03, "10.00 m/s = 1000 (0x03E8), lower byte first");
    CHECK(sent[7] == 88, "NB A9 4.4 kg/cm2 -> 88 (0.05/bit)");
    CHECK(sent[8] == 24, "NB SA9 1.2 kg/cm2 -> 24 (Annexure-4 8.1, 9.1)");
    CHECK(sent[5] == (0x02 | 0x10), "D1: health + speed valid; dynamic braking 0, light engine 0");
    CHECK(sent[6] == (0x04 | 0x08), "D2: A9 and SA9 valid, no acks");
    CHECK(sent[9] == 0 && sent[10] == 0, "spare bytes 0");
    loco_params.sensor_speed = 12.345678f;
    send_biu_command(LOCO_WAP7, TRAIN_MODE_FORMATION, NORMAL_BRAKE);
    CHECK(sent[3] == 0xD3 && sent[4] == 0x04, "12.345678 m/s -> 1235 (0x04D3), rounded");
    loco_params.sensor_speed = 10.0f;

    send_biu_command(LOCO_WAP7, TRAIN_MODE_FORMATION, NO_BRAKE_T);
    CHECK(sent[7] == 100 && sent[8] == 0, "no brake: BP 5.0 -> 100, BC 0");
    send_biu_command(LOCO_WAP7, TRAIN_MODE_FORMATION, FULL_SERVICE_BRAKE);
    CHECK(sent[7] == 70 && sent[8] == 48, "FSB formation: BP 3.5 -> 70, BC 2.4 -> 48 (A4 9.8)");
    send_biu_command(LOCO_WAP7, TRAIN_MODE_LEM, FULL_SERVICE_BRAKE);
    CHECK(sent[8] == 70, "FSB light engine: BC 3.5 -> 70 (A4 8.6)");
    send_biu_command(LOCO_WAP7, TRAIN_MODE_FORMATION, EMERGENCY_BRAKE);
    CHECK(sent[7] == 0 && sent[8] == 48, "EB formation: BP 0, BC 2.4");
    send_biu_command(LOCO_WAP7, TRAIN_MODE_LEM, LIGHT_ENGINE_BRAKE);
    CHECK(sent[7] == 100 && sent[8] == 70, "LE brake: BP left at 5.0, BC 3.5 (SA9 full application)");
    send_biu_command(LOCO_WAP5, TRAIN_MODE_LEM, FULL_SERVICE_BRAKE);
    CHECK((sent[6] & 0x0C) == 0x0C && sent[8] == 70, "WAP5 FSB: BC 3.5, both valid");

    send_biu_command(7, TRAIN_MODE_LEM, NORMAL_BRAKE);
    CHECK(sent[7] == 0 && (sent[6] & 0x0C) == 0x04, "no table row: BP 0 valid, BC not valid");
    CHECK(check_brake_malfunction(CMD_BP, 5.0f) == BRAKE_CHECK_NO_SPEC, "no table row: NO_SPEC, not OK");
    CHECK(validate_brake_pressure(7, 0, NORMAL_BRAKE, CMD_BP, 0.0f) == 0, "no table row: validate fails, even for 0.0");
    CHECK(validate_brake_pressure(LOCO_WAG9, 0, 9, CMD_BP, 5.0f) == 0, "brake type 9: no row");

    loco_params.speed_ok = 0;
    send_biu_command(LOCO_WAP7, TRAIN_MODE_LEM, NO_BRAKE_T);
    CHECK((sent[5] & 0x10) == 0 && (sent[5] & 0x80) == 0, "speed invalid, no dynamic braking");
    loco_params.speed_ok = 1;

    /* ---- settling, pressure by pressure ---- */
    send_biu_command(LOCO_WAG9, TRAIN_MODE_FORMATION, NO_BRAKE_T);
    advance_s(10);
    send_biu_command(LOCO_WAG9, TRAIN_MODE_FORMATION, EMERGENCY_BRAKE);
    advance_s(20);
    CHECK(check_brake_malfunction(CMD_BP, 2.0f) == BRAKE_CHECK_WAIT, "EB after 20 s of 24: wait");
    advance_s(5);
    CHECK(check_brake_malfunction(CMD_BP, 2.0f) == BRAKE_CHECK_MALFUNCTION, "EB after 25 s, BP 2.0: malfunction");
    CHECK(check_brake_malfunction(CMD_BP, 0.1f) == BRAKE_CHECK_OK, "EB after 25 s, BP 0.1: ok");
    send_biu_command(LOCO_WAG9, TRAIN_MODE_FORMATION, NO_BRAKE_T);
    advance_s(6);
    CHECK(check_brake_malfunction(CMD_BP, 3.0f) == BRAKE_CHECK_WAIT, "released 6 s ago: wait for EB's 60 s release");
    advance_s(55);
    CHECK(check_brake_malfunction(CMD_BP, 3.0f) == BRAKE_CHECK_MALFUNCTION, "61 s after release, BP 3.0: malfunction");
    CHECK(check_brake_malfunction(CMD_BP, 5.0f) == BRAKE_CHECK_OK, "61 s after release, BP 5.0: ok");
    send_biu_command(LOCO_WAG9, TRAIN_MODE_FORMATION, NO_BRAKE_T);
    CHECK(check_brake_malfunction(CMD_BP, 5.0f) == BRAKE_CHECK_OK, "resending the same command does not restart the clock");
    CHECK(check_brake_malfunction(CMD_MR, 9.0f) == BRAKE_CHECK_OK && check_brake_malfunction(CMD_MR, 7.0f) == BRAKE_CHECK_MALFUNCTION, "MR 8-10");

    /* LE -> NB: BP falls (NB apply 18 s), BC falls 3.5 -> 1.2 (LE release 60 s). */
    send_biu_command(LOCO_WAG9, TRAIN_MODE_FORMATION, LIGHT_ENGINE_BRAKE);
    advance_s(30);
    send_biu_command(LOCO_WAG9, TRAIN_MODE_FORMATION, NORMAL_BRAKE);
    advance_s(20);
    CHECK(check_brake_malfunction(CMD_BP, 4.4f) == BRAKE_CHECK_OK, "LE -> NB, 20 s: BP judged (NB apply 18 s)");
    CHECK(check_brake_malfunction(CMD_BC, 3.0f) == BRAKE_CHECK_WAIT, "LE -> NB, 20 s: BC still releasing (LE release 60 s)");
    advance_s(41);
    CHECK(check_brake_malfunction(CMD_BC, 1.2f) == BRAKE_CHECK_OK, "LE -> NB, 61 s: BC 1.2 ok");
    CHECK(validate_brake_pressure(LOCO_WAG9, 1, FULL_SERVICE_BRAKE, CMD_BC, 2.4f) == 1, "validate FSB BC 2.4 (formation)");

    /* ---- status frames ---- */
    const uint8_t p[8] = { 100, 24, 180, 88, 24, 0, 0, 0x07 };
    frame(0x220, p, 8);
    const BiuStatus *st = biu_status_get();
    CHECK(fabsf(st->pressure[BIU_P_BP].kgcm2 - 5.0f) < 1e-4f && st->pressure[BIU_P_BP].valid, "BP 100 -> 5.0, valid");
    CHECK(fabsf(st->pressure[BIU_P_MR].kgcm2 - 9.0f) < 1e-4f && st->pressure[BIU_P_MR].valid, "MR 180 -> 9.0, valid");
    CHECK(!st->pressure[BIU_P_A9_REF].valid, "A9 ref not valid (bit 3 clear)");
    const uint8_t codes[8] = { 3, 0, 0x40, 0x10, 0x05, 0x20, 0, 0 };
    frame(0x320, codes, 8);
    CHECK(st->isolation_counter == 3 && st->fault_code == 0x1040 && st->display_code == 0x2005, "codes lower byte first");
    const uint8_t disc[8] = { 0x01, 0x04, 0xF0, 0, 0, 0, 0, 0 };
    frame(0x420, disc, 8);
    CHECK((st->discrete[0] & BIU_ST_D1_HEALTHY) && (st->discrete[1] & BIU_ST_D2_PANEL_TEST_PROGRESS), "discretes");

    send_biu_command(LOCO_WAG9, TRAIN_MODE_FORMATION, NO_BRAKE_T);
    CHECK((sent[6] & 0x03) == 0x03, "fault and display code acked on the next command");
    send_biu_command(LOCO_WAG9, TRAIN_MODE_FORMATION, NO_BRAKE_T);
    CHECK((sent[6] & 0x03) == 0, "for one cycle only");
    frame(0x320, codes, 8);
    send_biu_command(LOCO_WAG9, TRAIN_MODE_FORMATION, NO_BRAKE_T);
    CHECK((sent[6] & 0x03) == 0, "the same code again is not re-acked");
    const uint8_t next[8] = { 3, 0, 0x28, 0x10, 0, 0, 0, 0 };
    frame(0x320, next, 8);
    send_biu_command(LOCO_WAG9, TRAIN_MODE_FORMATION, NO_BRAKE_T);
    CHECK((sent[6] & 0x03) == 0x01, "a new fault code (0x1028) is acked; display 0 not");
    { const uint8_t bad[11] = { 0x20, 0x02, 12 }; CHECK(process_biu_reply(bad, 11) == 0, "length byte past the buffer refused"); }
    { const uint8_t shortp[6] = { 0x20, 0x02, 6, 1, 2, 3 }; CHECK(process_biu_reply(shortp, 6) == 0, "pressure frame with 3 bytes refused"); }

    /* ---- stop, reset ---- */
    nmt(0x02, 0x00);
    CHECK(biu_nmt_state() == 0x04, "stop (all nodes): stopped");
    int before = sends;
    send_biu_command(LOCO_WAG9, TRAIN_MODE_FORMATION, NO_BRAKE_T);
    CHECK(sends == before, "stopped: no command frame");
    int hb = heartbeats;
    nmt(0x81, 0x20);
    CHECK(heartbeats == hb + 1 && last_hb[3] == 0x00 && biu_nmt_state() == 0x7F, "reset node: boot-up again, pre-operational");

    /* ---- codec edges ---- */
    CHECK(biu_kgcm2_to_byte(-1.0f) == 0 && biu_kgcm2_to_byte(13.0f) == 255 && biu_kgcm2_to_byte(NAN) == 0, "clamped, NaN -> 0");
    CHECK(biu_kgcm2_to_byte(2.16f) == 43, "2.16 -> 43 (rounded)");
    PressureReading r = current_to_pressure(12.0f, 10.0f);
    CHECK(r.status == SENSE_OK && fabsf(r.value_kgcm2 - 5.1f) < 1e-4f, "12 mA on 10 bar = 5.1 kg/cm2");
    CHECK(current_to_pressure(3.0f, 10.0f).status == SENSE_FAULT_OPEN, "3 mA open");

    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}

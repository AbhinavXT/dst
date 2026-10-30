/* make_fixture.c -- writes schema/fixtures/biu_synthetic.log (session 420):
 * the BIU frames as the VCC copies them to DLConsole (@biu_cmd, @biu_beat,
 * @biu_nmt, @biu_beatrx, @biu_press, @biu_codes, @biu_disc; patch 419).
 *
 * The copies are the firmware module's own: docs/biu-firmware's
 * brake_pressure_interface_user.c runs on its host-test stand-ins, on the CAN
 * link, with biu_dlconsole_mirror(1), and UDP_SendCapturePkt here prints each
 * copy as utility.c words it ("@<token>_<loco>_<ctrl> <time> <seq> <hex>").
 * The VCC's side (the commands, its heartbeats) is therefore the firmware's;
 * the BIU's frames fed to it are spec-derived (Annexure-1 2.2, values from
 * Annexure-4), not captured. No real @biu_* line has been captured yet.
 *
 * Build and run from the repository root (deterministic: same lines every run):
 *   B=$(mktemp -d) && (cd docs/biu-firmware && cp *.c *.h $B) && cp tests/biugen/make_fixture.c $B &&
 *   (cd $B && mv test_common_include.h common_include.h && mv test_enet_user.h enet_user.h &&
 *    mv test_utility.h utility.h && rm test_biu.c &&
 *    gcc -std=c11 -Wall -Wextra -Werror -I. make_fixture.c brake_pressure_interface_user.c -lm -o gen) &&
 *   $B/gen schema/fixtures/biu_synthetic.log
 * (A file, not stdout: the firmware's SERIAL_PRINTF goes to stdout.)
 */
#include "common_include.h"
#include "brake_pressure_interface_user.h"
#include "enet_user.h"
#include "utility.h"
#include <stdio.h>
#include <string.h>

LocoParams loco_params;
uint64_t fake_usec = 1000000;

ip_addr_t set_pc_ipAddr(ipAddress address) { ip_addr_t packed = { address.ip_addr0 }; return packed; }
void UDP_Send(const char *buffer, ip_addr_t *ip_address, uint16_t port, uint16_t length)
{
    (void)buffer; (void)ip_address; (void)port; (void)length;
}
void test_can_send(uint16_t cob_id, const uint8_t *data, uint8_t length) { (void)cob_id; (void)data; (void)length; }

static unsigned s_seq = 4200;
static FILE *s_out;
void UDP_SendCapturePkt(LOG_PKT_TYPE type, const uint8_t *buff, uint16_t len)
{
    static const char *const tokens[] = { "biu_cmd", "biu_beat", "biu_nmt", "biu_beatrx", "biu_press", "biu_codes", "biu_disc" };
    if (type < LOG_PKT_BIU_CMD || type > LOG_PKT_BIU_DISC) return;
    const unsigned seconds = (unsigned)(fake_usec / 1000000U);
    fprintf(s_out, "@%s_1_1 2026-10-30T10:%02u:%02u %u", tokens[type - LOG_PKT_BIU_CMD], (seconds / 60U) % 60U, seconds % 60U, s_seq++);
    for (uint16_t index = 0; index < len; ++index) fprintf(s_out, " %02X", buff[index]);
    fprintf(s_out, "\n");
}

static void advance_ms(unsigned ms) { fake_usec += (uint64_t)ms * 1000U; }
static void biu(uint16_t cob_id, const uint8_t *data, uint8_t length) { process_biu_can_frame(cob_id, data, length); }

/* The BIU's status for a BP / BC, all valid, MR 9.0: as the bench sends it. */
static void biu_status(float bp, float bc, uint16_t fault_code, uint8_t discrete1)
{
    const uint8_t pressures[8] = { biu_kgcm2_to_byte(bp), biu_kgcm2_to_byte(bc), biu_kgcm2_to_byte(9.0f),
                                   biu_kgcm2_to_byte(bp), biu_kgcm2_to_byte(bc), 0, 0, 0x1F };
    const uint8_t codes[8] = { 0, 0, (uint8_t)(fault_code & 0xFFU), (uint8_t)(fault_code >> 8), 0, 0, 0, 0 };
    const uint8_t discretes[8] = { discrete1, 0x00, 0xF0, 0, 0, 0, 0, 0 };
    biu(0x220, pressures, 8);
    biu(0x320, codes, 8);
    biu(0x420, discretes, 8);
}

/* One second of the VCC's 250 ms cycle on this brake, the BIU answering every 500 ms. */
static void run_second(uint8_t loco, uint8_t mode, uint8_t brake, float bp, float bc, uint16_t fault_code, uint8_t discrete1)
{
    for (int cycle = 0; cycle < 4; ++cycle) {
        biu_link_tick();
        send_biu_command(loco, mode, brake);
        if (cycle % 2 == 1) {
            const uint8_t operational = 0x05;
            biu(0x710, &operational, 1);
            biu_status(bp, bc, fault_code, discrete1);
        }
        advance_ms(250);
    }
}

int main(int argument_count, char **arguments)
{
    if (argument_count != 2 || !(s_out = fopen(arguments[1], "w"))) {
        fprintf(stderr, "usage: gen schema/fixtures/biu_synthetic.log\n");
        return 2;
    }
    loco_params.sensor_speed = 10.0f; loco_params.speed_ok = 1; loco_params.loco_health_status = 1;
    loco_params.train_type = FORMATION_TYPE;
    biu_link_select(BIU_LINK_CAN);
    biu_dlconsole_mirror(1);

    biu_link_init();                                   /* boot-up 0x720 [00] */
    advance_ms(500); biu_link_tick();                  /* pre-operational [7F] */
    { const uint8_t start[2] = { 0x01, 0x20 }; biu(0x000, start, 2); }
    { const uint8_t other[2] = { 0x02, 0x30 }; biu(0x000, other, 2); }   /* for another node: copied, not taken */

    run_second(LOCO_WAP7, TRAIN_MODE_FORMATION, NO_BRAKE_T,         5.0f, 0.0f, 0, 0x21);
    run_second(LOCO_WAP7, TRAIN_MODE_FORMATION, NORMAL_BRAKE,       4.4f, 1.2f, 0, 0x21);
    run_second(LOCO_WAP7, TRAIN_MODE_FORMATION, FULL_SERVICE_BRAKE, 3.5f, 2.4f, 0, 0x21);
    run_second(LOCO_WAP7, TRAIN_MODE_FORMATION, EMERGENCY_BRAKE,    0.0f, 2.4f, 0, 0x21);
    run_second(LOCO_WAP7, TRAIN_MODE_FORMATION, NO_BRAKE_T,         5.0f, 0.0f, 0, 0x21);
    loco_params.train_type = LIGHT_ENGINE_TYPE;
    run_second(LOCO_WAP7, TRAIN_MODE_LEM,       LIGHT_ENGINE_BRAKE, 5.0f, 3.5f, 0, 0x21);
    loco_params.train_type = FORMATION_TYPE;
    loco_params.dmi_context_values.biu_isolated = 1;   /* KAVACH isolates the BIU */
    run_second(LOCO_WAP7, TRAIN_MODE_FORMATION, EMERGENCY_BRAKE,    5.0f, 0.0f, 0x1040, 0x29);
    loco_params.dmi_context_values.biu_isolated = 0;
    run_second(9, TRAIN_MODE_FORMATION, NORMAL_BRAKE,               0.0f, 0.0f, 0, 0x21);   /* no table row: BP 0, BC not valid */
    run_second(LOCO_WAP7, TRAIN_MODE_FORMATION, NO_BRAKE_T,         5.0f, 0.0f, 0, 0x21);
    return fclose(s_out) != 0;
}

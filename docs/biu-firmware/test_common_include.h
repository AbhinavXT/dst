/* Test stand-in for the VCC's common_include.h: only what the BIU module uses. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#define SERIAL_PRINTF(...) printf(__VA_ARGS__)
enum { NO_BRAKE_T = 0, OVERSPEED_T, NORMAL_BRAKE, FULL_SERVICE_BRAKE, EMERGENCY_BRAKE, LIGHT_ENGINE_BRAKE };
enum { FORMATION_TYPE = 0, LIGHT_ENGINE_TYPE = 1 };
typedef struct { uint8_t biu_isolated; } DmiContext;
typedef struct {
    float sensor_speed;  uint8_t speed_ok; uint8_t train_type; uint8_t mbt;
    uint8_t loco_health_status; DmiContext dmi_context_values;
} LocoParams;
extern LocoParams loco_params;
extern uint64_t fake_usec;
static inline uint64_t ClockP_getTimeUsec(void) { return fake_usec; }
#define BIU_SPEED_VALID()         (loco_params.speed_ok == 1)
void test_can_send(uint16_t cob_id, const uint8_t *data, uint8_t length);
#define BIU_CAN_SEND(cob_id, data, length) test_can_send(cob_id, data, length)

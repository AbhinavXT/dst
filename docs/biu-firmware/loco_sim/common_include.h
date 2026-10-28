/* Loco simulator stand-in for the VCC's common_include.h: only what the BIU
 * module uses, with a real clock. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#define SERIAL_PRINTF(...) printf(__VA_ARGS__)
enum { NO_BRAKE_T = 0, OVERSPEED_T, NORMAL_BRAKE, FULL_SERVICE_BRAKE, EMERGENCY_BRAKE, LIGHT_ENGINE_BRAKE };
enum { FORMATION_TYPE = 0, LIGHT_ENGINE_TYPE = 1 };
typedef struct { uint8_t biu_isolated; } DmiContext;
typedef struct {
    float   sensor_speed;          /* m/s */
    uint8_t speed_ok;
    uint8_t train_type;
    uint8_t mbt;
    uint8_t loco_health_status;
    DmiContext dmi_context_values;
} LocoParams;
extern LocoParams loco_params;
static inline uint64_t ClockP_getTimeUsec(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}
#define BIU_SPEED_VALID()  (loco_params.speed_ok == 1)

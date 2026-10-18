#ifndef OTA_BOOT_GATE_H
#define OTA_BOOT_GATE_H

#include <stdint.h>

/* AXISRAM1 retention bank. Agent startup reserves the first 256 bytes. */
#define OTA_GATE_ADDR       0x34000000u
#define OTA_GATE_MAGIC      0x3247544Fu
#define OTA_GATE_WINDOW     1u
#define OTA_GATE_RUN_APP    2u
#define OTA_GATE_WAIT_MS    10000u

static inline void ota_gate_write(uint32_t mode)
{
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)OTA_GATE_ADDR;
    p[0] = 0u;
    p[1] = mode;
    p[2] = ~mode;
    p[0] = OTA_GATE_MAGIC;
}

static inline uint32_t ota_gate_take(void)
{
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)OTA_GATE_ADDR;
    uint32_t mode = 0u;
    if ((p[0] == OTA_GATE_MAGIC) && (p[2] == ~p[1])) { mode = p[1]; }
    p[0] = 0u;
    return mode;
}

#endif

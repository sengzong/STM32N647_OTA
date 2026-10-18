#ifndef APP_INPUT_H
#define APP_INPUT_H

#include <stdint.h>
#include <stddef.h>
#include "stm32n6xx_hal.h"

#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 1)
#include <arm_mve.h>
#endif

/* Buffers and their lengths must be aligned to complete MCU cache lines. */
static __attribute__((noinline)) void app_prepare_input(uint8_t *buffer, uint32_t length)
{
    uint32_t i = 0;

    /* Camera DMA has finished; discard the CPU's view of the previous frame. */
    SCB_InvalidateDCache_by_Addr(buffer, (int32_t)length);
#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 1)
    const uint8x16_t sign_bit = vdupq_n_u8(0x80);
    for (; i + 16 <= length; i += 16)
    {
        vst1q_u8(buffer + i, veorq_u8(vld1q_u8(buffer + i), sign_bit));
    }
#endif
    for (; i < length; i++)
    {
        buffer[i] ^= 0x80u;
    }
    /* The NPU reads memory directly, so publish every converted input byte. */
    SCB_CleanDCache_by_Addr(buffer, (int32_t)length);
}

#endif

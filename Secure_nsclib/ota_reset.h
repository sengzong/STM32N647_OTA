#ifndef OTA_RESET_H
#define OTA_RESET_H

/* Inline only: callers execute from internal SRAM with interrupts disabled.
 * ES0620 2.2.22 requires the ROM peripheral clocks at their reset selections.
 * Both old and new clock sources must stay alive until SYSRESETREQ. */
static __attribute__((always_inline)) inline void ota_reset_prepare(void)
{
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->SR & RCC_SR_HSIRDY) == 0u) { }
    RCC->APB4ENSR2 = RCC_APB4ENSR2_SYSCFGENS;
    (void)RCC->APB4ENR2;
    SYSCFG->CM55RSTCR |= SYSCFG_CM55RSTCR_CORE_RESET_TYPE;
    (void)SYSCFG->CM55RSTCR;

    RCC->CCIPR6 = 0u;
    RCC->CCIPR7 = 0x200u;
    RCC->CCIPR8 = 0u;
    RCC->CCIPR13 = 0u;
    (void)RCC->CCIPR13;
    __DSB();
    __ISB();
}

#endif

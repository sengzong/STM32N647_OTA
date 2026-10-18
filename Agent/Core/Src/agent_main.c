/**
  ******************************************************************************
  * @file    agent_main.c
  * @brief   Resident OTA agent - entry point.
  *
  * WHERE THIS IMAGE LIVES
  *   NOR 0x1400000 .. 0x14FFC00 (partition reserved by the boot manager),
  *   image entry 0x71400400, EF 0x71400400.
  *
  * WHY IT EXISTS
  *   The boot area holds exactly one application. When a freshly copied image
  *   turns out to be unbootable the FSBL rolls the boot area back to the golden
  *   backup and - because a rollback happened - jumps HERE instead of to the
  *   application. A resident agent is the only way to keep the OTA reception
  *   path alive independently of which application image currently runs,
  *   because this image is never part of the OTA update cycle itself.
  *
  * WHAT IT MAY AND MAY NOT DO
  *   May  : bring up ETH + NetXDuo, receive an OFW2 container over TCP, write
  *          the image into one of the three repository slots, and ask the FSBL
  *          to copy that slot into the boot area (via the 32-byte BOOTREC
  *          record + reset).
  *   May not: erase or program the boot area itself. That branch of
  *          app_netxduo.c is compiled out here with -DLED2_OTA_AGENT, so the
  *          worst case is a bad slot - never a bad boot area.
  *
  * WHAT IS DELIBERATELY MISSING
  *   RGB LCD, touch controller, DMA2D, camera/DCMIPP and the framebuffer.
  *   The whole point of this image is to be reachable when the normal
  *   application does not start, so it stays as small and as boring as
  *   possible: GPIO (status LEDs), USART1 (log), ETH1 + PHY.
  *
  * NOTE: comments are ASCII-only on purpose (GBK console host).
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "string.h"

/* USER CODE BEGIN Includes */
/* Driver headers are picked up through the project include paths, which point
 * straight at Appli/Drivers/BSP/<driver> - the agent project links the .c files
 * but keeps the headers where they are. */
#include "yt8512c.h"                    /* Ethernet PHY (YT8512C)          */
#include "uart.h"                       /* printf retarget over USART1     */
#include "app_azure_rtos.h"             /* ThreadX: tx_application_define + tx_kernel_enter */
#include "app_ota_flash.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define AGENT_LOG_ALIVE_PERIOD_MS   500u    /* heartbeat LED toggle period, 4 Hz */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/* ETH DMA descriptors - the linker script pins these at absolute addresses
 * (.tcp_sec @0x341F8000 / .nx_sec @0x341F8180), so the arrays MUST stay in
 * these sections and keep the layout the HAL expects. */
ETH_DMADescTypeDef DMARxDscrTab[ETH_DMA_RX_CH_CNT][ETH_RX_DESC_CNT] __attribute__((section(".RxDecripSection")));
ETH_DMADescTypeDef DMATxDscrTab[ETH_DMA_TX_CH_CNT][ETH_TX_DESC_CNT] __attribute__((section(".TxDecripSection")));

ETH_TxPacketConfig TxConfig;

DCMIPP_HandleTypeDef hdcmipp;   /* referenced by stm32n6xx_it.c (shared source) */
ETH_HandleTypeDef    heth1;      /* referenced by nx_stm32_eth_config.h        */
UART_HandleTypeDef   huart1;     /* referenced by Drivers/BSP/UART/uart.c      */

/* USER CODE BEGIN PV */
volatile uint8_t g_rtos_started = 0;     /* SysTick_Handler gates _tx_timer_interrupt on this */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_ETH1_Init(void);
static void SystemIsolation_Config(void);

/* Private user code ---------------------------------------------------------*/

/**
  * @brief  The agent entry point.
  */
int main(void)
{
  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* D-Cache stays OFF. Two reasons:
   *   - the NOR XIP window is cacheable by default and every slot read-back the
   *     agent performs (ota_slot_refresh, ota_store_to_next_slot) is a fresh
   *     flash read; running uncached removes the whole class of stale-line bugs;
   *   - the SRAM write agent (ota_run_agent) has no cache maintenance in it. */
  SCB_DisableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Update SystemCoreClock variable according to RCC registers values. */
  SystemCoreClockUpdate();

  /* MPU_Config() is NOT called here, exactly like in the application: the
   * MPU_PRIVILEGED_DEFAULT background region would make the PSRAM mapped by the
   * FSBL unusable. Nothing in the agent needs the MPU. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* The FSBL already configured the PLLs and mapped both XSPI memories, so the
   * agent inherits its clocks and never calls sys_clock_config_debug(). */
  /* USER CODE END Init */

  /* Update SystemCoreClock variable */
  SystemCoreClockUpdate();

  /* USER CODE BEGIN SysInit */
  /* No MX_XSPI1_Init / MX_XSPI2_Init here: the agent is only ever started by the
   * FSBL, which leaves PSRAM (XSPI1) memory mapped at 0x90000000 and the NOR
   * (XSPI2) memory mapped at 0x70000000. Re-initialising XSPI2 would break the
   * very code that is executing - it is XIP. */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  SystemIsolation_Config();
  /* USER CODE BEGIN 2 */
  uart_init(115200);                      /* printf over USART1 (PE5/PE6, AF7) */
  ota_gateway_init();
  printf("\r\nSTM32N647 LED_2 - resident OTA AGENT  [AGENT-TARGET v2]\r\n");
  printf("NOR partition 0x%07lX, entry 0x%08lX\r\n", (unsigned long)OTA_LAYOUT_AGENT,
         (unsigned long)(0x70000000u + OTA_LAYOUT_AGENT + 0x400u));
#if defined(LED2_PACKAGE_COMPAT)
  {
    volatile uint32_t *first = (volatile uint32_t *)0x90000000u;
    volatile uint32_t *last = (volatile uint32_t *)(0x90000000u + OTA_LAYOUT_RX_CAPACITY - 4u);
    *first = 0x13579BDFu; *last = 0x2468ACE0u;
    __DSB();
    if (*first != 0x13579BDFu || *last != 0x2468ACE0u)
    {
      printf("AGENT: external RAM receive-buffer check failed\r\n");
      Error_Handler();
    }
    printf("AGENT: package layout, 10 MB external receive buffer ready\r\n");
  }
#endif
  printf("Role: receive + store slots only - this image never writes the boot area.\r\n");
  printf("OTA probe: 0x%08X (EXPECT 0x314F5441 = SRAM agent OK)\r\n", (unsigned)ota_probe_run());

  printf("ETH MAC init...\r\n");
  MX_ETH1_Init();                         /* ETH MAC initialisation */
  printf("Ethernet PHY init...\r\n");
  yt8512c_init();                         /* Ethernet PHY (YT8512C) bring-up */
  printf("ETH1 ready (HW)\r\n");

  printf("ThreadX booting...\r\n");
  g_rtos_started = 1;                     /* only now may SysTick_Handler feed ThreadX */
  tx_kernel_enter();                      /* never returns; link + OTA threads run */

  /* Infinite loop (unreachable) */
  while (1)
  {
  }
}

/**
  * @brief ETH1 Initialization Function.
  *        Same parameters as the application so the FSBL-side RIF attributes
  *        (master = secure privileged) keep matching.
  */
static void MX_ETH1_Init(void)
{
  static uint8_t MACAddr[6];

  heth1.Instance = ETH1;
  MACAddr[0] = 0x00;
  MACAddr[1] = 0x80;
  MACAddr[2] = 0xE1;
  MACAddr[3] = 0x00;
  MACAddr[4] = 0x00;
  MACAddr[5] = 0x00;
  heth1.Init.MACAddr = &MACAddr[0];
  heth1.Init.MediaInterface = HAL_ETH_RMII_MODE;
  for (int ch = 0; ch < ETH_DMA_CH_CNT; ch++)
  {
    heth1.Init.TxDesc[ch] = DMATxDscrTab[ch];
    heth1.Init.RxDesc[ch] = DMARxDscrTab[ch];
  }
  heth1.Init.RxBuffLen = 1524;

  if (HAL_ETH_Init(&heth1) != HAL_OK)
  {
    Error_Handler();
  }

  memset(&TxConfig, 0, sizeof(ETH_TxPacketConfig));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;
}

/**
  * @brief RIF (resource isolation) configuration.
  *        Kept identical to the application: ETH1 must be a secure privileged
  *        master or the DMA cannot reach its descriptors in internal SRAM.
  *        DCMIPP / DMA2D / LTDC entries are harmless here (unused peripherals).
  */
static void SystemIsolation_Config(void)
{
  __HAL_RCC_RIFSC_CLK_ENABLE();
  RIMC_MasterConfig_t RIMC_master = {0};
  RIMC_master.MasterCID = RIF_CID_1;
  RIMC_master.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;

  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DCMIPP, &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_DMA2D, &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_ETH1, &RIMC_master);
  HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_LTDC1, &RIMC_master);

  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_DCMIPP, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_ETH1, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);
  HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_LTDCL1, RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);

  HAL_PWR_ConfigAttributes(PWR_ITEM_0, PWR_SEC_NPRIV);

  /* GPIOs the agent actually drives, plus the ones the shared MSP file touches.
   * The application's full list is not needed: no LCD, no camera, no touch. */
  HAL_GPIO_ConfigPinAttributes(GPIOA, GPIO_PIN_3, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOD, GPIO_PIN_4, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOD, GPIO_PIN_14, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOE, GPIO_PIN_5, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOE, GPIO_PIN_6, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOE, GPIO_PIN_10, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOF, GPIO_PIN_4, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOF, GPIO_PIN_5, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOF, GPIO_PIN_7, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOF, GPIO_PIN_10, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOF, GPIO_PIN_11, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOF, GPIO_PIN_12, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOF, GPIO_PIN_13, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOF, GPIO_PIN_14, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOF, GPIO_PIN_15, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOG, GPIO_PIN_4, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOG, GPIO_PIN_6, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOG, GPIO_PIN_10, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOH, GPIO_PIN_5, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION, GPIO_PIN_0, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION, GPIO_PIN_1, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION, GPIO_PIN_2, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION, GPIO_PIN_3, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION, GPIO_PIN_4, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION, GPIO_PIN_5, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION, GPIO_PIN_6, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION, GPIO_PIN_8, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION, GPIO_PIN_9, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION, GPIO_PIN_10, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPION, GPIO_PIN_11, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOO, GPIO_PIN_0, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOO, GPIO_PIN_2, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOO, GPIO_PIN_4, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOO, GPIO_PIN_5, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP, GPIO_PIN_0, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP, GPIO_PIN_1, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP, GPIO_PIN_2, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP, GPIO_PIN_3, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP, GPIO_PIN_4, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP, GPIO_PIN_5, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP, GPIO_PIN_6, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
  HAL_GPIO_ConfigPinAttributes(GPIOP, GPIO_PIN_7, GPIO_PIN_SEC | GPIO_PIN_NPRIV);
}

/**
  * @brief USART1 Initialization Function (log output, 115200 8N1).
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function.
  *        Only the pins the agent touches: the two board LEDs and the PHY reset
  *        line. Trimmed down from the application's version on purpose - the
  *        LCD / camera / touch pins are left alone.
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable.
   * ALL of them, exactly like the application does - not because the agent uses
   * every port, but because SystemIsolation_Config() calls
   * HAL_GPIO_ConfigPinAttributes() on pins spread over ports A..P. That HAL
   * writes the per-pin RIF SECCFGR register, and a write to a GPIO whose clock
   * is gated is silently dropped. Without the clocks the agent would inherit
   * the reset-time (secure + privileged) pin attributes instead of the
   * non-secure ones the application relies on. */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOP_CLK_ENABLE();
  __HAL_RCC_GPIOO_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPION_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, GPIO_PIN_10, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_RESET);

  /*Configure GPIO pin : PE10 (LED1). Held ON: a steady LED1 + blinking LED0 is
   * the agent's "no error" signature. */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, GPIO_PIN_RESET);

  /*Configure GPIO pin : PG10 (LED0) - the supervisor thread toggles this one */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pin : PA3 (PHY reset) */
  GPIO_InitStruct.Pin = GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/**
  * @brief  Error handler: LED1 solid + LED0 slow blink, so a stalled agent is
  *         visible without a serial terminal attached.
  */
void Error_Handler(void)
{
  volatile uint32_t i;

  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, GPIO_PIN_RESET);   /* LED1 solid ON */
  for (;;)
  {
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_10, GPIO_PIN_RESET); /* LED0 slow blink */
    for (i = 0; i < 6000000; i++) { __NOP(); }
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_10, GPIO_PIN_SET);
    for (i = 0; i < 6000000; i++) { __NOP(); }
  }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
}
#endif /* USE_FULL_ASSERT */

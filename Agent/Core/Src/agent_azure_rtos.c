/**
  ******************************************************************************
  * @file    agent_azure_rtos.c
  * @brief   Azure RTOS (ThreadX) bring-up for the resident OTA agent.
  *
  * Differences vs. the application's Appli/Core/Src/app_azure_rtos.c:
  *   - app_threadx_init() is NOT called: that one creates the LCD/touch UI
  *     thread with touch installation controls.
  *   - in its place a supervisor thread owns the LVGL progress display. It is not a feature
  *     for its own sake: it gives the agent a visible heartbeat (LED0 plus a
  *     periodic slot dump) so an operator can tell at a glance whether the
  *     board is running the agent or the application, and the NetXDuo threads
  *     are created with TX_DONT_START until the link comes up.
  *
  * NOTE: comments are ASCII-only on purpose (GBK console host).
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "app_azure_rtos.h"
#include "app_netxduo.h"
#include "app_ota_flash.h"
#include "main.h"
#include "stdio.h"
#include "ota_screen.h"

#if (USE_STATIC_ALLOCATION == 1)
__ALIGN_BEGIN static UCHAR tx_byte_pool_buffer[TX_APP_MEM_POOL_SIZE] __ALIGN_END;
static TX_BYTE_POOL tx_app_byte_pool;
__ALIGN_BEGIN static UCHAR nx_byte_pool_buffer[NX_APP_MEM_POOL_SIZE] __attribute__((section(".NetXPoolSection"))) __ALIGN_END;
static TX_BYTE_POOL nx_app_byte_pool;
#endif

#define SUP_THREAD_STACK_SIZE   8192
#define SUP_THREAD_PRIORITY     24      /* lower priority than the OTA thread (10) */

#define SUP_LED_PERIOD_TICKS    5       /* ThreadX tick is 100 Hz -> 50 ms    */
#define SUP_LOG_PERIOD_TICKS    200     /* 200 * 50 ms = 10 s between dumps   */

static TX_THREAD sup_thread;

/* Heartbeat: LED0 on PG10. The application keeps this LED solid, so a slow
 * blink is an unambiguous "the agent is running" signal on the bench. */
static VOID sup_thread_entry(ULONG id)
{
    ULONG tick = 0u;
    UINT  i;

    (void)id;

    printf("AGENT: supervisor thread up, heartbeat on LED0 (PG10)\r\n");
    ota_screen_init();
    ota_screen_show();

    for (;;)
    {
        ota_gateway_poll();
        ota_screen_poll();
        HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_10);

        if ((tick % SUP_LOG_PERIOD_TICKS) == 0u)
        {
            ota_slot_refresh();
            printf("AGENT: state %s | ", g_net_stack_ready ? "online" : "net init...");
            for (i = 0u; i < OTA_SLOT_COUNT; i++)
            {
                if (g_slots[i].ready)
                    printf("Slot-%u %lu KB \"%.16s\"  ", i,
                           (unsigned long)(g_slots[i].img_len / 1024u), g_slots[i].desc);
                else
                    printf("Slot-%u empty  ", i);
            }
            printf("\r\n");
        }

        tick++;
        tx_thread_sleep(SUP_LED_PERIOD_TICKS);
    }
}

void tx_application_define(void *first_unused_memory)
{
#if (USE_STATIC_ALLOCATION == 1)
    UINT   status = TX_SUCCESS;
    UCHAR *stack;

    TX_PARAMETER_NOT_USED(first_unused_memory);

    if (tx_byte_pool_create(&tx_app_byte_pool, "ThreadX App Memory Pool",
                            tx_byte_pool_buffer, TX_APP_MEM_POOL_SIZE) != TX_SUCCESS)
    {
        Error_Handler();
    }

    /* Supervisor thread (replaces the application's UI thread). */
    if (tx_byte_allocate(&tx_app_byte_pool, (VOID **)&stack,
                         SUP_THREAD_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
    {
        Error_Handler();
    }
    if (tx_thread_create(&sup_thread, "Agent Supervisor", sup_thread_entry, 0,
                         stack, SUP_THREAD_STACK_SIZE,
                         SUP_THREAD_PRIORITY, SUP_THREAD_PRIORITY,
                         TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
    {
        Error_Handler();
    }

    if (tx_byte_pool_create(&nx_app_byte_pool, "NetXDuo App Memory Pool",
                            nx_byte_pool_buffer, NX_APP_MEM_POOL_SIZE) != TX_SUCCESS)
    {
        Error_Handler();
    }

    /* Builds the ETH/NetXDuo stack and the TCP OTA client (link + OTA threads). */
    status = app_netxduo_init((VOID *)&nx_app_byte_pool);
    if (status != TX_SUCCESS)
    {
        Error_Handler();
    }
#else
    TX_PARAMETER_NOT_USED(first_unused_memory);
#endif
}

/**
 ****************************************************************************************************
 * @file        app.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-13
 * @brief       app.c文件
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 * 
 * 实验平台:正点原子 N647开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 * 
 ****************************************************************************************************
 */

#include "app.h"
#include "app_config.h"
#include "app_utils.h"
#include "app_lcd.h"
#include "app_camera.h"
#include "app_bqueue.h"
#include "app_cpuload.h"
#include "app_postprocess.h"
#include <stdio.h>
#include "pca9685.h"
#include "gimbal_controller.h"
#if 0  /* 机械臂暂屏蔽 */
#include "arm_controller.h"
#include "ik_solver.h"
#endif
#include "command_protocol.h"
#if 0  /* 机械臂暂屏蔽 */
#include "vision_to_arm.h"
#endif
#include "wifi_esp8266.h"
#include "app_touch.h"
#include "tx_api.h"
#include "cmw_camera.h"
#include "ll_aton_runtime.h"

typedef struct {
    int32_t nb_detect;
    od_pp_outBuffer_t detects[AI_OBJDETECT_YOLOV8_PP_MAX_BOXES_LIMIT];
    uint32_t nn_period_ms;
    uint32_t inf_ms;
    uint32_t pp_ms;
    uint32_t disp_ms;
} app_display_info_t;

typedef struct {
    TX_SEMAPHORE update;
    TX_MUTEX lock;
    app_display_info_t info;
} app_display_t;

static TX_SEMAPHORE isp_semaphore;

static void app_camera_display_pipe_vsync_cb(void);
static void app_camera_display_pipe_frame_cb(void);
static void app_camera_nn_pipe_frame_cb(void);

static TX_THREAD nn_thread;
static UCHAR nn_thread_stack[4096];
static TX_THREAD pp_thread;
static UCHAR pp_thread_stack[4096];
static TX_THREAD dp_thread;
static UCHAR dp_thread_stack[4096];
static TX_THREAD isp_thread;
static UCHAR isp_thread_stack[4096];
#if 0
static TX_THREAD arm_thread;
static UCHAR arm_thread_stack[2048];
#endif

static VOID nn_thread_entry(ULONG id);
static VOID pp_thread_entry(ULONG id);
static VOID dp_thread_entry(ULONG id);
static VOID isp_thread_entry(ULONG id);
static VOID arm_thread_entry(ULONG id);

static app_display_t display;

LL_ATON_DECLARE_NAMED_NN_INSTANCE_AND_INTERFACE(Default);
/* Keep one buffer available while capture and inference overlap. */
static uint8_t nn_input_buffers[NN_INPUT_BUFFER_COUNT][NN_WIDTH * NN_HEIGHT * NN_BPP] __attribute__((aligned(32))) __attribute__((section(".EXTRAM")));
static app_bqueue_t nn_input_queue;
static uint8_t nn_output_buffers[2][ALIGN_VALUE(NN_BUFFER_OUT_SIZE, 32)] __attribute__((aligned(32)));
static app_bqueue_t nn_output_queue;
static const char *nn_classes_table[NN_CLASSES] = NN_CLASSES_TABLE;

static app_cpuload_t cpuload;

extern I2C_HandleTypeDef hi2c4;
extern UART_HandleTypeDef huart1;

/* ══ 禁用 printf 输出：强覆盖 syscalls.c 的弱 _write ══════ */
int _write(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    return len;
}

/* USART6 — ESP8266 WiFi */
static UART_HandleTypeDef huart6;
static TX_THREAD wifi_thread;
static UCHAR wifi_thread_stack[2048];
static VOID wifi_thread_entry(ULONG id);

static void MX_USART6_UART_Init(void)
{
    /* 时钟使能 */
    __HAL_RCC_USART6_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PC6(USART6_TX) PC9(USART6_RX) — AF7 */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF7_USART6;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* USART6 外设初始化 */
    huart6.Instance = USART6;
    huart6.Init.BaudRate = 115200;
    huart6.Init.WordLength = UART_WORDLENGTH_8B;
    huart6.Init.StopBits = UART_STOPBITS_1;
    huart6.Init.Parity = UART_PARITY_NONE;
    huart6.Init.Mode = UART_MODE_TX_RX;
    huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart6.Init.OverSampling = UART_OVERSAMPLING_16;
    huart6.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart6.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart6.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    HAL_UART_Init(&huart6);
    HAL_UARTEx_DisableFifoMode(&huart6);

    /* 中断 */
    HAL_NVIC_SetPriority(USART6_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART6_IRQn);
    __HAL_UART_ENABLE_IT(&huart6, UART_IT_RXNE);
}

void USART6_IRQHandler(void)
{
    uint32_t sr = huart6.Instance->ISR;
    if (sr & USART_ISR_RXNE) {
        uint8_t ch = (uint8_t)(huart6.Instance->RDR & 0xFF);
        WiFi_ISR(ch);
    }
    if (sr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        huart6.Instance->RDR;
        huart6.Instance->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    }
}

/* USART1 — 仅发送云台角度 JSON，RX 已禁用 */

static void app_display_network_output(app_display_info_t *display_info);

void app_run(void)
{
    uint8_t *nn_input_ptrs[NN_INPUT_BUFFER_COUNT];
    uint32_t i;

    for (i = 0; i < NN_INPUT_BUFFER_COUNT; i++)
    {
        nn_input_ptrs[i] = nn_input_buffers[i];
    }
    app_lcd_init();
    app_bqueue_init(&nn_input_queue, NN_INPUT_BUFFER_COUNT, nn_input_ptrs);
    app_bqueue_init(&nn_output_queue, 2, (uint8_t *[2]){nn_output_buffers[0], nn_output_buffers[1]});
    app_cpuload_init(&cpuload);
    app_camera_init(app_camera_display_pipe_vsync_cb, app_camera_display_pipe_frame_cb, NULL, app_camera_nn_pipe_frame_cb);

    tx_semaphore_create(&isp_semaphore, NULL, 0);
    tx_semaphore_create(&display.update, NULL, 0);
    tx_mutex_create(&display.lock, NULL, TX_INHERIT);

    app_camera_display_pipe_start(app_lcd_get_bg_buffer(), CMW_MODE_CONTINUOUS);

    PCA9685_Init(&hi2c4);
    GimbalController_Init(&hi2c4);
    AppTouch_Init();

#if 0  /* 机械臂暂屏蔽 */
    ArmController_Init(&hi2c4);
#endif

    /* USART1 — printf 已禁用，RX 已禁用，仅 dp_thread 发送角度 JSON */

    /* USART6 + ESP8266 WiFi */
    MX_USART6_UART_Init();
    WiFi_Init(&huart6);

    /* 云台测试已禁用 — 上电即进入追踪模式 */

    tx_thread_create(&nn_thread, "NN Thread", nn_thread_entry, 0, nn_thread_stack, sizeof(nn_thread_stack), TX_MAX_PRIORITIES - 3, TX_MAX_PRIORITIES - 3, 10, TX_AUTO_START);
    tx_thread_create(&pp_thread, "PP Thread", pp_thread_entry, 0, pp_thread_stack, sizeof(pp_thread_stack), TX_MAX_PRIORITIES - 2, TX_MAX_PRIORITIES - 2, 10, TX_AUTO_START);
    tx_thread_create(&dp_thread, "DP Thread", dp_thread_entry, 0, dp_thread_stack, sizeof(dp_thread_stack), TX_MAX_PRIORITIES - 2, TX_MAX_PRIORITIES - 2, 10, TX_AUTO_START);
    tx_thread_create(&isp_thread, "ISP Thread", isp_thread_entry, 0, isp_thread_stack, sizeof(isp_thread_stack), TX_MAX_PRIORITIES - 4, TX_MAX_PRIORITIES - 4, 10, TX_AUTO_START);
#if 0  /* arm_thread 暂屏蔽 */
    tx_thread_create(&arm_thread, "ARM Thread", arm_thread_entry, 0, arm_thread_stack, sizeof(arm_thread_stack), TX_MAX_PRIORITIES - 3, TX_MAX_PRIORITIES - 3, 10, TX_AUTO_START);
#endif
    tx_thread_create(&wifi_thread, "WiFi Thread", wifi_thread_entry, 0, wifi_thread_stack, sizeof(wifi_thread_stack), TX_MAX_PRIORITIES - 4, TX_MAX_PRIORITIES - 4, 10, TX_AUTO_START);
}

static void app_camera_display_pipe_vsync_cb(void)
{
    tx_semaphore_put(&isp_semaphore);
}

static void app_camera_display_pipe_frame_cb(void)
{
    app_lcd_switch_bg_buffer();
    app_camera_display_pipe_set_address(app_lcd_get_bg_buffer());
}

static void app_camera_nn_pipe_frame_cb(void)
{
    uint8_t *buffer;

    buffer = app_bqueue_get_free(&nn_input_queue, 0);
    if (buffer != NULL)
    {
        app_camera_nn_pipe_set_address(buffer);
        app_bqueue_put_ready(&nn_input_queue);
    }
}

static VOID nn_thread_entry(ULONG id)
{
    uint32_t nn_out_len;
    uint32_t nn_in_len;
    uint8_t *nn_pipe_dst;
    uint8_t *capture_buffer;
    uint8_t *output_buffer;
    uint32_t nn_period[2];
    uint32_t nn_period_ms;
    uint32_t time_stamp;
    uint32_t inf_ms;
    uint32_t *capture_buffer_u32;
    uint32_t nn_in_words;
    uint32_t i;

    nn_in_len = LL_Buffer_len(LL_ATON_Input_Buffers_Info_Default());
    nn_out_len = LL_Buffer_len(LL_ATON_Output_Buffers_Info_Default());

    nn_period[1] = HAL_GetTick();

    nn_pipe_dst = app_bqueue_get_free(&nn_input_queue, 0);

    app_camera_nn_pipe_start(nn_pipe_dst, CMW_MODE_CONTINUOUS);

    while (1)
    {
        nn_period[0] = nn_period[1];
        nn_period[1] = HAL_GetTick();
        nn_period_ms = nn_period[1] - nn_period[0];

        capture_buffer = app_bqueue_get_ready(&nn_input_queue);
        output_buffer = app_bqueue_get_free(&nn_output_queue, 1);

        time_stamp = HAL_GetTick();

        /* Normalize uint8 [0,255] camera data to int8 [-128,127] for model input */
        /* XOR with 0x80 flips MSB of each byte: uint8->int8 in one word op */
        capture_buffer_u32 = (uint32_t *)capture_buffer;
        nn_in_words = nn_in_len / 4;
        for (i = 0; i < nn_in_words; i++)
        {
            capture_buffer_u32[i] ^= 0x80808080u;
        }

        LL_ATON_Set_User_Input_Buffer_Default(0, capture_buffer, nn_in_len);
        SCB_InvalidateDCache_by_Addr(output_buffer, nn_out_len);
        LL_ATON_Set_User_Output_Buffer_Default(0, output_buffer, nn_out_len);
        LL_ATON_RT_Main(&NN_Instance_Default);
        inf_ms = HAL_GetTick() - time_stamp;

        app_bqueue_put_free(&nn_input_queue);
        app_bqueue_put_ready(&nn_output_queue);

        tx_mutex_get(&display.lock, TX_WAIT_FOREVER);
        display.info.inf_ms = inf_ms;
        display.info.nn_period_ms = nn_period_ms;
        tx_mutex_put(&display.lock);
    }
}

static VOID pp_thread_entry(ULONG id)
{
    yolov8_pp_static_param_t pp_params;
    uint8_t *output_buffer;
    od_pp_out_t pp_output;
    uint32_t nn_pp[2];
    int32_t i;

    app_postprocess_init(&pp_params);

    while (1)
    {
        output_buffer = app_bqueue_get_ready(&nn_output_queue);
        pp_output.pOutBuff = NULL;

        nn_pp[0] = HAL_GetTick();
        app_postprocess_run((void *[]){(void *)output_buffer}, 1, &pp_output, &pp_params);
        nn_pp[1] = HAL_GetTick();

        tx_mutex_get(&display.lock, TX_WAIT_FOREVER);
        display.info.nb_detect = pp_output.nb_detect;
        for (i = 0; i < pp_output.nb_detect; i++)
        {
            display.info.detects[i] = pp_output.pOutBuff[i];
        }
        display.info.pp_ms = nn_pp[1] - nn_pp[0];
        tx_mutex_put(&display.lock);
        app_bqueue_put_free(&nn_output_queue);
        tx_semaphore_ceiling_put(&display.update, 1);
    }
}

static VOID dp_thread_entry(ULONG id)
{
    uint32_t disp_ms = 0;
    app_display_info_t display_info;
    uint32_t time_stamp;

    /* ══ 云台归中 ══ */
    static int test_done = 0;
    if (!test_done) {
        GimbalController_SetAngles(90.0f, 90.0f);
        test_done = 1;
    }

    while (1)
    {
        tx_semaphore_get(&display.update, TX_WAIT_FOREVER);
        tx_mutex_get(&display.lock, TX_WAIT_FOREVER);
        display_info = display.info;
        tx_mutex_put(&display.lock);
        display_info.disp_ms = disp_ms;

        AppTouch_Poll();
        if (!AppTouch_IsManual())
            GimbalController_Update(display_info.nb_detect, display_info.detects);

        /* ── USART1 发送云台角度 JSON（每帧） ── */
        {
            float p, t;
            GimbalController_GetAngles(&p, &t);
            t -= 13.0f;  /* 竖直方向补偿 -13° */

            /* 检查是否检测到 pest (class_index=1) */
            int pest = 0;
            for (int32_t k = 0; k < display_info.nb_detect; k++) {
                if (display_info.detects[k].class_index == 1) { pest = 1; break; }
            }

            char buf[64];
            int n = snprintf(buf, sizeof(buf),
                             "{\"pan\":%.1f,\"tilt\":%.1f,\"pest\":%d}\r\n", p, t, pest);
            HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 10);
        }

        /* 发布视觉数据到共享区，供 WiFi 回传和自动抓取使用 */
        {
            uint16_t px_u, px_v;
            if (GimbalController_IsTracking() &&
                GimbalController_GetTargetPos(&px_u, &px_v)) {
                Cmd_Vision_SetPixelPos(px_u, px_v);
            } else {
                Cmd_Vision_SetLost();
            }
        }

        time_stamp = HAL_GetTick();
        app_display_network_output(&display_info);
        disp_ms = HAL_GetTick() - time_stamp;
    }
}

/* ══ 自动抓取状态机 ═══════════════════════════════════════ */

typedef enum {
    AG_IDLE,
    AG_WAIT_STABLE,
    AG_GRAB,
    AG_COOLDOWN
} AutoGrabState;

#define AG_ENABLE         0       /* 自动抓取开关: 1=启用 0=关闭 */
#define AG_STABLE_THRESH  8       /* 连续居中帧数阈值 */
#define AG_CENTER_PX      12      /* 居中判定像素 (±12px) */
#define AG_COOLDOWN_MS    3000    /* 抓取后冷却时间 (ms) */

#if 0  /* arm_thread 暂屏蔽 */
/* 机械臂控制线程：独立运行，不阻塞显示/UART */
static VOID arm_thread_entry(ULONG id)
{
    static AutoGrabState ag_state = AG_IDLE;
    static int ag_stable_cnt = 0;
    static uint32_t ag_cooldown_tick = 0;
    static uint8_t ag_target_was_lost = 0;

    while (1) {
        CommandData cmd;
        CommandType evt = Cmd_Arm_Poll(&cmd);
        if (evt == CMD_NONE) {
            if (!AG_ENABLE) {
                tx_thread_sleep(5);
                continue;
            }
            /* ── 自动抓取状态机 ── */
            uint16_t u, v;
            uint8_t tracking = GimbalController_IsTracking();
            uint8_t has_target = GimbalController_GetTargetPos(&u, &v);

            int du = (int)u - NN_WIDTH / 2;
            int dv = (int)v - NN_HEIGHT / 2;
            uint8_t centered = has_target &&
                               (du > -AG_CENTER_PX) && (du < AG_CENTER_PX) &&
                               (dv > -AG_CENTER_PX) && (dv < AG_CENTER_PX);

            switch (ag_state) {
            case AG_IDLE:
                if (tracking && centered) {
                    ag_state = AG_WAIT_STABLE;
                    ag_stable_cnt = 1;
                }
                break;

            case AG_WAIT_STABLE:
                if (tracking && centered) {
                    ag_stable_cnt++;
                    if (ag_stable_cnt >= AG_STABLE_THRESH) {
                        ag_state = AG_GRAB;
                    }
                } else {
                    ag_state = AG_IDLE;
                    ag_stable_cnt = 0;
                }
                break;

            case AG_GRAB: {
                float gp, gt, wx, wy, jnt[5], grab[6];
                GimbalController_GetAngles(&gp, &gt);
                Vision_ConvertPanTilt(gp, gt, &wx, &wy);

                float arm_x = wx - BASE_OFFSET_X;
                float arm_y = wy - BASE_OFFSET_Y;
                float tgt_xyz[3] = {arm_x, arm_y, TARGET_Z};
                float geo_q[IK_JOINTS];
                if (!IKSolver_SolveGeo(tgt_xyz, -1.0f, geo_q)) {
                    printf("AUTOGRAB IK FAIL (%.0f,%.0f,%.0f)\r\n", arm_x, arm_y, tgt_xyz[2]);
                    ag_state = AG_COOLDOWN;
                    ag_cooldown_tick = HAL_GetTick();
                    break;
                }
                for (int k = 0; k < 5; k++) jnt[k] = geo_q[k];

                printf("AUTOGRAB table(%.0f,%.0f) arm(%.0f,%.0f,%.0f) -> J(%.0f,%.0f,%.0f,%.0f,%.0f)\r\n",
                       wx, wy, arm_x, arm_y, tgt_xyz[2], jnt[0], jnt[1], jnt[2], jnt[3], jnt[4]);
                ArmController_GoHome();
                for (int k = 0; k < 5; k++) grab[k] = jnt[k];
                grab[5] = 90;
                ArmController_MoveToJoints(grab);
                tx_thread_sleep(2000);  /* 停留 2 秒等机械臂稳定 */
                ArmController_GripperClose();
                tx_thread_sleep(300);
                ArmController_GoHome();
                ArmController_GripperOpen();
                printf("AUTOGRAB done\r\n");
                ag_state = AG_COOLDOWN;
                ag_cooldown_tick = HAL_GetTick();
                ag_target_was_lost = 0;
                break;
            }

            case AG_COOLDOWN:
                if (!tracking) ag_target_was_lost = 1;
                if (ag_target_was_lost &&
                    (HAL_GetTick() - ag_cooldown_tick) > AG_COOLDOWN_MS) {
                    ag_state = AG_IDLE;
                }
                break;
            }

            tx_thread_sleep(5);
            continue;
        }
        if (evt == CMD_A_HOME) {
            ArmController_GoHome();
        } else if (evt == CMD_A_JOINT) {
            ArmController_MoveToJoints(cmd.joint);
        } else if (evt == CMD_A_GRIP_OPEN) {
            ArmController_GripperOpen();
        } else if (evt == CMD_A_GRIP_CLOSE) {
            ArmController_GripperClose();
        } else if (evt == CMD_A_TST) {
            int j = (int)cmd.joint[0];
            float home[6] = {90,90,90,90,90,90};
            float cw[6]   = {90,90,90,90,90,90};
            float ccw[6]  = {90,90,90,90,90,90};
            cw[j]  = 45.0f;
            ccw[j] = 135.0f;
            printf("TST J%d: 90->45->90->135->90\r\n", j);
            ArmController_MoveToJoints(cw);
            ArmController_MoveToJoints(home);
            ArmController_MoveToJoints(ccw);
            ArmController_MoveToJoints(home);
            printf("TST J%d: done\r\n", j);
        } else if (evt == CMD_A_GRP) {
            ArmController_GripperMoveTo(cmd.joint[0]);
        } else if (evt == CMD_A_GRAB) {
            float gp, gt, wx, wy, jnt[5], grab[6];
            GimbalController_GetAngles(&gp, &gt);
            Vision_ConvertPanTilt(gp, gt, &wx, &wy);

            float arm_x = wx - BASE_OFFSET_X;
            float arm_y = wy - BASE_OFFSET_Y;
            float tgt_xyz[3] = {arm_x, arm_y, TARGET_Z};
            float geo_q[IK_JOINTS];
            if (IKSolver_SolveGeo(tgt_xyz, -1.0f, geo_q)) {
                for (int k = 0; k < 5; k++) jnt[k] = geo_q[k];
            } else {
                Vision_XYtoJoints(wx, wy, jnt);
            }

            printf("GRAB table(%.0f,%.0f) arm(%.0f,%.0f,%.0f) -> J(%.0f,%.0f,%.0f,%.0f,%.0f)\r\n",
                   wx, wy, arm_x, arm_y, tgt_xyz[2], jnt[0],jnt[1],jnt[2],jnt[3],jnt[4]);
            ArmController_GoHome();
            for (int k=0;k<5;k++) grab[k]=jnt[k];
            grab[5]=90;
            ArmController_MoveToJoints(grab);
            tx_thread_sleep(500);  /* 停留 500ms 等机械臂稳定 */
            ArmController_GripperClose();
            tx_thread_sleep(300);
            ArmController_GoHome();
            ArmController_GripperOpen();
            printf("GRAB done\r\n");
        } else if (evt == CMD_G_STAT) {
            float p, t;
            GimbalController_GetAngles(&p, &t);
            printf("G:%.1f %.1f\r\n", p, t);
        } else if (evt == CMD_A_POSE) {
            float q_now[6], q_ik[6];
            ArmController_GetAngles(q_now);

            /* 用户坐标系 → DH 坐标系（绕 Z 旋转 -90°）*/
            float dh_pose[6];
            dh_pose[0] = -cmd.pose[1];
            dh_pose[1] =  cmd.pose[0];
            dh_pose[2] =  cmd.pose[2];
            dh_pose[3] =  cmd.pose[3];
            dh_pose[4] =  cmd.pose[4];
            dh_pose[5] =  cmd.pose[5] - M_PI_2;

            printf("POSE usr=(%.0f,%.0f,%.0f) rpy=(%.1f,%.1f,%.1f)\r\n",
                   cmd.pose[0],cmd.pose[1],cmd.pose[2],
                   cmd.pose[3],cmd.pose[4],cmd.pose[5]);

            /* 几何法：仅 Z 补偿 */
            float geo_xyz[3] = {cmd.pose[0],
                                cmd.pose[1],
                                cmd.pose[2] - 65.0f};
            float geo_q[6];
            int has_geo = IKSolver_SolveGeo(geo_xyz, -1.0f, geo_q);

            if (has_geo) {
                printf("GEO -> J(%.1f %.1f %.1f %.1f %.1f)\r\n",
                       geo_q[0],geo_q[1],geo_q[2],geo_q[3],geo_q[4]);
                ArmController_MoveToJoints(geo_q);
            } else {
                printf("GEO FAIL: unreachable, stopped\r\n");
            }
        } else if (evt == CMD_A_FK) {
            float pose[6];
            IKSolver_Forward(cmd.joint, pose);
            printf("FK J(%.1f %.1f %.1f %.1f %.1f %.1f) ->\r\n",
                   cmd.joint[0],cmd.joint[1],cmd.joint[2],
                   cmd.joint[3],cmd.joint[4],cmd.joint[5]);
            printf("  pos=(%.1f,%.1f,%.1f) mm  rpy=(%.2f,%.2f,%.2f) rad\r\n",
                   pose[0],pose[1],pose[2],pose[3],pose[4],pose[5]);
        } else if (evt == CMD_A_STAT) {
            float q[6];
            ArmController_GetAngles(q);
            printf("J:%.1f %.1f %.1f %.1f %.1f %.1f\r\n",
                   q[0], q[1], q[2], q[3], q[4], q[5]);
        }
    }
}
#endif  /* arm_thread 暂屏蔽 */

/* WiFi 线程：驱动 ESP8266 状态机 + 周期状态回传 */
static VOID wifi_thread_entry(ULONG id)
{
    uint32_t last_status_ms = 0;
    while (1) {
        WiFi_Process();

        /* 每秒回传一次状态（手机已连接时） */
        uint32_t now = HAL_GetTick();
        if (WiFi_ClientConnected() && (now - last_status_ms) >= 1000) {
            last_status_ms = now;

            float pan, tilt;
            GimbalController_GetAngles(&pan, &tilt);

            /* 读取视觉数据 */
            uint16_t obj_u = 0, obj_v = 0;
            uint8_t obj_valid = 0;
            Cmd_Vision_GetPixel(&obj_u, &obj_v, &obj_valid);

            char buf[256];
            int n = snprintf(buf, sizeof(buf),
                "{\"p\":%.1f,\"t\":%.1f,"
                "\"vu\":%d,\"vv\":%d,\"vv\":%d}\r\n",
                pan, tilt,
                obj_u, obj_v, obj_valid);
            WiFi_Send(buf, (uint16_t)n);
        }
        tx_thread_sleep(10);
    }
}

static VOID isp_thread_entry(ULONG id)
{
    while (1)
    {
        tx_semaphore_get(&isp_semaphore, TX_WAIT_FOREVER);

        app_camera_isp_update();
    }
}

static uint8_t app_clamp_point(int32_t *x, int32_t *y)
{
    int32_t xi;
    int32_t yi;

    xi = *x;
    yi = *y;

    if (*x < 0)
    {
        *x = 0;
    }

    if (*y < 0)
    {
        *y = 0;
    }

    if (*x >= LCD_BG_WIDTH)
    {
        *x = LCD_BG_WIDTH - 1;
    }

    if (*y >= LCD_BG_HEIGHT)
    {
        *y = LCD_BG_HEIGHT - 1;
    }

    return (xi != *x) || (yi != *y);
}

static void app_display_detection(od_pp_outBuffer_t *detect, uint32_t color)
{
    int32_t xc;
    int32_t yc;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    int32_t w;
    int32_t h;

    xc = (int32_t)(LCD_BG_WIDTH * detect->x_center);
    yc = (int32_t)(LCD_BG_HEIGHT * detect->y_center);
    w = (int32_t)(LCD_BG_WIDTH * detect->width);
    h = (int32_t)(LCD_BG_HEIGHT * detect->height);

    x0 = xc - (w + 1) / 2;
    y0 = yc - (h + 1) / 2;
    x1 = xc + (w + 1) / 2;
    y1 = yc + (h + 1) / 2;

    app_clamp_point(&x0, &y0);
    app_clamp_point(&x1, &y1);

    UTIL_LCD_DrawRect(x0, y0, x1 - x0, y1 - y0, color);
    UTIL_LCDEx_PrintfAt(x0, y0, LEFT_MODE, nn_classes_table[detect->class_index]);
}

static void app_display_network_output(app_display_info_t *display_info)
{
    float cpuload_one_second;
    uint8_t line_nb = 0;
    int32_t i;

    app_lcd_draw_area_update();

    UTIL_LCD_FillRect(0, 0, LCD_FG_WIDTH, LCD_FG_HEIGHT, 0x00000000);

    app_cpuload_update(&cpuload);
    app_cpuload_get_info(&cpuload, NULL, &cpuload_one_second, NULL);

    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "CPU load");
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "%.1f%%", cpuload_one_second);
    line_nb += 2;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Inference");
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "%ums", display_info->inf_ms);
    line_nb += 2;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "FPS");
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "%.2f", 1000.0 / display_info->nn_period_ms);
    line_nb += 2;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "Blocks");
    line_nb += 1;
    UTIL_LCDEx_PrintfAt(0, LINE(line_nb), RIGHT_MODE, "%u", display_info->nb_detect);

    {
        int32_t tidx = GimbalController_GetTrackedIdx();

        for (i = 0; i < display_info->nb_detect; i++)
        {
            uint32_t color = (i == tidx) ? UTIL_LCD_COLOR_GREEN : UTIL_LCD_COLOR_RED;
            app_display_detection(&display_info->detects[i], color);
        }
    }

    app_lcd_draw_area_commit();
}
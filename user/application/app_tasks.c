/**
 * @file    app_tasks.c
 * @author  华畅
 * @brief   应用任务：触摸采集 / 界面绘制 / 心跳统计
 * @version 0.1
 * @date    2026-10-28
 *
 * @copyright Copyright (c) 2026
 *
 * 任务划分：
 *   TouchTask     : 等中断通知 -> 读 I2C -> 更新"最新触摸状态"
 *   UiTask        : LVGL 主循环（lv_timer_handler），界面由 LVGL 绘制
 *   HeartbeatTask : 每 5 秒打印堆余量、触摸中断数、栈余量
 */

#include "app_tasks.h"
#include "app_demo.h"       /* DEMO_POWER_TEST 宏 */
#include "app_power_test.h" /* App_PowerTest_Run */

#include "bsp_debug.h"
#include "bsp_lcd.h"
#include "bsp_touch.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"           /* uxTaskGetStackHighWaterMark */

#include "lvgl.h"            /* LVGL 核心 */
#include "lv_port_disp.h"    /* 显示适配 */
#include "lv_port_indev.h"   /* 触摸适配 */

/* 1 = 用 AI 生成的医疗监护仪界面；0 = 用按钮+开关测试界面 */
#define UI_USE_AI_DEMO          (1)

#if UI_USE_AI_DEMO
#include "AI_code.h"         /* user/modules/UI 里的界面 */
#endif

/* ---------------- 配置 ---------------- */
#define TOUCH_EVENT_FLAG        (0x01U)
#define TOUCH_WAIT_TIMEOUT_MS   (100U)
#if defined(configCHECK_FOR_STACK_OVERFLOW) && (configCHECK_FOR_STACK_OVERFLOW > 0)
#define TOUCH_TASK_STACK        (2048U)
#define UI_TASK_STACK           (4096U)
#define HEARTBEAT_TASK_STACK    (1024U)
#else
#define TOUCH_TASK_STACK        (2048U)
#define UI_TASK_STACK           (4096U)
#define HEARTBEAT_TASK_STACK    (1024U)
#endif
#define HEARTBEAT_PERIOD_MS     (5000U)

/* ---------------- 颜色与布局 ---------------- */
#define UI_COLOR_BG             (0x00101010U)
#define UI_COLOR_WHITE          (0x00FFFFFFU)
#define UI_COLOR_GREEN          (0x0000FF00U)
#define UI_COLOR_CYAN           (0x0000FFFFU)
#define UI_COLOR_RED            (0x00FF0000U)
#define UI_DOT_RADIUS           (3U)
#define UI_TITLE_X              (20U)
#define UI_JUMP_LIMIT           (200)

/* ---------------- 任务间对象 ---------------- */
static osThreadId_t       touch_task_id = 0;
static osThreadId_t       ui_task_id = 0;

/* ======================= 任务实现 ======================= */

/**
 * @brief  中断通知回调（在中断上下文中执行）
 */
static void app_touch_notify(void)
{
    if (touch_task_id != 0)
    {
        (void)osThreadFlagsSet(touch_task_id, TOUCH_EVENT_FLAG);
    }
}

/**
 * @brief  触摸采集任务
 */
static void App_TouchTask(void *argument)
{
    uint16_t x = 0U;
    uint16_t y = 0U;

    (void)argument;

    for (;;)
    {
        /* 等中断唤醒；超时也检查一次，防止漏掉中断 */
        (void)osThreadFlagsWait(TOUCH_EVENT_FLAG, osFlagsWaitAny, TOUCH_WAIT_TIMEOUT_MS);

        /* 读一次：内部会更新 touch_x / touch_y / 按下状态，
         * LVGL 的输入回调通过 BSP_TOUCH_GetLatest() 取用 */
        (void)BSP_TOUCH_ReadNow(&x, &y);
    }
}

/**
 * @brief  界面任务：LVGL 主循环
 *
 * UI_USE_AI_DEMO = 1 时加载 AI 生成的医疗监护仪界面（user/modules/UI），
 *                 0 时回到"按钮 + 开关"的最小测试界面。
 */
static void App_UiTask(void *argument)
{
#if UI_USE_AI_DEMO
    (void)argument;
#else
    lv_obj_t *btn;
    lv_obj_t *label;
    lv_obj_t *sw;

    (void)argument;
#endif

    /* ① 初始化 LVGL（必须在内核启动之后、任务上下文里） */
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

#if UI_USE_AI_DEMO
    /* ② AI 生成的监护仪界面 */
    (void)mm_create(lv_scr_act());
#else
    /* ② 测试界面：一个按钮 + 一个开关 */
    btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn, 240, 120);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -80);

    label = lv_label_create(btn);
    lv_label_set_text(label, "Touch Me!");
    lv_obj_center(label);

    sw = lv_switch_create(lv_scr_act());
    lv_obj_align(sw, LV_ALIGN_CENTER, 0, 80);
#endif

    /* ③ 主循环：处理刷新、动画、输入事件 */
    for (;;)
    {
        (void)lv_timer_handler();
        (void)osDelay(5U);
    }
}

/**
 * @brief  心跳任务：周期性打印运行状态
 */
static void App_HeartbeatTask(void *argument)
{
    (void)argument;

    for (;;)
    {
        (void)osDelay(HEARTBEAT_PERIOD_MS);

        Debug_Printf("alive %lu s | heap_free=%u | touch_irq=%lu | touch_stack=%u | ui_stack=%u\r\n",
                     (unsigned long)(HAL_GetTick() / 1000U),
                     (unsigned int)xPortGetFreeHeapSize(),
                     (unsigned long)BSP_TOUCH_IrqCount(),
                     (unsigned int)uxTaskGetStackHighWaterMark(touch_task_id),
                     (unsigned int)uxTaskGetStackHighWaterMark(ui_task_id));
    }
}

/* ======================= 创建入口 ======================= */

void App_CreateTasks(void)
{
    /* 调试模块（创建串口互斥锁） */
    Debug_Init();

#if DEMO_POWER_TEST
    /* ── 满载功耗测试模式 ──
     * 只创建一个 PowerTask，App_PowerTest_Run() 内部 while(1) 不返回。
     * 不创建 Touch / UI / Heartbeat（power test 不需要 LVGL 和触摸）。
     */
    const osThreadAttr_t power_attr =
    {
        .name       = "PowerTask",
        .priority   = osPriorityNormal,
        .stack_size = 4096,
    };
    (void)osThreadNew((osThreadFunc_t)App_PowerTest_Run, NULL, &power_attr);
    Debug_Printf("tasks created: PowerTask  heap_free=%u\r\n",
                 (unsigned int)xPortGetFreeHeapSize());
    return;
#endif

    /* ── 正常 UI 模式（触摸 + 界面 + 心跳） ── */
    {
        const osThreadAttr_t touch_attr =
        {
            .name       = "TouchTask",
            .priority   = osPriorityHigh,
            .stack_size = TOUCH_TASK_STACK,
        };
        const osThreadAttr_t ui_attr =
        {
            .name       = "UiTask",
            .priority   = osPriorityAboveNormal,
            .stack_size = UI_TASK_STACK,
        };
        const osThreadAttr_t heartbeat_attr =
        {
            .name       = "HeartbeatTask",
            .priority   = osPriorityLow,
            .stack_size = HEARTBEAT_TASK_STACK,
        };

        /* 触摸任务必须先创建，中断回调需要它的句柄 */
        touch_task_id = osThreadNew(App_TouchTask, NULL, &touch_attr);
        BSP_TOUCH_SetNotifyCallback(app_touch_notify);

        ui_task_id = osThreadNew(App_UiTask, NULL, &ui_attr);
        (void)osThreadNew(App_HeartbeatTask, NULL, &heartbeat_attr);
    }

    Debug_Printf("tasks created: touch/ ui/ heartbeat/  heap_free=%u\r\n",
                 (unsigned int)xPortGetFreeHeapSize());
}

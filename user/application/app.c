/**
 * @file    app.c
 * @author  华畅
 * @brief   应用层：上电自检与演示界面调度
 * @version 0.1
 * @date    2026-10-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "app.h"
#include "bsp_debug.h"
#include "app_selftest.h"
#include "app_power_test.h"

#include "bsp_lcd.h"
#include "bsp_sdram.h"
#include "bsp_touch.h"

/*===========================================================================
 * 运行模式
 *   0 = 正常运行（上电自检后启动 FreeRTOS，显示 LVGL 界面）
 *   1 = 满载功耗测试（App_Init 不返回，RTOS 不启动，CPU/FPU + SDRAM
 *       + DMA2D + GPIO + 全外设时钟 + 背光 100% 同时拉满，用于热测试）
 *===========================================================================*/
#define APP_POWER_TEST_MODE         (1U)



#define APP_COLOR_RED           (0x00FF0000U)
#define APP_COLOR_GREEN         (0x0000FF00U)
#define APP_COLOR_BLUE          (0x000000FFU)
#define APP_COLOR_WHITE         (0x00FFFFFFU)
#define APP_COLOR_BLACK         (0x00000000U)

#define APP_PANEL_RESET_HOLD_MS (50U)
#define APP_HEARTBEAT_MS        (10000U)

/**
 * @brief  SDRAM 自检（仅打印，供 App_Init 调用）
 *
 * @param  无
 * @return 无
 */
static void app_check_sdram(void)
{
    (void)App_SelfTest_Sdram();
}

/**
 * @brief  三种纯色全屏填充，验证 DMA2D + 图层格式 + stride
 *
 * @param  无
 * @return 无
 */
static void app_check_colors(void)
{
    LCD_Clear(APP_COLOR_RED);
    Debug_Printf("T1 clear RED ok\r\n");
    HAL_Delay(1200U);

    LCD_Clear(APP_COLOR_GREEN);
    Debug_Printf("T2 clear GREEN ok\r\n");
    HAL_Delay(1200U);

    LCD_Clear(APP_COLOR_BLUE);
    Debug_Printf("T3 clear BLUE ok\r\n");
    HAL_Delay(1200U);
}

void App_Init(void)
{
    Debug_Printf("\r\n--- boot: ARGB8888 layer ---\r\n");
    BSP_SDRAM_PrintConfig();        /* 打印 FMC SDCR/SDTR（实现在 bsp_sdram） */

    /* 打开 BusFault / MemManage 上报，出错时能定位 */
    SCB->SHCSR |= (SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_MEMFAULTENA_Msk);

    /* ① SDRAM 自检（会覆盖整个 16 MB，必须在点屏和绘图之前） */
    app_check_sdram();

    /* ② 点屏：释放屏复位 + 启动背光 PWM */
    HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_SET);
    HAL_Delay(APP_PANEL_RESET_HOLD_MS);
    BSP_LCD_Backlight_Init();                       /* 启动背光 PWM（实现在 bsp_lcd） */

    /* ③ 用 bsp_lcd 之前必须先设字体和颜色 */
    LCD_SetFont(&Font24);
    LCD_SetTextColor(APP_COLOR_WHITE);
    LCD_SetBackColor(APP_COLOR_BLACK);

    /* ④ 三色验证 */
    app_check_colors();

    /* ⑤ LTDC 寄存器 + CPU 直写像素自检 */
    App_SelfTest_PrintLtdc();
    App_SelfTest_CpuPixel();

    /* ⑥ 刷新率打印 */
    App_SelfTest_PrintTiming();

    /* ⑦ 触摸控制器复位与自检 */
    {
        uint8_t touch_result = BSP_TOUCH_Init();

        if (touch_result == 0U)
        {
            Debug_Printf("touch: OK (FT5426)\r\n");
        }
        else if (touch_result == 1U)
        {
            Debug_Printf("touch: no device on I2C3\r\n");
        }
        else
        {
            Debug_Printf("touch: wrong chip id\r\n");
        }
    }

    /* 到这里硬件就绪；界面由任务负责，本函数必须返回 */
#if (APP_POWER_TEST_MODE != 0U)
    /* ⑧ 满载功耗测试：进入后不返回，FreeRTOS 不会被启动 */
    Debug_Printf(">>> POWER TEST MODE: CPU+FPU / SDRAM / DMA2D / GPIO / all clocks / backlight 100%%\r\n");
    App_PowerTest_Run();
#endif
}

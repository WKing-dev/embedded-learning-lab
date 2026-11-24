/**
 * @file    app_demo.c
 * @author  华畅
 * @brief   上电演示：字体测试页 / 触摸画板 / 白底黑条闪烁测试
 * @version 0.1
 * @date    2026-10-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "app_demo.h"
#include "bsp_debug.h"

#include "bsp_lcd.h"
#include "bsp_touch.h"

#if DEMO_POWER_TEST
#include "app_power_test.h"
#endif

/* ---------------- 通用颜色 ---------------- */
#define DEMO_COLOR_BLACK        (0x00000000U)
#define DEMO_COLOR_WHITE        (0x00FFFFFFU)
#define DEMO_COLOR_RED          (0x00FF0000U)
#define DEMO_COLOR_GREEN        (0x0000FF00U)
#define DEMO_COLOR_BLUE         (0x000000FFU)
#define DEMO_COLOR_YELLOW       (0x00FFFF00U)
#define DEMO_COLOR_CYAN         (0x0000FFFFU)
#define DEMO_COLOR_MAGENTA      (0x00FF00FFU)
#define DEMO_COLOR_ORANGE       (0x00FF8000U)
#define DEMO_COLOR_GRAY         (0x00C0C0C0U)
#define DEMO_COLOR_DARK_GRAY    (0x00707070U)
#define DEMO_COLOR_BOARD_BG     (0x00101010U)

/* ---------------- 画板参数 ---------------- */
#define DEMO_TITLE_X            (20U)
#define DEMO_TOUCH_JUMP_LIMIT   (200)
#define DEMO_TOUCH_DOT_RADIUS   (3U)
#define DEMO_TOUCH_IRQ_PERIOD_MS  (5000U)
#define DEMO_TOUCH_RAW_PERIOD_MS  (1000U)

/* ---------------- 字体页布局 ---------------- */
#define DEMO_FONT_LINE_Y1       (50U)
#define DEMO_FONT_LINE_Y2       (90U)
#define DEMO_FONT_LINE_Y3       (122U)
#define DEMO_FONT_LINE_Y4       (150U)
#define DEMO_FONT_LINE_Y5       (176U)
#define DEMO_ASCII_TITLE_Y      (210U)
#define DEMO_ASCII_START_Y      (240U)
#define DEMO_ASCII_LINE_H       (22U)
#define DEMO_ASCII_COLS         (16U)
#define DEMO_ASCII_ROWS         (6U)
#define DEMO_ASCII_CODE_HINT_X  (300U)
#define DEMO_BAR_Y              (400U)
#define DEMO_BAR_H              (60U)
#define DEMO_BAR_W              (118U)
#define DEMO_BAR_STEP_X         (122U)
#define DEMO_BAR_START_X        (20U)
#define DEMO_BAR_COUNT          (8U)

/* ======================= 字体测试页 ======================= */

void App_Demo_FontPage(void)
{
    static const uint32_t bar_colors[DEMO_BAR_COUNT] =
    {
        DEMO_COLOR_WHITE, DEMO_COLOR_YELLOW, DEMO_COLOR_CYAN,  DEMO_COLOR_GREEN,
        DEMO_COLOR_MAGENTA, DEMO_COLOR_RED,  DEMO_COLOR_BLUE,  DEMO_COLOR_BLACK
    };
    char    line[DEMO_ASCII_COLS + 1U];
    uint8_t row = 0U;
    uint8_t col = 0U;
    uint8_t code = 0U;
    uint16_t y = 0U;

    LCD_SetBackColor(DEMO_COLOR_BLACK);
    LCD_Clear(DEMO_COLOR_BLACK);

    /* 标题 */
    LCD_SetFont(&Font24);
    LCD_SetTextColor(DEMO_COLOR_WHITE);
    LCD_DisplayStringAt(DEMO_TITLE_X, 8U, (uint8_t *)"FONT TEST  ARGB8888  1024x600", LEFT_MODE);

    /* 各字号对照 */
    LCD_SetFont(&Font24);
    LCD_SetTextColor(DEMO_COLOR_GREEN);
    LCD_DisplayStringAt(DEMO_TITLE_X, DEMO_FONT_LINE_Y1,
                        (uint8_t *)"Font24  ABCDEFG abcdefg 0123", LEFT_MODE);

    LCD_SetFont(&Font20);
    LCD_SetTextColor(DEMO_COLOR_YELLOW);
    LCD_DisplayStringAt(DEMO_TITLE_X, DEMO_FONT_LINE_Y2,
                        (uint8_t *)"Font20  ABCDEFG abcdefg 0123", LEFT_MODE);

    LCD_SetFont(&Font16);
    LCD_SetTextColor(DEMO_COLOR_CYAN);
    LCD_DisplayStringAt(DEMO_TITLE_X, DEMO_FONT_LINE_Y3,
                        (uint8_t *)"Font16  ABCDEFG abcdefg 0123", LEFT_MODE);

    LCD_SetFont(&Font12);
    LCD_SetTextColor(DEMO_COLOR_MAGENTA);
    LCD_DisplayStringAt(DEMO_TITLE_X, DEMO_FONT_LINE_Y4,
                        (uint8_t *)"Font12  ABCDEFG abcdefg 0123", LEFT_MODE);

    LCD_SetFont(&Font8);
    LCD_SetTextColor(DEMO_COLOR_ORANGE);
    LCD_DisplayStringAt(DEMO_TITLE_X, DEMO_FONT_LINE_Y5,
                        (uint8_t *)"Font8  ABCDEFG abcdefg 0123", LEFT_MODE);

    /* 全 ASCII 0x20 ~ 0x7E */
    LCD_SetFont(&Font16);
    LCD_SetTextColor(DEMO_COLOR_WHITE);
    LCD_DisplayStringAt(DEMO_TITLE_X, DEMO_ASCII_TITLE_Y,
                        (uint8_t *)"ASCII 0x20 - 0x7E:", LEFT_MODE);

    for (row = 0U; row < DEMO_ASCII_ROWS; row++)
    {
        for (col = 0U; col < DEMO_ASCII_COLS; col++)
        {
            code = (uint8_t)(0x20U + (row * DEMO_ASCII_COLS) + col);
            line[col] = ((code <= 0x7EU) ? (char)code : ' ');
        }
        line[DEMO_ASCII_COLS] = '\0';

        y = (uint16_t)(DEMO_ASCII_START_Y + (row * DEMO_ASCII_LINE_H));

        LCD_SetFont(&Font16);
        LCD_SetTextColor(DEMO_COLOR_GRAY);
        LCD_DisplayStringAt(DEMO_TITLE_X, y, (uint8_t *)line, LEFT_MODE);

        LCD_SetFont(&Font12);
        LCD_SetTextColor(DEMO_COLOR_DARK_GRAY);
        LCD_DisplayStringAt(DEMO_ASCII_CODE_HINT_X, y, (uint8_t *)"0123456789ABCDEF", LEFT_MODE);
    }

    /* 底部色条 */
    for (col = 0U; col < DEMO_BAR_COUNT; col++)
    {
        LCD_SetTextColor(bar_colors[col]);
        LCD_FillRect((uint16_t)(DEMO_BAR_START_X + (col * DEMO_BAR_STEP_X)),
                     DEMO_BAR_Y, DEMO_BAR_W, DEMO_BAR_H);
    }

    /* 外框与说明 */
    LCD_SetTextColor(DEMO_COLOR_WHITE);
    LCD_DrawRect(0U, 0U, (uint16_t)LCD_GetXSize(), (uint16_t)LCD_GetYSize());

    LCD_SetFont(&Font16);
    LCD_SetTextColor(DEMO_COLOR_YELLOW);
    LCD_DisplayStringAt(DEMO_TITLE_X, 480U,
                        (uint8_t *)"If the text above is readable, the font layer is OK.", LEFT_MODE);

    LCD_SetTextColor(DEMO_COLOR_CYAN);
    LCD_DisplayStringAt(DEMO_TITLE_X, 510U,
                        (uint8_t *)"Colors: white / yellow / cyan / green / magenta / red / blue / black",
                        LEFT_MODE);

    Debug_Printf("font page drawn\r\n");
}

/* ======================= 触摸画板 ======================= */

void App_Demo_TouchBoard(void)
{
    uint16_t last_x = 0xFFFFU;
    uint16_t last_y = 0xFFFFU;
    uint16_t x = 0U;
    uint16_t y = 0U;
    uint8_t  event = BSP_TOUCH_NONE;
    uint32_t last_irq_ms = HAL_GetTick();
    uint32_t last_raw_ms = HAL_GetTick();
    uint32_t last_printed_irq = 0xFFFFFFFFU;
    int32_t  diff_x = 0;
    int32_t  diff_y = 0;

    LCD_SetBackColor(DEMO_COLOR_BOARD_BG);
    LCD_Clear(DEMO_COLOR_BOARD_BG);

    LCD_SetFont(&Font24);
    LCD_SetTextColor(DEMO_COLOR_WHITE);
    LCD_DisplayStringAt(DEMO_TITLE_X, 10U,
                        (uint8_t *)"TOUCH BOARD  (EXTI driven)", LEFT_MODE);

    LCD_SetFont(&Font16);
    LCD_SetTextColor(DEMO_COLOR_GREEN);
    LCD_DisplayStringAt(DEMO_TITLE_X, 45U, (uint8_t *)"draw with your finger", LEFT_MODE);

    LCD_SetTextColor(DEMO_COLOR_CYAN);
    LCD_DisplayStringAt(DEMO_TITLE_X, 70U, (uint8_t *)"white frame = screen edge", LEFT_MODE);

    LCD_SetTextColor(DEMO_COLOR_WHITE);
    LCD_DrawRect(0U, 0U, (uint16_t)LCD_GetXSize(), (uint16_t)LCD_GetYSize());

    while (1)
    {
        event = BSP_TOUCH_Scan(&x, &y);

        if (event == BSP_TOUCH_DOWN)
        {
            if ((x != last_x) || (y != last_y))
            {
                LCD_SetTextColor(DEMO_COLOR_RED);

                if (last_x != 0xFFFFU)
                {
                    diff_x = ((int32_t)x) - ((int32_t)last_x);
                    diff_y = ((int32_t)y) - ((int32_t)last_y);

                    if ((diff_x > -DEMO_TOUCH_JUMP_LIMIT) && (diff_x < DEMO_TOUCH_JUMP_LIMIT) &&
                        (diff_y > -DEMO_TOUCH_JUMP_LIMIT) && (diff_y < DEMO_TOUCH_JUMP_LIMIT))
                    {
                        LCD_DrawLine(last_x, last_y, x, y);
                    }
                }

                LCD_FillCircle(x, y, DEMO_TOUCH_DOT_RADIUS);

                last_x = x;
                last_y = y;
            }
        }
        else if (event == BSP_TOUCH_UP)
        {
            last_x = 0xFFFFU;
            last_y = 0xFFFFU;
        }
        else
        {
            /* 无事件，什么都不做 */
        }

        /* 每 5 秒报一次中断计数：空闲时应几乎不增长 */
        if ((HAL_GetTick() - last_irq_ms) > DEMO_TOUCH_IRQ_PERIOD_MS)
        {
            last_irq_ms = HAL_GetTick();
            Debug_Printf("touch irq=%lu\r\n", (unsigned long)BSP_TOUCH_IrqCount());
        }

        /* 每 1 秒打印一次原始字节；这一秒没有新中断就打印 idle */
        if ((HAL_GetTick() - last_raw_ms) > DEMO_TOUCH_RAW_PERIOD_MS)
        {
            uint8_t  raw[BSP_TOUCH_RAW_LEN];
            uint32_t irq_now = BSP_TOUCH_IrqCount();

            last_raw_ms = HAL_GetTick();

            if (irq_now == last_printed_irq)
            {
                Debug_Printf("touch: idle (irq=%lu)\r\n", (unsigned long)irq_now);
            }
            else
            {
                last_printed_irq = irq_now;

                if (BSP_TOUCH_GetRawData(raw, (uint8_t)sizeof(raw)) == BSP_TOUCH_RAW_LEN)
                {
                    Debug_Printf("raw=%02X %02X %02X %02X %02X  (x=%u y=%u) irq=%lu\r\n",
                                 raw[0], raw[1], raw[2], raw[3], raw[4],
                                 (unsigned)x, (unsigned)y, (unsigned long)irq_now);
                }
            }
        }
    }
}

/* ======================= 演示调度 ======================= */

void App_Demo_Run(void)
{
#if DEMO_POWER_TEST
    Debug_Printf("power test mode\r\n");
    App_PowerTest_Run();            /* 不返回 */
#elif DEMO_FLICKER_TEST
    Debug_Printf("flicker test start\r\n");
    App_Demo_FlickerBar();          /* 不返回 */
#else
#if DEMO_TOUCH_BOARD
    {
        uint8_t init_result = BSP_TOUCH_Init();

        if (init_result == 0U)
        {
            Debug_Printf("touch: OK (FT5426)\r\n");
        }
        else if (init_result == 1U)
        {
            Debug_Printf("touch: no device on I2C3\r\n");
        }
        else
        {
            Debug_Printf("touch: wrong chip id\r\n");
        }
    }

    Debug_Printf("touch board\r\n");
    App_Demo_TouchBoard();          /* 不返回 */
#else
    Debug_Printf("font test page\r\n");
    App_Demo_FontPage();
#endif
#endif
}

/**
 * @file    app_selftest.c
 * @author  华畅
 * @brief   开机自检实现（SDRAM / LTDC 寄存器 / CPU 直写像素 / 刷新率）
 * @version 0.1
 * @date    2026-10-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "app_selftest.h"
#include "bsp_debug.h"

#include "bsp_sdram.h"
#include "bsp_lcd.h"

#include "ltdc.h"       /* 提供 hltdc */

#define SELFTEST_COLOR_RED          (0x00FF0000U)
#define SELFTEST_COLOR_GREEN        (0x0000FF00U)
#define SELFTEST_COLOR_BLUE         (0x000000FFU)
#define SELFTEST_COLOR_WHITE        (0x00FFFFFFU)
#define SELFTEST_COLOR_YELLOW       (0x00FFFF00U)
#define SELFTEST_COLOR_BLACK        (0x00000000U)

#define SELFTEST_BOX_SIZE           (40U)
#define SELFTEST_BOX_TR_X           (984U)
#define SELFTEST_BOX_BL_Y           (560U)
#define SELFTEST_BOX_C_X            (492U)
#define SELFTEST_BOX_C_Y            (280U)

#define SELFTEST_PLL3_VCO_HZ        (512000000UL)
#define SELFTEST_FRAME_CLOCKS       (853440UL)      /* 1344 * 635 */
#define SELFTEST_PLL3_R_DIV_MASK    (0x7FU)
#define SELFTEST_PLL3_R_DIV_SHIFT   (24U)
#define SELFTEST_CPSR_Y_MASK        (0x7FFU)
#define SELFTEST_CPSR_X_SHIFT       (16U)
#define SELFTEST_CPSR_X_MASK        (0x1FFFU)

/**
 * @brief  用 CPU 直接填满整屏（不经过 DMA2D）
 *
 * @note   按当前图层位宽写入，颜色参数统一为 24 位 0x00RRGGBB。
 * @param[in] color 颜色值
 * @return 无
 */
static void selftest_fill_cpu(uint32_t color)
{
    uint32_t i = 0U;
    uint32_t pixel_count = 0U;

    pixel_count = ((uint32_t)LCD_GetXSize()) * ((uint32_t)LCD_GetYSize());

    if (hltdc.LayerCfg[0].PixelFormat == LTDC_PIXEL_FORMAT_ARGB8888)
    {
        volatile uint32_t *frame_buffer = (volatile uint32_t *)SDRAM_BASE;
        for (i = 0U; i < pixel_count; i++)
        {
            frame_buffer[i] = color;
        }
    }
    else if (hltdc.LayerCfg[0].PixelFormat == LTDC_PIXEL_FORMAT_RGB888)
    {
        volatile uint8_t *frame_buffer = (volatile uint8_t *)SDRAM_BASE;
        uint8_t red   = (uint8_t)((color >> 16) & 0xFFU);
        uint8_t green = (uint8_t)((color >> 8) & 0xFFU);
        uint8_t blue  = (uint8_t)(color & 0xFFU);

        for (i = 0U; i < pixel_count; i++)
        {
            frame_buffer[(i * 3U) + 0U] = blue;
            frame_buffer[(i * 3U) + 1U] = green;
            frame_buffer[(i * 3U) + 2U] = red;
        }
    }
    else
    {
        volatile uint16_t *frame_buffer = (volatile uint16_t *)SDRAM_BASE;
        uint16_t value = LCD_RGB888to565(color);

        for (i = 0U; i < pixel_count; i++)
        {
            frame_buffer[i] = value;
        }
    }
}

/**
 * @brief  用 CPU 直接画一个方块（不经过 DMA2D 和 bsp_lcd）
 *
 * @param[in] x      左上角 X 坐标
 * @param[in] y      左上角 Y 坐标
 * @param[in] size   边长（像素）
 * @param[in] color  颜色值（24 位）
 * @return 无
 */
static void selftest_box(uint16_t x, uint16_t y, uint16_t size, uint32_t color)
{
    uint16_t ix = 0U;
    uint16_t iy = 0U;
    uint32_t stride = (uint32_t)LCD_GetXSize();
    uint32_t index = 0U;

    if (hltdc.LayerCfg[0].PixelFormat == LTDC_PIXEL_FORMAT_ARGB8888)
    {
        volatile uint32_t *frame_buffer = (volatile uint32_t *)SDRAM_BASE;

        for (iy = 0U; iy < size; iy++)
        {
            for (ix = 0U; ix < size; ix++)
            {
                index = (((uint32_t)(y + iy)) * stride) + (uint32_t)(x + ix);
                frame_buffer[index] = color;
            }
        }
    }
    else if (hltdc.LayerCfg[0].PixelFormat == LTDC_PIXEL_FORMAT_RGB888)
    {
        volatile uint8_t *frame_buffer = (volatile uint8_t *)SDRAM_BASE;
        uint8_t red   = (uint8_t)((color >> 16) & 0xFFU);
        uint8_t green = (uint8_t)((color >> 8) & 0xFFU);
        uint8_t blue  = (uint8_t)(color & 0xFFU);

        for (iy = 0U; iy < size; iy++)
        {
            for (ix = 0U; ix < size; ix++)
            {
                index = ((((uint32_t)(y + iy)) * stride) + (uint32_t)(x + ix)) * 3U;
                frame_buffer[index + 0U] = blue;
                frame_buffer[index + 1U] = green;
                frame_buffer[index + 2U] = red;
            }
        }
    }
    else
    {
        volatile uint16_t *frame_buffer = (volatile uint16_t *)SDRAM_BASE;
        uint16_t value = LCD_RGB888to565(color);

        for (iy = 0U; iy < size; iy++)
        {
            for (ix = 0U; ix < size; ix++)
            {
                index = (((uint32_t)(y + iy)) * stride) + (uint32_t)(x + ix);
                frame_buffer[index] = value;
            }
        }
    }
}

uint32_t App_SelfTest_Sdram(void)
{
    SDRAM_TestResult_t result;

    result = BSP_SDRAM_Test();

    Debug_Printf("\r\n===== SDRAM memtest =====\r\n");
    Debug_Printf("1 data bus : err=%lu  (%lu ms)\r\n",
                 (unsigned long)result.data_err, (unsigned long)result.ms_data);
    Debug_Printf("2 addr bus : err=%lu  (%lu ms)\r\n",
                 (unsigned long)result.addr_err, (unsigned long)result.ms_addr);
    Debug_Printf("3 block rw : err=%lu  (%lu ms)\r\n",
                 (unsigned long)result.block_err, (unsigned long)result.ms_block);
    Debug_Printf("TOTAL      : err=%lu\r\n", (unsigned long)result.total_err);
    Debug_Printf("===== memtest %s =====\r\n\r\n",
                 (result.total_err == 0U) ? "OK" : "FAIL");

    return result.total_err;
}

void App_SelfTest_PrintLtdc(void)
{
    Debug_Printf("\r\n--- LTDC regs ---\r\n");
    Debug_Printf("GCR     = %08lX\r\n", (unsigned long)LTDC->GCR);
    Debug_Printf("SRCR    = %08lX\r\n", (unsigned long)LTDC->SRCR);
    Debug_Printf("BPCR    = %08lX\r\n", (unsigned long)LTDC->BPCR);
    Debug_Printf("AWCR    = %08lX\r\n", (unsigned long)LTDC->AWCR);
    Debug_Printf("TWCR    = %08lX\r\n", (unsigned long)LTDC->TWCR);

    /* 本版 CMSIS 里图层寄存器在 LTDC_Layer1，不在 LTDC-> 下面 */
    Debug_Printf("L1 CFBAR  = %08lX\r\n", (unsigned long)LTDC_Layer1->CFBAR);
    Debug_Printf("L1 CFBLR  = %08lX\r\n", (unsigned long)LTDC_Layer1->CFBLR);
    Debug_Printf("L1 CFBLNR = %08lX\r\n", (unsigned long)LTDC_Layer1->CFBLNR);
    Debug_Printf("L1 PFCR   = %08lX\r\n", (unsigned long)LTDC_Layer1->PFCR);
    Debug_Printf("L1 WHPCR  = %08lX\r\n", (unsigned long)LTDC_Layer1->WHPCR);
    Debug_Printf("L1 WVPCR  = %08lX\r\n", (unsigned long)LTDC_Layer1->WVPCR);
    Debug_Printf("L1 CR     = %08lX\r\n", (unsigned long)LTDC_Layer1->CR);

    Debug_Printf("cfg: W=%lu H=%lu pf=%lu fb=%08lX  Xsize=%lu Ysize=%lu\r\n",
                 (unsigned long)hltdc.LayerCfg[0].ImageWidth,
                 (unsigned long)hltdc.LayerCfg[0].ImageHeight,
                 (unsigned long)hltdc.LayerCfg[0].PixelFormat,
                 (unsigned long)hltdc.LayerCfg[0].FBStartAdress,
                 (unsigned long)LCD_GetXSize(),
                 (unsigned long)LCD_GetYSize());
}

void App_SelfTest_CpuPixel(void)
{
    uint32_t read_back_1 = 0U;
    uint32_t read_back_2 = 0U;

    /* ① 整屏填绿：这步不均匀说明地址或 stride 有问题 */
    selftest_fill_cpu(SELFTEST_COLOR_GREEN);
    Debug_Printf("D1 CPU fill GREEN done\r\n");
    HAL_Delay(1500U);

    /* ② 黑底 + 四角与中心各一个方块 */
    selftest_fill_cpu(SELFTEST_COLOR_BLACK);
    selftest_box(0U, 0U, SELFTEST_BOX_SIZE, SELFTEST_COLOR_RED);
    selftest_box(SELFTEST_BOX_TR_X, 0U, SELFTEST_BOX_SIZE, SELFTEST_COLOR_GREEN);
    selftest_box(0U, SELFTEST_BOX_BL_Y, SELFTEST_BOX_SIZE, SELFTEST_COLOR_BLUE);
    selftest_box(SELFTEST_BOX_TR_X, SELFTEST_BOX_BL_Y, SELFTEST_BOX_SIZE, SELFTEST_COLOR_WHITE);
    selftest_box(SELFTEST_BOX_C_X, SELFTEST_BOX_C_Y, SELFTEST_BOX_SIZE, SELFTEST_COLOR_YELLOW);
    Debug_Printf("D2 corner boxes: TL=red TR=green BL=blue BR=white C=yellow\r\n");

    /* ③ 用 bsp_lcd 画同样的位置，验证驱动路径 */
    HAL_Delay(2000U);
    LCD_Clear(SELFTEST_COLOR_BLACK);
    LCD_SetTextColor(SELFTEST_COLOR_RED);
    LCD_FillRect(0U, 0U, SELFTEST_BOX_SIZE, SELFTEST_BOX_SIZE);
    LCD_SetTextColor(SELFTEST_COLOR_WHITE);
    LCD_FillRect(SELFTEST_BOX_TR_X, SELFTEST_BOX_BL_Y, SELFTEST_BOX_SIZE, SELFTEST_BOX_SIZE);

    /* ④ 读回验证 */
    read_back_1 = LCD_ReadPixelRGB888(20U, 20U);
    read_back_2 = LCD_ReadPixelRGB888(1000U, 580U);
    Debug_Printf("D3 readback (20,20)=%06lX (expect FF0000)  (1000,580)=%06lX (expect FFFFFF)\r\n",
                 (unsigned long)read_back_1, (unsigned long)read_back_2);

    /* ⑤ 一条贯穿全宽的横线，检查行对齐 */
    LCD_SetTextColor(SELFTEST_COLOR_YELLOW);
    LCD_DrawLine(0U, 300U, (uint16_t)(LCD_GetXSize() - 1U), 300U);
    Debug_Printf("D4 hline y=300 full width\r\n");
}

void App_SelfTest_PrintTiming(void)
{
    uint32_t r_div = 0U;
    uint32_t pclk_hz = 0U;
    uint32_t fps_x10 = 0U;

    r_div = (((RCC->PLL3DIVR >> SELFTEST_PLL3_R_DIV_SHIFT) & SELFTEST_PLL3_R_DIV_MASK) + 1U);
    pclk_hz = SELFTEST_PLL3_VCO_HZ / r_div;
    fps_x10 = ((pclk_hz * 10UL) / SELFTEST_FRAME_CLOCKS);

    Debug_Printf("\r\n--- display timing ---\r\n");
    Debug_Printf("PLL3R   = %lu\r\n", (unsigned long)r_div);
    Debug_Printf("PCLK    = %lu.%03lu MHz\r\n",
                 (unsigned long)(pclk_hz / 1000000UL),
                 (unsigned long)((pclk_hz % 1000000UL) / 1000UL));
    Debug_Printf("refresh = %lu.%lu Hz\r\n",
                 (unsigned long)(fps_x10 / 10UL), (unsigned long)(fps_x10 % 10UL));
}

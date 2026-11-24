/**
 * @file    app_selftest.h
 * @author  华畅
 * @brief   开机自检接口（SDRAM / LTDC / 时序）
 * @version 0.1
 * @date    2026-10-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef __APP_SELFTEST_H
#define __APP_SELFTEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
 * @brief  跑 SDRAM 三段自检并打印结果
 *
 * @note   会覆盖整个 16 MB（含帧缓存），必须在点屏和绘图之前调用。
 * @param  无
 * @return 0 = 全部通过，非 0 = 错误总数
 */
uint32_t App_SelfTest_Sdram(void);

/**
 * @brief  打印 LTDC 关键寄存器，用于和 CubeMX 配置比对
 *
 * @param  无
 * @return 无
 */
void App_SelfTest_PrintLtdc(void);

/**
 * @brief  用 CPU 直写方式做像素级验证（整屏填色 + 四角色块 + 读回）
 *
 * @note   绕开 DMA2D，用来区分"总线/地址问题"和"加速器问题"。
 * @param  无
 * @return 无
 */
void App_SelfTest_CpuPixel(void);

/**
 * @brief  打印当前实际刷新率（从 RCC 寄存器反算）
 *
 * @param  无
 * @return 无
 */
void App_SelfTest_PrintTiming(void);

#ifdef __cplusplus
}
#endif
#endif /* __APP_SELFTEST_H */

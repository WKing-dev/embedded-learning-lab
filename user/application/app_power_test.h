/**
 * @file    app_power_test.h
 * @author  华畅
 * @brief   满载功耗测试（用于热设计验证）
 * @version 0.1
 * @date    2026-10-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef __APP_POWER_TEST_H
#define __APP_POWER_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
 * @brief  进入满载功耗测试（函数不返回）
 *
 * 让 CPU/FPU 与 FMC 总线同时接近满载，并把屏幕保持在白底状态，
 * 便于用热像仪或热电偶测量各器件温度。
 *
 * @note   会持续写入 SDRAM 的 4 MB 之后区域（不影响帧缓存）。
 * @param  无
 * @return 无（不返回）
 */
void App_PowerTest_Run(void);

#ifdef __cplusplus
}
#endif
#endif /* __APP_POWER_TEST_H */

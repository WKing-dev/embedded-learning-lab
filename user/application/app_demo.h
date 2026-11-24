/**
 * @file    app_demo.h
 * @author  华畅
 * @brief   上电演示选择与各演示界面
 * @version 0.1
 * @date    2026-10-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef __APP_DEMO_H
#define __APP_DEMO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 1 = 白底 + 缓慢移动黑条（判断刷新率是否闪烁）；0 = 不启用 */
#define DEMO_FLICKER_TEST       (0)

/* 1 = 触摸画板；0 = 字体测试页
 * 注意：DEMO_FLICKER_TEST 为 1 时本项被忽略 */
#define DEMO_TOUCH_BOARD        (1)

/* 1 = 满载功耗测试（用于测温度）；优先级最高 */
#define DEMO_POWER_TEST         (1)

/**
 * @brief  运行选定的演示界面（函数不返回）
 *
 * @note   根据 DEMO_FLICKER_TEST / DEMO_TOUCH_BOARD 选择，内部会做触摸初始化。
 * @param  无
 * @return 无（正常情况不会返回）
 */
void App_Demo_Run(void);

/**
 * @brief  字体测试页：5 种字号 + 全 ASCII + 8 个色条
 *
 * @param  无
 * @return 无
 */
void App_Demo_FontPage(void);

/**
 * @brief  触摸画板：手指划过处画红线
 *
 * @note   函数不返回；每 1 秒打印原始触摸字节，每 5 秒打印中断计数。
 * @param  无
 * @return 无
 */
void App_Demo_TouchBoard(void);

#if DEMO_FLICKER_TEST
/**
 * @brief  闪烁测试：纯白背景 + 缓慢左右移动的黑竖条（不返回）
 *
 * @param  无
 * @return 无
 */
void App_Demo_FlickerBar(void);
#endif

#ifdef __cplusplus
}
#endif
#endif /* __APP_DEMO_H */

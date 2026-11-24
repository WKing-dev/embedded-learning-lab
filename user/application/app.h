/**
 * @file    app.h
 * @author  华畅
 * @brief   应用层入口
 * @version 0.2
 * @date    2026-10-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef __APP_H
#define __APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
 * @brief  应用硬件初始化（上电自检 + 点屏 + 触摸复位）
 *
 * @note   必须返回！所有界面逻辑都放在任务里。
 * @param  无
 * @return 无
 */
void App_Init(void);

/**
 * @brief  创建应用任务（触摸 / 界面 / 心跳）
 *
 * @note   必须在 osKernelInitialize() 之后、osKernelStart() 之前调用。
 * @param  无
 * @return 无
 */
void App_CreateTasks(void);

#ifdef __cplusplus
}
#endif
#endif /* __APP_H */

/**
 * @file    app_tasks.h
 * @author  华畅
 * @brief   应用任务定义
 * @version 0.1
 * @date    2026-10-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef __APP_TASKS_H
#define __APP_TASKS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 触摸点（通过队列在任务间传递） */
typedef struct Touch_Point_Struct_s
{
    uint16_t x;         /* 屏幕 X 坐标 */
    uint16_t y;         /* 屏幕 Y 坐标 */
    uint8_t  event;     /* BSP_TOUCH_DOWN / BSP_TOUCH_UP */
} Touch_Point_Struct;

/**
 * @brief  创建队列并启动三个应用任务
 *
 * @note   必须在 osKernelInitialize() 之后调用。
 * @param  无
 * @return 无
 */
void App_CreateTasks(void);

#ifdef __cplusplus
}
#endif
#endif /* __APP_TASKS_H */

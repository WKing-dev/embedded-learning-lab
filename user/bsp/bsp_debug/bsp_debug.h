/**
 * @file    bsp_debug.h
 * @author  华畅
 * @brief   调试打印接口（USART3）—— BSP 层，各层均可调用
 * @version 0.2
 * @date    2026-10-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef __BSP_DEBUG_H
#define __BSP_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
 * @brief  初始化调试模块（创建串口打印互斥锁）
 *
 * @note   必须在 osKernelInitialize() 之后、启动调度器之前调用。
 * @param  无
 * @return 无
 */
void Debug_Init(void);

/**
 * @brief  通过 USART3 打印一行调试信息
 *
 * @note   阻塞发送；单条超过 128 字节会被截断；内部有互斥锁，
 *         多任务同时打印不会交错；不要在中断里调用。
 * @param[in] format 格式字符串
 * @param[in] ...    可变参数
 * @return 无
 */
void Debug_Printf(const char *format, ...);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_DEBUG_H */

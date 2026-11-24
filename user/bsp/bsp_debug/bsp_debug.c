/**
 * @file    bsp_debug.c
 * @author  华畅
 * @brief   调试打印实现（USART3，阻塞发送，带时间戳与互斥锁）
 * @version 0.2
 * @date    2026-10-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "bsp_debug.h"
#include "usart.h"          /* 提供 huart3 */

#include <stdio.h>
#include <stdarg.h>

#include "cmsis_os2.h"      /* 互斥锁 */

/* ---------------- 配置 ---------------- */
#define DEBUG_BUFFER_SIZE       (128)
#define DEBUG_TX_TIMEOUT_MS     (100U)
#define DEBUG_PREFIX_SIZE       (24)
#define DEBUG_MUTEX_TIMEOUT_MS  (100U)

/* 1 = 每行前面加 "[   12345 ms] "；0 = 不加 */
#define DEBUG_WITH_TIMESTAMP    (1)

/* 串口打印互斥锁；未初始化时为 0（启动阶段不加锁） */
static osMutexId_t debug_mutex = 0;

void Debug_Init(void)
{
    if (debug_mutex == 0)
    {
        debug_mutex = osMutexNew(NULL);
    }
}

void Debug_Printf(const char *format, ...)
{
#if DEBUG_WITH_TIMESTAMP
    char    content[DEBUG_BUFFER_SIZE];
    char    output[DEBUG_BUFFER_SIZE + DEBUG_PREFIX_SIZE];
#else
    char    output[DEBUG_BUFFER_SIZE];
#endif
    va_list args;
    int32_t length = 0;
    uint8_t locked = 0U;

    /* 多任务同时打印时加锁，避免日志交错 */
    if (debug_mutex != 0)
    {
        if (osMutexAcquire(debug_mutex, DEBUG_MUTEX_TIMEOUT_MS) == osOK)
        {
            locked = 1U;
        }
        else
        {
            return;     /* 拿不到锁就放弃这条日志，避免阻塞 */
        }
    }

    va_start(args, format);

#if DEBUG_WITH_TIMESTAMP
    length = vsnprintf(content, sizeof(content), format, args);
#else
    length = vsnprintf(output, sizeof(output), format, args);
#endif

    va_end(args);

    if (length > 0)
    {
#if DEBUG_WITH_TIMESTAMP
        length = snprintf(output, sizeof(output), "[%8lu ms] %s",
                          (unsigned long)HAL_GetTick(), content);
        if (length >= (int32_t)sizeof(output))
        {
            length = (int32_t)sizeof(output) - 1;
        }
#else
        if (length > (int32_t)sizeof(output))
        {
            length = (int32_t)sizeof(output);
        }
#endif

        if (length > 0)
        {
            (void)HAL_UART_Transmit(&huart3, (uint8_t *)output,
                                    (uint16_t)length, DEBUG_TX_TIMEOUT_MS);
        }
    }

    if (locked != 0U)
    {
        (void)osMutexRelease(debug_mutex);
    }
}

/**
 * @file    bsp_touch_isr.c
 * @author  华畅
 * @brief   触摸中断回调（EXTI 入口，只负责唤醒任务）
 * @version 0.2
 * @date    2026-10-28
 *
 * @copyright Copyright (c) 2026
 *
 * 调用链：
 *   FT5426 INT 拉低 -> PH13 下降沿
 *     -> EXTI15_10_IRQHandler()
 *     -> HAL_GPIO_EXTI_IRQHandler()
 *     -> 本文件 HAL_GPIO_EXTI_Callback()
 *     -> BSP_TOUCH_IRQNotify()：计数 + 通知回调（2~3 µs）
 *     -> 应用层回调里 osThreadFlagsSet() 唤醒触摸任务
 */

#include "bsp_touch.h"

/**
 * @brief  HAL 外部中断回调（EXTI 线 10~15 共用）
 *
 * @note   运行在中断上下文，只做通知，不读 I2C、不打印。
 * @param[in] gpio_pin 触发中断的引脚掩码
 * @return 无
 */
void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
    if (gpio_pin == TOUCH_INT_Pin)
    {
        BSP_TOUCH_IRQNotify();
    }
}

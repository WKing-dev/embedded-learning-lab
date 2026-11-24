/**
 * @file    bsp_touch.h
 * @author  华畅
 * @brief   FT5426 电容触摸驱动接口（I2C3，从机地址 0x38）
 * @version 0.3
 * @date    2026-10-28
 *
 * @copyright Copyright (c) 2026
 *
 * 分工（适配 RTOS）：
 *   - 中断里只调 BSP_TOUCH_IRQNotify()：2~3 µs，通过回调唤醒任务
 *   - 任务里调 BSP_TOUCH_ReadNow()：真正读 I2C、标定、滤波
 */

#ifndef __BSP_TOUCH_H
#define __BSP_TOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "i2c.h"        /* 提供 hi2c3 */

/* ---------------- 事件定义 ---------------- */
#define BSP_TOUCH_NONE      (0U)    /* 没有新事件 */
#define BSP_TOUCH_DOWN      (1U)    /* 有触点，*x / *y 有效 */
#define BSP_TOUCH_UP        (2U)    /* 刚抬起 */

/* 原始数据缓存长度：TD_STATUS + XH + XL + YH + YL */
#define BSP_TOUCH_RAW_LEN   (5U)

/* 中断通知回调类型（由应用层注册，BSP 本身不依赖 RTOS） */
typedef void (*BSP_TOUCH_NotifyFn)(void);

/**
 * @brief  触摸控制器初始化（复位 + 校验 ID）
 *
 * @note   可在启动调度器之前调用。
 * @param  无
 * @return 0 = 成功，1 = 找不到设备，2 = ID 不匹配
 */
uint8_t BSP_TOUCH_Init(void);

/**
 * @brief  注册中断通知回调（应用层在这里唤醒任务）
 *
 * @param[in] fn 回调函数；传 0 表示取消
 * @return 无
 */
void BSP_TOUCH_SetNotifyCallback(BSP_TOUCH_NotifyFn fn);

/**
 * @brief  中断入口：计数 + 触发通知回调
 *
 * @note   由 HAL_GPIO_EXTI_Callback 调用；不访问 I2C，耗时 2~3 µs。
 * @param  无
 * @return 无
 */
void BSP_TOUCH_IRQNotify(void);

/**
 * @brief  真正读取一次触摸（访问 I2C，双读校验 + 重试 + 标定 + 滤波）
 *
 * @note   在任务上下文调用；单次耗时约 350~550 µs。
 * @param[out] x 屏幕 X 坐标，仅当返回 BSP_TOUCH_DOWN 时有效
 * @param[out] y 屏幕 Y 坐标，仅当返回 BSP_TOUCH_DOWN 时有效
 * @return BSP_TOUCH_NONE / BSP_TOUCH_DOWN / BSP_TOUCH_UP
 */
uint8_t BSP_TOUCH_ReadNow(uint16_t *x, uint16_t *y);

/**
 * @brief  取走最近一次事件（兼容裸机轮询方式，不访问 I2C）
 *
 * @param[out] x 屏幕 X 坐标
 * @param[out] y 屏幕 Y 坐标
 * @return BSP_TOUCH_NONE / BSP_TOUCH_DOWN / BSP_TOUCH_UP
 */
/**
 * @brief  读取当前触摸状态（给 LVGL 这类"轮询式"输入设备用）
 *
 * 与 BSP_TOUCH_Scan 的区别：Scan 是"取走事件"，取一次就没了；
 * 本函数只是"看一眼现在什么状态"，可以反复调用。
 *
 * @param[out] x       最新屏幕 X 坐标
 * @param[out] y       最新屏幕 Y 坐标
 * @param[out] pressed 1 = 现在按着，0 = 没按
 * @return 无
 */
void BSP_TOUCH_GetLatest(uint16_t *x, uint16_t *y, uint8_t *pressed);

uint8_t BSP_TOUCH_Scan(uint16_t *x, uint16_t *y);

/**
 * @brief  累计进入中断的次数
 *
 * @param  无
 * @return 中断计数
 */
uint32_t BSP_TOUCH_IrqCount(void);

/**
 * @brief  取出最近一次读到的原始字节（调试用）
 *
 * @param[out] buffer 接收缓冲区
 * @param[in]  length 缓冲区长度
 * @return 实际拷贝的字节数
 */
uint8_t BSP_TOUCH_GetRawData(uint8_t *buffer, uint8_t length);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_TOUCH_H */

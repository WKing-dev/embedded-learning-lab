/**
 * @file app_encoder.h
 * @author WLJ
 * @brief 应用层：光栅尺位置采集与打印
 * @version 0.1
 * @date 2026-11-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef _APP_ENCODER_H_
#define _APP_ENCODER_H_

/**
 * @brief  应用初始化
 * @note   在所有 MX_xxx_Init() 之后调用一次
 * @return 无
 */
void App_Encoder_Init(void);

/**
 * @brief  应用主任务
 * @note   在主循环中反复调用
 * @return 无
 */
void App_Encoder_Task(void);

/**
 * @brief  1 ms 节拍处理
 * @note   放入 SysTick 中断
 * @return 无
 */
void App_Encoder_Timing_Handle(void);

#endif /* _APP_ENCODER_H_ */

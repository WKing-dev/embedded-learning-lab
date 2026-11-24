/**
  * @file    bsp_led.h
  * @brief   板载指示灯 LED1 / LED2（BSP 层）
  * @note    目标板：DCDC 模块 / STM32H743VIT6 / LQFP100
  *          - LED1 = PE4、LED2 = PE2，推挽输出、无上下拉、低速（由 CubeMX 配好）
  *          - **高电平点亮**，上电默认低电平 = 灭
  *          - 分层：application -> bsp_led -> HAL
  */
#ifndef __BSP_LED_H__
#define __BSP_LED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* ============================ 可配置参数 ============================ */

/** 指示灯数量 */
#define BSP_LED_NUM           2U

#define BSP_LED_1             0U   /**< 指示灯 1：PE4（`LED1`） */
#define BSP_LED_2             1U   /**< 指示灯 2：PE2（`LED2`） */

/**
  * @brief 心跳灯翻转周期（ms）
  * @note  500 ms 翻转一次 = 1 Hz 闪烁，用肉眼确认程序还在跑。
  */
#define BSP_LED_HEARTBEAT_MS  500U

/* ========================== 模块数据结构体 ========================== */

typedef struct __Bsp_Led_Struct
{
  uint16_t count;                    /**< 心跳计时（ms），0 表示该翻转一次了 */
  uint8_t  on[BSP_LED_NUM];          /**< 各灯当前状态：1 = 亮 */
  uint8_t  heartbeat[BSP_LED_NUM];   /**< 1 = 该灯参与心跳闪烁 */
  uint8_t  inited;                   /**< 1 = 已初始化 */
} Bsp_Led_Struct;

/** 模块实例 */
extern Bsp_Led_Struct Bsp_Led;

/* ============================== 对外接口 ============================== */

/**
  * @brief  模块初始化：两灯全灭，LED1 默认参与心跳闪烁
  * @note   必须在 MX_GPIO_Init() 之后调用
  */
void BSP_LED_Init(void);

/**
  * @brief  点亮指定指示灯
  * @param[in] idx BSP_LED_1 / BSP_LED_2
  */
void BSP_LED_On(uint8_t idx);

/**
  * @brief  熄灭指定指示灯
  * @param[in] idx BSP_LED_1 / BSP_LED_2
  */
void BSP_LED_Off(uint8_t idx);

/**
  * @brief  翻转指定指示灯
  * @param[in] idx BSP_LED_1 / BSP_LED_2
  */
void BSP_LED_Toggle(uint8_t idx);

/**
  * @brief  取指定指示灯的当前状态
  * @param[in] idx BSP_LED_1 / BSP_LED_2
  * @retval 1 = 亮，0 = 灭；索引非法返回 0
  */
uint8_t BSP_LED_Get(uint8_t idx);

/**
  * @brief  使能 / 关闭某一路的心跳闪烁
  * @param[in] idx    BSP_LED_1 / BSP_LED_2
  * @param[in] enable 1 = 参与心跳，0 = 不参与（改由 On/Off 控制）
  */
void BSP_LED_Set_Heartbeat(uint8_t idx, uint8_t enable);

/**
  * @brief  模块主任务：处理心跳闪烁
  * @note   在 main 的 while(1) 里反复调用；count > 0 时直接返回，不阻塞
  */
void BSP_LED_Task(void);

/**
  * @brief  计时处理
  * @note   必须放入 1 ms 定时中断（本工程注册在 SysTick_Handler），仅递减 count
  */
void BSP_LED_Timing_Handle(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LED_H__ */

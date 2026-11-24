/**
  * @file    bsp_wdg.h
  * @brief   外部看门狗 STWD100 —— 喂狗信号输出（BSP 层）
  * @note    目标板：DCDC 模块 / STM32H743VIT6 / LQFP100
  *          - WDI = PA3，推挽输出、无上下拉、低速（由 CubeMX 配好）
  *          - STWD100 为**边沿触发**式喂狗，超时 210 ms，
  *            因此必须周期性**翻转电平**，保持高或低都不算喂狗
  *          - 分层：application -> bsp_wdg -> HAL
  */
#ifndef __BSP_WDG_H__
#define __BSP_WDG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* ============================ 可配置参数 ============================ */

/**
  * @brief WDI 翻转周期（ms）
  * @note  STWD100 超时 210 ms，这里取 100 ms 留约 2 倍余量。
  *        翻转周期必须**小于** 210 ms，否则看门狗会复位。
  */
#define BSP_WDG_PERIOD_MS     100U

/* ========================== 上次复位原因 ========================== */

#define BSP_WDG_RST_UNKNOWN   0U   /**< 标志全无（异常情况） */
#define BSP_WDG_RST_PIN       1U   /**< NRST 引脚复位：外部看门狗 / 复位按键 */
#define BSP_WDG_RST_POR       2U   /**< 上电复位 */
#define BSP_WDG_RST_BOR       3U   /**< 掉电 / 欠压复位 */
#define BSP_WDG_RST_SOFT      4U   /**< 软件复位 */
#define BSP_WDG_RST_IWDG      5U   /**< 内部独立看门狗复位 */
#define BSP_WDG_RST_WWDG      6U   /**< 窗口看门狗复位 */
#define BSP_WDG_RST_LPWR      7U   /**< 低功耗模式复位 */

/* ========================== 模块数据结构体 ========================== */

typedef struct __Bsp_Wdg_Struct
{
  uint32_t feed_cnt; /**< 累计喂狗（WDI 翻转）次数 */
  uint16_t count;    /**< 计时计数器（ms），0 表示该翻转一次了 */
  uint8_t  level;    /**< WDI 当前电平：0 = 低，1 = 高 */
  uint8_t  inited;   /**< 1 = 已初始化 */
} Bsp_Wdg_Struct;

/** 模块实例 */
extern Bsp_Wdg_Struct Bsp_Wdg;

/* ============================== 对外接口 ============================== */

/**
  * @brief  模块初始化：把 WDI 置为确定电平
  * @note   必须在 MX_GPIO_Init() 之后调用
  */
void BSP_WDG_Init(void);

/**
  * @brief  模块主任务：到时间翻转一次 WDI
  * @note   在 main 的 while(1) 里反复调用；count > 0 时直接返回，不阻塞。
  *         **主循环卡住 → 本函数不再被调用 → 210 ms 后 STWD100 复位 MCU。**
  */
void BSP_WDG_Task(void);

/**
  * @brief  计时处理
  * @note   必须放入 1 ms 定时中断（本工程注册在 SysTick_Handler），仅递减 count
  */
void BSP_WDG_Timing_Handle(void);

/**
  * @brief  取累计喂狗次数
  * @retval 自本次上电以来 WDI 的翻转次数（每 100 ms 加 1）
  */
uint32_t BSP_WDG_Get_Feed_Cnt(void);

/**
  * @brief  取 WDI 当前电平
  * @retval 0 = 低，1 = 高
  */
uint8_t BSP_WDG_Get_Level(void);

/**
  * @brief  读取并清除"上次复位原因"
  * @retval BSP_WDG_RST_xxx
  * @note   必须在启动早期调用一次（读后即清除标志），
  *         PIN 表示被 NRST 复位 —— 也就是外部看门狗或复位按键动作了。
  */
uint8_t BSP_WDG_Get_Reset_Cause(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_WDG_H__ */

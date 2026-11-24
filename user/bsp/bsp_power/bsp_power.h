/**
  * @file    bsp_power.h
  * @brief   5V 输出检测 —— eFuse 故障脚输入（BSP 层）
  * @note    目标板：DCDC 模块 / STM32H743VIT6 / LQFP100
  *          - FLT1 = PD12、FLT2 = PD13，开漏、低有效，
  *            外部经 R118(10 kΩ)/R120(20 kΩ) 分压到 +5V_2，空闲电平约 3.79 V
  *          - 分层：app_power -> bsp_power -> HAL
  *
  *          【实测记录 2026-11-09】TPS25940 的 ILIM 引脚实际贴装 17.8 Ω
  *          （设计值 17.8 kΩ，对应 I_LIM = 89/R = 5.0 A）。
  *          按手册电气特性表，R(ILIM) 被识别为 SHORT 时进入单点故障保护模式：
  *            I_LIM = 0.55 / 0.67 / 0.80 A（min/typ/max）
  *            I_FASTRIP = 1.5 × I_LIM + 0.375 ≈ 1.38 A
  *          而实测负载约 0.6 A，正好落在该限流带内 —— 若器件限流偏下限
  *          （0.55 A），0.6 A 负载就会进入恒流，输出电压下塌。
  *          FLT 的触发源为 欠压 / 过压 / 反向电压-电流 / 热关断（手册 9.3.5），
  *          过流本身不拉低 FLT，软件不能把 FLT 当成过流标志。
  */
#ifndef __BSP_POWER_H__
#define __BSP_POWER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* ============================ 通道索引 ============================ */

/** 故障输入路数 */
#define BSP_POWER_FLT_NUM    2U

#define BSP_POWER_FLT1       0U   /**< PD12，输出 1 的 eFuse 故障 */
#define BSP_POWER_FLT2       1U   /**< PD13，输出 2 的 eFuse 故障 */

/* ========================== 模块数据结构体 ========================== */

/**
  * @brief 电源监测模块数据
  * @note  所有变量封装在结构体内
  */
typedef struct __Bsp_Power_Struct
{
  uint8_t flt[BSP_POWER_FLT_NUM];   /**< 最近一次读到的电平：1 = 故障（低电平），0 = 正常 */
  uint8_t inited;                   /**< 1 = 已初始化 */
} Bsp_Power_Struct;

/** 模块实例 */
extern Bsp_Power_Struct Bsp_Power;

/* ============================== 对外接口 ============================== */

/**
  * @brief  模块初始化：清零状态并把当前电平读入结构体
  * @note   必须在 MX_GPIO_Init() 之后调用
  */
void BSP_Power_Init(void);

/**
  * @brief  读一路 FLT 引脚电平
  * @param[in] idx BSP_POWER_FLT1 / BSP_POWER_FLT2
  * @retval 1 = 故障（被拉低），0 = 正常；索引非法返回 0
  */
uint8_t BSP_Power_Read_Flt(uint8_t idx);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_POWER_H__ */

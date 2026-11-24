/**
  * @file    bsp_adc.h
  * @brief   5V 输出检测 —— ADC 采集与换算（BSP 层）
  * @note    目标板：DCDC 模块 / STM32H743VIT6 / LQFP100
  *          - ADC1，16 bit，DMA Circular，5 个 Rank 循环扫描
  *          - 通道索引必须与 CubeMX 里 Rank1~Rank5 的通道顺序完全一致
  *          - 分层：app_power -> bsp_adc -> HAL
  */
#ifndef __BSP_ADC_H__
#define __BSP_ADC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* ============================ 通道索引 ============================ */

/** 采集通道数量（与 ADC1 的 NbrOfConversion 一致） */
#define BSP_ADC_CH_COUNT     5U

#define BSP_ADC_CH_IMON1     0U   /**< Rank1  PC4  ADC1_INP4  输出 1 电流 */
#define BSP_ADC_CH_IMON2     1U   /**< Rank2  PC5  ADC1_INP8  输出 2 电流 */
#define BSP_ADC_CH_5V3       2U   /**< Rank3  PC1  ADC1_INP11 输出 3 电压 */
#define BSP_ADC_CH_5V2       3U   /**< Rank4  PA2  ADC1_INP14 输出 2 电压 */
#define BSP_ADC_CH_5V1       4U   /**< Rank5  PA1  ADC1_INP17 输出 1 电压 */

/* ============================ 换算系数 ============================ */

/**
  * @brief 分压配置选择（按板上实际贴装值选）
  * @note  1 = R22 实际 200 kΩ（板 A-1 现状）：分压比 0.709
  *            母线 >= 4.65 V 时引脚被 ESD 钳到 3.3 V 以上，读数恒为满量程；
  *            只有母线 < 4.65 V 时的读数是真实的。
  *        0 = R22 为设计值 20 kΩ：分压比 0.196，正常工作
  * 换料/并联电阻后只需改这一个开关，不用动其它代码。
  */
#define BSP_ADC_R22_IS_200K     1U

/**
  * @brief 电压通道换算：V(mV) = raw * NUM / DEN
  * @note  两种分压配置的系数分别如下（VDDA = 3300 mV、16 位）：
  *        - R21 = 82 kΩ、R22 = 200 kΩ → 比值 0.709 → raw * 7099 / 100000
  *        - R21 = 82 kΩ、R22 = 20 kΩ  → 比值 0.196 → raw * 2568 / 10000
  *
  *        【实测记录 2026-11-09】板 A-1 上 R22 实际贴装 200 kΩ（设计值 20 kΩ），
  *        5.68 V 算出 4.03 V，被 MCU 的 ESD 二极管钳到 3.5 V，因此三路 raw 恒为 65535。
  *        在 R22 上并联 22 kΩ（等效 19.8 kΩ）即可恢复，之后把上面的开关改为 0。
  */
#if (BSP_ADC_R22_IS_200K != 0U)
#define BSP_ADC_V_MV_NUM     7099U
#define BSP_ADC_V_MV_DEN     100000U
#else
#define BSP_ADC_V_MV_NUM     2568U
#define BSP_ADC_V_MV_DEN     10000U
#endif

/**
  * @brief 当前分压配置下能量到的电压上限（mV）
  * @note  raw 打满 65535 时对应的母线电压。超过它的输入一律读到这个值，
  *        所以实际电压只能表达为 ">该值"。
  */
#define BSP_ADC_V_MAX_MV     (((65535UL) * BSP_ADC_V_MV_NUM) / BSP_ADC_V_MV_DEN)

/**
  * @brief 电流通道换算：V_IMON(uV) = raw * NUM / DEN
  * @note  V_IMON(uV) = raw / 65535 * 3300000 = raw * 50.354
  */
#define BSP_ADC_I_UV_NUM     5035U
#define BSP_ADC_I_UV_DEN     100U

/* ---------------- IMON → 电流（TPS25940-Q1 手册式 6） ----------------
 *
 *   V_IMON = [ I_OUT × GAIN(IMON) + I(IMON_OS) ] × R_IMON
 *
 *   GAIN(IMON)  = 52.3 uA/A   （1 A <= I_OUT <= 5 A 条件下的实测值）
 *   I(IMON_OS)  = 0.8  uA     （典型失调）
 *
 *   反解： I_OUT(A) = ( V_IMON/R_IMON - 0.8uA ) / 52.3uA/A
 *
 *   小电流段失调项不可忽略：R_IMON = 10 kΩ 时 0.8 uA 相当于 8 mV 偏置。
 * ------------------------------------------------------------------- */

/** GAIN(IMON)，放大 1000 倍存的整数形式（52.3 uA/A → 52300） */
#define BSP_ADC_IMON_GAIN_UA_PER_A   52300UL

/** IMON 失调电流，放大 1000 倍存的整数形式（0.8 uA → 800 nA） */
#define BSP_ADC_IMON_OFFSET_NA       800UL

/**
  * @brief IMON → GND 的采样电阻（Ω）
  * @note  板上实测 R113 = 100 kΩ。
  *        手册式(6)：V_IMON = [I_OUT × 52.3uA/A + 0.8uA] × R_IMON
  *
  *        R = 100 kΩ 时的量程（ADC 参考 3.3 V）：
  *          - 满量程电流 = (3.3V/100kΩ - 0.8uA)/52.3uA/A = 0.6155 A
  *          - 实际负载约 0.6 A → V_IMON = 3.22 V，占满量程 97.5%，余量仅 2.5%
  *          - 超过 0.6155 A 就会 ADC 削顶，无法再区分过流程度
  *          - 芯片侧安全：0.6 A 对应 3.22 V，远低于 IMON 推荐上限 6 V
  *        若需要保留过流检测余量，把 R113 换成 62 kΩ（满量程 1 A）。
  */
#define BSP_ADC_IMON_R_OHM           100000U

/* ========================== 模块数据结构体 ========================== */

/**
  * @brief ADC 模块数据
  * @note  所有变量封装在结构体内，不使用散乱全局变量
  */
typedef struct __Bsp_Adc_Struct
{
  volatile uint16_t raw[BSP_ADC_CH_COUNT];  /**< DMA 目标缓冲区，按 Rank 顺序 */
  uint8_t           inited;                 /**< 1 = 已完成校准并启动采集 */
} Bsp_Adc_Struct;

/** 模块实例，供应用层只读访问（一般通过下面的接口取值） */
extern Bsp_Adc_Struct Bsp_Adc;

/* ============================== 对外接口 ============================== */

/**
  * @brief  模块初始化：ADC 校准 + 启动循环 DMA 采集
  * @retval HAL_OK / HAL_ERROR
  * @note   必须在 MX_ADC1_Init()、MX_DMA_Init() 之后调用
  */
HAL_StatusTypeDef BSP_ADC_Init(void);

/**
  * @brief  取某个通道的最新原始码值
  * @param[in] ch 通道索引，见 BSP_ADC_CH_xxx
  * @retval 原始码值（0~65535）；索引非法返回 0
  */
uint16_t BSP_ADC_Get_Raw(uint8_t ch);

/**
  * @brief  取电压通道的电压值
  * @param[in] ch 只对 BSP_ADC_CH_5V1 / 5V2 / 5V3 有意义
  * @retval 电压（mV）；索引非法返回 0
  */
uint32_t BSP_ADC_Get_Voltage_Mv(uint8_t ch);

/**
  * @brief  取 IMON 引脚电压
  * @param[in] ch 只对 BSP_ADC_CH_IMON1 / IMON2 有意义
  * @retval 电压（uV）；索引非法返回 0
  */
uint32_t BSP_ADC_Get_Imon_Uv(uint8_t ch);

/**
  * @brief  把 IMON 引脚电压换算成输出电流（0.1 mA 分辨率）
  * @param[in] imon_uv     IMON 引脚电压（uV），可用平均值
  * @param[in] r_imon_ohm  IMON 到 GND 的采样电阻（Ω），传 0 直接返回 0
  * @retval 电流（0.1 mA 为单位，即 5994 表示 599.4 mA）
  * @note   I_OUT(mA) = ( V_IMON/R_IMON - 0.8uA ) / 52.3uA/A × 1000
  *         内部做了四舍五入，避免整数截断造成约 0.3% 偏小。
  *         应用层做平均后应调用本函数，而不是 BSP_ADC_Get_Current_Ma_x10()，
  *         否则会绕过平均值重新取一次瞬时值。
  */
uint32_t BSP_ADC_Imon_Uv_To_Ma_x10(uint32_t imon_uv, uint32_t r_imon_ohm);

/**
  * @brief  取某通道当前瞬时 IMON 电压并换算成输出电流（0.1 mA 分辨率）
  * @param[in] ch          BSP_ADC_CH_IMON1 / IMON2
  * @param[in] r_imon_ohm  IMON 到 GND 的采样电阻（Ω），传 0 直接返回 0
  * @retval 电流（0.1 mA 为单位）
  */
uint32_t BSP_ADC_Get_Current_Ma_x10(uint8_t ch, uint32_t r_imon_ohm);

/**
  * @brief  把 IMON 电压按手册式(6)换算成输出电流（整数 mA）
  * @param[in] ch          BSP_ADC_CH_IMON1 / IMON2
  * @param[in] r_imon_ohm  IMON 到 GND 的采样电阻（Ω），传 0 直接返回 0
  * @retval 电流（mA）
  */
uint32_t BSP_ADC_Get_Current_Ma(uint8_t ch, uint32_t r_imon_ohm);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_ADC_H__ */

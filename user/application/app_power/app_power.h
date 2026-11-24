/**
  * @file    app_power.h
  * @brief   5V 输出检测 —— 应用层（状态机 + 周期打印）
  * @note    分层：application -> bsp_adc / bsp_power -> HAL
  *          标准三接口：App_Power_Init / App_Power_Task / App_Power_Timing_Handle
  */
#ifndef __APP_POWER_H__
#define __APP_POWER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ============================ 可配置参数 ============================ */

/** Task 时间片（ms），由 Timing_Handle 每 1 ms 递减 */
#define APP_POWER_TASK_PERIOD_MS   100U

/** 打印分频：每 10 个时间片打印一次，即 1 s 一次 */
#define APP_POWER_PRINT_DIV        10U

/** 电压通道数（5V1 / 5V2 / 5V3） */
#define APP_POWER_V_NUM            3U

/** 电流与故障通道数（输出 1 / 输出 2） */
#define APP_POWER_I_NUM            2U

/**
  * @brief IMON → GND 的采样电阻（Ω）
  * @note  板上实测 **R113 = 13 kΩ**（原 100 kΩ，2026-11-11 已更换）。
  *        手册式(6)：V_IMON = [I_OUT × 52.3uA/A + 0.8uA] × R_IMON
  *        换算式：I_OUT(A) = (V_IMON − 0.0104V) / 0.68
  *
  *        量程核算（R = 13 kΩ、ADC 参考 3.3 V）：
  *          0.6 A → I(IMON) = 32.2 uA → V_IMON ≈ 418 mV
  *          5   A → I(IMON) = 262  uA → V_IMON ≈ 3.41 V（芯片线性上限 3.48 V，刚好不饱和）
  *          满量程 3.3 V 对应约 4.85 A
  *        原 100 kΩ 时 5 A 需要 26 V，必然削顶，故改为 13 kΩ。
  *
  *        填 0 表示不换算电流，只打印 IMON 引脚电压。
  */
#define APP_POWER_IMON_R_OHM       13000U

/**
  * @brief 故障计数去重窗口（单位：Task 时间片，10 片 = 1 s）
  * @note  自动重试型 eFuse 故障后会周期性重试，FLT 随之反复拉低；
  *        加这个窗口后，1 s 内只记一次故障，计数不会被重试刷爆。
  */
#define APP_POWER_FLT_LOCK_TASK    10U

/**
  * @brief IMON 滑动平均点数
  * @note  每 100 ms 采样一次，8 点 = 800 ms 窗口。
  *        IMON 源阻抗 100 kΩ 且手册禁止在 IMON 脚加旁路电容，单次采样噪声可达
  *        100 多个 LSB，做平均后显示稳定；带载时信号大，滤波几乎不影响读数。
  *        设为 1 表示不平均。
  */
#define APP_POWER_IMON_AVG_NUM     8U

/**
  * @brief 判饱和的原始码值门限
  * @note  16 位满量程 65535，取 65500 作为门限。
  *        超过门限说明引脚输入电压 >= 3.3 V（ADC 削顶，非芯片饱和），
  *        此时打印 ">上限值"，避免把削顶后的固定值当成真实电压。
  */
#define APP_POWER_ADC_SAT_RAW      65500U

/* ============================ 状态枚举 ============================ */

typedef enum __App_Power_State_Enum
{
  APP_POWER_IDLE  = 0,   /**< 空闲：ADC 未就绪，不做采样 */
  APP_POWER_RUN   = 1,   /**< 正常：周期采样并打印 */
  APP_POWER_FAULT = 2    /**< 故障：至少一路 eFuse 报故障 */
} App_Power_State_Enum;

/* ========================== 模块数据结构体 ========================== */

typedef struct __App_Power_Struct
{
  uint8_t  state;                        /**< 当前状态，App_Power_State_Enum */
  uint16_t count;                        /**< 计时计数器（ms），0 表示时间片到期 */
  uint8_t  print_div;                    /**< 打印分频计数 */
  uint8_t  flt[APP_POWER_I_NUM];         /**< FLT 当前电平：1 = 故障 */
  uint8_t  flt_prev[APP_POWER_I_NUM];    /**< 上一轮电平，用于上升沿判断 */
  uint8_t  flt_lock[APP_POWER_I_NUM];    /**< 故障计数锁定剩余时间片（去重窗口） */
  uint32_t flt_cnt[APP_POWER_I_NUM];     /**< 故障累计次数（已去重） */
  uint32_t v_mv[APP_POWER_V_NUM];        /**< 三路输出电压（mV） */
  uint16_t imon_raw[APP_POWER_I_NUM];    /**< IMON 原始码值（与 imon_uv 同一时刻） */
  uint32_t imon_uv[APP_POWER_I_NUM];     /**< 两路 IMON 引脚电压（uV，已平均） */
  uint32_t imon_sum[APP_POWER_I_NUM];    /**< 平均窗口内的累加和 */
  uint32_t imon_ring[APP_POWER_I_NUM][APP_POWER_IMON_AVG_NUM]; /**< 平均窗口历史值 */
  uint8_t  imon_ring_idx;                /**< 环形缓冲写指针 */
  uint8_t  rst_cause;                    /**< 上次复位原因，见 BSP_WDG_RST_xxx */
} App_Power_Struct;

/** 模块实例 */
extern App_Power_Struct App_Power;

/* ============================== 对外接口 ============================== */

/**
  * @brief  模块初始化
  * @note   内部完成 ADC 校准 + 启动循环 DMA，必须在 MX_ADC1_Init()、
  *         MX_DMA_Init()、MX_USART1_UART_Init() 之后调用
  */
void App_Power_Init(void);

/**
  * @brief  模块主任务
  * @note   在主循环中反复调用；count > 0 时直接返回，不阻塞
  */
void App_Power_Task(void);

/**
  * @brief  计时处理
  * @note   必须放入 1 ms 定时中断（本工程注册在 SysTick_Handler），仅递减 count
  */
void App_Power_Timing_Handle(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_POWER_H__ */

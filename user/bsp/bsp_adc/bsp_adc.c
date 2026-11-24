/**
  * @file    bsp_adc.c
  * @brief   5V 输出检测 —— ADC 采集与换算（BSP 层）
  * @note    本文件不做任何打印，不依赖 printf / UART
  */
#include "bsp_adc.h"
#include "adc.h"        /* CubeMX 生成的 hadc1 */
#include <string.h>

/* ============================== 模块实例 ============================== */

Bsp_Adc_Struct Bsp_Adc;

/* ============================== 内部函数 ============================== */

/**
  * @brief  判断通道索引是否合法
  * @param[in] ch 通道索引
  * @retval 1 = 合法，0 = 非法
  */
static uint8_t bsp_adc_is_valid(uint8_t ch)
{
  return ((ch < BSP_ADC_CH_COUNT) ? 1U : 0U);
}

/* ============================== 对外接口 ============================== */

HAL_StatusTypeDef BSP_ADC_Init(void)
{
  HAL_StatusTypeDef st;

  (void)memset((void *)Bsp_Adc.raw, 0, sizeof(Bsp_Adc.raw));
  Bsp_Adc.inited = 0U;

  /* H743 的 ADC 出厂未校准，不校准直接转换误差可达几十个 LSB */
  st = HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
  if (st != HAL_OK)
  {
    return st;
  }

  /* Circular DMA：启动一次，缓冲区持续刷新 */
  st = HAL_ADC_Start_DMA(&hadc1, (uint32_t *)(uintptr_t)&Bsp_Adc.raw[0],
                         (uint32_t)BSP_ADC_CH_COUNT);
  if (st != HAL_OK)
  {
    return st;
  }

  Bsp_Adc.inited = 1U;
  return HAL_OK;
}

uint16_t BSP_ADC_Get_Raw(uint8_t ch)
{
  if (bsp_adc_is_valid(ch) == 0U)
  {
    return 0U;
  }
  return Bsp_Adc.raw[ch];
}

uint32_t BSP_ADC_Get_Voltage_Mv(uint8_t ch)
{
  if (bsp_adc_is_valid(ch) == 0U)
  {
    return 0U;
  }

  return (((uint32_t)Bsp_Adc.raw[ch] * BSP_ADC_V_MV_NUM) / BSP_ADC_V_MV_DEN);
}

uint32_t BSP_ADC_Get_Imon_Uv(uint8_t ch)
{
  if (bsp_adc_is_valid(ch) == 0U)
  {
    return 0U;
  }

  return (((uint32_t)Bsp_Adc.raw[ch] * BSP_ADC_I_UV_NUM) / BSP_ADC_I_UV_DEN);
}

uint32_t BSP_ADC_Imon_Uv_To_Ma_x10(uint32_t imon_uv, uint32_t r_imon_ohm)
{
  uint32_t i_mon_x10;

  if (r_imon_ohm == 0U)
  {
    return 0U;
  }

  /* V_IMON/R_IMON 的数值就是流过采样电阻的电流，单位 uA；
     放大 10 倍得 0.1 uA 单位；除之前加半个除数，等效四舍五入，
     去掉纯截断带来的约 0.3% 偏小（例如 0.6 A 会读成 598 mA） */
  i_mon_x10 = ((imon_uv * 10U) + (r_imon_ohm / 2U)) / r_imon_ohm;

  /* 扣掉 I(IMON_OS) = 0.8 uA（= 8 个 0.1 uA），低于失调直接判 0 */
  if (i_mon_x10 <= 8U)
  {
    return 0U;
  }
  i_mon_x10 -= 8U;

  /* I_OUT(mA) = x10 × 1.912；换成 0.1 mA 单位就是 x10 × 1912 / 100 */
  return ((i_mon_x10 * 1912U) / 100U);
}

uint32_t BSP_ADC_Get_Current_Ma_x10(uint8_t ch, uint32_t r_imon_ohm)
{
  if (bsp_adc_is_valid(ch) == 0U)
  {
    return 0U;
  }

  return BSP_ADC_Imon_Uv_To_Ma_x10(BSP_ADC_Get_Imon_Uv(ch), r_imon_ohm);
}

uint32_t BSP_ADC_Get_Current_Ma(uint8_t ch, uint32_t r_imon_ohm)
{
  return (BSP_ADC_Get_Current_Ma_x10(ch, r_imon_ohm) / 10U);
}

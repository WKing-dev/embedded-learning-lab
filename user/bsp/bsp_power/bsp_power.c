/**
  * @file    bsp_power.c
  * @brief   5V 输出检测 —— eFuse 故障脚输入（BSP 层）
  * @note    本文件不做任何打印，不依赖 printf / UART
  */
#include "bsp_power.h"

/* ============================== 模块实例 ============================== */

Bsp_Power_Struct Bsp_Power;

/* ============================== 内部函数 ============================== */

/**
  * @brief  取故障输入对应的端口与引脚
  * @param[in]  idx  通道索引
  * @param[out] port 端口指针
  * @param[out] pin  引脚掩码
  * @retval 1 = 索引合法，0 = 非法
  */
static uint8_t bsp_power_pin_of(uint8_t idx, GPIO_TypeDef **port, uint16_t *pin)
{
  if (idx == BSP_POWER_FLT1)
  {
    *port = FLT1_GPIO_Port;
    *pin  = FLT1_Pin;
    return 1U;
  }

  if (idx == BSP_POWER_FLT2)
  {
    *port = FLT2_GPIO_Port;
    *pin  = FLT2_Pin;
    return 1U;
  }

  return 0U;
}

/* ============================== 对外接口 ============================== */

void BSP_Power_Init(void)
{
  uint8_t i;

  for (i = 0U; i < BSP_POWER_FLT_NUM; i++)
  {
    Bsp_Power.flt[i] = 0U;
  }
  Bsp_Power.inited = 0U;

  /* 上电先采一次，避免把"开机就处于故障"的沿误记成新故障 */
  for (i = 0U; i < BSP_POWER_FLT_NUM; i++)
  {
    (void)BSP_Power_Read_Flt(i);
  }

  Bsp_Power.inited = 1U;
}

uint8_t BSP_Power_Read_Flt(uint8_t idx)
{
  GPIO_TypeDef *port = 0;
  uint16_t      pin  = 0U;
  uint8_t       level;

  if (bsp_power_pin_of(idx, &port, &pin) == 0U)
  {
    return 0U;
  }

  /* FLT 开漏、低有效：读到低电平表示该路 eFuse 报故障 */
  level = (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) ? 1U : 0U;
  Bsp_Power.flt[idx] = level;

  return level;
}

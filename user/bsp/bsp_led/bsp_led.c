/**
  * @file    bsp_led.c
  * @brief   板载指示灯 LED1 / LED2（BSP 层）
  * @note    本文件不做任何打印，不依赖 printf / UART
  */
#include "bsp_led.h"

/* ============================== 模块实例 ============================== */

Bsp_Led_Struct Bsp_Led;

/* ============================== 内部函数 ============================== */

/**
  * @brief  取指示灯对应的端口与引脚
  * @param[in]  idx  通道索引
  * @param[out] port 端口指针
  * @param[out] pin  引脚掩码
  * @retval 1 = 索引合法，0 = 非法
  */
static uint8_t bsp_led_pin_of(uint8_t idx, GPIO_TypeDef **port, uint16_t *pin)
{
  if (idx == BSP_LED_1)
  {
    *port = LED1_GPIO_Port;
    *pin  = LED1_Pin;
    return 1U;
  }

  if (idx == BSP_LED_2)
  {
    *port = LED2_GPIO_Port;
    *pin  = LED2_Pin;
    return 1U;
  }

  return 0U;
}

/**
  * @brief  写一路指示灯的电平并同步状态
  * @param[in] idx 通道索引
  * @param[in] on  1 = 亮（高电平），0 = 灭
  */
static void bsp_led_write(uint8_t idx, uint8_t on)
{
  GPIO_TypeDef *port = 0;
  uint16_t      pin  = 0U;

  if (bsp_led_pin_of(idx, &port, &pin) == 0U)
  {
    return;
  }

  /* 本板为高电平点亮 */
  HAL_GPIO_WritePin(port, pin, (on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  Bsp_Led.on[idx] = (on != 0U) ? 1U : 0U;
}

/* ============================== 对外接口 ============================== */

void BSP_LED_Init(void)
{
  uint8_t i;

  Bsp_Led.count  = 0U;
  Bsp_Led.inited = 0U;

  for (i = 0U; i < BSP_LED_NUM; i++)
  {
    Bsp_Led.on[i]        = 0U;
    Bsp_Led.heartbeat[i] = 0U;
    bsp_led_write(i, 0U);       /* 上电全灭 */
  }

  /* 指示灯 1 作为心跳灯，程序在跑就一直闪 */
  Bsp_Led.heartbeat[BSP_LED_1] = 1U;

  Bsp_Led.inited = 1U;
}

void BSP_LED_On(uint8_t idx)
{
  bsp_led_write(idx, 1U);
}

void BSP_LED_Off(uint8_t idx)
{
  bsp_led_write(idx, 0U);
}

void BSP_LED_Toggle(uint8_t idx)
{
  uint8_t on;

  on = BSP_LED_Get(idx);
  bsp_led_write(idx, (on != 0U) ? 0U : 1U);
}

uint8_t BSP_LED_Get(uint8_t idx)
{
  if (idx >= BSP_LED_NUM)
  {
    return 0U;
  }

  return Bsp_Led.on[idx];
}

void BSP_LED_Set_Heartbeat(uint8_t idx, uint8_t enable)
{
  if (idx >= BSP_LED_NUM)
  {
    return;
  }

  Bsp_Led.heartbeat[idx] = (enable != 0U) ? 1U : 0U;
}

void BSP_LED_Task(void)
{
  uint8_t i;

  if (Bsp_Led.inited == 0U)
  {
    return;
  }

  /* 时间片控制：未到期直接返回，严禁阻塞 */
  if (Bsp_Led.count > 0U)
  {
    return;
  }
  Bsp_Led.count = (uint16_t)BSP_LED_HEARTBEAT_MS;

  for (i = 0U; i < BSP_LED_NUM; i++)
  {
    if (Bsp_Led.heartbeat[i] != 0U)
    {
      BSP_LED_Toggle(i);
    }
  }
}

void BSP_LED_Timing_Handle(void)
{
  if (Bsp_Led.count > 0U)
  {
    Bsp_Led.count--;
  }
}

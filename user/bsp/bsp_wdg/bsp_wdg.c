/**
  * @file    bsp_wdg.c
  * @brief   外部看门狗 STWD100 —— 喂狗信号输出（BSP 层）
  * @note    本文件不做任何打印，不依赖 printf / UART
  */
#include "bsp_wdg.h"

/* ============================== 模块实例 ============================== */

Bsp_Wdg_Struct Bsp_Wdg;

/* ============================== 对外接口 ============================== */

void BSP_WDG_Init(void)
{
  Bsp_Wdg.feed_cnt = 0U;
  Bsp_Wdg.count    = 0U;
  Bsp_Wdg.level    = 0U;
  Bsp_Wdg.inited   = 0U;

  /* 上电先给一个确定电平，避免引脚悬空被当成一次边沿 */
  HAL_GPIO_WritePin(WDI_GPIO_Port, WDI_Pin, GPIO_PIN_RESET);

  Bsp_Wdg.inited = 1U;
}

void BSP_WDG_Task(void)
{
  GPIO_PinState st;

  if (Bsp_Wdg.inited == 0U)
  {
    return;
  }

  /* 时间片控制：未到期直接返回，严禁阻塞 */
  if (Bsp_Wdg.count > 0U)
  {
    return;
  }
  Bsp_Wdg.count = (uint16_t)BSP_WDG_PERIOD_MS;

  /* STWD100 是边沿触发式喂狗：翻转电平才算喂，保持高或低都不算 */
  Bsp_Wdg.level ^= 1U;
  st = (Bsp_Wdg.level != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
  HAL_GPIO_WritePin(WDI_GPIO_Port, WDI_Pin, st);

  Bsp_Wdg.feed_cnt++;
}

void BSP_WDG_Timing_Handle(void)
{
  if (Bsp_Wdg.count > 0U)
  {
    Bsp_Wdg.count--;
  }
}

uint32_t BSP_WDG_Get_Feed_Cnt(void)
{
  return Bsp_Wdg.feed_cnt;
}

uint8_t BSP_WDG_Get_Level(void)
{
  return Bsp_Wdg.level;
}

uint8_t BSP_WDG_Get_Reset_Cause(void)
{
  uint32_t rsr = RCC->RSR;
  uint8_t  cause;

  /* 顺序有讲究：上电时 POR 和 PIN 可能同时置位，
     所以把 PIN 放到 POR 之后判断，避免把上电误报成看门狗复位 */
  if ((rsr & RCC_RSR_IWDG1RSTF) != 0U)
  {
    cause = BSP_WDG_RST_IWDG;
  }
  else if ((rsr & RCC_RSR_WWDG1RSTF) != 0U)
  {
    cause = BSP_WDG_RST_WWDG;
  }
  else if ((rsr & RCC_RSR_LPWRRSTF) != 0U)
  {
    cause = BSP_WDG_RST_LPWR;
  }
  else if ((rsr & RCC_RSR_SFTRSTF) != 0U)
  {
    cause = BSP_WDG_RST_SOFT;
  }
  else if ((rsr & RCC_RSR_BORRSTF) != 0U)
  {
    cause = BSP_WDG_RST_BOR;
  }
  else if ((rsr & RCC_RSR_PORRSTF) != 0U)
  {
    cause = BSP_WDG_RST_POR;
  }
  else if ((rsr & RCC_RSR_PINRSTF) != 0U)
  {
    cause = BSP_WDG_RST_PIN;
  }
  else
  {
    cause = BSP_WDG_RST_UNKNOWN;
  }

  /* 读后清除，避免下次上电被上次的残留标志影响 */
  __HAL_RCC_CLEAR_RESET_FLAGS();

  return cause;
}

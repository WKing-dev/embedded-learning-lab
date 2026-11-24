/**
  * @file    app_power.c
  * @brief   5V 输出检测 —— 应用层（状态机 + 周期打印）
  * @note    本文件只做业务逻辑与打印，ADC / GPIO 细节全部在 bsp 层
  */
#include <stdio.h>
#include "app_power.h"
#include "bsp_adc.h"
#include "bsp_power.h"
#include "bsp_wdg.h"

/* ============================== 模块实例 ============================== */

App_Power_Struct App_Power;

/* ============================== 内部常量 ============================== */

/** 复位原因代号 → 打印字符串 */
static const char *app_power_rst_name(uint8_t cause)
{
  switch (cause)
  {
    case BSP_WDG_RST_PIN:  return "PIN(NRST, ext-wdg or key)";
    case BSP_WDG_RST_POR:  return "POWER-ON";
    case BSP_WDG_RST_BOR:  return "BROWN-OUT";
    case BSP_WDG_RST_SOFT: return "SOFTWARE";
    case BSP_WDG_RST_IWDG: return "IWDG";
    case BSP_WDG_RST_WWDG: return "WWDG";
    case BSP_WDG_RST_LPWR: return "LOW-POWER";
    default:               return "UNKNOWN";
  }
}

/** 电压通道顺序与名字（打印用，只读不占变量） */
static const uint8_t app_power_v_ch[APP_POWER_V_NUM] =
{
  BSP_ADC_CH_5V1, BSP_ADC_CH_5V2, BSP_ADC_CH_5V3
};
static const char *const app_power_v_name[APP_POWER_V_NUM] =
{
  "5V1", "5V2", "5V3"
};

/** 电流 / 故障通道顺序与名字（打印用） */
static const uint8_t app_power_i_ch[APP_POWER_I_NUM] =
{
  BSP_ADC_CH_IMON1, BSP_ADC_CH_IMON2
};
static const char *const app_power_i_name[APP_POWER_I_NUM] =
{
  "IMON1", "IMON2"
};

/* ============================== 内部函数 ============================== */

/**
  * @brief  采集一轮数据（电压 / IMON / FLT）
  * @retval 1 = 至少一路处于故障，0 = 全部正常
  */
static uint8_t app_power_sample(void)
{
  uint8_t  i;
  uint8_t  fault = 0U;
  uint32_t imon_now;

  for (i = 0U; i < APP_POWER_V_NUM; i++)
  {
    App_Power.v_mv[i] = BSP_ADC_Get_Voltage_Mv(app_power_v_ch[i]);
  }

  for (i = 0U; i < APP_POWER_I_NUM; i++)
  {
    /* 原始码值与电压取同一时刻，保证打印出来的一行能互相对照 */
    App_Power.imon_raw[i] = BSP_ADC_Get_Raw(app_power_i_ch[i]);
    imon_now = BSP_ADC_Get_Imon_Uv(app_power_i_ch[i]);

    /* 滑动平均：环形窗口内"减旧值、加新值"，避免累加求和溢出 */
    App_Power.imon_sum[i] -= App_Power.imon_ring[i][App_Power.imon_ring_idx];
    App_Power.imon_ring[i][App_Power.imon_ring_idx] = imon_now;
    App_Power.imon_sum[i] += imon_now;
    App_Power.imon_uv[i] = App_Power.imon_sum[i] / APP_POWER_IMON_AVG_NUM;

    /* 读 FLT 电平：低有效，1 = 故障 */
    App_Power.flt[i] = BSP_Power_Read_Flt(i);

    /* 上升沿（正常 -> 故障）累计一次；
       自动重试型 eFuse 会周期性重试，用锁定窗口做去重，1 s 内只记一次 */
    if ((App_Power.flt[i] != 0U) && (App_Power.flt_prev[i] == 0U))
    {
      if (App_Power.flt_lock[i] == 0U)
      {
        App_Power.flt_cnt[i]++;
        App_Power.flt_lock[i] = (uint8_t)APP_POWER_FLT_LOCK_TASK;
      }
    }
    App_Power.flt_prev[i] = App_Power.flt[i];

    if (App_Power.flt_lock[i] > 0U)
    {
      App_Power.flt_lock[i]--;
    }

    if (App_Power.flt[i] != 0U)
    {
      fault = 1U;
    }
  }

  /* 两个通道共用同一个写指针，每次采样只前进一格 */
  App_Power.imon_ring_idx++;
  if (App_Power.imon_ring_idx >= APP_POWER_IMON_AVG_NUM)
  {
    App_Power.imon_ring_idx = 0U;
  }

  return fault;
}

/**
  * @brief  打印一轮采样结果
  * @note   同时打印原始码值，便于核对 Rank 与信号是否对应
  */
static void app_power_print(void)
{
  uint8_t i;
#if (APP_POWER_IMON_R_OHM != 0U)
  uint32_t cur_x10;
#endif

  /*
    三路输出电压：R22 实际 200 kΩ，分压比 0.709，5V 母线会让 ADC 长期削顶，
    换算成电压没有意义，这里直接输出原始码值。
    等 R22 换成 20 kΩ 后，把下面一行换回 BSP_ADC_Get_Voltage_Mv() 的打印即可。
  */
  for (i = 0U; i < APP_POWER_V_NUM; i++)
  {
    printf("%s=%u ", app_power_v_name[i],
           (unsigned)BSP_ADC_Get_Raw(app_power_v_ch[i]));
  }
  printf("\r\n");

  /* 两路 IMON：IMON 是电流检测通道，用平均后的电压换算电流（0.1 mA 分辨率） */
  for (i = 0U; i < APP_POWER_I_NUM; i++)
  {
#if (APP_POWER_IMON_R_OHM != 0U)
    cur_x10 = BSP_ADC_Imon_Uv_To_Ma_x10(App_Power.imon_uv[i], APP_POWER_IMON_R_OHM);
    printf("%s=%lu.%lumA(%luuV)  ", app_power_i_name[i],
           (unsigned long)(cur_x10 / 10U),
           (unsigned long)(cur_x10 % 10U),
           (unsigned long)App_Power.imon_uv[i]);
#else
    /* 采样电阻未配置时退化为打印引脚电压（uV，已平均） */
    printf("%s=%luuV  ", app_power_i_name[i],
           (unsigned long)App_Power.imon_uv[i]);
#endif
  }

  /* 故障状态与累计次数 */
  printf("FLT1=%u FLT2=%u fcnt=%lu/%lu\r\n",
         (unsigned)App_Power.flt[0], (unsigned)App_Power.flt[1],
         (unsigned long)App_Power.flt_cnt[0],
         (unsigned long)App_Power.flt_cnt[1]);

  /* 外部看门狗自检：喂狗计数应约 10 次/秒，uptime 持续增长说明没被复位 */
  printf("WDG feed=%lu WDI=%u up=%lus\r\n",
         (unsigned long)BSP_WDG_Get_Feed_Cnt(),
         (unsigned)BSP_WDG_Get_Level(),
         (unsigned long)(HAL_GetTick() / 1000U));
}

/* ============================== 对外接口 ============================== */

void App_Power_Init(void)
{
  uint8_t i;
  uint8_t j;

  App_Power.state         = APP_POWER_IDLE;
  App_Power.count         = 0U;
  App_Power.print_div     = 0U;
  App_Power.imon_ring_idx = 0U;

  for (i = 0U; i < APP_POWER_I_NUM; i++)
  {
    App_Power.flt[i]      = 0U;
    App_Power.flt_prev[i] = 0U;
    App_Power.flt_lock[i] = 0U;
    App_Power.flt_cnt[i]  = 0U;
    App_Power.imon_raw[i] = 0U;
    App_Power.imon_uv[i]  = 0U;
    App_Power.imon_sum[i] = 0U;

    /* 平均窗口清零，从 0 开始累积（约 8 个采样周期后稳定） */
    for (j = 0U; j < APP_POWER_IMON_AVG_NUM; j++)
    {
      App_Power.imon_ring[i][j] = 0U;
    }
  }

  for (i = 0U; i < APP_POWER_V_NUM; i++)
  {
    App_Power.v_mv[i] = 0U;
  }

  BSP_Power_Init();

  /* 复位原因要在启动最早期读一次（读后标志即被清除） */
  App_Power.rst_cause = BSP_WDG_Get_Reset_Cause();

  if (BSP_ADC_Init() != HAL_OK)
  {
    printf("ADC init FAIL\r\n");
    return;
  }

  App_Power.state = APP_POWER_RUN;
  printf("ADC init OK, 16bit, 5 channels, 36MHz\r\n");
  printf("last reset: %s\r\n", app_power_rst_name(App_Power.rst_cause));
}

void App_Power_Task(void)
{
  uint8_t fault;

  /* 时间片控制：未到期直接返回，严禁阻塞等待 */
  if (App_Power.count > 0U)
  {
    return;
  }
  App_Power.count = (uint16_t)APP_POWER_TASK_PERIOD_MS;

  switch (App_Power.state)
  {
    case APP_POWER_RUN:
    case APP_POWER_FAULT:
      fault = app_power_sample();

      /* 状态迁移：有故障进 FAULT，全部恢复回 RUN */
      App_Power.state = (fault != 0U) ? APP_POWER_FAULT : APP_POWER_RUN;

      App_Power.print_div++;
      if (App_Power.print_div >= APP_POWER_PRINT_DIV)
      {
        App_Power.print_div = 0U;
        app_power_print();
      }
      break;

    case APP_POWER_IDLE:
    default:
      /* ADC 未就绪，保持空闲，等待重新初始化 */
      App_Power.state = APP_POWER_IDLE;
      break;
  }
}

void App_Power_Timing_Handle(void)
{
  if (App_Power.count > 0U)
  {
    App_Power.count--;
  }
}

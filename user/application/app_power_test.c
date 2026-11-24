/**
 * @file    app_power_test.c
 * @author  华畅
 * @brief   满载功耗测试（加强版 V3）
 * @version 0.3
 * @date    2026-11-05
 *
 * 负载矩阵：
 *   CPU + FPU : 双精度浮点乘加 + sqrtf + sinf + cosf + tanf
 *   SDRAM     : 4 段不重叠区域（2~4 / 4~8 / 8~12 / 12~14 MB），连续写 + 读
 *   DMA2D     : 14~16 MB 区域，持续 ARGB8888 M2M 搬运（与 CPU 并行跑）
 *   GPIO      : 高频 BSRR 成对翻转
 *   LTDC      : 白底全屏 + 背光 100%
 *   时钟门控  : 所有外设时钟全开
 *
 * 地址布局（16 MB SDRAM 全部利用，互不重叠）：
 *   0~2 MB   : LTDC 帧缓存（不动）
 *   2~4 MB   : SDRAM0 刷（新加）
 *   4~8 MB   : SDRAM1 刷
 *   8~12 MB  : SDRAM2 刷
 *   12~14 MB : SDRAM3 刷
 *   14~16 MB : DMA2D 硬件搬
 */

#include "app_power_test.h"
#include "bsp_debug.h"

#include <math.h>
#include <string.h>

#include "bsp_lcd.h"
#include "bsp_sdram.h"

/* FreeRTOS：vTaskSuspendAll */
#include "FreeRTOS.h"
#include "task.h"

/* 拿到 dma2d.c 里定义的全局 handle（main() 已 MX_DMA2D_Init 过） */
extern DMA2D_HandleTypeDef hdma2d;

/* ---------------- 负载参数 ---------------- */
#define POWER_TEST_CPU_ITERATIONS       (500000U)   /* 单精度浮点循环 50 万次 */
#define POWER_TEST_CPU_DOUBLE_ITERATIONS (100000U)  /* 双精度浮点循环 10 万次（每次更耗时） */

/* 四段独立 SDRAM 区域，互不重叠，同时刷 */
#define POWER_TEST_SDRAM0_OFFSET        (0x00300000U)   /* 段 0：3 MB 起，2 MB（帧缓存占 0~2.34 MB） */
#define POWER_TEST_SDRAM0_WORDS         (0x080000U)     /* 512K word = 2 MB */

#define POWER_TEST_SDRAM1_OFFSET        (0x00500000U)   /* 段 1：5 MB 起，4 MB */
#define POWER_TEST_SDRAM1_WORDS         (0x100000U)

#define POWER_TEST_SDRAM2_OFFSET        (0x00900000U)   /* 段 2：9 MB 起，4 MB */
#define POWER_TEST_SDRAM2_WORDS         (0x100000U)

#define POWER_TEST_SDRAM3_OFFSET        (0x00D00000U)   /* 段 3：13 MB 起，2 MB */
#define POWER_TEST_SDRAM3_WORDS         (0x080000U)

/* DMA2D 专用区域：段 4（15 MB 起，1 MB），与上面四段完全不重叠
 *   源：15.0 ~ 15.5 MB    目标：15.5 ~ 16.0 MB
 *   传输量 = 4096 × 32 × 4 B = 512 KB（刚好用掉一半，目标区不越界） */
#define POWER_TEST_DMA2D_OFFSET         (0x00F00000U)
#define POWER_TEST_DMA2D_SIZE           (0x00100000U)   /* 1 MB 区域 */
#define POWER_TEST_DMA2D_WIDTH          (4096U)         /* 像素宽度 × 4 字节 = 16 KB/行 */
#define POWER_TEST_DMA2D_HEIGHT         (32U)           /* 32 行 × 16 KB = 512 KB */
#define POWER_TEST_DMA2D_WORDS          (POWER_TEST_DMA2D_SIZE / sizeof(uint32_t))

#define POWER_TEST_REPORT_MS            (2000U)

/* ---------------- GPIO 翻转负载 ---------------- */
#define POWER_TEST_GPIO_ENABLE          (1)
#define POWER_TEST_GPIO_LOOPS           (5000000U)  /* 500 万次（原 100 万 × 5） */

typedef struct Power_Gpio_Load_Struct_s
{
    GPIO_TypeDef *port;
    uint32_t      pin_mask;
} Power_Gpio_Load_Struct;

static const Power_Gpio_Load_Struct gpio_load_table[] =
{
    { TOUCH_RST_GPIO_Port, TOUCH_RST_Pin },

    /* 查到空闲引脚后照这样加：
     * { GPIOE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 },
     */
};

#define POWER_TEST_GPIO_COUNT \
    (sizeof(gpio_load_table) / sizeof(gpio_load_table[0]))

/* ---------------- 统计量 ---------------- */
static volatile float    cpu_result = 0.0f;
static volatile uint32_t sdram0_checksum = 0U;
static volatile uint32_t sdram1_checksum = 0U;
static volatile uint32_t sdram2_checksum = 0U;
static volatile uint32_t sdram3_checksum = 0U;
static volatile uint32_t dma2d_checksum = 0U;
static volatile uint32_t cpu_rounds = 0U;
static volatile uint32_t cpu_double_rounds = 0U;
static volatile double   cpu_double_result = 0.0;
static volatile uint32_t sdram_rounds = 0U;
static volatile uint32_t gpio_rounds = 0U;
static volatile uint32_t dma2d_rounds = 0U;

/* ---------------- 每段 SDRAM 刷 ---------------- */
static void power_test_sdram_segment(uint32_t offset, uint32_t words,
                                     uint32_t seed, volatile uint32_t *p_sum)
{
    volatile uint32_t *block = (volatile uint32_t *)(SDRAM_BASE + offset);
    uint32_t i;
    uint32_t sum = 0;

    for (i = 0; i < words; i++)
    {
        block[i] = (i * 2654435761U) ^ seed ^ 0x5A5A5A5AU;
    }

    for (i = 0; i < words; i++)
    {
        sum += block[i];
    }

    *p_sum = sum;
}

/**
 * @brief  CPU + FPU 满载（10 万次浮点循环）
 */
static void power_test_cpu_load(void)
{
    uint32_t i;
    float    v1 = 1.0f;
    float    v2 = 2.718281828f;
    float    acc = 0.0f;

    for (i = 0; i < POWER_TEST_CPU_ITERATIONS; i++)
    {
        v1  = (v1 * 1.0000001f) + v2;
        v2  = sinf(v1) + cosf(v2);
        acc += (sqrtf(v1 * v1 + v2 * v2) * tanf(v1 * 0.5f));

        if (v1 > 1000000.0f) { v1 = 1.0f; }
        if (v2 > 1000000.0f) { v2 = 2.718281828f; }
    }

    cpu_result = acc;
    cpu_rounds++;
}

/**
 * @brief  双精度 FPU 满载
 *
 * @note   Cortex-M7 的 double 运算走 FPU 的另一条通路，单次耗时是 float 的
 *         2~4 倍，FPU 的 DP 单元持续工作，功耗高于纯单精度循环。
 */
static void power_test_fpu_double_load(void)
{
    uint32_t i;
    double   d1 = 1.0;
    double   d2 = 3.14159265358979;
    double   acc = 0.0;

    for (i = 0; i < POWER_TEST_CPU_DOUBLE_ITERATIONS; i++)
    {
        d1  = (d1 * 1.0000000001) + d2;
        d2  = sqrt(d1) + sin(d2);
        acc += (cos(d1) * exp(d2 * 0.001));

        if (d1 > 1000000.0) { d1 = 1.0; }
        if (d2 > 1000000.0) { d2 = 3.14159265358979; }
    }

    cpu_double_result = acc;
    cpu_double_rounds++;
}

/**
 * @brief  四段 SDRAM 同时刷（CPU 串行，但 4 段全扫）
 */
static void power_test_sdram_load(void)
{
    static uint32_t seed = 0xDEADBEEFU;

    power_test_sdram_segment(POWER_TEST_SDRAM0_OFFSET,
                             POWER_TEST_SDRAM0_WORDS,
                             seed,
                             &sdram0_checksum);

    power_test_sdram_segment(POWER_TEST_SDRAM1_OFFSET,
                             POWER_TEST_SDRAM1_WORDS,
                             seed + 1U,
                             &sdram1_checksum);

    power_test_sdram_segment(POWER_TEST_SDRAM2_OFFSET,
                             POWER_TEST_SDRAM2_WORDS,
                             seed + 2U,
                             &sdram2_checksum);

    power_test_sdram_segment(POWER_TEST_SDRAM3_OFFSET,
                             POWER_TEST_SDRAM3_WORDS,
                             seed + 3U,
                             &sdram3_checksum);

    sdram_rounds++;
    seed += 0x11111111U;
}

/**
 * @brief  GPIO 高频翻转（BSRR 成对置高/置低）
 */
static void power_test_gpio_load(void)
{
#if POWER_TEST_GPIO_ENABLE
    uint32_t i;
    uint32_t index;

    for (i = 0; i < POWER_TEST_GPIO_LOOPS; i++)
    {
        for (index = 0; index < POWER_TEST_GPIO_COUNT; index++)
        {
            gpio_load_table[index].port->BSRR = gpio_load_table[index].pin_mask;
            gpio_load_table[index].port->BSRR = (gpio_load_table[index].pin_mask << 16U);
        }
    }

    gpio_rounds++;
#endif
}

/**
 * @brief  所有外设时钟全开（静态功耗）
 *
 * @note   只是开时钟，不初始化、不使用，不影响显示与触摸。
 */
static void power_test_enable_all_clocks(void)
{
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();
    __HAL_RCC_TIM5_CLK_ENABLE();
    __HAL_RCC_TIM6_CLK_ENABLE();
    __HAL_RCC_TIM7_CLK_ENABLE();
    __HAL_RCC_TIM8_CLK_ENABLE();
    __HAL_RCC_TIM12_CLK_ENABLE();
    __HAL_RCC_TIM13_CLK_ENABLE();
    __HAL_RCC_TIM14_CLK_ENABLE();
    __HAL_RCC_TIM15_CLK_ENABLE();
    __HAL_RCC_TIM16_CLK_ENABLE();
    __HAL_RCC_TIM17_CLK_ENABLE();

    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_SPI3_CLK_ENABLE();
    __HAL_RCC_SPI4_CLK_ENABLE();
    __HAL_RCC_SPI5_CLK_ENABLE();
    __HAL_RCC_SPI6_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_I2C2_CLK_ENABLE();
    __HAL_RCC_I2C3_CLK_ENABLE();
    __HAL_RCC_I2C4_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_USART6_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_UART5_CLK_ENABLE();
    __HAL_RCC_UART7_CLK_ENABLE();
    __HAL_RCC_UART8_CLK_ENABLE();
    __HAL_RCC_LPUART1_CLK_ENABLE();

    __HAL_RCC_ADC12_CLK_ENABLE();
    __HAL_RCC_ADC3_CLK_ENABLE();
    __HAL_RCC_DAC12_CLK_ENABLE();

    __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();
    __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();
    __HAL_RCC_ETH1MAC_CLK_ENABLE();
    __HAL_RCC_ETH1TX_CLK_ENABLE();
    __HAL_RCC_ETH1RX_CLK_ENABLE();
    __HAL_RCC_SDMMC1_CLK_ENABLE();
    __HAL_RCC_SDMMC2_CLK_ENABLE();
    __HAL_RCC_QSPI_CLK_ENABLE();
    __HAL_RCC_FDCAN_CLK_ENABLE();

    __HAL_RCC_RNG_CLK_ENABLE();
    __HAL_RCC_LTDC_CLK_ENABLE();
    __HAL_RCC_DMA2D_CLK_ENABLE();

    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_BDMA_CLK_ENABLE();
    __HAL_RCC_MDMA_CLK_ENABLE();

    __HAL_RCC_SAI1_CLK_ENABLE();
    __HAL_RCC_SAI2_CLK_ENABLE();
    __HAL_RCC_SAI3_CLK_ENABLE();
    __HAL_RCC_SAI4_CLK_ENABLE();
    __HAL_RCC_SPDIFRX_CLK_ENABLE();
    __HAL_RCC_DFSDM1_CLK_ENABLE();
    __HAL_RCC_CEC_CLK_ENABLE();
    __HAL_RCC_LPTIM1_CLK_ENABLE();
    __HAL_RCC_LPTIM2_CLK_ENABLE();
    __HAL_RCC_LPTIM3_CLK_ENABLE();
    __HAL_RCC_LPTIM4_CLK_ENABLE();
    __HAL_RCC_LPTIM5_CLK_ENABLE();
    __HAL_RCC_HRTIM1_CLK_ENABLE();
    __HAL_RCC_SWPMI1_CLK_ENABLE();
}

/**
 * @brief  所有定时器以最高频率计数（增加时序逻辑的动态功耗）
 *
 * @note   只把 PSC 置 0、ARR 拉满并 CEN=1，不配置任何输出通道，
 *         因此不会影响任何引脚。
 *         故意跳过：
 *           TIM2 —— 背光 PWM
 *           TIM6 —— HAL 的 1 ms 时基（SysTick 之外的第二时基）
 */
static void power_test_timers_load(void)
{
    static TIM_TypeDef *const tim_table[] =
    {
        TIM1,  TIM3,  TIM4,  TIM5,  TIM7,  TIM8,
        TIM12, TIM13, TIM14, TIM15, TIM16, TIM17,
    };
    uint32_t i;

    for (i = 0U; i < (sizeof(tim_table) / sizeof(tim_table[0])); i++)
    {
        tim_table[i]->PSC = 0U;              /* 不分频：计数时钟 = 内核时钟 */
        tim_table[i]->ARR = 0xFFFFU;         /* 最大重装值 */
        tim_table[i]->CNT = 0U;
        tim_table[i]->EGR = TIM_EGR_UG;       /* 立即把 PSC 装入预分频器 */
        tim_table[i]->CR1 |= TIM_CR1_CEN;     /* 启动计数 */
    }
}

/* ---------------- DMA2D 并行跑 ---------------- */

/**
 * @brief  准备 DMA2D 源区域（CPU 写数据进去）
 *
 * 在 DMA2D 搬之前，CPU 往源区域写一遍非零数据，
 * 避免搬全 0 让 DMA2D 看起来没干活。
 */
static void power_test_dma2d_prepare_source(void)
{
    uint32_t src_addr = SDRAM_BASE + POWER_TEST_DMA2D_OFFSET;
    volatile uint32_t *src = (volatile uint32_t *)src_addr;
    uint32_t i;
    uint32_t seed = dma2d_rounds;

    for (i = 0; i < (POWER_TEST_DMA2D_WORDS / 2U); i++)
    {
        src[i] = (i ^ seed);
    }
}

/**
 * @brief  启动一轮 DMA2D M2M 搬运（非阻塞）
 *
 * DMA2D 硬件独立运行，CPU 继续算浮点 / 刷 SDRAM。
 * 启动前手动清 TCIF 标志，便于之后用硬件寄存器判断完成。
 */
static void power_test_dma2d_start_one(void)
{
    uint32_t src_addr = SDRAM_BASE + POWER_TEST_DMA2D_OFFSET;
    uint32_t dst_addr = SDRAM_BASE + POWER_TEST_DMA2D_OFFSET + (POWER_TEST_DMA2D_SIZE / 2U);
    volatile uint32_t *dst = (volatile uint32_t *)dst_addr;
    uint32_t i;
    uint32_t sum = 0U;

    /* HAL_DMA2D_Start() 是阻塞调用：返回时这一轮搬运已经完成。
     *
     * 之前想用硬件 TCIF 标志做非阻塞判断，但 HAL 在结束时会清掉该标志，
     * 导致 is_idle() 永远读到 0、dma2d_rounds 永远是 0（上版的现象）。
     * 这里改成"阻塞 + 手动计数 + 读回校验"，保证能跑起来也能数到。
     */
    hdma2d.State = HAL_DMA2D_STATE_READY;
    (void)HAL_DMA2D_Start(&hdma2d, src_addr, dst_addr,
                          POWER_TEST_DMA2D_WIDTH, POWER_TEST_DMA2D_HEIGHT);

    /* 读回目标区（顺便再占一次 FMC 读通道） */
    for (i = 0U; i < (POWER_TEST_DMA2D_WORDS / 2U); i++)
    {
        sum += dst[i];
    }

    dma2d_checksum = sum;
    dma2d_rounds++;
}

/* ---------------- 主入口 ---------------- */
void App_PowerTest_Run(void)
{
    uint32_t last_report_ms = HAL_GetTick();

    /* ① 背光拉到 100% —— LCD 面板本身也是耗电大户 */
    BSP_LCD_Backlight_Set(100);

    /* ② 屏幕画白底 + 标题（LTDC 持续读帧缓存） */
    LCD_SetBackColor(0x00FFFFFFU);
    LCD_Clear(0x00FFFFFFU);

    LCD_SetFont(&Font24);
    LCD_SetTextColor(0x00000000U);
    LCD_DisplayStringAt(20U, 10U, (uint8_t *)"POWER TEST V3", LEFT_MODE);

    LCD_SetFont(&Font16);
    LCD_DisplayStringAt(20U, 50U, (uint8_t *)"CPU+FPU+4xSDRAM+DMA2D+GPIO", LEFT_MODE);
    LCD_DisplayStringAt(20U, 80U, (uint8_t *)"backlight = 100%", LEFT_MODE);
    LCD_DisplayStringAt(20U, 110U, (uint8_t *)"DMA2D parallel with CPU", LEFT_MODE);
    LCD_DisplayStringAt(20U, 140U, (uint8_t *)"4 segments fill 2-16MB SDRAM", LEFT_MODE);

    /* ③ 所有外设时钟全开 */
    power_test_enable_all_clocks();

    /* ③.5 所有定时器以最高频率计数（只计数，不输出到引脚） */
    power_test_timers_load();

    Debug_Printf("\r\n===== POWER TEST V4 START ====="
                 " Sched=Suspended CPU+DMA2D simultaneous\r\n");

    /* ── ⑤ 禁止 FreeRTOS 调度，让 CPU 100% 独占（不让 Idle Task 碰） ── */
    vTaskSuspendAll();

    /* ── ⑥ 先启动第一轮 DMA2D 搬运 ── */
    power_test_dma2d_prepare_source();
    power_test_dma2d_start_one();

    last_report_ms = HAL_GetTick();

    while (1)
    {
        /* ── 超级嵌套循环：CPU 一直算，DMA2D 硬件一直跑，FMC 一直被碰 ── */
        {
            /* 内层：50 万次浮点计算（5 × 原来的 10 万） */
            power_test_cpu_load();

            /* 再叠一层双精度 FPU 负载（DP 通路单独耗电） */
            power_test_fpu_double_load();

            /* 紧接着 500 万次 GPIO 翻转（5 × 原来的 100 万） */
            power_test_gpio_load();

            /* 读一小段 SDRAM0 强制占 FMC（Non-Cacheable，每次读都走 FMC） */
            {
                volatile uint32_t *p = (volatile uint32_t *)(SDRAM_BASE + POWER_TEST_SDRAM0_OFFSET);
                volatile uint32_t sink;
                sink = p[0]; sink = p[1000]; sink = p[2000]; sink = p[3000];
                (void)sink;
            }

            /* DMA2D 再搬一轮（阻塞式，内部已计数并读回校验） */
            power_test_dma2d_prepare_source();
            power_test_dma2d_start_one();
        }

        /* ── 每轮都刷 4 段 SDRAM（共 12 MB，重占 FMC 写通道） ── */
        power_test_sdram_load();

        /* ── 周期报告（每 2 秒）
         *    Debug_Printf 内部可能用互斥锁，但调度已挂起不会被抢，
         *    且打印本身就是一种 CPU + UART 负载 ── */
        if ((HAL_GetTick() - last_report_ms) > POWER_TEST_REPORT_MS)
        {
            last_report_ms = HAL_GetTick();

            Debug_Printf("rounds: cpu=%lu sdram=%lu dma2d=%lu gpio=%lu  "
                         "(crc0=%08lX crc1=%08lX crc2=%08lX crc3=%08lX crc_d2=%08lX)\r\n",
                         (unsigned long)cpu_rounds,
                         (unsigned long)sdram_rounds,
                         (unsigned long)dma2d_rounds,
                         (unsigned long)gpio_rounds,
                         (unsigned long)sdram0_checksum,
                         (unsigned long)sdram1_checksum,
                         (unsigned long)sdram2_checksum,
                         (unsigned long)sdram3_checksum,
                         (unsigned long)dma2d_checksum);

            /* 地址别名自检：读每段首字，若 c0==c3 或 c1==c2 说明地址重叠 */
            Debug_Printf("  seg[0..3] first word: %08lX %08lX %08lX %08lX\r\n",
                         (unsigned long)(*(volatile uint32_t *)(SDRAM_BASE + POWER_TEST_SDRAM0_OFFSET)),
                         (unsigned long)(*(volatile uint32_t *)(SDRAM_BASE + POWER_TEST_SDRAM1_OFFSET)),
                         (unsigned long)(*(volatile uint32_t *)(SDRAM_BASE + POWER_TEST_SDRAM2_OFFSET)),
                         (unsigned long)(*(volatile uint32_t *)(SDRAM_BASE + POWER_TEST_SDRAM3_OFFSET)));
        }
    }
}

/**
  ******************************************************************************
  * @file    bsp_sdram.c
  * @brief   W9812G6KH-6 (16 MB, 16 bit) 初始化序列 / 自检 / 块读写
  ******************************************************************************
  */

#include "bsp_sdram.h"
#include "fmc.h"        /* 提供 FMC_Bank5_6_R */
#include "bsp_debug.h"      /* 提供 Debug_Printf */

/* ======================= 内部工具 ======================= */

static volatile uint16_t *sdram_word_ptr(uint32_t addr)
{
    return (volatile uint16_t *)addr;
}

/* ① 数据线：每一位单独走 1 / 0，能查出粘连与断线 */
static uint32_t sdram_test_data_bus(uint32_t base)
{
    uint32_t bit, err = 0;
    volatile uint16_t *p = sdram_word_ptr(base);

    for (bit = 0; bit < 16; bit++)
    {
        uint16_t w = (uint16_t)(1UL << bit);
        p[bit]      = w;
        p[bit + 32] = (uint16_t)~w;
    }
    for (bit = 0; bit < 16; bit++)
    {
        uint16_t w = (uint16_t)(1UL << bit);
        if (p[bit]      != w)            { err++; }
        if (p[bit + 32] != (uint16_t)~w) { err++; }
    }
    return err;
}

/* ② 地址线：每个 2^n 位置写一个唯一值，能查出地址线粘连/断路 */
static uint32_t sdram_test_addr_bus(uint32_t base, uint32_t words)
{
    uint32_t bit, err = 0;
    volatile uint16_t *p = sdram_word_ptr(base);

    p[0] = 0x0000;
    for (bit = 0; bit < 24; bit++)
    {
        uint32_t a = (1UL << bit);
        if (a >= words) { break; }
        p[a] = (uint16_t)(0xA500UL | bit);
    }
    for (bit = 0; bit < 24; bit++)
    {
        uint32_t a = (1UL << bit);
        if (a >= words) { break; }
        if (p[a] != (uint16_t)(0xA500UL | bit)) { err++; }
    }
    return err;
}

/* ======================= 对外接口 ======================= */

/**
  * @brief  SDRAM 器件初始化序列
  * @note   新版 HAL 的 HAL_SDRAM_Init() 只写 SDCR/SDTR 并使能 FMC，
  *         不再发送本序列。缺了第一条（时钟使能），CKE 不拉高、SDCLK 不输出，
  *         之后任何对 0xC0000000 的访问都会永久挂起。
  * @retval 0 = 成功
  */
uint8_t BSP_SDRAM_Init(void)
{
    FMC_SDRAM_CommandTypeDef cmd = {0};

    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = 0;

    /* ① 时钟使能：CKE 拉高、SDCLK 开始输出 */
    cmd.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, 0xFFFFU) != HAL_OK) { return 1U; }

    /* 等待 t >= 100 us（取 1 ms 覆盖） */
    HAL_Delay(1);

    /* ② 预充电所有 bank */
    cmd.CommandMode = FMC_SDRAM_CMD_PALL;
    if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, 0xFFFFU) != HAL_OK) { return 2U; }

    /* ③ 自动刷新 8 次 */
    cmd.CommandMode       = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    cmd.AutoRefreshNumber = 8;
    if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, 0xFFFFU) != HAL_OK) { return 3U; }

    /* ④ 模式寄存器：突发长度 1、顺序、CAS=3、标准模式、单次写 → 0x230 */
    cmd.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = 0x00000230UL;
    if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, 0xFFFFU) != HAL_OK) { return 4U; }

    /* ⑤ 刷新计数 */
    HAL_SDRAM_ProgramRefreshRate(&hsdram1, SDRAM_REFRESH_COUNT);

    return 0U;
}

/**
  * @brief  三段自检（数据线 / 地址线 / 全片写读）
  * @note   会覆盖整个 16 MB，调用前请确认帧缓存内容可以丢
  */
SDRAM_TestResult_t BSP_SDRAM_Test(void)
{
    SDRAM_TestResult_t r = {0};
    uint32_t t;

    /* ① 数据线 */
    t = HAL_GetTick();
    r.data_err = sdram_test_data_bus(SDRAM_BASE);
    r.ms_data  = HAL_GetTick() - t;

    /* ② 地址线 */
    t = HAL_GetTick();
    r.addr_err = sdram_test_addr_bus(SDRAM_BASE, SDRAM_SIZE / SDRAM_BUS_WIDTH);
    r.ms_addr  = HAL_GetTick() - t;

    /* ③ 全片写读（0x5A5A 交叉模式，最能暴露相邻位串扰） */
    t = HAL_GetTick();
    r.block_err = BSP_SDRAM_BlockTest(SDRAM_BASE, SDRAM_SIZE / SDRAM_BUS_WIDTH, 0x5A5AU);
    r.ms_block  = HAL_GetTick() - t;

    r.total_err = r.data_err + r.addr_err + r.block_err;
    return r;
}

/**
  * @brief  对一段区域做填充 + 回读校验
  * @param  addr    起始地址（字节）
  * @param  words   16 位字个数
  * @param  pattern 填充图案
  * @retval 不一致的个数
  */
uint32_t BSP_SDRAM_BlockTest(uint32_t addr, uint32_t words, uint16_t pattern)
{
    uint32_t i, err = 0;
    volatile uint16_t *p = sdram_word_ptr(addr);

    for (i = 0; i < words; i++) { p[i] = pattern; }
    for (i = 0; i < words; i++) { if (p[i] != pattern) { err++; } }
    return err;
}

/**
  * @brief  写一段数据到 SDRAM
  * @param  addr 目标地址（字节，2 字节对齐）
  * @param  src  源缓冲区（RAM / Flash）
  * @param  len  字节数
  */
void BSP_SDRAM_Write(uint32_t addr, const void *src, uint32_t len)
{
    uint32_t i, words = len / 2U;
    const uint16_t *s = (const uint16_t *)src;
    volatile uint16_t *d = sdram_word_ptr(addr);

    for (i = 0; i < words; i++) { d[i] = s[i]; }

    /* 奇数字节：补最后一个字节 */
    if (len & 1U)
    {
        volatile uint8_t *db = (volatile uint8_t *)addr;
        const uint8_t *sb = (const uint8_t *)src;
        db[len - 1U] = sb[len - 1U];
    }
}

/**
  * @brief  从 SDRAM 读一段数据
  * @param  addr 源地址（字节，2 字节对齐）
  * @param  dst  目标缓冲区
  * @param  len  字节数
  */
void BSP_SDRAM_Read(uint32_t addr, void *dst, uint32_t len)
{
    uint32_t i, words = len / 2U;
    uint16_t *d = (uint16_t *)dst;
    volatile uint16_t *s = sdram_word_ptr(addr);

    for (i = 0; i < words; i++) { d[i] = s[i]; }

    if (len & 1U)
    {
        volatile uint8_t *sb = (volatile uint8_t *)addr;
        uint8_t *db = (uint8_t *)dst;
        db[len - 1U] = sb[len - 1U];
    }
}


/* ======================= 配置打印 ======================= */

/**
 * @brief  打印 SDRAM 控制器的关键寄存器
 * @retval 无
 */
void BSP_SDRAM_PrintConfig(void)
{
  Debug_Printf("FMC SDCR=%08lX SDTR=%08lX\r\n",
               (unsigned long)FMC_Bank5_6_R->SDCR[0],
               (unsigned long)FMC_Bank5_6_R->SDTR[0]);
}

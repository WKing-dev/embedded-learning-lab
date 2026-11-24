/**
  ******************************************************************************
  * @file    bsp_sdram.h
  * @brief   W9812G6KH-6 (16 MB, 16 bit) on FMC Bank1 / 0xC0000000
  *
  *  说明：
  *   - FMC 的引脚 / 时钟 / SDCR / SDTR 由 CubeMX 生成的 fmc.c 负责
  *   - 本模块负责「SDRAM 器件初始化命令序列」+ 自检 + 块读写
  *   - BSP_SDRAM_Init() 必须在 HAL_SDRAM_Init() 之后调用（见 fmc.c）
  ******************************************************************************
  */
#ifndef __BSP_SDRAM_H
#define __BSP_SDRAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "fmc.h"

/* ============================ 容量与几何 ============================ */

/* W9812G6KH-6：4 Bank × 4096 行 × 512 列 × 16 bit = 16 MB */
#define SDRAM_BASE          (0xC0000000UL)              /* FMC Bank1 起始地址        */
#define SDRAM_SIZE          (16UL * 1024UL * 1024UL)    /* 总容量 16 MB              */
#define SDRAM_BUS_WIDTH     (2U)                        /* 数据总线 16 位 = 2 字节   */

/* 前 2 MB 留给 LTDC 帧缓存（RGB565 全屏 1.20 MB，留余量），其余用于自检/素材 */
#define SDRAM_FB_RESERVE    (0x00200000UL)
#define SDRAM_TEST_BASE     (SDRAM_BASE + SDRAM_FB_RESERVE)
#define SDRAM_TEST_WORDS    ((SDRAM_SIZE - SDRAM_FB_RESERVE) / SDRAM_BUS_WIDTH)

/* 刷新计数：COUNT = (64ms / 4096 行) × SDCLK(100MHz) − 20 ≈ 1542
 *
 * 【功耗测试专用】改成 760 ≈ 32 ms 刷一遍（原 64 ms 的一半）。
 * 刷新更频繁永远不会出错，只增加 SDRAM 颗粒的静态功耗，
 * 用于把板子温度推向更接近最坏情况。正式固件请改回 1542。 */
#define SDRAM_REFRESH_COUNT (760UL)

/* ============================ 接口 ============================ */

/* 自检结果（不在这里打印，由调用者决定怎么输出） */
typedef struct
{
    uint32_t data_err;      /* 数据线走位测试错误数 */
    uint32_t addr_err;      /* 地址线 2^n 测试错误数 */
    uint32_t block_err;     /* 全片写读错误数 */
    uint32_t total_err;     /* 三项之和 */
    uint32_t ms_data;
    uint32_t ms_addr;
    uint32_t ms_block;
} SDRAM_TestResult_t;

/* SDRAM 器件初始化序列（时钟使能 → 预充电 → 8 次刷新 → 模式寄存器 → 刷新计数）
 * 返回 0 = 成功，非 0 = 有命令执行失败 */
/**
 * @brief  打印 SDRAM 控制器的关键寄存器（SDCR / SDTR）
 *
 * @note   内部直接读 FMC 寄存器并输出到调试串口，便于核对时序配置。
 * @param  无
 * @return 无
 */
void BSP_SDRAM_PrintConfig(void);

uint8_t BSP_SDRAM_Init(void);

/* 三段自检：数据线 / 地址线 / 全片写读 */
SDRAM_TestResult_t BSP_SDRAM_Test(void);

/* 对指定区域做一次「填充 + 回读」校验，返回不一致的个数（老化测试用） */
uint32_t BSP_SDRAM_BlockTest(uint32_t addr, uint32_t words, uint16_t pattern);

/* 块读写（按 16 位字搬运，addr 必须是 2 字节对齐） */
void BSP_SDRAM_Write(uint32_t addr, const void *src, uint32_t len);
void BSP_SDRAM_Read(uint32_t addr, void *dst, uint32_t len);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_SDRAM_H */

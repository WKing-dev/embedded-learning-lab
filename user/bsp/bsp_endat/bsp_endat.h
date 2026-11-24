/**
 * @file bsp_endat.h
 * @author WLJ
 * @brief EnDat 物理层：SPI1 + DMA + 收发方向控制
 * @version 0.1
 * @date 2026-11-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef _BSP_ENDAT_H_
#define _BSP_ENDAT_H_

#include "stm32f4xx_hal.h"

/* ============================================================================
 * EnDat SPI 时序配置（排查用，跑通后可固定）
 *
 * BSP_ENDAT_SPI_MODE:
 *   0 = CPOL=0, CPHA=1Edge
 *   1 = CPOL=0, CPHA=2Edge
 *   2 = CPOL=1, CPHA=1Edge   ← 当前默认（在跳变沿的另一侧采样）
 *   3 = CPOL=1, CPHA=2Edge   （官方 demo 的配置）
 *
 * BSP_ENDAT_SPI_PRESCALER: 64 -> 1.31 MHz；128 -> 656 kHz；256 -> 328 kHz
 *   降速用于排除信号建立时间不足（跳变后采样过早）的问题
 * ==========================================================================*/
/* EnDat 2.2 规范：编码器下降沿更新数据，主机上升沿采样
   → CPOL=1（空闲高）+ CPHA=1（第二边沿/上升沿采样）= SPI 模式 3 */
#define BSP_ENDAT_SPI_MODE        3
#define BSP_ENDAT_SPI_PRESCALER   SPI_BAUDRATEPRESCALER_64

/* 一次 EnDat 事务的物理层状态 */
typedef enum __Bsp_Endat_State_Enum
{
    BSP_ENDAT_IDLE = 0,     /* 空闲，可以发起新事务 */
    BSP_ENDAT_BUSY,         /* 收发进行中 */
    BSP_ENDAT_DONE,         /* 一帧已收完，等待上层取走 */
    BSP_ENDAT_ERROR         /* 传输出错（超时 / DMA 异常） */
} Bsp_Endat_State_Enum;

/* BSP 数据全部封装在结构体内 */
typedef struct __Bsp_Endat_Struct
{
    Bsp_Endat_State_Enum state;      /* 当前状态 */
    uint8_t *rx_buffer;              /* 接收缓冲区指针 */
    uint16_t rx_length;              /* 接收字节数 */
    uint8_t command;                 /* 待发送的命令字节（DMA 需要稳定地址） */
    uint32_t transaction_count;      /* 事务计数 */
    uint32_t error_count;            /* 出错计数 */
} Bsp_Endat_Struct;

extern Bsp_Endat_Struct bsp_endat;

/**
 * @brief  物理层初始化
 * @note   必须在 MX_SPI1_Init() 与 MX_DMA_Init() 之后调用，
 *         初始化后把 DATA 总线置为接收态。
 * @return 无
 */
void Bsp_Endat_Init(void);

/**
 * @brief  发起一次 EnDat 事务（非阻塞）
 * @param[in]  command   命令字节
 * @param[out] rx_buffer 接收缓冲区，调用期间必须有效
 * @param[in]  rx_length 接收字节数
 * @return 0 = 已启动；1 = 当前忙或启动失败
 */
uint8_t Bsp_Endat_Start(uint8_t command, uint8_t *rx_buffer, uint16_t rx_length);

/**
 * @brief  按自定义内容发送一帧（用于读参数：命令 + 地址）
 * @param[in]  tx        发送内容（第 0 字节先发）
 * @param[in]  tx_len    发送字节数
 * @param[in]  de_clocks DE 保持高电平的时钟数
 * @param[out] rx        接收缓冲
 * @param[in]  rx_len    接收字节数
 * @return 0 = 成功；1 = 失败
 */
uint8_t Bsp_Endat_Start_Cfg(const uint8_t *tx, uint8_t tx_len, uint8_t de_clocks,
                            uint8_t *rx, uint16_t rx_len);

/** 长传输缓冲大小（读参数需等 t_ac ≤ 12 ms ≈ 2000 字节 @1.3125 MHz） */
#define BSP_ENDAT_LONG_BYTES      2048U

/**
 * @brief  长传输（用于读参数：需要保持时钟长达 12 ms）
 * @param[in] tx          发送内容
 * @param[in] tx_len      发送字节数
 * @param[in] de_clocks   DE 保持高电平的时钟数
 * @param[in] total_bytes 总传输字节数
 * @return 0 = 成功；1 = 失败
 */
uint8_t Bsp_Endat_Start_Long(const uint8_t *tx, uint8_t tx_len, uint8_t de_clocks,
                             uint16_t total_bytes);

/**
 * @brief  取长传输接收缓冲
 * @return 缓冲区指针（长度 = total_bytes）
 */
const uint8_t *Bsp_Endat_Get_Long_Buffer(void);

/**
 * @brief  中止当前事务并放开总线
 * @return 无
 */
void Bsp_Endat_Abort(void);

/**
 * @brief  读取物理层状态
 * @return 当前状态
 */
Bsp_Endat_State_Enum Bsp_Endat_Get_State(void);

/**
 * @brief  清除 DONE / ERROR 标志，回到 IDLE
 * @return 无
 */
void Bsp_Endat_Clear_Flag(void);

#endif /* _BSP_ENDAT_H_ */

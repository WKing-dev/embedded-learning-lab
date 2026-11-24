/**
 * @file lic411_port.h
 * @author WLJ
 * @brief LIC411 EnDat 协议层的移植接口
 * @version 1.0
 * @date 2026-11-18
 *
 * ============================================================================
 *  移植说明
 * ============================================================================
 *  lic411.c 是纯协议实现，不包含任何硬件代码。
 *  移植到新平台时，只需在本平台实现下面 6 个函数即可，协议层无需改动。
 *
 *  参考实现见 user/bsp/bsp_endat/bsp_endat.c（STM32F4 + SPI/DMA）。
 *
 *  ---- 一次完整事务的时序（由 Port_Start 负责） ----
 *
 *      CLOCK  ‾‾|_|‾|_|‾|_|‾|_|‾ ... ‾‾‾‾‾‾
 *      DATA   ‾‾[ 命令 8 位 ]‾‾[     编码器回应     ]
 *      DE     ‾‾‾‾‾‾‾‾‾‾‾‾‾\__________________
 *             └─ de_clocks ─┘
 *
 *      1. DE 拉高（本机驱动总线），DATA 先保持低 ≥2 µs（t_ST）
 *      2. 连续打 tx_len 个字节的时钟（LSB 先行），期间在指定时刻释放 DE
 *      3. 继续打时钟，同时采样 DATA，直到收满 rx_len 字节
 *      4. 时钟在结束时保持高电平
 *
 *  ---- 关键时序参数（EnDat 2.2，1.3125 MHz 时钟） ----
 *      · 时钟：空闲高，连续无中断；数据在下降沿变化，上升沿采样
 *      · 命令：2T(2 个时钟低) + 6 位模式指令 = 8 个时钟
 *      · DE：命令 8 个时钟后释放总线
 *      · 帧：S + F1 + F2 + 位置值(36) + CRC5，起始位在收到位流偏移6
 *
 * ============================================================================
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef _LIC411_PORT_H_
#define _LIC411_PORT_H_

#include <stdint.h>

/* ---------------- 事务状态 ---------------- */

/**
 * @brief 传输事务状态
 */
typedef enum
{
    LIC411_PORT_IDLE = 0,   /**< 空闲，可以启动新事务 */
    LIC411_PORT_BUSY,       /**< 传输进行中 */
    LIC411_PORT_DONE,       /**< 传输完成，数据已就绪 */
    LIC411_PORT_ERROR       /**< 传输出错 */
} Lic411_Port_State;

/* ---------------- 需要移植者实现的 6 个函数 ---------------- */

/**
 * @brief  初始化物理层（时钟、数据、方向控制引脚的初始化）
 * @return 0 = 成功；其它 = 失败
 */
uint8_t Lic411_Port_Init(void);

/**
 * @brief  启动一次事务（非阻塞，完成后由 Lic411_Port_Get_State 报告）
 * @param[in]  tx        发送内容（第 0 字节先发，LSB 先行）
 * @param[in]  tx_len    发送字节数（通常为 1）
 * @param[in]  de_clocks DE 保持高电平的时钟数（之后释放总线交给编码器）
 * @param[out] rx        接收缓冲
 * @param[in]  rx_len    接收字节数
 * @return 0 = 已启动；其它 = 失败
 */
uint8_t Lic411_Port_Start(const uint8_t *tx, uint8_t tx_len, uint8_t de_clocks,
                          uint8_t *rx, uint16_t rx_len);

/**
 * @brief  读取事务状态
 * @return 当前状态
 */
Lic411_Port_State Lic411_Port_Get_State(void);

/**
 * @brief  清除 DONE / ERROR 标志，回到 IDLE
 * @return 无
 */
void Lic411_Port_Clear_Flag(void);

/**
 * @brief  中止当前事务并释放总线
 * @return 无
 */
void Lic411_Port_Abort(void);

/**
 * @brief  读取毫秒级系统计数（用于时间戳；无 RTOS 时可用 SysTick 计数换算）
 * @return 毫秒计数，自由运行、允许溢出回绕
 */
uint32_t Lic411_Port_Get_Tick(void);

#endif /* _LIC411_PORT_H_ */

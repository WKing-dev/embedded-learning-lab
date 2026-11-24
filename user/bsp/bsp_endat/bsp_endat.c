/**
 * @file bsp_endat.c
 * @author WLJ
 * @brief EnDat 物理层实现：SPI1 + DMA2 + PA4 收发方向控制
 * @version 0.1
 * @date 2026-11-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "bsp_endat.h"
#include "main.h"
#include "spi.h"
#include "lic411_port.h"

/* 方向控制：原理图上 U3 的 DE 与 /RE 并联接到 PA4
 * 高电平 = 驱动使能（发送），低电平 = 接收器使能（接收） */
#define BSP_ENDAT_DE_TX()   HAL_GPIO_WritePin(ENDAT_DE_GPIO_Port, ENDAT_DE_Pin, GPIO_PIN_SET)
#define BSP_ENDAT_DE_RX()   HAL_GPIO_WritePin(ENDAT_DE_GPIO_Port, ENDAT_DE_Pin, GPIO_PIN_RESET)

Bsp_Endat_Struct bsp_endat;

/* 一次事务的时钟总数：8 个命令 + 64 个接收 = 72，必须连续不断 */
#define BSP_ENDAT_TX_BYTES      8U
#define BSP_ENDAT_CMD_CLOCKS    8U
#define BSP_ENDAT_TIMER_HZ      4000000U
#define BSP_ENDAT_TST_TICKS     8U          /* 2 µs */

static uint8_t bsp_endat_tx_buffer[BSP_ENDAT_TX_BYTES];
static uint8_t bsp_endat_rx_buffer[BSP_ENDAT_TX_BYTES];

/* 长传输专用缓冲（读参数时需保持时钟约 12 ms） */
static uint8_t bsp_endat_long_tx[BSP_ENDAT_LONG_BYTES];
static uint8_t bsp_endat_long_rx[BSP_ENDAT_LONG_BYTES];
static uint8_t bsp_endat_long_active;
static uint32_t bsp_endat_clk_hz = 1312500U;   /* 当前 SPI 时钟频率，DE 延时按它换算 */
static TIM_HandleTypeDef bsp_endat_timer;

/**
 * @brief  使用 TIM2 进行短延时
 * @param[in] ticks 计数值，1 tick = 0.25 µs
 * @return 无
 */
static void Bsp_Endat_Delay_Ticks(uint32_t ticks)
{
    uint32_t start = __HAL_TIM_GET_COUNTER(&bsp_endat_timer);

    while ((__HAL_TIM_GET_COUNTER(&bsp_endat_timer) - start) < ticks)
    {
    }
}

/**
 * @brief  初始化 4 MHz 自由运行定时器
 * @return 0 = 成功；1 = 失败
 */
static uint8_t Bsp_Endat_Timer_Init(void)
{
    RCC_ClkInitTypeDef clock_config;
    uint32_t flash_latency;
    uint32_t timer_clock;

    HAL_RCC_GetClockConfig(&clock_config, &flash_latency);
    timer_clock = HAL_RCC_GetPCLK1Freq();
    if (clock_config.APB1CLKDivider != RCC_HCLK_DIV1)
    {
        timer_clock *= 2U;
    }

    __HAL_RCC_TIM2_CLK_ENABLE();
    bsp_endat_timer.Instance = TIM2;
    bsp_endat_timer.Init.Prescaler = (timer_clock / BSP_ENDAT_TIMER_HZ) - 1U;
    bsp_endat_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    bsp_endat_timer.Init.Period = 0xFFFFFFFFU;
    bsp_endat_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    bsp_endat_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&bsp_endat_timer) != HAL_OK)
    {
        return 1U;
    }

    return (HAL_TIM_Base_Start(&bsp_endat_timer) == HAL_OK) ? 0U : 1U;
}

/**
 * @brief  按宏配置重新设置 SPI 时序并重新初始化
 * @note   CubeMX 生成的初始化默认是 CPOL=High / CPHA=2Edge，
 *         这里按 BSP_ENDAT_SPI_MODE 覆盖，便于逐个模式验证
 * @return 无
 */
static void Bsp_Endat_Config(uint8_t mode, uint32_t prescaler)
{
    switch (mode)
    {
        case 0U:
            hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
            hspi1.Init.CLKPhase    = SPI_PHASE_1EDGE;
            break;
        case 1U:
            hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
            hspi1.Init.CLKPhase    = SPI_PHASE_2EDGE;
            break;
        case 2U:
            hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
            hspi1.Init.CLKPhase    = SPI_PHASE_1EDGE;
            break;
        default:
            hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
            hspi1.Init.CLKPhase    = SPI_PHASE_2EDGE;
            break;
    }

    hspi1.Init.BaudRatePrescaler = prescaler;

    /* HAL 的分频编码：bit3~5 的值 v 对应分频 2^(v+1) */
    {
        uint32_t v   = ((uint32_t)prescaler >> 3U) & 0x07U;
        uint32_t div = 1UL << (v + 1U);
        bsp_endat_clk_hz = (SystemCoreClock / 2U) / div;
    }

    (void)HAL_SPI_Init(&hspi1);
}


/**
 * @brief  物理层初始化
 * @note   先把总线置为接收态，避免上电期间误驱动 DATA 差分线
 * @return 无
 */
void Bsp_Endat_Init(void)
{
    if (Bsp_Endat_Timer_Init() != 0U)
    {
        bsp_endat.state = BSP_ENDAT_ERROR;
        return;
    }
    Bsp_Endat_Config(BSP_ENDAT_SPI_MODE, BSP_ENDAT_SPI_PRESCALER);

    BSP_ENDAT_DE_RX();

    bsp_endat.state             = BSP_ENDAT_IDLE;
    bsp_endat.rx_buffer         = NULL;
    bsp_endat.rx_length         = 0U;
    bsp_endat.command           = 0U;
    bsp_endat.transaction_count = 0U;
    bsp_endat.error_count       = 0U;
}

/**
 * @brief  发起一次 EnDat 事务（非阻塞）
 * @param[in]  command   命令字节
 * @param[out] rx_buffer 接收缓冲区
 * @param[in]  rx_length 接收字节数
 * @return 0 = 已启动；1 = 忙或启动失败
 */
uint8_t Bsp_Endat_Start(uint8_t command, uint8_t *rx_buffer, uint16_t rx_length)
{
    if ((bsp_endat.state != BSP_ENDAT_IDLE) || (rx_buffer == NULL) || (rx_length == 0U))
    {
        return 1U;
    }

    if (rx_length > (BSP_ENDAT_TX_BYTES - 1U))
    {
        return 1U;
    }

    bsp_endat.command   = command;
    bsp_endat.rx_buffer = rx_buffer;
    bsp_endat.rx_length = rx_length;
    bsp_endat.state     = BSP_ENDAT_BUSY;

    /* 命令放第一个字节，其余补 0：72 个时钟一次连续打完，中间不停 */
    bsp_endat_tx_buffer[0] = command;
    for (uint8_t i = 1U; i < BSP_ENDAT_TX_BYTES; i++)
    {
        bsp_endat_tx_buffer[i] = 0x00U;
    }

    BSP_ENDAT_DE_TX();      /* 总线交给本机驱动；此时 MOSI 保持上一帧结尾的低电平 */

    /* 规范 2.3.1：第一个时钟之前 DATA 必须保持低电平 t_ST ≥ 2 µs */
    Bsp_Endat_Delay_Ticks(BSP_ENDAT_TST_TICKS);

    if (HAL_SPI_TransmitReceive_DMA(&hspi1, bsp_endat_tx_buffer, bsp_endat_rx_buffer,
                                    BSP_ENDAT_TX_BYTES) != HAL_OK)
    {
        BSP_ENDAT_DE_RX();
        bsp_endat.state = BSP_ENDAT_ERROR;
        bsp_endat.error_count++;
        return 1U;
    }

    /* 8 个时钟 ≈ 6.1 µs 后释放总线，时钟继续跑，不再中断 */
    Bsp_Endat_Delay_Ticks(25U);
    BSP_ENDAT_DE_RX();

    bsp_endat.transaction_count++;
    return 0U;
}

/**
 * @brief  按自定义内容发送一帧
 * @param[in]  tx        发送内容
 * @param[in]  tx_len    发送字节数（不超过 BSP_ENDAT_TX_BYTES）
 * @param[in]  de_clocks DE 保持高电平的时钟数
 * @param[out] rx        接收缓冲
 * @param[in]  rx_len    接收字节数（不超过 BSP_ENDAT_TX_BYTES - 1）
 * @return 0 = 成功；1 = 失败
 */
uint8_t Bsp_Endat_Start_Cfg(const uint8_t *tx, uint8_t tx_len, uint8_t de_clocks,
                            uint8_t *rx, uint16_t rx_len)
{
    uint8_t i;
    uint32_t delay_ticks;

    if ((tx == NULL) || (rx == NULL) || (tx_len == 0U) ||
        (tx_len > BSP_ENDAT_TX_BYTES) || (rx_len > (BSP_ENDAT_TX_BYTES - 1U)) ||
        (bsp_endat.state != BSP_ENDAT_IDLE))
    {
        return 1U;
    }

    bsp_endat.rx_buffer = rx;
    bsp_endat.rx_length = rx_len;
    bsp_endat.state     = BSP_ENDAT_BUSY;

    for (i = 0U; i < BSP_ENDAT_TX_BYTES; i++)
    {
        bsp_endat_tx_buffer[i] = (i < tx_len) ? tx[i] : 0x00U;
        /* 关键：每次传输前必须清空接收缓冲。
           否则编码器没有响应（超时/断线）时，DMA 不会写入任何数据，
           旧数据会原样留在缓冲里，被再次解码并"通过 CRC"，
           表现出来就是同一个值冻结几百帧 —— 上电时更会被误当成初值。 */
        bsp_endat_rx_buffer[i] = 0xFFU;
    }

    BSP_ENDAT_DE_TX();

    /* 规范 2.3.1：第一个时钟之前 DATA 必须保持低电平 t_ST ≥ 2 µs */
    Bsp_Endat_Delay_Ticks(BSP_ENDAT_TST_TICKS);

    if (HAL_SPI_TransmitReceive_DMA(&hspi1, bsp_endat_tx_buffer, bsp_endat_rx_buffer,
                                    BSP_ENDAT_TX_BYTES) != HAL_OK)
    {
        BSP_ENDAT_DE_RX();
        bsp_endat.state = BSP_ENDAT_ERROR;
        bsp_endat.error_count++;
        return 1U;
    }

    /* DE 宽度 = de_clocks × 时钟周期（+2 周期补偿函数调用开销）。
       实测释放点比第 8 个时钟晚约 1 个时钟（DMA 启动延迟），第 9 位 MOSI=0；
       1918/1918 帧 CRC 全过，证明编码器不受这个多余时钟影响，故保持现状，
       不改为 BSY 同步（未经实测验证的改动风险更大）。 */
    delay_ticks = (((uint32_t)de_clocks * BSP_ENDAT_TIMER_HZ) +
                   bsp_endat_clk_hz - 1U) / bsp_endat_clk_hz;
    Bsp_Endat_Delay_Ticks(delay_ticks);
    BSP_ENDAT_DE_RX();

    bsp_endat.transaction_count++;
    return 0U;
}

/**
 * @brief  长传输（读参数用，保持时钟约 12 ms 等待 t_ac）
 * @param[in] tx          发送内容
 * @param[in] tx_len      发送字节数
 * @param[in] de_clocks   DE 保持高电平的时钟数
 * @param[in] total_bytes 总传输字节数（不超过 BSP_ENDAT_LONG_BYTES）
 * @return 0 = 成功；1 = 失败
 */
uint8_t Bsp_Endat_Start_Long(const uint8_t *tx, uint8_t tx_len, uint8_t de_clocks,
                             uint16_t total_bytes)
{
    uint16_t i;
    uint32_t delay_ticks;

    if ((tx == NULL) || (tx_len == 0U) || (total_bytes == 0U) ||
        (total_bytes > BSP_ENDAT_LONG_BYTES) || (bsp_endat.state != BSP_ENDAT_IDLE))
    {
        return 1U;
    }

    bsp_endat_long_active = 1U;
    bsp_endat.rx_length    = 0U;
    bsp_endat.state        = BSP_ENDAT_BUSY;

    for (i = 0U; i < total_bytes; i++)
    {
        bsp_endat_long_tx[i] = (i < (uint16_t)tx_len) ? tx[i] : 0x00U;
        bsp_endat_long_rx[i] = 0xFFU;
    }

    BSP_ENDAT_DE_TX();

    if (HAL_SPI_TransmitReceive_DMA(&hspi1, bsp_endat_long_tx, bsp_endat_long_rx,
                                    total_bytes) != HAL_OK)
    {
        BSP_ENDAT_DE_RX();
        bsp_endat_long_active = 0U;
        bsp_endat.state = BSP_ENDAT_ERROR;
        bsp_endat.error_count++;
        return 1U;
    }

    delay_ticks = (((uint32_t)de_clocks * BSP_ENDAT_TIMER_HZ) +
                   bsp_endat_clk_hz - 1U) / bsp_endat_clk_hz;
    Bsp_Endat_Delay_Ticks(delay_ticks);
    BSP_ENDAT_DE_RX();

    bsp_endat.transaction_count++;
    return 0U;
}

/**
 * @brief  取长传输接收缓冲
 * @return 缓冲区指针
 */
const uint8_t *Bsp_Endat_Get_Long_Buffer(void)
{
    return bsp_endat_long_rx;
}

/**
 * @brief  中止当前事务并放开总线
 * @return 无
 */
void Bsp_Endat_Abort(void)
{
    if (bsp_endat.state == BSP_ENDAT_BUSY)
    {
        (void)HAL_SPI_Abort(&hspi1);
    }

    BSP_ENDAT_DE_RX();
    bsp_endat.state = BSP_ENDAT_IDLE;
}

/**
 * @brief  读取物理层状态
 * @return 当前状态
 */
Bsp_Endat_State_Enum Bsp_Endat_Get_State(void)
{
    return bsp_endat.state;
}

/**
 * @brief  清除 DONE / ERROR 标志，回到 IDLE
 * @return 无
 */
void Bsp_Endat_Clear_Flag(void)
{
    if ((bsp_endat.state == BSP_ENDAT_DONE) || (bsp_endat.state == BSP_ENDAT_ERROR))
    {
        bsp_endat.state = BSP_ENDAT_IDLE;
    }
}

/**
 * @brief  SPI 发送完成回调
 * @note   这里必须第一时间切换方向：命令已经发完，编码器即将开始回数据
 * @param[in] hspi SPI 句柄
 * @return 无
 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    /* 全双工传输时由 RxCplt 统一收尾，这里不做处理 */
    (void)hspi;
}

/**
 * @brief  SPI 接收完成回调
 * @note   中断里只置状态，不做解析
 * @param[in] hspi SPI 句柄
 * @return 无
 */
static void Bsp_Endat_On_Complete(void)
{
    /* 长模式：数据已在 bsp_endat_long_rx 里，不做拷贝 */
    if (bsp_endat_long_active == 0U)
    {
        /* 丢弃第 1 个字节（命令期），其余按顺序拷给调用者 */
        for (uint8_t i = 0U; i < bsp_endat.rx_length; i++)
        {
            bsp_endat.rx_buffer[i] = bsp_endat_rx_buffer[i + 1U];
        }
    }

    bsp_endat_long_active = 0U;
    bsp_endat.state = BSP_ENDAT_DONE;
}

/**
 * @brief  SPI 全双工 DMA 传输完成回调
 * @note   HAL_SPI_TransmitReceive_DMA 完成后走的是 TxRxCplt，不是 RxCplt
 * @param[in] hspi SPI 句柄
 * @return 无
 */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI1)
    {
        return;
    }

    Bsp_Endat_On_Complete();
}

/**
 * @brief  SPI 接收完成回调（保留兼容）
 * @param[in] hspi SPI 句柄
 * @return 无
 */
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI1)
    {
        return;
    }

    Bsp_Endat_On_Complete();
}

/**
 * @brief  SPI 错误回调
 * @note   出错也必须放开总线，否则编码器永远无法再应答
 * @param[in] hspi SPI 句柄
 * @return 无
 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI1)
    {
        return;
    }

    BSP_ENDAT_DE_RX();
    bsp_endat.state = BSP_ENDAT_ERROR;
    bsp_endat.error_count++;
}
/* ============================================================================
 * LIC411 协议层的移植接口实现
 * 协议层（lic411.c）通过这 5 个函数访问硬件，除此之外不依赖任何平台细节。
 * ==========================================================================*/

/**
 * @brief  初始化物理层
 * @return 0 = 成功
 */
uint8_t Lic411_Port_Init(void)
{
    Bsp_Endat_Init();
    return 0U;
}

/**
 * @brief  启动一次事务
 * @param[in]  tx        发送内容
 * @param[in]  tx_len    发送字节数
 * @param[in]  de_clocks DE 保持高电平的时钟数
 * @param[out] rx        接收缓冲
 * @param[in]  rx_len    接收字节数
 * @return 0 = 已启动；其它 = 失败
 */
uint8_t Lic411_Port_Start(const uint8_t *tx, uint8_t tx_len, uint8_t de_clocks,
                          uint8_t *rx, uint16_t rx_len)
{
    return Bsp_Endat_Start_Cfg(tx, tx_len, de_clocks, rx, rx_len);
}

/**
 * @brief  读取事务状态（BSP 状态 → 移植层状态）
 * @return 当前状态
 */
Lic411_Port_State Lic411_Port_Get_State(void)
{
    switch (Bsp_Endat_Get_State())
    {
        case BSP_ENDAT_IDLE:
            return LIC411_PORT_IDLE;

        case BSP_ENDAT_BUSY:
            return LIC411_PORT_BUSY;

        case BSP_ENDAT_DONE:
            return LIC411_PORT_DONE;

        default:
            return LIC411_PORT_ERROR;
    }
}

/**
 * @brief  清除 DONE / ERROR 标志
 * @return 无
 */
void Lic411_Port_Clear_Flag(void)
{
    Bsp_Endat_Clear_Flag();
}

/**
 * @brief  中止当前事务
 * @return 无
 */
void Lic411_Port_Abort(void)
{
    Bsp_Endat_Abort();
}
/**
 * @brief  读取毫秒级系统计数
 * @return 毫秒计数
 */
uint32_t Lic411_Port_Get_Tick(void)
{
    return HAL_GetTick();
}

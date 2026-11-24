/**
 * @file lic411.c
 * @author WLJ
 * @brief HEIDENHAIN LIC 411（EnDat 2.2）位置读取模块实现
 * @version 0.2
 * @date 2026-11-17
 *
 * @copyright Copyright (c) 2026
 *
 * 帧结构（44 位）：
 *   S(1) + F1(1) + F2(1) + 位置值(36, LSB 先行) + CRC5(5)
 *
 * 注意：数据并非从第 0 个时钟开始，编码器要等 max(t_cal, 14.5 clk) 后才回数据，
 *       因此捕获窗口取 8 字节（64 位），覆盖 14.5 + 44 = 58.5 个时钟。
 */

#include "lic411.h"
#include "lic411_port.h"

Lic411_Struct lic411;

/**
 * @brief  计算 EnDat 5 位 CRC
 * @note   生成多项式 x^5 + x^3 + x + 1 (0x0B)，初值全 1，末尾取反
 * @param[in] bits   参与校验的位序列（F1、F2 + 位置值）
 * @param[in] length 位序列长度
 * @return 5 位 CRC 值
 */
static uint8_t Lic411_Crc5(const uint8_t *bits, uint16_t length)
{
    uint8_t shift_register[5] = {1U, 1U, 1U, 1U, 1U};
    uint8_t crc = 0U;
    uint16_t i;

    for (i = 0U; i < length; i++)
    {
        uint8_t feedback = (uint8_t)(shift_register[4] ^ bits[i]);

        shift_register[4] = shift_register[3];
        shift_register[3] = (uint8_t)(shift_register[2] ^ feedback);
        shift_register[2] = shift_register[1];
        shift_register[1] = (uint8_t)(shift_register[0] ^ feedback);
        shift_register[0] = feedback;
    }

    /* 规范 4.4.2：输出时从 ff[4] 到 ff[0]，每位移入前取反 */
    for (i = LIC411_CRC_BITS; i > 0U; i--)
    {
        shift_register[i - 1U] = (shift_register[i - 1U] != 0U) ? 0U : 1U;
        crc = (uint8_t)((crc << 1U) | shift_register[i - 1U]);
    }

    return crc;
}

/**
 * @brief  把原始字节展开成位序列
 * @note   EnDat 为 LSB 先行，第 0 位即线上第一个位
 * @param[in]  raw  原始字节
 * @param[out] bits 位序列，长度 LIC411_RAW_BITS
 * @return 无
 */
static void Lic411_Raw_To_Bits(const uint8_t *raw, uint8_t *bits)
{
    uint16_t i;

    for (i = 0U; i < LIC411_RAW_BITS; i++)
    {
        bits[i] = (uint8_t)((raw[i >> 3U] >> (i & 7U)) & 1U);
    }
}

/**
 * @brief  按指定帧结构解码一帧
 * @param[in]  raw           原始字节
 * @param[in]  offset        起始位偏移
 * @param[in]  crc_msb_first CRC 位序，1 = 高位先发
 * @param[out] sample        解码结果
 * @return 1 = 解码成功；0 = 失败
 */
static uint8_t lic411_decode_ex(const uint8_t *raw, uint8_t offset, uint8_t width,
                                uint8_t crc_msb_first, uint8_t crc_include_start,
                                uint64_t *value, uint8_t *error1, uint8_t *error2)
{
    uint8_t bits[LIC411_RAW_BITS];
    uint8_t payload[48];
    uint8_t crc_receive = 0U;
    uint8_t crc_calculate;
    uint64_t counts = 0U;
    uint16_t index = 0U;
    uint16_t i;
    uint8_t  head = (uint8_t)(1U + LIC411_STATUS_BITS);   /* S + 状态位 */

    if ((uint16_t)(offset + head + width + LIC411_CRC_BITS) > LIC411_RAW_BITS)
    {
        return 0U;
    }

    Lic411_Raw_To_Bits(raw, bits);

    if (bits[offset] != 1U)
    {
        return 0U;                                  /* 起始位必须是 1 */
    }

    /* 安装修正后与标准错误字/BZFQ交叉验证：有效位置帧恒为 F1=0、F2=1。 */
    if (bits[offset + 1U] != 0U)
    {
        return 0U;
    }
    if ((LIC411_STATUS_BITS >= 2U) && (bits[offset + 2U] != 1U))
    {
        return 0U;
    }

    /* CRC 载荷：S + 状态位 + 位置值（LSB 先行） */
    if (crc_include_start != 0U)
    {
        payload[index++] = bits[offset];
    }
    for (i = 0U; i < LIC411_STATUS_BITS; i++)
    {
        payload[index++] = bits[offset + 1U + i];
    }

    for (i = 0U; i < width; i++)
    {
        uint8_t bit = bits[offset + head + i];

        payload[index++] = bit;
        if (bit != 0U)
        {
            counts |= ((uint64_t)1U << i);
        }
    }

    crc_calculate = Lic411_Crc5(payload, index);

    for (i = 0U; i < LIC411_CRC_BITS; i++)
    {
        uint8_t bit = bits[offset + head + width + i];

        if (crc_msb_first != 0U)
        {
            crc_receive = (uint8_t)((crc_receive << 1U) | bit);
        }
        else
        {
            crc_receive |= (uint8_t)(bit << i);
        }
    }

    if (crc_calculate != crc_receive)
    {
        return 0U;
    }

    if (value != NULL)  { *value  = counts; }
    if (error1 != NULL) { *error1 = bits[offset + 1U]; }
    if (error2 != NULL) { *error2 = bits[offset + 2U]; }

    return 1U;
}

/**
 * @brief  按固定帧结构解码一帧
 * @param[in]  raw           原始字节
 * @param[in]  offset        起始位偏移
 * @param[in]  crc_msb_first CRC 位序，1 = 高位先发
 * @param[out] sample        解码结果
 * @return 1 = 解码成功；0 = 失败
 */
static uint8_t Lic411_Decode(const uint8_t *raw, uint8_t offset, uint8_t crc_msb_first,
                             Lic411_Sample_Struct *sample)
{
    uint64_t value = 0U;
    uint8_t error1 = 0U;
    uint8_t error2 = 0U;

    /* 手册 4.4.2：CRC 覆盖范围 = F1 + F2 + 位置值，不含起始位 S */
    if (lic411_decode_ex(raw, offset, (uint8_t)LIC411_DATA_BITS, crc_msb_first, 0U,
                         &value, &error1, &error2) == 0U)
    {
        return 0U;
    }

    sample->counts  = value;
    sample->error_1 = error1;
    sample->error_2 = error2;

    return 1U;
}

/**
 * @brief  尝试用任意参数解码一帧（供上电自检扫描使用）
 * @param[in]  raw               原始字节
 * @param[in]  offset            起始位偏移
 * @param[in]  width             数据位宽
 * @param[in]  crc_msb_first     CRC 位序
 * @param[in]  crc_include_start CRC 是否包含起始位
 * @param[out] value             解出的计数值
 * @return 1 = 成功；0 = 失败
 */
uint8_t Lic411_Try_Decode(const uint8_t *raw, uint8_t offset, uint8_t width,
                          uint8_t crc_msb_first, uint8_t crc_include_start,
                          uint64_t *value)
{
    return lic411_decode_ex(raw, offset, width, crc_msb_first, crc_include_start,
                            value, NULL, NULL);
}
/**
 * @brief  在当前帧上收集所有能解码的帧结构
 * @note   单帧可能有多个假阳性（CRC 只有 5 位），后续靠多帧交叉过滤
 * @return 无
 */
static void Lic411_Collect_Candidates(void)
{
    Lic411_Sample_Struct temp;
    uint8_t offset;
    uint8_t crc_msb;

    lic411.candidate_count = 0U;

    for (offset = 0U; offset <= LIC411_MAX_OFFSET; offset++)
    {
        for (crc_msb = 0U; crc_msb < 2U; crc_msb++)
        {
            if (Lic411_Decode(lic411.sample.raw, offset, crc_msb, &temp) != 0U)
            {
                lic411.candidate_offset[lic411.candidate_count]  = offset;
                lic411.candidate_crc_msb[lic411.candidate_count] = crc_msb;
                lic411.candidate_count++;
            }
        }
    }
}

/**
 * @brief  用当前帧过滤候选帧结构
 * @note   只有连续多帧都能解码的帧结构才会保留
 * @return 无
 */
static void Lic411_Filter_Candidates(void)
{
    Lic411_Sample_Struct temp;
    uint8_t read_index;
    uint8_t write_index = 0U;

    for (read_index = 0U; read_index < lic411.candidate_count; read_index++)
    {
        uint8_t offset  = lic411.candidate_offset[read_index];
        uint8_t crc_msb = lic411.candidate_crc_msb[read_index];

        if (Lic411_Decode(lic411.sample.raw, offset, crc_msb, &temp) != 0U)
        {
            lic411.candidate_offset[write_index]  = offset;
            lic411.candidate_crc_msb[write_index] = crc_msb;
            write_index++;
        }
    }

    lic411.candidate_count = write_index;
}

/**
 * @brief  就近优先搜索并解码一帧
 * @note   帧头会随 t_cal 的抖动漂移，这里从上次成功的偏移开始向两侧搜；
 *         解出的值先放临时变量，由调用者做完跳变校验再发布
 * @param[out] out 解码结果
 * @return 1 = 成功；0 = 失败
 */
/**
 * @brief  解码失败时分类拒收原因
 * @note   扫描全部合法偏移，判断失败属于哪一类：
 *         找不到起始位 / 错误位为 1 / CRC 真错 / 帧跑出 ±2 搜索范围
 * @return 无
 */
static void Lic411_Classify_Failure(void)
{
    uint8_t  bits[LIC411_RAW_BITS];
    uint8_t  off;
    uint8_t  saw_start = 0U;
    uint8_t  saw_clean = 0U;
    uint8_t  saw_crc   = 0U;
    uint64_t value;
    uint8_t  e1;
    uint8_t  e2;

    Lic411_Raw_To_Bits(lic411.sample.raw, bits);

    /* 全偏移扫描，按"最宽松"的条件归类，避免被噪声起始位误导 */
    for (off = 0U; off <= (uint8_t)LIC411_MAX_OFFSET; off++)
    {
        if ((uint16_t)(off + 1U + LIC411_STATUS_BITS + LIC411_DATA_BITS +
                        LIC411_CRC_BITS) > LIC411_RAW_BITS)
        {
            break;              /* 越界，后面的偏移更不可能装下整帧 */
        }
        if (bits[off] == 0U)
        {
            continue;
        }
        saw_start = 1U;
        if ((bits[off + 1U] != 0U) || (bits[off + 2U] != 1U))
        {
            continue;
        }
        saw_clean = 1U;
        if (lic411_decode_ex(lic411.sample.raw, off, (uint8_t)LIC411_DATA_BITS,
                             lic411.layout_crc_msb_first, 0U, &value, &e1, &e2) != 0U)
        {
            saw_crc = 1U;
            break;
        }
    }

    if (saw_crc != 0U)
    {
        lic411.reject_wide++;       /* 能解出，但帧跑出 ±2 搜索范围 */
    }
    else if (saw_clean != 0U)
    {
        lic411.reject_crc++;        /* 起始位、错误位都正常，CRC 真的错 */
    }
    else if (saw_start != 0U)
    {
        lic411.reject_errbits++;    /* 有起始位，但 F1 = 1 */
    }
    else
    {
        lic411.reject_nostart++;
    }
}
static uint8_t Lic411_Decode_Verified(Lic411_Sample_Struct *out)
{
    uint8_t step;
    uint8_t off;

    /* 帧头只会因 t_cal 抖动漂 ±1~2 个时钟，只在附近搜：
       范围放大会让错误偏移有更多机会"碰巧"通过 5 位 CRC */
    for (step = 0U; step <= LIC411_SEARCH_SPAN; step++)
    {
        for (uint8_t sgn = 0U; sgn < 2U; sgn++)
        {
            int16_t cand;

            if (step == 0U)
            {
                if (sgn != 0U) { continue; }
                cand = (int16_t)lic411.layout_offset;
            }
            else
            {
                cand = (sgn == 0U) ? ((int16_t)lic411.layout_offset + (int16_t)step)
                                   : ((int16_t)lic411.layout_offset - (int16_t)step);
            }

            if ((cand < 0) || (cand > (int16_t)LIC411_MAX_OFFSET)) { continue; }

            off = (uint8_t)cand;
            if (Lic411_Decode(lic411.sample.raw, off,
                              lic411.layout_crc_msb_first, out) != 0U)
            {
                lic411.sample.raw_offset = off;   /* 记录本帧实际使用的偏移 */
                return 1U;
            }
        }
    }

    return 0U;
}
/**
 * @brief  处理一帧收到的数据
 * @note   标定阶段用多帧交叉过滤；锁定后按固定结构解码
 * @return 无
 */
static void Lic411_Handle_Frame(void)
{
    uint8_t decoded = 0U;
    Lic411_Sample_Struct fresh;

    if (lic411.auto_detect_enable != 0U)
    {
        if (lic411.candidate_count == 0U)
        {
            Lic411_Collect_Candidates();
        }
        else
        {
            Lic411_Filter_Candidates();
        }

        if (lic411.candidate_count != 1U)
        {
            lic411.frame_error_count++;
            return;                                 /* 继续收集 / 过滤 */
        }

        /* 只剩唯一候选，锁定 */
        lic411.layout_offset        = lic411.candidate_offset[0];
        lic411.layout_crc_msb_first = lic411.candidate_crc_msb[0];
        lic411.auto_detect_enable   = 0U;

        decoded = Lic411_Decode(lic411.sample.raw, lic411.layout_offset,
                                lic411.layout_crc_msb_first, &fresh);
    }
    else
    {
        /* 就近优先搜索；解到临时变量，只有通过跳变校验才发布 */
        decoded = Lic411_Decode_Verified(&fresh);
    }

    if (decoded == 0U)
    {
        lic411.frame_error_count++;
        Lic411_Classify_Failure();
        lic411.crc_error_count++;
        lic411.decode_fail_count++;

        if (lic411.decode_fail_count >= LIC411_DECODE_FAIL_MAX)
        {
            lic411.auto_detect_enable = 1U;         /* 连续失败，重新标定 */
            lic411.candidate_count    = 0U;
            lic411.decode_fail_count  = 0U;
        }
        return;
    }

    /* 记录当前帧的原始值（滤波前），供调试时与 RAW 逐帧对照 */
    lic411.sample.raw_counts = fresh.counts;

    /* 增量跟踪滤波
       编码器的绝对码偶发整段读错（实测为数据位 14/16 同时翻转，
       造成约 81920 个计数的位置误差），但相邻帧的真实位移是连续的小量。
       因此不直接采用绝对值，只累加"可信的单帧增量"：
       增量超过 LIC411_TRACK_MAX_STEP 判为滑移，丢弃本次增量，基准仍然跟随。 */
#if (LIC411_TRACK_ENABLE == 0U)
    /* 直接输出编码器绝对值（周期内的绝对位置） */
#if (LIC411_SLIP_GUARD == 1U)
    /* 码字滑移保持
       实测：读数头在部分位置会让相关解算的两个峰几乎等高，于是每帧在相邻
       两个码字（相差 160000 计数 = 8 个栅距）之间来回跳，两条曲线都跟随
       真实位移。单帧跳变占多数，也有 A,B,A,B 的持续来回跳。
       处理：跳变超过半个码字时先保持上次输出，只有连续 SLIP_HOLD 帧都
       停在新位置才认作真实位移；一旦回到原位置立即解除保持。 */
    if (lic411.has_last == 0U)
    {
        lic411.slip_last    = fresh.counts;
        lic411.slip_hold    = 0U;
        lic411.sample.counts = fresh.counts;
    }
    else
    {
        int64_t d = (int64_t)fresh.counts - (int64_t)lic411.slip_last;

        if ((d > (int64_t)LIC411_SLIP_THRESHOLD) || (d < -(int64_t)LIC411_SLIP_THRESHOLD))
        {
            if (lic411.slip_hold < 255U)
            {
                lic411.slip_hold++;
            }

            if (lic411.slip_hold >= LIC411_SLIP_HOLD)
            {
                /* 连续多帧都停在新位置 → 真实位移，接受并重建基准 */
                lic411.slip_last     = fresh.counts;
                lic411.slip_hold     = 0U;
                lic411.sample.counts = fresh.counts;
            }
            else
            {
                /* 疑似码字滑移 → 保持上次输出 */
                lic411.slip_count++;
                lic411.sample.counts = lic411.slip_last;
            }
        }
        else
        {
            /* 正常位移：立即跟随，并解除保持 */
            lic411.slip_last     = fresh.counts;
            lic411.slip_hold     = 0U;
            lic411.sample.counts = fresh.counts;
        }
    }
#else
    lic411.sample.counts = fresh.counts;
#endif
#else
    /* 码字解卷（sawtooth unwrap）
       编码器偶尔把绝对位置读成相邻码字，偏差恒为 LIC411_WRAP_PERIOD
       （实测 160000 计数 = 8 个 20 µm 栅距），并且会一直带着这个偏移
       继续输出。真实位置是连续的，所以按"超过半周期就折算整码字"的
       规则解卷即可。不设上限：快速移动导致的多个码字跨越也能一次算对。 */
    if (lic411.track_valid == 0U)
    {
        lic411.wrap_level   = 0;
        lic411.track_raw    = fresh.counts;
        lic411.track_valid  = 1U;
    }
    else
    {
        int64_t d      = (int64_t)fresh.counts - (int64_t)lic411.track_raw;
        int64_t period = (int64_t)LIC411_WRAP_PERIOD;
        int64_t half   = period / 2;

        if ((d > half) || (d < -half))
        {
            int64_t n;

            if (d >= 0)
            {
                n = (d + half) / period;        /* 四舍五入到最近的码字数 */
            }
            else
            {
                n = -(((-d) + half) / period);
            }

            lic411.wrap_level -= n;
            lic411.slip_count++;
        }

        lic411.track_raw = fresh.counts;
    }

    lic411.track_counts = (lic411.wrap_level * (int64_t)LIC411_WRAP_PERIOD)
                        + (int64_t)fresh.counts;
    lic411.sample.counts = (uint64_t)lic411.track_counts;
#endif

    /* 快照本帧原始数据：sample.raw 是 DMA 接收缓冲，打印时可能已被下一帧覆盖 */
    for (uint8_t i = 0U; i < LIC411_RAW_BYTES; i++)
    {
        lic411.raw_snap[i] = lic411.sample.raw[i];
    }

    lic411.sample.error_1 = fresh.error_1;
    lic411.sample.error_2 = fresh.error_2;
    lic411.last_counts    = fresh.counts;
    lic411.has_last    = 1U;
    lic411.decode_fail_count   = 0U;
    lic411.frame_ok_count++;
    lic411.sample.timestamp_ms = Lic411_Port_Get_Tick();
    lic411.sample.nm           = (int64_t)lic411.sample.counts * (int64_t)LIC411_STEP_NM;
    lic411.sample.mm           = ((double)lic411.sample.nm) / 1000000.0;
}

/**
 * @brief  模块初始化
 * @return 无
 */
void Lic411_Init(void)
{
    (void)Lic411_Port_Init();

    lic411.cmd                  = LIC411_CMD_ERROR_RESET;
    lic411.reset_frames_left    = LIC411_RESET_FRAMES;
    lic411.state                = LIC411_IDLE;
    lic411.period_count         = 0U;
    lic411.timeout_count        = 0U;
    /* 帧结构已实测确定（EnDat 2.2 单帧）：
       RAW 第 7 位 = 起始位 S，第 8/9 位 = Error1/Error2，
       S + F1 + F2 + 36 位位置值（LSB 先行）+ CRC5（MSB 位序）。
       CRC 覆盖 F1、F2 和36位位置值，不含起始位 S。 */
    lic411.layout_offset        = LIC411_LAYOUT_OFFSET;
    lic411.layout_crc_msb_first = LIC411_LAYOUT_CRC_MSB;
    lic411.auto_detect_enable   = 0U;
    lic411.decode_fail_count    = 0U;
    lic411.candidate_count      = 0U;
    lic411.frame_ok_count       = 0U;
    lic411.zero_counts          = LIC411_CALIBRATED_ZERO;
    lic411.frame_error_count    = 0U;
    lic411.has_last             = 0U;
    lic411.last_counts          = 0U;
    lic411.reject_jump_count    = 0U;
    lic411.wrap_level           = 0;
    lic411.track_counts         = 0;
    lic411.track_raw            = 0U;
    lic411.track_valid          = 0U;
    lic411.slip_count           = 0U;
    lic411.slip_hold            = 0U;
    lic411.slip_last            = 0U;
    lic411.reject_nostart       = 0U;
    lic411.reject_errbits       = 0U;
    lic411.reject_crc           = 0U;
    lic411.reject_wide          = 0U;
    lic411.hist_index           = 0U;
    lic411.hist_count           = 0U;
    lic411.hist[0]              = 0U;
    lic411.hist[1]              = 0U;
    lic411.hist[2]              = 0U;
    lic411.crc_error_count      = 0U;
    lic411.timeout_error_count  = 0U;

    lic411.sample.counts       = 0U;
    lic411.sample.nm           = 0;
    lic411.sample.mm           = 0.0;
    lic411.sample.error_1      = 0U;
    lic411.sample.error_2      = 0U;
    lic411.sample.timestamp_ms = 0U;
}

uint8_t Lic411_Set_Current_Position_As_Zero(void)
{
    if (lic411.frame_ok_count == 0U)
    {
        return 1U;
    }

    lic411.zero_counts = lic411.sample.counts;
    return 0U;
}

uint64_t Lic411_Get_Zero_Counts(void)
{
    return lic411.zero_counts;
}

/**
 * @brief  模块主任务（状态机，非阻塞）
 * @return 无
 */
void Lic411_Task(void)
{
    switch (lic411.state)
    {
        case LIC411_IDLE:
            if (lic411.period_count > 0U)
            {
                return;                             /* 采样周期未到 */
            }

            /* 上电后先发若干帧错误复位（EnDat 2.2 #12），再切回位置读取 */
        if (lic411.reset_frames_left > 0U)
        {
            lic411.cmd = LIC411_CMD_ERROR_RESET;
            lic411.reset_frames_left--;
        }
        else
        {
            lic411.cmd = LIC411_CMD_POSITION;
        }

        if (Lic411_Port_Start(&lic411.cmd, 1U, LIC411_CMD_CLOCKS,
                                      lic411.sample.raw, LIC411_RAW_BYTES) == 0U)
            {
                lic411.timeout_count = LIC411_TIMEOUT_MS;
                lic411.state         = LIC411_WAIT;
            }
            else
            {
                lic411.frame_error_count++;
                lic411.period_count = LIC411_PERIOD_MS;
            }
            break;

        case LIC411_WAIT:
            if (Lic411_Port_Get_State() == LIC411_PORT_DONE)
            {
                Lic411_Port_Clear_Flag();
                Lic411_Handle_Frame();
                lic411.state        = LIC411_IDLE;
                lic411.period_count = LIC411_PERIOD_MS;
            }
            else if (Lic411_Port_Get_State() == LIC411_PORT_ERROR)
            {
                Lic411_Port_Clear_Flag();
                lic411.timeout_error_count++;
                lic411.state        = LIC411_IDLE;
                lic411.period_count = LIC411_PERIOD_MS;
            }
            else if (lic411.timeout_count == 0U)
            {
                Lic411_Port_Abort();
                lic411.timeout_error_count++;
                lic411.frame_error_count++;
                lic411.state        = LIC411_IDLE;
                lic411.period_count = LIC411_PERIOD_MS;
            }
            else
            {
                /* 等待物理层完成，什么也不做 */
            }
            break;

        default:
            lic411.state = LIC411_IDLE;
            break;
    }
}

/**
 * @brief  1 ms 节拍处理
 * @note   只递减计数器，不做业务逻辑
 * @return 无
 */
void Lic411_Timing_Handle(void)
{
    if (lic411.period_count > 0U)
    {
        lic411.period_count--;
    }

    if (lic411.timeout_count > 0U)
    {
        lic411.timeout_count--;
    }
}

/**
 * @brief  手动指定帧结构
 * @param[in] offset        起始位偏移
 * @param[in] crc_msb_first CRC 位序
 * @return 无
 */
void Lic411_Set_Layout(uint8_t offset, uint8_t crc_msb_first)
{
    lic411.layout_offset        = offset;
    lic411.layout_crc_msb_first = crc_msb_first;
    lic411.auto_detect_enable   = 0U;
}

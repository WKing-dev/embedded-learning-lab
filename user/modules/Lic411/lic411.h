/**
 * @file lic411.h
 * @author WLJ
 * @brief HEIDENHAIN LIC 411（EnDat 2.2）位置读取模块
 * @version 0.1
 * @date 2026-11-17
 *
 * @copyright Copyright (c) 2026
 *
 * 一帧结构（共 44 位）：
 *   S(1) + F1(1) + F2(1) + 位置值(36, LSB 先行) + CRC5(5)
 *
 * 编码器参数（ID 1116403-09）：
 *   测量步距 1 nm / 数据字 36 位 / t_cal ≤ 5 µs
 */

#ifndef _LIC411_H_
#define _LIC411_H_

#include <stdint.h>
#include <stddef.h>   /* NULL */

/* ---------------- 协议常量 ---------------- */
/* 读位置值命令字节。
 */
/* 模式指令 #8「编码器发送位置值（带附加信息）」：M2M1M0(M2)(M1)(M0) = 111000
 * 线上 8 位 = [2T: 0,0] + [111000] = 0,0,1,1,1,0,0,0
 * SPI 为 LSB 先行 -> 字节值 0x1C；若改成 MSB 先行，字节值必须改为 0x38
 * 注意：规范原文"Version 2.2 supports type 2.2 mode commands and type 2.1 commands"，
 *       即 EnDat 2.2 编码器同时支持 2.1 与 2.2 指令；本机用 2.2 指令 #8
 *       编码器实际支持哪套指令集，见其存储区 Word 37 的 bit1 */
#define LIC411_CMD_POSITION        0x1CU  /* EnDat 2.2 #8：发送带附加数据的位置值 */
#define LIC411_CMD_ERROR_RESET     0x54U  /* EnDat 2.2 #12：发送位置值并接收错误重置 */
#define LIC411_RESET_FRAMES        2U     /* 上电后先发几帧复位指令 */
#define LIC411_CMD_CLOCKS          8U      /* 模式指令占用的时钟数（2T + 6 位） */
#define LIC411_RAW_BYTES          7U      /* 收 7 字节 = 56 位（偏移7 + 43 位帧 = 50 位） */
#define LIC411_RAW_BITS           (LIC411_RAW_BYTES * 8U)
/* 位置值宽度：LIC 411 目录规格为 36 位。
   帧总长 43 = S(1) + F1(1) + 位置(36) + CRC5(5)
   与"44 位(S+F1+F2+36+CRC5)"不同：本机实测帧长恒为 43 位。
   注意：S+F1+F2+35 与 S+F1+36 的 CRC 载荷都是 38 位、且是同一批线上位，
   因此 CRC 无法区分两者 —— 只能靠 bit 行为判定（实测该位每两帧翻转一次，
   是位置最低位，不是错误位）。 */
#define LIC411_DATA_BITS          36U
#define LIC411_STATUS_BITS         2U     /* S 之后 2 个错误位：F1(位置无效) + F2 */
#define LIC411_CRC_BITS           5U
#define LIC411_STEP_NM             1U     /* 测量步距 1 nm */
#define LIC411_CALIBRATED_ZERO     28148908773ULL /* 机械标定零点 */
#define LIC411_MAX_OFFSET         20U     /* 起始位最大搜索偏移（64 - 44） */
/* 码字滑动补偿开关
   LIC 411 是绝对式编码器，用伪随机码(PRC)做绝对编码，相关窗口长度
   = 8 个栅距 = 160 um。光学信号余量不足时，相关峰偶发锁到相邻码字，
   位置值整段偏移 160,000 计数（= 160 um @1nm），且只在移动跨越
   码字边界时发生 —— 这是补偿，不是修复。
   0 = 关闭：直接输出编码器原值（移动时会看到 ±160,000 的跳变）
   1 = 打开：按连续性补偿一个码字，输出连续；首帧仍用编码器绝对值作基准，
              因此绝对零点不丢 */
#define LIC411_TRACK_ENABLE       0U     /* 安装已修正：直接保留编码器36位绝对位置 */
#define LIC411_SLIP_GUARD         0U     /* 码字滑移保持：跳变超过半个码字先压住，
                                            连续 LIC411_SLIP_HOLD 帧才认作真实位移 */
#define LIC411_SLIP_THRESHOLD     80000U /* 半个码字（1 码字 = 160000） */
#define LIC411_SLIP_HOLD          8U     /* 连续 8 帧（80 ms）才接受新位置 */
#define LIC411_TRACK_MAX_STEP     40000U  /* 单帧可信增量上限：10 ms 内 40 µm（4 mm/s） */
#define LIC411_WRAP_PERIOD        160000U  /* 一个码字的长度（= 8 栅距 = 160 um @1nm） */
#define LIC411_WRAP_TOL           80000U   /* 半周期：超过它就必然是码字跨越，不设窗口 */
#define LIC411_MAX_JUMP           1000000U   /* 单帧允许的最大跳变（1 mm @1nm/10ms = 100 mm/s） */
#define LIC411_FRAME_PERIOD       51U     /* 实测帧周期（时钟数） */
#define LIC411_SEARCH_SPAN        2U      /* 帧头搜索范围：上次偏移 ±2 */
#define LIC411_LAYOUT_OFFSET      6U      /* 起始位在 RAW 中的位置（44 位帧，实测） */
#define LIC411_LAYOUT_CRC_MSB     1U      /* CRC 位序：1 = MSB 先行（实测） */
#define LIC411_FRAME_BITS         44U     /* S + F1 + F2 + 36 + CRC5 */

/* ---------------- 运行参数 ---------------- */
#define LIC411_PERIOD_MS          2U     /* 采样周期 */
#define LIC411_TIMEOUT_MS         20U     /* 单次事务超时 */
#define LIC411_DECODE_FAIL_MAX    3U      /* 连续解码失败几次后重新标定 */
#define LIC411_CANDIDATE_MAX      ((LIC411_MAX_OFFSET + 1U) * 2U)   /* 候选帧结构上限 */

/* 模块状态 */
typedef enum __Lic411_State_Enum
{
    LIC411_IDLE = 0,        /* 空闲，等待采样周期 */
    LIC411_WAIT,            /* 事务进行中，等待物理层完成 */
    LIC411_ERROR            /* 异常，下一周期自动恢复 */
} Lic411_State_Enum;

/* 一次采样结果 */
typedef struct __Lic411_Sample_Struct
{
    uint64_t counts;              /* 编码器绝对计数（36 位） */
    int64_t  nm;                  /* 位移，单位 nm */
    double   mm;                  /* 位移，单位 mm */
    uint8_t  error_1;             /* 状态位 F1；本机有效帧实测为 0 */
    uint8_t  error_2;             /* 状态位 F2；本机有效帧实测为 1 */
    uint64_t raw_counts;            /* 当前帧解出的原始值（未滤波，调试用） */
    uint8_t  raw_offset;            /* 当前帧实际使用的起始位偏移（调试用） */
    uint8_t  raw[LIC411_RAW_BYTES]; /* 原始位流，调试用 */
    uint32_t timestamp_ms;        /* 采样时刻 */
} Lic411_Sample_Struct;

/* 模块运行数据 */
typedef struct __Lic411_Struct
{
    Lic411_State_Enum state;            /* 状态机状态 */
    uint8_t  cmd;                       /* 本帧使用的模式指令字节 */
    Lic411_Sample_Struct sample;        /* 最近一次采样 */

    uint16_t period_count;              /* 采样周期节拍（1 ms 递减） */
    uint16_t timeout_count;             /* 事务超时节拍（1 ms 递减） */

    uint8_t  layout_offset;             /* 起始位偏移 */
    uint8_t  layout_crc_msb_first;      /* CRC 位序：1 = 高位先发 */
    uint8_t  auto_detect_enable;        /* 1 = 允许自动标定帧结构 */
    uint8_t  decode_fail_count;         /* 连续解码失败计数 */
    uint8_t  candidate_offset[LIC411_CANDIDATE_MAX];  /* 候选偏移 */
    uint8_t  candidate_crc_msb[LIC411_CANDIDATE_MAX]; /* 候选 CRC 位序 */
    uint8_t  candidate_count;                         /* 候选数量 */

    uint32_t frame_ok_count;            /* 成功帧数 */
    uint64_t zero_counts;               /* 当前使用的零点；复位后恢复为标定零点 */
    uint64_t last_counts;                /* 上一帧被接受的计数值 */
    uint8_t  has_last;                   /* 是否已有上一帧 */
    uint32_t reject_jump_count;          /* 被连续性校验丢弃的帧数 */
    int64_t  wrap_level;                 /* 已跨越的绝对码周期数（圈数） */
    int64_t  track_counts;               /* 增量跟踪得到的连续位置（对外输出） */
    uint64_t track_raw;                  /* 上一帧的编码器原始读数（增量基准） */
    uint8_t  track_valid;                /* 跟踪基准是否已建立 */
    uint64_t slip_count;                 /* 累计检测到的滑移次数 */
    uint8_t  slip_hold;                  /* 滑移保持计数：跳变已持续的帧数 */
    uint64_t slip_last;                  /* 滑移保持期间对外输出的值 */
    uint16_t reset_frames_left;          /* 上电后剩余的复位指令帧数 */
    uint32_t reject_nostart;             /* 拒收原因：找不到起始位 */
    uint32_t reject_errbits;             /* 拒收原因：错误位 E1/E2 为 1 */
    uint32_t reject_crc;                 /* 拒收原因：起始位正常、错误位为 0，但 CRC 不匹配 */
    uint32_t reject_wide;                /* 拒收原因：帧跑出 ±2 搜索范围（全偏移扫描能解出） */
    uint64_t hist[3];                    /* 保留：旧中值滤波缓冲（已不使用） */
    uint8_t  raw_snap[LIC411_RAW_BYTES]; /* 被接受那一帧的原始数据快照 */
    uint8_t  hist_index;                 /* 环形缓冲写入位置 */
    uint8_t  hist_count;                 /* 已积累的样本数（最多 3） */
    uint32_t frame_error_count;         /* 失败帧数 */
    uint32_t crc_error_count;           /* CRC 校验错误 */
    uint32_t timeout_error_count;       /* 超时 / 物理层错误 */
} Lic411_Struct;

extern Lic411_Struct lic411;

/**
 * @brief  模块初始化
 * @note   调用前须已实现 lic411_port.h 中的移植接口
 * @return 无
 */
void Lic411_Init(void);

/**
 * @brief  模块主任务
 * @note   在主循环中反复调用，内部为状态机，不阻塞
 * @return 无
 */
void Lic411_Task(void);

/**
 * @brief  1 ms 节拍处理
 * @note   必须放入 1 ms 定时中断（本项目放在 SysTick），仅递减计数器
 * @return 无
 */
void Lic411_Timing_Handle(void);

/**
 * @brief  把当前有效位置设为临时零点
 * @note   零点只保存在RAM中；重新上电或复位后恢复 LIC411_CALIBRATED_ZERO
 * @return 0 = 设置成功；1 = 当前还没有有效位置
 */
uint8_t Lic411_Set_Current_Position_As_Zero(void);

/**
 * @brief  获取当前使用的零点计数
 * @return 零点对应的编码器绝对计数
 */
uint64_t Lic411_Get_Zero_Counts(void);

/**
 * @brief  手动指定帧结构（跳过自动标定）
 * @param[in] offset        起始位偏移
 * @param[in] crc_msb_first CRC 位序，1 = 高位先发
 * @return 无
 */
void Lic411_Set_Layout(uint8_t offset, uint8_t crc_msb_first);

/**
 * @brief  用任意参数尝试解码（上电自检用）
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
                          uint64_t *value);

#endif /* _LIC411_H_ */

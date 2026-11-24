/**
 * @file    bsp_touch.c
 * @author  华畅
 * @brief   FT5426 电容触摸驱动（I2C3）—— 中断里直接处理
 * @version 0.2
 * @date    2026-10-27
 *
 * @copyright Copyright (c) 2026
 *
 * 工作方式：
 *    FT5426 有数据时把 INT 拉低，产生 PH13 下降沿；
 *    HAL_GPIO_EXTI_Callback() -> BSP_TOUCH_IRQHandler() 在中断里完成
 *    读取、解析、标定、滤波，并把结果存起来；
 *    主循环调用 BSP_TOUCH_Scan() 只取结果，不再访问 I2C。
 */

#include "bsp_touch.h"

/* ---------------- 芯片与总线参数 ---------------- */
#define TP_I2C_ADDR                 (0x38U)     /* 7 位从机地址 */
#define TP_CHIP_ID_EXPECT           (0x54U)     /* FT5426 的芯片 ID */
#define TP_I2C_TIMEOUT_MS           (5U)        /* 中断内读，超时给短 */
#define TP_I2C_READY_TRIALS         (3U)        /* 探活重试次数 */
#define TP_I2C_READY_TIMEOUT_MS     (10U)       /* 单次探活超时 */
#define TP_READY_WAIT_MAX_MS        (300U)      /* 复位后等待设备应答的上限 */
#define TP_READY_POLL_MS            (10U)       /* 探活轮询间隔 */

/* ---------------- 复位时序 ---------------- */
#define TP_RESET_LOW_MS             (20U)       /* RST 保持低电平的时间 */
#define TP_RESET_READY_MS           (300U)      /* RST 释放后等待芯片就绪的时间 */

/* ---------------- 寄存器地址 ---------------- */
#define TP_REG_POINT_DATA           (0x02U)     /* TD_STATUS 起，含第 1 点坐标 */
#define TP_REG_CHIP_ID              (0xA3U)     /* 芯片 ID */

/* ---------------- 数据解析 ---------------- */
#define TP_POINT_DATA_LEN           (5U)        /* TD_STATUS + XH/XL/YH/YL */
#define TP_POINT_COUNT_MASK         (0x0FU)     /* TD_STATUS 低 4 位 = 触点数 */
#define TP_TOUCH_VALID_MASK         (0x80U)     /* XH 的 bit7 = 触摸有效 */
#define TP_COORD_HIGH_MASK          (0x0FU)     /* XH / YH 的低 4 位是高 4 位坐标 */

/* ---------------- 滤波与重试参数 ---------------- */
#define TP_FILTER_POINTS            (3U)        /* 中值滤波点数 */
#define TP_STABLE_DIFF_MAX          (3)         /* 连读两次允许的最大差值（像素） */
#define TP_STABLE_RETRY_MAX         (2U)        /* 双读校验最多尝试次数 */

#define TP_ABS32(value)             (((value) < 0) ? (-(value)) : ((value)))

/* ---------------- 读点结果 ---------------- */
#define TP_READ_NO_TOUCH            (0U)        /* 无触点 */
#define TP_READ_OK                  (1U)        /* 有触点且稳定 */
#define TP_READ_UNSTABLE            (2U)        /* 有触点但抖动，本次不更新 */

/* ======================= 标定参数表 =======================
 * 换不同分辨率的屏时，只需要改这张表。
 * 公式：screen = (b - a * raw) / 1000，系数里已包含 180 度翻转。
 */
typedef struct Touch_Calib_Struct_s
{
    uint16_t screen_width;      /* 屏幕宽度（像素） */
    uint16_t screen_height;     /* 屏幕高度（像素） */
    int32_t  x_scale_a;         /* X 轴系数 a */
    int32_t  x_offset_b;        /* X 轴偏移 b */
    int32_t  y_scale_a;         /* Y 轴系数 a */
    int32_t  y_offset_b;        /* Y 轴偏移 b */
} Touch_Calib_Struct;

static const Touch_Calib_Struct touch_calib =
{
    1024U,          /* screen_width  */
    600U,           /* screen_height */
    1006,           /* x_scale_a     */
    1020410,        /* x_offset_b    */
    1009,           /* y_scale_a     */
    598870          /* y_offset_b    */
};

#define TP_CAL_SCALE                (1000L)     /* 定点系数的放大倍数 */

/* ======================= 调试缓存 ======================= */
typedef struct Touch_Raw_Debug_Struct_s
{
    uint8_t data[BSP_TOUCH_RAW_LEN];    /* 最近一次读到的原始 5 字节 */
    uint8_t valid;                      /* 0 = 还没采到过 */
} Touch_Raw_Debug_Struct;

static volatile Touch_Raw_Debug_Struct touch_raw_debug;

/* ======================= 中断共享状态 =======================
 * 中断上下文写、主循环读，因此全部使用 volatile。
 */
static volatile uint8_t  touch_event = BSP_TOUCH_NONE;   /* 最近一次事件 */
static volatile uint16_t touch_x = 0U;                   /* 最近一次屏幕坐标 */
static volatile uint16_t touch_y = 0U;
static volatile uint32_t irq_count = 0U;                 /* 中断计数 */

static uint8_t touch_was_down = 0U;                      /* 上次是否有触点 */

/* 中断通知回调（由应用层注册；BSP 不依赖 RTOS） */
static BSP_TOUCH_NotifyFn touch_notify_fn = (BSP_TOUCH_NotifyFn)0;

/* 中值滤波的滑动窗口，只在中断里访问 */
static uint16_t filter_x[TP_FILTER_POINTS];
static uint16_t filter_y[TP_FILTER_POINTS];
static uint8_t  filter_index = 0U;
static uint8_t  filter_count = 0U;

/* ======================= 内部函数 ======================= */

/**
 * @brief  读取触摸芯片的一个寄存器（可连续读多个字节）
 *
 * @param[in]  reg    寄存器地址
 * @param[out] buffer 接收缓冲区
 * @param[in]  length 读取长度（字节）
 * @return 1 = 读取成功，0 = 读取失败
 */
static uint8_t tp_read_register(uint8_t reg, uint8_t *buffer, uint16_t length)
{
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Read(&hi2c3,
                              (uint16_t)(TP_I2C_ADDR << 1),
                              reg,
                              I2C_MEMADD_SIZE_8BIT,
                              buffer,
                              length,
                              TP_I2C_TIMEOUT_MS);

    return ((status == HAL_OK) ? 1U : 0U);
}

/**
 * @brief  读取一个触点的原始坐标（未做方向翻转和标定）
 *
 * 数据格式：TD_STATUS、XH、XL、YH、YL 共 5 字节，
 * X / Y 均为 12 位，高 4 位在 XH / YH 的低 4 位里。
 *
 * @param[out] x 原始 X 坐标
 * @param[out] y 原始 Y 坐标
 * @return 1 = 读到有效触点，0 = 无触点或数据无效
 */
static uint8_t tp_read_raw_point(uint16_t *x, uint16_t *y)
{
    uint8_t buffer[TP_POINT_DATA_LEN];
    uint8_t i = 0U;

    if (tp_read_register(TP_REG_POINT_DATA, buffer, TP_POINT_DATA_LEN) == 0U)
    {
        return 0U;
    }

    /* 保存原始字节，方便调试 */
    for (i = 0U; i < BSP_TOUCH_RAW_LEN; i++)
    {
        touch_raw_debug.data[i] = buffer[i];
    }
    touch_raw_debug.valid = 1U;

    if ((buffer[0] & TP_POINT_COUNT_MASK) == 0U)
    {
        return 0U;                                  /* 触点数 = 0 */
    }

    if ((buffer[1] & TP_TOUCH_VALID_MASK) == 0U)
    {
        return 0U;                                  /* 触摸无效 */
    }

    *x = (uint16_t)((((uint16_t)(buffer[1] & TP_COORD_HIGH_MASK)) << 8) | buffer[2]);
    *y = (uint16_t)((((uint16_t)(buffer[3] & TP_COORD_HIGH_MASK)) << 8) | buffer[4]);

    /* 空闲时芯片返回 FF FF FF FF，会解出 4095，这里当作无触摸 */
    if ((*x >= touch_calib.screen_width) || (*y >= touch_calib.screen_height))
    {
        return 0U;
    }

    return 1U;
}

/**
 * @brief  读一个稳定的触点：连读两次一致才采信，不一致时重试
 *
 * 先读一次判断有没有触点：
 *   - 没有触点立刻返回，避免释放时做无谓的重试
 *   - 有触点则再读一次比较，一致即采信；不一致则把新值作为基准再试
 *
 * @param[out] x 原始 X 坐标
 * @param[out] y 原始 Y 坐标
 * @return TP_READ_NO_TOUCH / TP_READ_OK / TP_READ_UNSTABLE
 */
static uint8_t tp_read_stable_point(uint16_t *x, uint16_t *y)
{
    uint16_t base_x = 0U;
    uint16_t base_y = 0U;
    uint16_t next_x = 0U;
    uint16_t next_y = 0U;
    int32_t  diff_x = 0;
    int32_t  diff_y = 0;
    uint8_t  retry  = 0U;

    if (tp_read_raw_point(&base_x, &base_y) == 0U)
    {
        return TP_READ_NO_TOUCH;
    }

    for (retry = 0U; retry < TP_STABLE_RETRY_MAX; retry++)
    {
        if (tp_read_raw_point(&next_x, &next_y) == 0U)
        {
            continue;
        }

        diff_x = TP_ABS32(((int32_t)base_x) - ((int32_t)next_x));
        diff_y = TP_ABS32(((int32_t)base_y) - ((int32_t)next_y));

        if ((diff_x <= TP_STABLE_DIFF_MAX) && (diff_y <= TP_STABLE_DIFF_MAX))
        {
            *x = (uint16_t)((((uint32_t)base_x) + ((uint32_t)next_x)) / 2U);
            *y = (uint16_t)((((uint32_t)base_y) + ((uint32_t)next_y)) / 2U);
            return TP_READ_OK;
        }

        /* 不一致：把最新值作为下一次比较的基准 */
        base_x = next_x;
        base_y = next_y;
    }

    return TP_READ_UNSTABLE;
}

/**
 * @brief  原始坐标 -> 屏幕坐标（方向翻转 + 线性标定 + 限幅）
 *
 * @param[in,out] x 输入原始 X，输出屏幕 X
 * @param[in,out] y 输入原始 Y，输出屏幕 Y
 * @return 无
 */
static void tp_map_to_screen(uint16_t *x, uint16_t *y)
{
    int32_t screen_x = 0;
    int32_t screen_y = 0;
    int32_t max_x = (int32_t)(touch_calib.screen_width - 1U);
    int32_t max_y = (int32_t)(touch_calib.screen_height - 1U);

    screen_x = (int32_t)((touch_calib.x_offset_b -
                          (touch_calib.x_scale_a * ((int32_t)(*x)))) / TP_CAL_SCALE);
    screen_y = (int32_t)((touch_calib.y_offset_b -
                          (touch_calib.y_scale_a * ((int32_t)(*y)))) / TP_CAL_SCALE);

    if (screen_x < 0)
    {
        screen_x = 0;
    }
    if (screen_x > max_x)
    {
        screen_x = max_x;
    }
    if (screen_y < 0)
    {
        screen_y = 0;
    }
    if (screen_y > max_y)
    {
        screen_y = max_y;
    }

    *x = (uint16_t)screen_x;
    *y = (uint16_t)screen_y;
}

/**
 * @brief  三个数的中值
 *
 * @param[in] a 第一个数
 * @param[in] b 第二个数
 * @param[in] c 第三个数
 * @return 三者按大小排序后位于中间的那个数
 */
static uint16_t tp_median_of_three(uint16_t a, uint16_t b, uint16_t c)
{
    uint16_t temp = 0U;

    if (a > b)
    {
        temp = a;
        a = b;
        b = temp;
    }
    if (b > c)
    {
        temp = b;
        b = c;
        c = temp;
    }
    if (a > b)
    {
        temp = a;
        a = b;
        b = temp;
    }

    return b;
}

/**
 * @brief  把一组坐标压入滑动窗口并取中值
 *
 * @param[in]  x     屏幕 X 坐标
 * @param[in]  y     屏幕 Y 坐标
 * @param[out] out_x 滤波后的 X 坐标
 * @param[out] out_y 滤波后的 Y 坐标
 * @return 无
 */
static void tp_push_and_filter(uint16_t x, uint16_t y, uint16_t *out_x, uint16_t *out_y)
{
    filter_x[filter_index] = x;
    filter_y[filter_index] = y;

    filter_index = (uint8_t)((filter_index + 1U) % TP_FILTER_POINTS);
    if (filter_count < TP_FILTER_POINTS)
    {
        filter_count++;
    }

    if (filter_count < TP_FILTER_POINTS)
    {
        *out_x = x;
        *out_y = y;
        return;
    }

    *out_x = tp_median_of_three(filter_x[0U], filter_x[1U], filter_x[2U]);
    *out_y = tp_median_of_three(filter_y[0U], filter_y[1U], filter_y[2U]);
}

/* ======================= 对外接口 ======================= */

uint8_t BSP_TOUCH_Init(void)
{
    uint8_t  chip_id = 0U;
    uint32_t wait_ms = 0U;

    /* 硬件复位：低电平保持一段时间，再释放并等待芯片就绪 */
    HAL_GPIO_WritePin(TOUCH_RST_GPIO_Port, TOUCH_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(TP_RESET_LOW_MS);
    HAL_GPIO_WritePin(TOUCH_RST_GPIO_Port, TOUCH_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(TP_RESET_READY_MS);

    /* 等待设备应答 */
    while (HAL_I2C_IsDeviceReady(&hi2c3,
                                 (uint16_t)(TP_I2C_ADDR << 1),
                                 TP_I2C_READY_TRIALS,
                                 TP_I2C_READY_TIMEOUT_MS) != HAL_OK)
    {
        if (wait_ms > TP_READY_WAIT_MAX_MS)
        {
            return 1U;
        }
        HAL_Delay(TP_READY_POLL_MS);
        wait_ms = wait_ms + TP_READY_POLL_MS;
    }

    /* 校验芯片 ID */
    if (tp_read_register(TP_REG_CHIP_ID, &chip_id, 1U) == 0U)
    {
        return 1U;
    }
    if (chip_id != TP_CHIP_ID_EXPECT)
    {
        return 2U;
    }

    /* 清内部状态 */
    touch_event        = BSP_TOUCH_NONE;
    touch_x            = 0U;
    touch_y            = 0U;
    irq_count          = 0U;
    touch_was_down     = 0U;
    filter_index       = 0U;
    filter_count       = 0U;
    touch_raw_debug.valid = 0U;

    return 0U;
}

void BSP_TOUCH_SetNotifyCallback(BSP_TOUCH_NotifyFn fn)
{
    touch_notify_fn = fn;
}

void BSP_TOUCH_IRQNotify(void)
{
    irq_count++;

    if (touch_notify_fn != (BSP_TOUCH_NotifyFn)0)
    {
        touch_notify_fn();
    }
}

uint8_t BSP_TOUCH_ReadNow(uint16_t *x, uint16_t *y)
{
    uint16_t raw_x    = 0U;
    uint16_t raw_y    = 0U;
    uint16_t screen_x = 0U;
    uint16_t screen_y = 0U;
    uint8_t  result   = TP_READ_NO_TOUCH;

    result = tp_read_stable_point(&raw_x, &raw_y);

    if (result == TP_READ_NO_TOUCH)
    {
        /* 真的没有触点了：只有"之前在触摸"才上报抬起 */
        filter_index = 0U;
        filter_count = 0U;

        if (touch_was_down != 0U)
        {
            touch_was_down = 0U;
            touch_event = BSP_TOUCH_UP;
            return BSP_TOUCH_UP;
        }
        return BSP_TOUCH_NONE;
    }

    if (result == TP_READ_OK)
    {
        touch_was_down = 1U;

        tp_map_to_screen(&raw_x, &raw_y);
        tp_push_and_filter(raw_x, raw_y, &screen_x, &screen_y);

        touch_x = screen_x;
        touch_y = screen_y;
        touch_event = BSP_TOUCH_DOWN;

        *x = screen_x;
        *y = screen_y;
        return BSP_TOUCH_DOWN;
    }

    /* 有触点但数据一直抖动：本次不更新 */
    return BSP_TOUCH_NONE;
}

void BSP_TOUCH_GetLatest(uint16_t *x, uint16_t *y, uint8_t *pressed)
{
    *x       = touch_x;
    *y       = touch_y;
    *pressed = touch_was_down;
}

uint8_t BSP_TOUCH_Scan(uint16_t *x, uint16_t *y)
{
    uint8_t event = touch_event;

    if (event == BSP_TOUCH_NONE)
    {
        return BSP_TOUCH_NONE;
    }

    *x = touch_x;
    *y = touch_y;

    touch_event = BSP_TOUCH_NONE;       /* 取走即清，保证同一事件只返回一次 */

    return event;
}

uint32_t BSP_TOUCH_IrqCount(void)
{
    return irq_count;
}

uint8_t BSP_TOUCH_GetRawData(uint8_t *buffer, uint8_t length)
{
    uint8_t copy_len = 0U;
    uint8_t i = 0U;

    if ((buffer == NULL) || (length == 0U))
    {
        return 0U;
    }

    if (touch_raw_debug.valid == 0U)
    {
        return 0U;
    }

    copy_len = (length < BSP_TOUCH_RAW_LEN) ? length : BSP_TOUCH_RAW_LEN;

    for (i = 0U; i < copy_len; i++)
    {
        buffer[i] = touch_raw_debug.data[i];
    }

    return copy_len;
}

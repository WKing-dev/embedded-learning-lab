#ifndef __AI_CODE_H
#define __AI_CODE_H

#include "stm32h7xx_hal.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 状态枚举
 *===========================================================================*/
typedef enum {
    MM_STATUS_NONE = 0,   /* 无标 */
    MM_STATUS_BOLT,       /* 闪电 */
    MM_STATUS_WARN,       /* 警告 */
    MM_STATUS_MAX
} mm_status_t;

/* 计时器模式 */
typedef enum {
    MM_TIMER_COUNTUP = 0,
    MM_TIMER_COUNTDOWN
} mm_timer_mode_t;

/*===========================================================================
 * 数据结构
 *===========================================================================*/
typedef struct {
    int32_t         seconds;    /* 当前秒数 */
    int32_t         initial;    /* 初始秒数(用于复位) */
    bool            running;    /* 是否运行中 */
    mm_timer_mode_t mode;
} mm_timer_t;

typedef struct {
    /* 状态 */
    mm_status_t status_pressure1;
    mm_status_t status_pressure2;
    mm_status_t status_vsat;
    mm_status_t status_hct;

    /* 数值 */
    int32_t pressure1;       /* mmHg */
    int32_t pressure2;       /* mmHg */
    int32_t vsat_int;        /* 整数部分 */
    int32_t vsat_dec;        /* 小数部分 (0-9) */
    int32_t hct;             /* % */

    /* 计时器 */
    mm_timer_t timer1;       /* 48:29 正计时 */
    mm_timer_t timer2;       /* 25:29 倒计时 */
    mm_timer_t timer3;       /* 2:36  倒计时 */

    /* 运行时 */
    lv_timer_t *tick_timer;
} mm_state_t;

/*===========================================================================
 * API
 *===========================================================================*/

/**
 * @brief 创建医疗监护仪界面
 * @param parent 父对象(通常为 lv_scr_act())
 * @return 界面根对象
 */
lv_obj_t *mm_create(lv_obj_t *parent);

/**
 * @brief 获取全局状态
 */
mm_state_t *mm_get_state(void);

/**
 * @brief 设置数值(外部数据源调用)
 */
void mm_set_pressure1(int32_t value);
void mm_set_pressure2(int32_t value);
void mm_set_vsat(int32_t int_part, int32_t dec_part);
void mm_set_hct(int32_t value);

/**
 * @brief 设置状态图标(外部报警逻辑调用)
 */
void mm_set_status_pressure1(mm_status_t s);
void mm_set_status_pressure2(mm_status_t s);
void mm_set_status_vsat(mm_status_t s);
void mm_set_status_hct(mm_status_t s);

/**
 * @brief 导出当前状态为 JSON 字符串
 * @return 实际写入长度
 */
size_t mm_export_json(char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* __AI_CODE_H */
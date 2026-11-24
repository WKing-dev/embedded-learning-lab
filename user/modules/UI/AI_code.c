/**
 * @file    AI_code.c
 * @brief   LVGL 8.3.11 医疗监护仪界面实现
 * @note    800x480 触摸屏,STM32H50
 *
 * 交互:
 *   - 点击数值框   → 弹出放大数字键盘,顶部显示框实时显示输入
 *   - 点击状态方块 → 循环切换 无/闪电/警告
 *   - 点击计时显示 → 弹键盘输入 mm:ss
 *   - 点击播放按钮 → 开始/暂停,长按复位
 *   - 点击下载图标 → 导出 JSON 到日志
 */

#include "AI_code.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* 中文字体(由 alimama.c 提供,包含:体外循环计时器,压力 + ASCII) */
LV_FONT_DECLARE(alimama);

/*===========================================================================
 * 颜色
 *===========================================================================*/
#define COL_MONITOR_BG      lv_color_hex(0x1F365A)
#define COL_PANEL_BG1       lv_color_hex(0x253D60)
#define COL_PANEL_BG2       lv_color_hex(0x1A2B44)
#define COL_PANEL_BORDER    lv_color_hex(0x16243A)
#define COL_HEADER_TOP      lv_color_hex(0xA9C9E8)
#define COL_HEADER_BOT      lv_color_hex(0x74A5D1)
#define COL_HEADER_TEXT     lv_color_hex(0x1A2E4C)
#define COL_VALUE_BG        lv_color_hex(0x0E1A2B)
#define COL_VALUE_TEXT      lv_color_hex(0xFFFFFF)
#define COL_BTN_TOP         lv_color_hex(0xA9C9E8)
#define COL_BTN_BOT         lv_color_hex(0x86B2DA)
#define COL_BTN_BORDER      lv_color_hex(0x5A86B5)
#define COL_WARN_RED        lv_color_hex(0xED322B)
#define COL_WARN_YELLOW     lv_color_hex(0xFECA2C)
#define COL_KB_KEY_BG       lv_color_hex(0xE8F0FA)
#define COL_KB_KEY_TEXT     lv_color_hex(0x1A2E4C)

/*===========================================================================
 * 布局(800x480)
 *===========================================================================*/
#define SCREEN_W            1024
#define SCREEN_H            600
#define GAP                 4
#define PANEL_W             ((SCREEN_W - GAP*3) / 2)
#define PANEL_H_TOP         ((SCREEN_H - GAP*3) * 2 / 5)
#define PANEL_H_BOT         ((SCREEN_H - GAP*3) * 3 / 5)
#define HEADER_H            36
#define VALUE_H             60

/*===========================================================================
 * 全局状态
 *===========================================================================*/
static mm_state_t g_state;

typedef struct {
    lv_obj_t *scr;
    lv_obj_t *p1_status;
    lv_obj_t *p1_value;
    lv_obj_t *p2_status;
    lv_obj_t *p2_value;
    lv_obj_t *t_disp[3];
    lv_obj_t *t_btn_icon[3];
    lv_obj_t *vsat_status;
    lv_obj_t *vsat_int_lbl;
    lv_obj_t *vsat_dec_lbl;
    lv_obj_t *hct_status;
    lv_obj_t *hct_value;

    /* 数字键盘(按需创建) */
    lv_obj_t *kb_modal;
    lv_obj_t *kb;
    lv_obj_t *kb_ta;       /* 可见显示框 */
    lv_obj_t *kb_title;    /* 顶部标题 */
    void    (*kb_commit)(const char *text);

    /* 键盘用 group(保证光标闪烁/输入有效) */
    lv_group_t *kb_group;
} mm_ui_t;

static mm_ui_t g_ui;

/*===========================================================================
 * 时钟图标指针坐标(LVGL 不复制点数据,必须静态存储)
 *===========================================================================*/
static lv_point_t g_clk_hour_pts[] = { {13, 14}, {13, 6} };
static lv_point_t g_clk_min_pts[]  = { {13, 14}, {19, 20} };

/*===========================================================================
 * 工具
 *===========================================================================*/
static void mm_format_mmss(int32_t total_sec, char *out, size_t out_size)
{
    if (total_sec < 0) total_sec = 0;
    int32_t m = total_sec / 60;
    int32_t s = total_sec % 60;
    snprintf(out, out_size, "%d:%02d", (int)m, (int)s);
}

/*===========================================================================
 * 状态图标
 *===========================================================================*/
static void mm_draw_status_icon(lv_obj_t *obj, mm_status_t status)
{
    if (!obj) return;
    lv_obj_clean(obj);

    /* 默认底板 */
    lv_obj_set_style_bg_color(obj, COL_VALUE_BG, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 6, 0);

    if (status == MM_STATUS_NONE) return;

    if (status == MM_STATUS_BOLT) {
        /* 红圆 + 白闪电 */
        lv_obj_set_style_bg_color(obj, COL_WARN_RED, 0);
        lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_t *lbl = lv_label_create(obj);
        lv_label_set_text(lbl, LV_SYMBOL_CHARGE);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
        lv_obj_center(lbl);
    } else if (status == MM_STATUS_WARN) {
        /* 黄底 + 黑感叹号 */
        lv_obj_set_style_bg_color(obj, COL_WARN_YELLOW, 0);
        lv_obj_set_style_radius(obj, 4, 0);
        lv_obj_t *lbl = lv_label_create(obj);
        lv_label_set_text(lbl, "!");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x111111), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
        lv_obj_center(lbl);
    }
}

/*===========================================================================
 * 通用控件
 *===========================================================================*/
static lv_obj_t *mm_create_panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COL_PANEL_BG1, 0);
    lv_obj_set_style_bg_grad_color(panel, COL_PANEL_BG2, 0);
    lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(panel, COL_PANEL_BORDER, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 2, 0);
    lv_obj_set_style_pad_all(panel, 6, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static lv_obj_t *mm_create_header(lv_obj_t *parent, const char *text, int w)
{
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, w, HEADER_H);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(hdr, COL_HEADER_TOP, 0);
    lv_obj_set_style_bg_grad_color(hdr, COL_HEADER_BOT, 0);
    lv_obj_set_style_bg_grad_dir(hdr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 2, 0);
    lv_obj_set_style_pad_left(hdr, 10, 0);
    lv_obj_set_style_pad_right(hdr, 4, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(hdr);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, COL_HEADER_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &alimama, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    return hdr;
}

static lv_obj_t *mm_create_value_box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, COL_VALUE_BG, 0);
    lv_obj_set_style_border_color(box, COL_PANEL_BORDER, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 3, 0);
    lv_obj_set_style_pad_right(box, 8, 0);
    lv_obj_set_style_pad_left(box, 4, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

/*===========================================================================
 * 时钟图标
 *===========================================================================*/
static void mm_create_clock_icon(lv_obj_t *parent)
{
    lv_obj_t *clk = lv_obj_create(parent);
    lv_obj_set_size(clk, 28, 28);
    lv_obj_align(clk, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_set_style_bg_opa(clk, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(clk, lv_color_white(), 0);
    lv_obj_set_style_border_width(clk, 2, 0);
    lv_obj_set_style_radius(clk, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(clk, 0, 0);
    lv_obj_clear_flag(clk, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(clk, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *hour = lv_line_create(clk);
    lv_line_set_points(hour, g_clk_hour_pts, 2);
    lv_obj_set_style_line_color(hour, lv_color_white(), 0);
    lv_obj_set_style_line_width(hour, 2, 0);
    lv_obj_set_style_line_rounded(hour, true, 0);

    lv_obj_t *min = lv_line_create(clk);
    lv_line_set_points(min, g_clk_min_pts, 2);
    lv_obj_set_style_line_color(min, lv_color_white(), 0);
    lv_obj_set_style_line_width(min, 2, 0);
    lv_obj_set_style_line_rounded(min, true, 0);
}

/*===========================================================================
 * 数字键盘(放大版 + 实时显示)
 *===========================================================================*/
static void mm_kb_close(void)
{
    if (g_ui.kb_ta && g_ui.kb_group) {
        lv_group_remove_obj(g_ui.kb_ta);
    }
    if (g_ui.kb_modal) {
        lv_obj_del(g_ui.kb_modal);
        g_ui.kb_modal = NULL;
    }
    g_ui.kb = NULL;
    g_ui.kb_ta = NULL;
    g_ui.kb_title = NULL;
    g_ui.kb_commit = NULL;
}

static void mm_kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char *txt = lv_textarea_get_text(g_ui.kb_ta);
        if (g_ui.kb_commit) g_ui.kb_commit(txt);
        mm_kb_close();
    } else if (code == LV_EVENT_CANCEL) {
        mm_kb_close();
    }
}

/* 点击遮罩空白处取消(仅点到遮罩本体时) */
static void mm_kb_modal_click_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    lv_obj_t *cur = lv_event_get_current_target(e);
    if (target == cur) mm_kb_close();
}

/* 关闭按钮（右上角 ×） */
static void mm_kb_close_cb(lv_event_t *e)
{
    (void)e;
    mm_kb_close();
}

/**
 * @brief 打开数字键盘
 * @param title     顶部标题
 * @param initial   初始文本
 * @param accepted  允许的字符集
 * @param commit    确认回调
 */
static void mm_open_numeric_kb(const char *title,
                               const char *initial,
                               const char *accepted,
                               void (*commit)(const char *text))
{
    if (g_ui.kb) return;

    /* ===== 遮罩（只盖底部，减少重绘面积）===== */
    g_ui.kb_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_ui.kb_modal, 680, 400);
    lv_obj_align(g_ui.kb_modal, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(g_ui.kb_modal, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(g_ui.kb_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_ui.kb_modal, 0, 0);
    lv_obj_set_style_radius(g_ui.kb_modal, 0, 0);
    lv_obj_set_style_pad_all(g_ui.kb_modal, 0, 0);
    lv_obj_clear_flag(g_ui.kb_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_ui.kb_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_ui.kb_modal, mm_kb_modal_click_cb,
                        LV_EVENT_CLICKED, NULL);

    /* ===== 顶部标题 ===== */
    g_ui.kb_title = lv_label_create(g_ui.kb_modal);
    lv_label_set_text(g_ui.kb_title, title ? title : "Input");
    lv_obj_set_style_text_color(g_ui.kb_title, lv_color_white(), 0);
    /* 标题可能含中文（压力/计时器），用 alimama 字体（含 ASCII + 所需汉字） */
    lv_obj_set_style_text_font(g_ui.kb_title, &alimama, 0);
    lv_obj_align(g_ui.kb_title, LV_ALIGN_TOP_MID, 0, 12);

    /* ===== 关闭按钮（右上角 ×）===== */
    lv_obj_t *kb_btn_close = lv_btn_create(g_ui.kb_modal);
    lv_obj_set_size(kb_btn_close, 36, 36);
    lv_obj_align(kb_btn_close, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_set_style_radius(kb_btn_close, 0, 0);
    lv_obj_set_style_shadow_width(kb_btn_close, 0, 0);
    lv_obj_set_style_bg_color(kb_btn_close, COL_BTN_TOP, 0);
    lv_obj_add_event_cb(kb_btn_close, mm_kb_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *kb_lbl_close = lv_label_create(kb_btn_close);
    lv_label_set_text(kb_lbl_close, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(kb_lbl_close, COL_KB_KEY_TEXT, 0);
    lv_obj_center(kb_lbl_close);

    /* ===== 可见显示框(实时显示输入) ===== */
    g_ui.kb_ta = lv_textarea_create(g_ui.kb_modal);
    lv_obj_set_size(g_ui.kb_ta, 600, 70);
    lv_obj_align(g_ui.kb_ta, LV_ALIGN_TOP_MID, 0, 42);
    lv_textarea_set_one_line(g_ui.kb_ta, true);
    lv_textarea_set_text(g_ui.kb_ta, initial ? initial : "");
    lv_textarea_set_accepted_chars(g_ui.kb_ta,
                                   accepted ? accepted : "0123456789-");
    lv_textarea_set_max_length(g_ui.kb_ta, 12);
    lv_textarea_set_align(g_ui.kb_ta, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_style_bg_color(g_ui.kb_ta, COL_VALUE_BG, 0);
    lv_obj_set_style_border_color(g_ui.kb_ta, COL_BTN_BORDER, 0);
    lv_obj_set_style_border_width(g_ui.kb_ta, 2, 0);
    lv_obj_set_style_radius(g_ui.kb_ta, 6, 0);
    lv_obj_set_style_text_color(g_ui.kb_ta, COL_VALUE_TEXT, 0);
    lv_obj_set_style_text_font(g_ui.kb_ta, &lv_font_montserrat_28, 0);
    lv_obj_set_style_pad_left(g_ui.kb_ta, 12, 0);
    lv_obj_set_style_pad_right(g_ui.kb_ta, 12, 0);

    /* 光标样式 */
    lv_obj_set_style_border_color(g_ui.kb_ta, COL_BTN_BORDER, LV_PART_CURSOR);
    lv_obj_set_style_border_width(g_ui.kb_ta, 2, LV_PART_CURSOR);

    /* ===== 放大版键盘 ===== */
    g_ui.kb = lv_keyboard_create(g_ui.kb_modal);
    lv_keyboard_set_mode(g_ui.kb, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(g_ui.kb, 600, 240);
    lv_obj_align(g_ui.kb, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(g_ui.kb, COL_PANEL_BG1, 0);
    lv_obj_set_style_border_color(g_ui.kb, COL_BTN_BORDER, 0);
    lv_obj_set_style_border_width(g_ui.kb, 2, 0);
    lv_obj_set_style_radius(g_ui.kb, 6, 0);
    lv_obj_set_style_pad_all(g_ui.kb, 6, 0);
    lv_obj_set_style_pad_gap(g_ui.kb, 6, 0);

    /* 按键样式（按键字体 20 号、直角，减少渲染成本） */
    lv_obj_set_style_bg_color(g_ui.kb, COL_KB_KEY_BG, LV_PART_ITEMS);
    lv_obj_set_style_text_color(g_ui.kb, COL_KB_KEY_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(g_ui.kb, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_border_color(g_ui.kb, COL_BTN_BORDER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(g_ui.kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(g_ui.kb, 0, LV_PART_ITEMS);

    /* 按下反馈 */
    lv_obj_set_style_bg_color(g_ui.kb, COL_BTN_TOP,
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(g_ui.kb, lv_color_white(),
                                LV_PART_ITEMS | LV_STATE_PRESSED);

    /* 绑定 textarea */
    lv_keyboard_set_textarea(g_ui.kb, g_ui.kb_ta);
    lv_obj_add_event_cb(g_ui.kb, mm_kb_event_cb, LV_EVENT_ALL, NULL);

    /* 加入 group 并聚焦,确保光标闪烁、按键输入有效 */
    if (!g_ui.kb_group) {
        g_ui.kb_group = lv_group_create();
    }
    lv_group_add_obj(g_ui.kb_group, g_ui.kb_ta);
    lv_group_focus_obj(g_ui.kb_ta);

    g_ui.kb_commit = commit;
}

/*===========================================================================
 * 数值 / 状态 更新辅助
 *===========================================================================*/
static void update_pressure_label(lv_obj_t *value_box, int32_t value)
{
    if (!value_box) return;
    lv_obj_t *num = lv_obj_get_child(value_box, 0);
    if (num) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)value);
        lv_label_set_text(num, buf);
    }
}

/*===========================================================================
 * 键盘提交回调
 *===========================================================================*/
static void kb_commit_pressure1(const char *text)
{
    int32_t v = (int32_t)strtol(text, NULL, 10);
    mm_set_pressure1(v);
}

static void kb_commit_pressure2(const char *text)
{
    int32_t v = (int32_t)strtol(text, NULL, 10);
    mm_set_pressure2(v);
}

static void kb_commit_vsat(const char *text)
{
    int ip = 0, dp = 0;
    if (sscanf(text, "%d.%d", &ip, &dp) == 2) {
        if (dp < 0) dp = 0;
        if (dp > 9) dp = dp % 10;
        mm_set_vsat(ip, dp);
    } else if (sscanf(text, "%d", &ip) == 1) {
        mm_set_vsat(ip, 0);
    }
}

static void kb_commit_hct(const char *text)
{
    int32_t v = (int32_t)strtol(text, NULL, 10);
    mm_set_hct(v);
}

static void kb_commit_timer1(const char *text)
{
    int m = 0, s = 0;
    if (sscanf(text, "%d:%d", &m, &s) == 2) {
        g_state.timer1.seconds = m * 60 + s;
        g_state.timer1.initial = g_state.timer1.seconds;
        lv_obj_t *lbl = lv_obj_get_child(g_ui.t_disp[0], 0);
        if (lbl) {
            char buf[16];
            mm_format_mmss(g_state.timer1.seconds, buf, sizeof(buf));
            lv_label_set_text(lbl, buf);
        }
    }
}

static void kb_commit_timer2(const char *text)
{
    int m = 0, s = 0;
    if (sscanf(text, "%d:%d", &m, &s) == 2) {
        g_state.timer2.seconds = m * 60 + s;
        g_state.timer2.initial = g_state.timer2.seconds;
        lv_obj_t *lbl = lv_obj_get_child(g_ui.t_disp[1], 0);
        if (lbl) {
            char buf[16];
            mm_format_mmss(g_state.timer2.seconds, buf, sizeof(buf));
            lv_label_set_text(lbl, buf);
        }
    }
}

static void kb_commit_timer3(const char *text)
{
    int m = 0, s = 0;
    if (sscanf(text, "%d:%d", &m, &s) == 2) {
        g_state.timer3.seconds = m * 60 + s;
        g_state.timer3.initial = g_state.timer3.seconds;
        lv_obj_t *lbl = lv_obj_get_child(g_ui.t_disp[2], 0);
        if (lbl) {
            char buf[16];
            mm_format_mmss(g_state.timer3.seconds, buf, sizeof(buf));
            lv_label_set_text(lbl, buf);
        }
    }
}

/*===========================================================================
 * 事件回调
 *===========================================================================*/
static void pressure_value_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *obj = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(obj);

    char buf[16];
    if (idx == 0) {
        snprintf(buf, sizeof(buf), "%d", (int)g_state.pressure1);
        mm_open_numeric_kb("压力 1 (mmHg)", buf, "0123456789-",
                           kb_commit_pressure1);
    } else if (idx == 1) {
        snprintf(buf, sizeof(buf), "%d", (int)g_state.pressure2);
        mm_open_numeric_kb("压力 2 (mmHg)", buf, "0123456789-",
                           kb_commit_pressure2);
    } else {
        snprintf(buf, sizeof(buf), "%d", (int)g_state.hct);
        mm_open_numeric_kb("HCT (%)", buf, "0123456789",
                           kb_commit_hct);
    }
}

static void vsat_value_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d.%d",
             (int)g_state.vsat_int, (int)g_state.vsat_dec);
    mm_open_numeric_kb("v SAT O2 (%)", buf, "0123456789.",
                       kb_commit_vsat);
}

static void status_click_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *obj = lv_event_get_target(e);
    intptr_t key = (intptr_t)lv_obj_get_user_data(obj);

    mm_status_t cur = MM_STATUS_NONE;
    switch (key) {
        case 0: cur = g_state.status_pressure1; break;
        case 1: cur = g_state.status_pressure2; break;
        case 2: cur = g_state.status_vsat;      break;
        case 3: cur = g_state.status_hct;       break;
        default: return;
    }
    mm_status_t next = (mm_status_t)((cur + 1) % MM_STATUS_MAX);

    switch (key) {
        case 0: mm_set_status_pressure1(next); break;
        case 1: mm_set_status_pressure2(next); break;
        case 2: mm_set_status_vsat(next);      break;
        case 3: mm_set_status_hct(next);       break;
    }
}

static void timer_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *obj = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(obj);
    mm_timer_t *t = (idx == 0) ? &g_state.timer1 :
                    (idx == 1) ? &g_state.timer2 : &g_state.timer3;
    t->running = !t->running;
    if (g_ui.t_btn_icon[idx])
        lv_label_set_text(g_ui.t_btn_icon[idx],
                          t->running ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
}

static void timer_btn_long_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED) return;
    lv_obj_t *obj = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(obj);
    mm_timer_t *t = (idx == 0) ? &g_state.timer1 :
                    (idx == 1) ? &g_state.timer2 : &g_state.timer3;
    t->running = false;
    t->seconds = t->initial;
    if (g_ui.t_btn_icon[idx])
        lv_label_set_text(g_ui.t_btn_icon[idx], LV_SYMBOL_PLAY);
    lv_obj_t *lbl = lv_obj_get_child(g_ui.t_disp[idx], 0);
    if (lbl) {
        char buf[16];
        mm_format_mmss(t->seconds, buf, sizeof(buf));
        lv_label_set_text(lbl, buf);
    }
}

static void timer_disp_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *obj = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(obj);
    mm_timer_t *t = (idx == 0) ? &g_state.timer1 :
                    (idx == 1) ? &g_state.timer2 : &g_state.timer3;

    char buf[16];
    mm_format_mmss(t->seconds, buf, sizeof(buf));

    void (*cb)(const char *) = (idx == 0) ? kb_commit_timer1 :
                               (idx == 1) ? kb_commit_timer2 :
                                            kb_commit_timer3;
    const char *title = (idx == 0) ? "计时器 1 (mm:ss)" :
                        (idx == 1) ? "计时器 2 (mm:ss)" :
                                     "计时器 3 (mm:ss)";
    mm_open_numeric_kb(title, buf, "0123456789:", cb);
}

static void download_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    char buf[256];
    mm_export_json(buf, sizeof(buf));
    LV_LOG_USER("Export JSON:\n%s", buf);
}

/*===========================================================================
 * 面板
 *===========================================================================*/
static void create_pressure_panel(lv_obj_t *parent, int x, int y, int w, int h,
                                  const char *title, int pressure_idx,
                                  lv_obj_t **out_status, lv_obj_t **out_value)
{
    lv_obj_t *panel = mm_create_panel(parent, x, y, w, h);
    mm_create_header(panel, title, w - 20);

    const int ICON_SIZE = 56;
    const int ROW_Y     = 50;
    const int ICON_X    = 8;
    const int BOX_X     = ICON_X + ICON_SIZE + 8;
    const int BOX_W     = w - BOX_X - 20;

    lv_obj_t *st = lv_obj_create(panel);
    lv_obj_set_size(st, ICON_SIZE, ICON_SIZE);
    lv_obj_set_pos(st, ICON_X, ROW_Y);
    lv_obj_set_style_border_width(st, 0, 0);
    lv_obj_set_style_radius(st, 6, 0);
    lv_obj_set_style_bg_color(st, COL_VALUE_BG, 0);
    lv_obj_clear_flag(st, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(st, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(st, (void *)(intptr_t)pressure_idx);
    lv_obj_add_event_cb(st, status_click_event_cb, LV_EVENT_CLICKED, NULL);
    *out_status = st;

    lv_obj_t *vb = mm_create_value_box(panel, BOX_X, ROW_Y, BOX_W, ICON_SIZE);
    lv_obj_add_flag(vb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(vb, (void *)(intptr_t)pressure_idx);
    lv_obj_add_event_cb(vb, pressure_value_event_cb, LV_EVENT_CLICKED, NULL);
    *out_value = vb;

    lv_obj_t *num = lv_label_create(vb);
    lv_label_set_text(num, "0");
    lv_obj_set_style_text_color(num, COL_VALUE_TEXT, 0);
    lv_obj_set_style_text_font(num, &lv_font_montserrat_28, 0);
    lv_obj_align(num, LV_ALIGN_RIGHT_MID, -86, 0);

    lv_obj_t *unit = lv_label_create(vb);
    lv_label_set_text(unit, "mmHg");
    lv_obj_set_style_text_color(unit, COL_VALUE_TEXT, 0);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_14, 0);
    lv_obj_align(unit, LV_ALIGN_RIGHT_MID, -26, 4);
}

static void create_timer_panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *panel = mm_create_panel(parent, x, y, w, h);
    mm_create_header(panel, "体外循环计时器", w - 60);

    mm_create_clock_icon(panel);

    for (int i = 0; i < 3; i++) {
        int row_y = HEADER_H + 12 + i * 62;

        lv_obj_t *disp = lv_obj_create(panel);
        lv_obj_set_pos(disp, 8, row_y);
        lv_obj_set_size(disp, 200, 52);
        lv_obj_set_style_bg_color(disp, COL_VALUE_BG, 0);
        lv_obj_set_style_border_width(disp, 0, 0);
        lv_obj_set_style_radius(disp, 3, 0);
        lv_obj_clear_flag(disp, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(disp, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(disp, (void *)(intptr_t)i);
        lv_obj_add_event_cb(disp, timer_disp_event_cb, LV_EVENT_CLICKED, NULL);
        g_ui.t_disp[i] = disp;

        lv_obj_t *lbl = lv_label_create(disp);
        lv_label_set_text(lbl, "0:00");
        lv_obj_set_style_text_color(lbl, COL_VALUE_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
        lv_obj_align(lbl, LV_ALIGN_RIGHT_MID, -8, 0);

        lv_obj_t *btn = lv_btn_create(panel);
        lv_obj_set_pos(btn, 220, row_y);
        lv_obj_set_size(btn, 52, 52);
        lv_obj_set_style_bg_color(btn, COL_BTN_TOP, 0);
        lv_obj_set_style_bg_grad_color(btn, COL_BTN_BOT, 0);
        lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_color(btn, COL_BTN_BORDER, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, timer_btn_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(btn, timer_btn_long_event_cb,
                            LV_EVENT_LONG_PRESSED, NULL);

        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_color(icon, COL_HEADER_TEXT, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_center(icon);
        g_ui.t_btn_icon[i] = icon;
    }
}

static void create_bcare_panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *panel = mm_create_panel(parent, x, y, w, h);
    mm_create_header(panel, "B-Care5", w - 60);

    lv_obj_t *ic = lv_label_create(panel);
    lv_label_set_text(ic, LV_SYMBOL_IMAGE " O2");
    lv_obj_set_style_text_color(ic, lv_color_white(), 0);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_14, 0);
    lv_obj_align(ic, LV_ALIGN_TOP_RIGHT, -8, 8);

    /* ===== v SAT O2 ===== */
    lv_obj_t *lbl1 = lv_label_create(panel);
    lv_label_set_text(lbl1, "v SAT O2");
    lv_obj_set_style_text_color(lbl1, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(lbl1, 10, HEADER_H + 8);

    lv_obj_t *st1 = lv_obj_create(panel);
    lv_obj_set_size(st1, 56, 56);
    lv_obj_set_pos(st1, 10, HEADER_H + 40);
    lv_obj_set_style_border_width(st1, 0, 0);
    lv_obj_set_style_radius(st1, 6, 0);
    lv_obj_set_style_bg_color(st1, COL_VALUE_BG, 0);
    lv_obj_clear_flag(st1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(st1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(st1, (void *)(intptr_t)2);
    lv_obj_add_event_cb(st1, status_click_event_cb, LV_EVENT_CLICKED, NULL);
    g_ui.vsat_status = st1;

    lv_obj_t *vb1 = mm_create_value_box(panel, 76, HEADER_H + 40, w - 100, 56);
    lv_obj_add_flag(vb1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(vb1, vsat_value_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *vint = lv_label_create(vb1);
    lv_label_set_text(vint, "72.");
    lv_obj_set_style_text_color(vint, COL_VALUE_TEXT, 0);
    lv_obj_set_style_text_font(vint, &lv_font_montserrat_28, 0);
    lv_obj_align(vint, LV_ALIGN_RIGHT_MID, -54, 0);
    g_ui.vsat_int_lbl = vint;

    lv_obj_t *vdec = lv_label_create(vb1);
    lv_label_set_text(vdec, "0");
    lv_obj_set_style_text_color(vdec, COL_VALUE_TEXT, 0);
    lv_obj_set_style_text_font(vdec, &lv_font_montserrat_20, 0);
    lv_obj_align(vdec, LV_ALIGN_RIGHT_MID, -44, 0);
    g_ui.vsat_dec_lbl = vdec;

    lv_obj_t *unit1 = lv_label_create(vb1);
    lv_label_set_text(unit1, "%");
    lv_obj_set_style_text_color(unit1, COL_VALUE_TEXT, 0);
    lv_obj_set_style_text_font(unit1, &lv_font_montserrat_14, 0);
    lv_obj_align(unit1, LV_ALIGN_RIGHT_MID, -26, 4);

    /* ===== HCT ===== */
    lv_obj_t *lbl2 = lv_label_create(panel);
    lv_label_set_text(lbl2, "HCT");
    lv_obj_set_style_text_color(lbl2, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl2, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(lbl2, 10, HEADER_H + 110);

    lv_obj_t *st2 = lv_obj_create(panel);
    lv_obj_set_size(st2, 56, 56);
    lv_obj_set_pos(st2, 10, HEADER_H + 142);
    lv_obj_set_style_border_width(st2, 0, 0);
    lv_obj_set_style_radius(st2, 6, 0);
    lv_obj_set_style_bg_color(st2, COL_VALUE_BG, 0);
    lv_obj_clear_flag(st2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(st2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(st2, (void *)(intptr_t)3);
    lv_obj_add_event_cb(st2, status_click_event_cb, LV_EVENT_CLICKED, NULL);
    g_ui.hct_status = st2;

    lv_obj_t *vb2 = mm_create_value_box(panel, 76, HEADER_H + 142, w - 100, 56);
    lv_obj_add_flag(vb2, LV_OBJ_FLAG_CLICKABLE);
    /* idx=2 走 pressure_value_event_cb 的 else 分支 → HCT */
    lv_obj_set_user_data(vb2, (void *)(intptr_t)2);
    lv_obj_add_event_cb(vb2, pressure_value_event_cb, LV_EVENT_CLICKED, NULL);
    g_ui.hct_value = vb2;

    lv_obj_t *hval = lv_label_create(vb2);
    lv_label_set_text(hval, "15");
    lv_obj_set_style_text_color(hval, COL_VALUE_TEXT, 0);
    lv_obj_set_style_text_font(hval, &lv_font_montserrat_28, 0);
    lv_obj_align(hval, LV_ALIGN_RIGHT_MID, -44, 0);

    lv_obj_t *unit2 = lv_label_create(vb2);
    lv_label_set_text(unit2, "%");
    lv_obj_set_style_text_color(unit2, COL_VALUE_TEXT, 0);
    lv_obj_set_style_text_font(unit2, &lv_font_montserrat_14, 0);
    lv_obj_align(unit2, LV_ALIGN_RIGHT_MID, -26, 4);
}

static void create_download_btn(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, 32, 32);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, download_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ic = lv_label_create(btn);
    lv_label_set_text(ic, LV_SYMBOL_DOWNLOAD);
    lv_obj_set_style_text_color(ic, lv_color_white(), 0);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_20, 0);
    lv_obj_center(ic);
}

/*===========================================================================
 * 定时器
 *===========================================================================*/
static void mm_tick_cb(lv_timer_t *t)
{
    (void)t;
    mm_timer_t *timers[3] = { &g_state.timer1, &g_state.timer2, &g_state.timer3 };
    for (int i = 0; i < 3; i++) {
        mm_timer_t *tm = timers[i];
        if (!tm->running) continue;

        if (tm->mode == MM_TIMER_COUNTUP) {
            tm->seconds++;
        } else {
            tm->seconds--;
            if (tm->seconds <= 0) {
                tm->seconds = 0;
                tm->running = false;
                if (g_ui.t_btn_icon[i])
                    lv_label_set_text(g_ui.t_btn_icon[i], LV_SYMBOL_PLAY);
            }
        }
        lv_obj_t *disp = g_ui.t_disp[i];
        if (disp) {
            lv_obj_t *lbl = lv_obj_get_child(disp, 0);
            if (lbl) {
                char buf[16];
                mm_format_mmss(tm->seconds, buf, sizeof(buf));
                lv_label_set_text(lbl, buf);
            }
        }
    }
}

/*===========================================================================
 * 公开 API
 *===========================================================================*/
mm_state_t *mm_get_state(void) { return &g_state; }

void mm_set_pressure1(int32_t value)
{
    g_state.pressure1 = value;
    update_pressure_label(g_ui.p1_value, value);
}

void mm_set_pressure2(int32_t value)
{
    g_state.pressure2 = value;
    update_pressure_label(g_ui.p2_value, value);
}

void mm_set_vsat(int32_t int_part, int32_t dec_part)
{
    g_state.vsat_int = int_part;
    g_state.vsat_dec = dec_part;
    if (g_ui.vsat_int_lbl) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d.", (int)int_part);
        lv_label_set_text(g_ui.vsat_int_lbl, buf);
    }
    if (g_ui.vsat_dec_lbl) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", (int)dec_part);
        lv_label_set_text(g_ui.vsat_dec_lbl, buf);
    }
}

void mm_set_hct(int32_t value)
{
    g_state.hct = value;
    if (g_ui.hct_value) {
        lv_obj_t *num = lv_obj_get_child(g_ui.hct_value, 0);
        if (num) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", (int)value);
            lv_label_set_text(num, buf);
        }
    }
}

void mm_set_status_pressure1(mm_status_t s)
{
    g_state.status_pressure1 = s;
    mm_draw_status_icon(g_ui.p1_status, s);
}

void mm_set_status_pressure2(mm_status_t s)
{
    g_state.status_pressure2 = s;
    mm_draw_status_icon(g_ui.p2_status, s);
}

void mm_set_status_vsat(mm_status_t s)
{
    g_state.status_vsat = s;
    mm_draw_status_icon(g_ui.vsat_status, s);
}

void mm_set_status_hct(mm_status_t s)
{
    g_state.status_hct = s;
    mm_draw_status_icon(g_ui.hct_status, s);
}

size_t mm_export_json(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;
    int n = snprintf(buf, buf_size,
        "{\"pressure1\":%d,\"pressure2\":%d,"
        "\"vsat\":%d.%d,\"hct\":%d,"
        "\"timer1\":%d,\"timer2\":%d,\"timer3\":%d}",
        (int)g_state.pressure1,
        (int)g_state.pressure2,
        (int)g_state.vsat_int, (int)g_state.vsat_dec,
        (int)g_state.hct,
        (int)g_state.timer1.seconds,
        (int)g_state.timer2.seconds,
        (int)g_state.timer3.seconds);
    return (n < 0) ? 0 : (size_t)n;
}

/*===========================================================================
 * 入口
 *===========================================================================*/
lv_obj_t *mm_create(lv_obj_t *parent)
{
    if (!parent) parent = lv_scr_act();

    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_set_size(scr, SCREEN_W, SCREEN_H);
    lv_obj_center(scr);
    lv_obj_set_style_bg_color(scr, COL_MONITOR_BG, 0);
    lv_obj_set_style_border_width(scr, 4, 0);
    lv_obj_set_style_border_color(scr, COL_PANEL_BORDER, 0);
    lv_obj_set_style_radius(scr, 3, 0);
    lv_obj_set_style_pad_all(scr, GAP, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    g_ui.scr = scr;

    /* 初始化 group */
    if (!g_ui.kb_group) {
        g_ui.kb_group = lv_group_create();
    }

    /* 初始状态 */
    g_state.status_pressure1 = MM_STATUS_NONE;
    g_state.status_pressure2 = MM_STATUS_NONE;
    g_state.status_vsat      = MM_STATUS_NONE;
    g_state.status_hct       = MM_STATUS_WARN;

    g_state.pressure1 = 172;
    g_state.pressure2 = -18;
    g_state.vsat_int  = 72;
    g_state.vsat_dec  = 0;
    g_state.hct       = 15;

    g_state.timer1.seconds = 48 * 60 + 29;
    g_state.timer1.initial = 48 * 60 + 29;
    g_state.timer1.running = false;
    g_state.timer1.mode    = MM_TIMER_COUNTUP;

    g_state.timer2.seconds = 25 * 60 + 29;
    g_state.timer2.initial = 25 * 60 + 29;
    g_state.timer2.running = false;
    g_state.timer2.mode    = MM_TIMER_COUNTDOWN;

    g_state.timer3.seconds = 2 * 60 + 36;
    g_state.timer3.initial = 2 * 60 + 36;
    g_state.timer3.running = false;
    g_state.timer3.mode    = MM_TIMER_COUNTDOWN;

    int px1 = GAP, py1 = GAP;
    int px2 = GAP + PANEL_W + GAP, py2 = GAP;

    create_pressure_panel(scr, px1, py1, PANEL_W, PANEL_H_TOP,
                          "压力 1", 0, &g_ui.p1_status, &g_ui.p1_value);
    create_pressure_panel(scr, px2, py2, PANEL_W, PANEL_H_TOP,
                          "压力 2", 1, &g_ui.p2_status, &g_ui.p2_value);

    create_timer_panel(scr, px1, py1 + PANEL_H_TOP + GAP,
                       PANEL_W, PANEL_H_BOT);
    create_bcare_panel(scr, px2, py2 + PANEL_H_TOP + GAP,
                       PANEL_W, PANEL_H_BOT);

    create_download_btn(scr, px2 + PANEL_W - 36, py2 + 4);

    /* 初始化状态/数值 */
    mm_draw_status_icon(g_ui.p1_status,  g_state.status_pressure1);
    mm_draw_status_icon(g_ui.p2_status,  g_state.status_pressure2);
    mm_draw_status_icon(g_ui.vsat_status, g_state.status_vsat);
    mm_draw_status_icon(g_ui.hct_status, g_state.status_hct);

    mm_set_pressure1(g_state.pressure1);
    mm_set_pressure2(g_state.pressure2);
    mm_set_vsat(g_state.vsat_int, g_state.vsat_dec);
    mm_set_hct(g_state.hct);

    for (int i = 0; i < 3; i++) {
        mm_timer_t *t = (i == 0) ? &g_state.timer1 :
                        (i == 1) ? &g_state.timer2 : &g_state.timer3;
        char buf[16];
        mm_format_mmss(t->seconds, buf, sizeof(buf));
        lv_obj_t *disp = g_ui.t_disp[i];
        if (disp) {
            lv_obj_t *lbl = lv_obj_get_child(disp, 0);
            if (lbl) lv_label_set_text(lbl, buf);
        }
    }

    g_state.tick_timer = lv_timer_create(mm_tick_cb, 200, NULL);

    return scr;
}
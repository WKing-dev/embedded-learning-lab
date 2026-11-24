/**
 * @file app_encoder.c
 * @brief LIC 411 位置采集、滤波和串口输出
 */

#include "app_encoder.h"
#include "lic411.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#define APP_PRINT_PERIOD_MS   100U
#define APP_STATUS_PERIOD_MS  1000U
#define APP_FILTER_SIZE       5U
#define APP_DISPLAY_UNIT_NM   100LL
#define APP_COUNTS_SPLIT      1000000000ULL
#define APP_COMMAND_SIZE      16U

typedef struct
{
    uint64_t samples[APP_FILTER_SIZE];
    uint64_t filtered_counts;
    int64_t position_nm;
    int64_t display_01um;
    uint32_t last_frame_count;
    uint16_t print_timer_ms;
    uint16_t status_timer_ms;
    uint8_t sample_index;
    uint8_t sample_count;
    uint8_t display_valid;
} App_Encoder_Data;

static App_Encoder_Data app_encoder;
static uint8_t app_rx_byte;
static char app_command[APP_COMMAND_SIZE];
static uint8_t app_command_length;
static volatile uint8_t app_command_ready;

static void App_Encoder_Process_Command(void)
{
    uint8_t is_zero_command;

    if (app_command_ready == 0U)
    {
        return;
    }

    is_zero_command = (strcmp(app_command, "ZERO") == 0) ? 1U : 0U;
    app_command_ready = 0U;

    if (is_zero_command != 0U)
    {
        if (Lic411_Set_Current_Position_As_Zero() == 0U)
        {
            printf("OK ZERO\r\n");
        }
        else
        {
            printf("ERROR NO_VALID_POSITION\r\n");
        }
    }
    else
    {
        printf("ERROR UNKNOWN_COMMAND\r\n");
    }
}

static uint64_t App_Encoder_Get_Median(void)
{
    uint64_t sorted[APP_FILTER_SIZE];
    uint8_t i;

    for (i = 0U; i < app_encoder.sample_count; i++)
    {
        sorted[i] = app_encoder.samples[i];
    }

    for (i = 1U; i < app_encoder.sample_count; i++)
    {
        uint64_t value = sorted[i];
        uint8_t index = i;

        while ((index > 0U) && (sorted[index - 1U] > value))
        {
            sorted[index] = sorted[index - 1U];
            index--;
        }
        sorted[index] = value;
    }

    return sorted[app_encoder.sample_count / 2U];
}

static void App_Encoder_Update_Position(void)
{
    int64_t display_center_nm;
    int64_t delta_nm;

    app_encoder.samples[app_encoder.sample_index] = lic411.sample.counts;
    app_encoder.sample_index = (uint8_t)((app_encoder.sample_index + 1U) % APP_FILTER_SIZE);

    if (app_encoder.sample_count < APP_FILTER_SIZE)
    {
        app_encoder.sample_count++;
    }

    app_encoder.filtered_counts = App_Encoder_Get_Median();
    app_encoder.position_nm = (int64_t)app_encoder.filtered_counts -
                              (int64_t)Lic411_Get_Zero_Counts();

    if (app_encoder.display_valid == 0U)
    {
        int64_t rounding = (app_encoder.position_nm >= 0) ?
                           (APP_DISPLAY_UNIT_NM / 2LL) : -(APP_DISPLAY_UNIT_NM / 2LL);

        app_encoder.display_01um = (app_encoder.position_nm + rounding) / APP_DISPLAY_UNIT_NM;
        app_encoder.display_valid = 1U;
        return;
    }

    /* 100 nm 迟滞，避免静止时在相邻显示值之间来回跳动。 */
    display_center_nm = app_encoder.display_01um * APP_DISPLAY_UNIT_NM;
    delta_nm = app_encoder.position_nm - display_center_nm;

    if (delta_nm >= APP_DISPLAY_UNIT_NM)
    {
        app_encoder.display_01um += delta_nm / APP_DISPLAY_UNIT_NM;
    }
    else if (delta_nm <= -APP_DISPLAY_UNIT_NM)
    {
        app_encoder.display_01um -= (-delta_nm) / APP_DISPLAY_UNIT_NM;
    }
}

static void App_Encoder_Print_Position(void)
{
    int64_t display = app_encoder.display_01um;
    uint64_t magnitude = (display < 0) ? (uint64_t)(-display) : (uint64_t)display;
    uint32_t mm = (uint32_t)(magnitude / 10000ULL);
    uint32_t fraction = (uint32_t)(magnitude % 10000ULL);
    uint32_t counts_high = (uint32_t)(app_encoder.filtered_counts / APP_COUNTS_SPLIT);
    uint32_t counts_low = (uint32_t)(app_encoder.filtered_counts % APP_COUNTS_SPLIT);

    printf("pos=%s%lu.%04lu mm counts=%lu%09lu f1=%u f2=%u\r\n",
           (display < 0) ? "-" : "",
           (unsigned long)mm,
           (unsigned long)fraction,
           (unsigned long)counts_high,
           (unsigned long)counts_low,
           (unsigned int)lic411.sample.error_1,
           (unsigned int)lic411.sample.error_2);
}

void App_Encoder_Init(void)
{
    app_encoder = (App_Encoder_Data){0};
    app_command_length = 0U;
    app_command_ready = 0U;
    Lic411_Init();
    (void)HAL_UART_Receive_IT(&huart3, &app_rx_byte, 1U);
    printf("EnDat position acquisition started\r\n");
}

void App_Encoder_Task(void)
{
    App_Encoder_Process_Command();
    Lic411_Task();

    if (lic411.frame_ok_count != app_encoder.last_frame_count)
    {
        app_encoder.last_frame_count = lic411.frame_ok_count;
        App_Encoder_Update_Position();
    }

    if (lic411.frame_ok_count == 0U)
    {
        if (app_encoder.status_timer_ms == 0U)
        {
            app_encoder.status_timer_ms = APP_STATUS_PERIOD_MS;
            printf("Waiting for valid EnDat data: crc=%lu timeout=%lu\r\n",
                   (unsigned long)lic411.crc_error_count,
                   (unsigned long)lic411.timeout_error_count);
        }
        return;
    }

    if ((app_encoder.print_timer_ms == 0U) && (app_encoder.display_valid != 0U))
    {
        app_encoder.print_timer_ms = APP_PRINT_PERIOD_MS;
        App_Encoder_Print_Position();
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART3)
    {
        return;
    }

    if (app_command_ready == 0U)
    {
        if (app_rx_byte == '\n')
        {
            if (app_command_length > 0U)
            {
                app_command[app_command_length] = '\0';
                app_command_ready = 1U;
                app_command_length = 0U;
            }
        }
        else if (app_rx_byte != '\r')
        {
            if (app_command_length < (APP_COMMAND_SIZE - 1U))
            {
                app_command[app_command_length++] = (char)app_rx_byte;
            }
            else
            {
                app_command_length = 0U;
            }
        }
    }

    (void)HAL_UART_Receive_IT(&huart3, &app_rx_byte, 1U);
}

void App_Encoder_Timing_Handle(void)
{
    Lic411_Timing_Handle();

    if (app_encoder.print_timer_ms > 0U)
    {
        app_encoder.print_timer_ms--;
    }

    if (app_encoder.status_timer_ms > 0U)
    {
        app_encoder.status_timer_ms--;
    }
}

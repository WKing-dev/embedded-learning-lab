# STM32H743 RGB888 LCD 测试工程

这是一个基于 **STM32H743BIT6** 的 1024 × 600 LCD 学习与板级测试项目，围绕 LTDC RGB888 显示、外部 SDRAM、DMA2D、FT5426 电容触摸、FreeRTOS 和 LVGL 展开。

本项目保存在 `embedded-learning-lab` 仓库的 **`lcd-888-h743`** 分支。

## 功能概览

- 驱动 1024 × 600 RGB LCD；
- LTDC 使用外部 SDRAM 作为帧缓冲；
- 支持 ARGB8888、RGB888、RGB565 等像素格式；
- 使用 DMA2D 加速清屏、填充和图像搬运；
- 支持基本图形、文本、字体和位图绘制；
- 驱动 W9812G6KH-6 16 MB SDRAM，并提供数据线、地址线和块读写自检；
- 通过 I2C3 驱动 FT5426 电容触摸控制器；
- 触摸中断只做事件通知，任务上下文完成 I2C 读取、标定和滤波；
- 集成 FreeRTOS CMSIS-RTOS2；
- 集成 LVGL 8.3.11，并提供显示与触摸适配；
- 提供颜色、像素、LTDC 时序、刷新率和 SDRAM 上电自检；
- 提供整机满载功耗/热测试模式。

## 当前运行模式

当前源码中 `APP_POWER_TEST_MODE` 被设置为 `1`，上电自检完成后会进入满载功耗测试：

- 背光设置为 100%；
- CPU/FPU 持续运算；
- 多段 SDRAM 持续写入和读取；
- DMA2D 与 CPU 并行工作；
- GPIO 和外设时钟参与负载测试；
- FreeRTOS 和普通 LVGL 界面不会启动。

如需运行正常的 FreeRTOS + LVGL + 触摸界面，请在 `user/application/app.c` 中将：

```c
#define APP_POWER_TEST_MODE (1U)
```

改为：

```c
#define APP_POWER_TEST_MODE (0U)
```

随后可在 `user/application/app_demo.h` 中选择字体页、触摸画板或闪烁测试。功耗测试会持续改写 SDRAM 测试区域，不应与正常 UI 数据混用。

## 硬件与开发环境

| 项目 | 配置 |
| --- | --- |
| MCU | STM32H743BIT6（LQFP208） |
| CPU 主频 | 400 MHz |
| LCD 分辨率 | 1024 × 600 |
| LCD 接口 | LTDC RGB888 |
| LTDC 像素时钟 | 约 34.13 MHz |
| 帧缓冲基址 | `0xC0000000` |
| 外部 SDRAM | W9812G6KH-6，16 MB，16-bit |
| 图形加速 | DMA2D |
| 触摸控制器 | FT5426，I2C3，7-bit 地址 `0x38` |
| 操作系统 | FreeRTOS / CMSIS-RTOS2 |
| GUI | LVGL 8.3.11 |
| 调试串口 | USART3，115200 8N1 |
| 工程工具 | STM32CubeMX、Keil MDK-ARM V5 |
| STM32Cube 固件包 | STM32Cube FW_H7 V1.13.0 |

## 显示时序

| 参数 | 数值 |
| --- | --- |
| Active Width | 1024 |
| Active Height | 600 |
| HSync | 20 |
| HBP | 140 |
| HFP | 160 |
| VSync | 3 |
| VBP | 20 |
| VFP | 12 |

这些参数必须与实际 LCD 面板规格匹配。更换面板时需要同时检查像素时钟、同步极性和前后肩参数。

## 主要控制信号

| STM32 引脚 | 用途 |
| --- | --- |
| PI10 | LCD RESET |
| PH13 | 触摸中断，下降沿触发 |
| PH14 | 触摸复位 |
| I2C3 | FT5426 配置和坐标读取 |
| USART3 | 调试日志输出 |

RGB 数据、同步信号和 SDRAM FMC 引脚由 STM32CubeMX 配置，详细映射请查看 `LCD_888_H743.ioc`。

## 软件结构

```text
Core/                         STM32CubeMX 生成的初始化、FreeRTOS 和中断代码
Drivers/                      STM32 HAL 和 CMSIS 驱动
Middlewares/Third_Party/      FreeRTOS 中间件
MDK-ARM/                      Keil 工程文件
user/application/             启动、自检、任务、界面和功耗测试
user/bsp/bsp_lcd/             LCD 绘图、图层和背光控制
user/bsp/bsp_sdram/           SDRAM 初始化、读写与自检
user/bsp/bsp_touch/           FT5426 触摸驱动
user/bsp/bsp_debug/           调试串口封装
user/bsp/LVGL/                LVGL 8.3.11 源码与演示资源
user/modules/UI/              项目界面代码与资源
doc/                          项目文档
```

正常模式下主要包含三个任务：

- `TouchTask`：等待触摸中断通知，读取并更新最新触摸状态；
- `UiTask`：初始化 LVGL、显示适配和输入适配，并周期调用 `lv_timer_handler()`；
- `HeartbeatTask`：输出运行状态和任务栈余量等调试信息。

## 上电流程

1. 初始化 MPU、CPU Cache、系统时钟和 HAL；
2. 初始化 GPIO、DMA2D、FMC/SDRAM、I2C3、LTDC、TIM2 和 USART3；
3. 打印 SDRAM/FMC 配置并执行 SDRAM 自检；
4. 释放 LCD 复位并启动背光 PWM；
5. 依次显示红、绿、蓝全屏，验证颜色通道；
6. 检查 LTDC 寄存器、CPU 直写像素和刷新率；
7. 复位并识别 FT5426 触摸控制器；
8. 根据 `APP_POWER_TEST_MODE` 进入满载测试或启动 FreeRTOS/LVGL。

## 编译与运行

1. 使用 STM32CubeMX 打开 `LCD_888_H743.ioc`，核对 MCU、时钟、LTDC、FMC 和 I2C3 配置。
2. 使用 Keil MDK-ARM V5 打开 `MDK-ARM/LCD_888_H743.uvprojx`。
3. 根据目标场景设置 `APP_POWER_TEST_MODE` 和 `app_demo.h` 中的演示宏。
4. 编译工程并下载到 STM32H743BIT6。
5. 使用串口工具打开 USART3：115200 波特率、8 数据位、无校验、1 停止位。
6. 复位开发板，观察串口自检信息和 LCD 显示结果。

## 自检与演示

项目提供以下验证手段：

- SDRAM 数据线、地址线和块读写测试；
- RGB 三色全屏测试；
- CPU 直写帧缓冲与像素回读；
- LTDC 时序与刷新率输出；
- FT5426 设备探测、芯片 ID 校验和触摸坐标读取；
- 字体和色条测试页；
- 触摸画板；
- 白底移动黑条的闪烁测试；
- CPU/FPU + SDRAM + DMA2D + 背光满载功耗测试。

## 注意事项

- 当前 SDRAM 刷新计数为 `760`，用于提高功耗的压力测试；正式固件应根据 SDRAM 规格和实际时钟恢复并验证正常刷新值。
- 1024 × 600 ARGB8888 单帧约占 2.34 MiB。修改像素格式、帧缓冲地址或 SDRAM 测试区域时，必须避免地址重叠。
- STM32H7 启用了 Cache 和 MPU；修改帧缓冲、DMA2D 或 SDRAM 内存属性时必须重新验证缓存一致性。
- 满载功耗测试会长时间高负载运行，并将背光设置为 100%。测试时应监测器件温度和供电能力。
- 触摸坐标包含项目特定标定参数；更换面板或触摸模组后需要重新标定。
- LVGL 演示目录包含较大的图片、字体和生成资源，因此仓库体积较大。

## 关键代码

- [`user/application/app.c`](user/application/app.c)：上电自检与运行模式选择；
- [`user/application/app_tasks.c`](user/application/app_tasks.c)：FreeRTOS 触摸、UI 和心跳任务；
- [`user/application/app_power_test.c`](user/application/app_power_test.c)：满载功耗和热测试；
- [`user/application/app_selftest.c`](user/application/app_selftest.c)：SDRAM、LTDC、像素和时序自检；
- [`user/bsp/bsp_lcd/bsp_lcd.c`](user/bsp/bsp_lcd/bsp_lcd.c)：LCD 绘图、图层和背光；
- [`user/bsp/bsp_sdram/bsp_sdram.c`](user/bsp/bsp_sdram/bsp_sdram.c)：SDRAM 初始化和自检；
- [`user/bsp/bsp_touch/bsp_touch.c`](user/bsp/bsp_touch/bsp_touch.c)：FT5426 触摸驱动；
- [`user/bsp/LVGL/`](user/bsp/LVGL/)：LVGL 8.3.11 和适配资源。

## 说明

本仓库用于个人学习、LCD 板级验证和技术交流。项目未提供统一的开源许可证；其中 STM32、FreeRTOS 和 LVGL 等第三方组件仍遵循各自的许可证。

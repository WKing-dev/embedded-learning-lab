# DCDC 电源板 ADC 测试工程

这是一个基于 **STM32H743VIT6** 的 DCDC 电源板学习与测试项目，用于通过 ADC + DMA 采集三路 5 V 输出和两路电流监测信号，同时监测 eFuse 故障、外部看门狗和板载状态灯。

本项目保存在 `embedded-learning-lab` 仓库的 **`dcdc-adc`** 分支。

## 功能概览

- ADC1 以 16 位精度扫描 5 个通道；
- DMA1 Stream 0 以循环模式持续搬运采样数据；
- 监测 `5V1`、`5V2`、`5V3` 三路输出；
- 监测 `IMON1`、`IMON2` 两路电流检测信号；
- 对 IMON 数据执行 8 点滑动平均；
- 读取两路低有效 eFuse 故障信号；
- 对自动重试型故障进行 1 秒计数去重；
- 驱动外部看门狗 WDI，并记录喂狗次数；
- 识别并输出上一次 MCU 复位原因；
- 通过 USART1 周期打印采样和状态信息；
- 提供两路 LED 的开关、翻转和心跳控制接口。

## 硬件与开发环境

| 项目 | 配置 |
| --- | --- |
| MCU | STM32H743VIT6（LQFP100） |
| CPU 主频 | 400 MHz |
| ADC | ADC1，16 位，5 通道扫描 |
| ADC 有效时钟 | 36 MHz |
| ADC 采样时间 | 387.5 cycles |
| DMA | DMA1 Stream 0，循环模式 |
| 调试串口 | USART1，115200 8N1 |
| 工程工具 | STM32CubeMX、Keil MDK-ARM V5 |
| STM32Cube 固件包 | STM32Cube FW_H7 V1.13.0 |

## 信号与引脚

### ADC 通道

| Rank | STM32 引脚 | ADC 通道 | 用途 |
| --- | --- | --- | --- |
| 1 | PC4 | ADC1_INP4 | IMON1，输出 1 电流监测 |
| 2 | PC5 | ADC1_INP8 | IMON2，输出 2 电流监测 |
| 3 | PC1 | ADC1_INP11 | 5V3 电压监测 |
| 4 | PA2 | ADC1_INP14 | 5V2 电压监测 |
| 5 | PA1 | ADC1_INP17 | 5V1 电压监测 |

### GPIO

| STM32 引脚 | 用途 |
| --- | --- |
| PE4 | LED1 |
| PE2 | LED2 |
| PA3 | 外部看门狗 WDI |
| PD12 | eFuse FLT1，低有效 |
| PD13 | eFuse FLT2，低有效 |
| PA9 / PA10 | USART1 TX / RX |

## 软件结构

```text
Core/                         STM32CubeMX 生成的初始化与中断代码
Drivers/                      STM32 HAL 和 CMSIS 驱动
MDK-ARM/                      Keil 工程文件
user/application/app_power/   电源监测状态机、滤波和周期打印
user/bsp/bsp_adc/             ADC 校准、DMA 采集与量程换算
user/bsp/bsp_power/           eFuse 故障输入
user/bsp/bsp_wdg/             外部看门狗和复位原因
user/bsp/bsp_led/             LED 与心跳控制
docs/                         编码规范和开发工作流文档
```

业务层只负责状态机与数据输出，ADC 和 GPIO 细节集中在 BSP 层，便于后续校准、扩展和复用。

## 工作流程

1. 系统完成时钟、MPU、GPIO、DMA、ADC1 和 USART1 初始化。
2. `BSP_LED_Init()` 和 `BSP_WDG_Init()` 初始化指示灯与外部看门狗。
3. `App_Power_Init()` 完成 ADC 校准并启动循环 DMA。
4. 主循环持续调用 `BSP_WDG_Task()` 和 `App_Power_Task()`。
5. SysTick 每 1 ms 调用各模块的计时处理函数。
6. 应用每 100 ms 更新一次采样和故障状态，每 1 秒输出一次信息。

## 编译与运行

1. 使用 STM32CubeMX 打开 `DCDC_ADC.ioc`，检查芯片、时钟、ADC 和 DMA 配置。
2. 使用 Keil MDK-ARM V5 打开 `MDK-ARM/DCDC_ADC.uvprojx`。
3. 编译工程并下载到 STM32H743VIT6。
4. 连接 DCDC 测试板，并确认 ADC 输入未超过 3.3 V。
5. 使用串口工具打开 USART1：115200 波特率、8 数据位、无校验、1 停止位。
6. 复位开发板并观察初始化、采样、故障和看门狗输出。

初始化输出示例：

```text
ADC init OK, 16bit, 5 channels, 36MHz
last reset: POWER-ON
```

周期输出包含：

```text
5V1=... 5V2=... 5V3=...
IMON1=...mA(...uV) IMON2=...mA(...uV) FLT1=0 FLT2=0 fcnt=0/0
WDG feed=... WDI=... up=...s
```

## 关键参数

| 参数 | 当前值 |
| --- | --- |
| 应用采样周期 | 100 ms |
| 串口打印周期 | 1 s |
| IMON 平均窗口 | 8 点（约 800 ms） |
| 故障计数去重窗口 | 1 s |
| IMON 采样电阻 | 13 kΩ |
| ADC 饱和判断阈值 | 65500 |

## 当前注意事项

- 板上三路 5 V 采样分压电阻 `R22` 当前按实测为 200 kΩ，5 V 输入可能使 ADC 长期削顶，因此程序暂时输出三路电压通道的原始 ADC 码值。
- 当硬件将 `R22` 调整为设计目标值后，应重新核对 `bsp_adc.h` 中的分压换算系数，并恢复电压值输出。
- IMON 电流换算依赖 13 kΩ 采样电阻和器件典型参数，正式使用前应通过标准负载进行校准。
- STM32H7 带有数据缓存，修改 DMA 缓冲区或 MPU 配置时必须重新验证缓存一致性。
- 本项目用于板级学习和测试，不能替代量产环境中的完整保护与校准流程。

## 关键代码

- [`user/application/app_power/app_power.c`](user/application/app_power/app_power.c)：状态机、滑动平均、故障计数和打印；
- [`user/bsp/bsp_adc/bsp_adc.c`](user/bsp/bsp_adc/bsp_adc.c)：ADC/DMA 初始化、原始值读取和量程换算；
- [`user/bsp/bsp_power/bsp_power.c`](user/bsp/bsp_power/bsp_power.c)：FLT 输入读取；
- [`user/bsp/bsp_wdg/bsp_wdg.c`](user/bsp/bsp_wdg/bsp_wdg.c)：外部看门狗与复位原因；
- [`user/bsp/bsp_led/bsp_led.c`](user/bsp/bsp_led/bsp_led.c)：LED 控制与心跳逻辑。

## 说明

本仓库用于个人学习、板级调试和技术交流。项目未提供开源许可证；未经许可，不代表可以将代码直接用于商业产品。

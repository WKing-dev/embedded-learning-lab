# EnDat 2.2 编码器学习工程

这是一个基于 **STM32F405RGT6** 的 EnDat 2.2 学习与验证项目，用于读取 HEIDENHAIN LIC 411 C1 直线光栅尺的位置数据。

项目重点不只是“读出一个数值”，还包括 EnDat 物理层时序、SPI + DMA 非阻塞收发、5 位 CRC 校验、帧同步、位置滤波，以及协议层的可移植设计。

## 项目状态

目前已经实现：

- EnDat 2.2 模式指令发送与位置帧接收；
- SPI1 + DMA2 非阻塞通信；
- DATA 收发方向控制与 EnDat 时序处理；
- 5 位 CRC 校验和帧头搜索；
- F1、F2 状态位解析；
- 5 点中值滤波与位置格式化输出；
- 通过串口命令将当前位置设为软件零点；
- 协议层与 STM32 HAL 解耦，便于移植到其他 MCU。

当前仍属于学习和验证阶段。实测位置数据存在约 80,000 counts（80 μm）的周期性回绕，绝对位置高位和参数区读取方式仍需进一步确认。详细过程见 [`docs/EnDat2.2_开发总结.md`](docs/EnDat2.2_开发总结.md)。

## 硬件与开发环境

| 项目 | 配置 |
| --- | --- |
| MCU | STM32F405RGT6（LQFP64） |
| 编码器 | HEIDENHAIN LIC 411 C1 |
| 协议 | EnDat 2.2 |
| 主频 | 168 MHz |
| EnDat 时钟 | 1.3125 MHz |
| SPI | SPI1，主机模式，Mode 3，LSB First |
| 调试串口 | USART3，115200 8N1 |
| 工程工具 | STM32CubeMX、Keil MDK-ARM V5 |
| STM32Cube 固件包 | STM32Cube FW_F4 V1.28.3 |

## 主要接口

| STM32 引脚 | 功能 |
| --- | --- |
| PA5 | EnDat CLOCK / SPI1 SCK |
| PA6 | EnDat DATA / SPI1 MISO |
| PA7 | EnDat DATA / SPI1 MOSI |
| PA4 | DATA 收发方向控制（DE、/RE） |
| PB10 | USART3 TX |
| PB11 | USART3 RX |
| PD2 | 状态 LED |

EnDat 的 CLOCK 和 DATA 均通过差分收发器连接编码器。接线、终端电阻和时序说明请参考开发总结，接线前务必核对实际硬件原理图。

## 软件结构

```text
Core/                         STM32CubeMX 生成的初始化与中断代码
Drivers/                      STM32 HAL 和 CMSIS 驱动
MDK-ARM/                      Keil 工程文件
user/application/             应用层、位置显示与串口命令
user/bsp/bsp_endat/           STM32F4 EnDat 物理层实现
user/modules/Lic411/          可移植的 LIC 411 / EnDat 协议层
docs/                         开发总结、编码规范与工作流文档
tools/                        辅助脚本
```

协议层位于 `user/modules/Lic411/`。移植到其他平台时，主要需要实现 `lic411_port.h` 中定义的硬件抽象接口。

## 编译和运行

1. 使用 STM32CubeMX 打开 `EnDat.ioc`，检查芯片、时钟和外设配置。
2. 使用 Keil MDK-ARM V5 打开 `MDK-ARM/EnDat.uvprojx`。
3. 编译工程并下载到 STM32F405RGT6。
4. 按照硬件定义连接 EnDat 编码器。
5. 使用串口工具打开 USART3（115200、8 数据位、无校验、1 停止位）。
6. 复位开发板，观察位置和状态输出。

正常输出示例：

```text
EnDat position acquisition started
pos=0.1234 mm counts=000000000123456 f1=0 f2=0
```

串口发送以下命令可将当前位置设为软件零点：

```text
ZERO
```

成功后设备返回 `OK ZERO`；尚未获得有效位置时返回 `ERROR NO_VALID_POSITION`。

## 关键参数

- 位置采样周期：2 ms；
- 串口位置输出周期：100 ms；
- 单次事务超时：20 ms；
- 位置滤波：5 点中值滤波；
- 默认测量步距：1 nm/count；
- 启动后先发送错误复位指令，再进入位置读取。

其中机械标定零点、测量步距、帧布局等参数与具体编码器和安装状态有关，使用其他设备时应重新验证，不应直接照搬。

## 进一步阅读

- [`docs/EnDat2.2_开发总结.md`](docs/EnDat2.2_开发总结.md)：通信时序、帧结构、CRC、调试过程和遗留问题；
- [`user/modules/Lic411/lic411_port.h`](user/modules/Lic411/lic411_port.h)：协议层移植接口与时序约定；
- [`user/bsp/bsp_endat/bsp_endat.c`](user/bsp/bsp_endat/bsp_endat.c)：SPI、DMA 和方向控制实现；
- [`user/application/app_encoder.c`](user/application/app_encoder.c)：应用任务、滤波、输出和串口命令。

## 说明

本仓库用于个人学习、协议验证和技术交流。项目未提供开源许可证；未经许可，不代表可以将代码直接用于商业产品。

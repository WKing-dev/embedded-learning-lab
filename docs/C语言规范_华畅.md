# 草稿

1.c代码格式可通过vscode格式化来解决。部分注释也可以。

2.规范重点在变量，结构体，指针的命名，还有文件的命名

3.然后就是对代码的复杂程度和框架进行规范，比如让函数只负责一个简单的内容等

4.还有就是程序的测试和log进行规范。

5.其实完全可以做一个模板，新人完全可以照着模板来写代码，规范可以作为一个模板教程来用



# 目录

1. 排版
2. 注释
3. 命名规范
4. 可读性
5. 模块化和框架

6. 测试规范
7. 代码编辑协同以及审查



# 1.排版

排版可使用vscode内部的格式化功能，

# 2.注释

## 2.1.文件头注释

.c文件头注释

```c

/**
 * @file Test.c
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2025-12-01
 *
 * @copyright Copyright (c) 2025
 *
 */
```

.h文件头注释

```c

/**
 * @file Test.h
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2025-12-01
 *
 * @copyright Copyright (c) 2025
 *
 */
```



## 2.2.函数注释

```c
/**
 * @brief  计算数组中所有整数的和  
 * 
 * 这个函数遍历给定的整型数组，将所有元素累加，并返回累加结果。  
 * 如果数组长度为 0，则返回 0。  
 *
 * @note  调用方保证传入的 `arr` 指针有效且 `len` >= 0。
 * @param[in]  arr     指向整型数组的指针；不能为空。  
 * @param[in]  len     数组元素的数量 (长度)。  
 * @return            计算得到的整数和 (long 型)，可能为负数 / 零 / 正数。  
 * 
 */
long Sum_Int_Array(const int *arr, size_t len)
{
    long sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += arr[i];
    }
    return sum;
}

```



# 3.命名规范

## 3.1.变量/常量命名规范

### 3.1.1.常规变量常量命名规范

变量命名采用 UNIX 风格：全小写加下划线。变量名应优先表达业务含义，不再强制通过 `g_`、`s_`、`c_` 等前缀表示作用域或常量属性。

推荐规则：

- 局部变量、函数参数、文件内静态变量、全局变量统一使用小写下划线命名。
- 作用域通过 `static`、头文件声明和模块边界控制，不通过变量名前缀表达。
- 对外可见的全局变量应尽量少用；确实需要时，名称应体现模块或业务含义，避免不同模块之间重名。
- 涉及物理量或时间的变量，优先在名称末尾标明单位，例如 `speed_rpm`、`pressure_kpa`、`timeout_ms`。
- 常量优先使用 `static const` 或宏定义；`static const` 使用小写下划线，宏定义使用全大写下划线。

```c
/* 文件内静态变量 */
static uint32_t pump_error_count = 0U;

/* 对外可见变量，名称体现模块和含义 */
Pump_Runtime pump_runtime;

/* 常量 */
static const uint32_t motor_start_timeout_ms = 100U;

/* 局部变量 */
uint32_t target_speed_rpm = 3000U;
```

### 3.1.2.指针变量命名规范

指针变量不强制添加 `p_` 前缀。指针属性由类型表达，变量名应表达被指向对象的含义。

```c
/* 指向接收缓冲区的指针 */
uint8_t *rx_buffer = NULL;

/* 指向电机配置的指针 */
Motor_Config *motor_config = NULL;
```

### 3.1.3结构体命名规范

结构体类型为首字母大写，结构体变量依旧参照 UNIX 风格使用小写下划线命名，tag 后面加 `_s` 表示结构体。

```c
/* 结构体类型 */
typedef struct Can_Message_Struct_s
{
    uint8_t buf[128];
    uint16_t len;
} Can_Message_Struct;

/* 普通结构体变量 */
Can_Message_Struct motor_message;

/* 文件内静态结构体变量 */
static Can_Message_Struct motor_message_cache;

/* 结构体常量 */
const Can_Message_Struct default_motor_message;

/* 指针结构体变量 */
Can_Message_Struct *current_motor_message;

```

### 3.1.4 .共用体命名规范

共用体一般用于不同模块或者板于板之间的通讯，将数组转换成结构体数据。

所以一定要添加结构体按1字节对齐的编译标识，不然字节对齐会出错。

```c
#pragma pack(1) // 指定结构体按照1字节对齐
typedef union __CAN_Comm_RX_UP_Board_Union
{
    struct {
        int16_t input_voltage;
        int16_t cap_voltage;
        int16_t input_current;
        int16_t target_power;
    } data;
    uint8_t buffer[8];

} CAN_Comm_RX_UP_Board_Union;
#pragma pack() // 取消结构体对齐
CAN_Comm_RX_UP_Board_Union can_comm_rx_up_board;

```

### 3.1.5 .枚举体命名规范

枚举体一般用于给对象或者一些设备的ID进行编号，作用类似于宏定义

```c

typedef enum __DJI_Motor_Group_enum
{
    DJI_MGRP1, // 底盘电机组
    DJI_MGRP2, // 云台电机组
    DJI_MGRP3, // 发射器电机组
    DJI_MGRP_SUM
} DJI_Motor_Group_enum;

```





## 3.2.函数命名规范

函数命名以首字母大写为主

```c
int Add_Function_Init(int para)
{
    para+=1;
    return para;
}

```



## 3.3.宏定义命名规范

宏定义命名一律使用全大写字母和下划线

```c
#define PI 3.14159265359f
#define PIX2 6.2831852f
#define RPM_TO_RADIAN 0.10471975511965977f
```



## 3.4.文件命名规范

文件命名则以全小写为主例:

pid.c,pid.h。

# 4.可读性

## 4.1.运算符尽可能用括号括起来，尽可能不要使用默认优先级。



```c
//正例：
int result = (a + b) * (c - d);
bool valid = ((x > 0) && (y < 100));
float value = (speed / 60.0f) * time;
//反例：
int result = a + b * c - d;        // 优先级不明显
bool valid = x > 0 && y < 100;     // 逻辑表达式可读性差
float value = speed / 60.0f * time; // 阅读成本高

```



## 4.2.不要使用魔法数字，尽量使用宏定义或者枚举来代替这个数字。



```c
//正例：
//宏定义
#define MAX_SPEED        5000
#define TIMEOUT_MS       30

if (speed > MAX_SPEED)
{
    speed = MAX_SPEED;
}

//枚举
typedef enum __MotorMode_enum
{
    MODE_IDLE  = 0,
    MODE_RUN   = 1,
    MODE_ERROR = 2
} MotorMode_enum;

SetMotorMode(MODE_RUN);

//反例：

if (speed > 5000)     // 魔法数字
{
    speed = 5000;
}

if (mode == 2)        // 没有语义
{
    HandleError();
}


```



## 4.3.不要使用难懂的技巧性很高的语句，除非很有必要时。



```c
//正例：
if (flag == true)
{
    count = count + 1;
}

if (p != NULL)
{
    DoSomething(p);
}
//反例：
//依赖短路特性做赋值
flag && (count++);   // 可读性非常差

//使用逗号表达式
x = (y = 5, y + 3);  // 让人摸不着头脑

// 用指针自增做逻辑
while (*p++) { }     // 不清楚谁在 ++

// 在条件里做复杂运算
if ((x = GetValue()) > 0 && (++y < 10))
{
    ...
}
```



# 5.模块化和框架

## 5.1.代码分层

应用层主要负责业务代码，只调用驱动层和中间层不考虑单片机底层的硬件。

系统层主要负责任务的调度，一些调试系统和计时系统。

模块层主要时一些模块的驱动，向下调用BSP层，包含一些算法库等。

BSP层主要是在厂商给的库之间增加一层封装，方便以后更换芯片和厂商的时候移植当前框架。

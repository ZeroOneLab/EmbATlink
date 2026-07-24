# EmbATlink

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

一款轻量级、分层解耦的嵌入式 AT 指令驱动框架，专为资源受限的 MCU 设计。

**EmbATlink 是什么？** 它是一个 AT 指令收发引擎，帮你把"串口收发 AT 指令、匹配响应、检测 URC 事件"这些重复性工作封装好。你只需在应用层拼接 AT 命令字符串、注册感兴趣的关键字，剩下的收发、超时、重试、URC 扫描全部由框架处理。

**用在哪里？** 任何需要通过串口发送 AT 指令与无线模组（WiFi / 蓝牙 / 4G / NB-IoT 等）通信的嵌入式项目。裸机和 RTOS 均可使用。

**怎么移植？** 核心驱动层 (`at_driver`) 与硬件完全解耦，移植时只需适配 `at_port.c` / `at_port.h` 中的 2 个宏和 6 个端口函数，无需修改驱动层一行代码。

## 目录

- [核心特性](#核心特性)
- [资源占用](#资源占用)
- [文件结构](#文件结构)
- [系统架构](#系统架构)
- [快速开始](#快速开始)
- [Demo 工程说明](#demo-工程说明)
- [移植指南](#移植指南)
- [API 速查](#api-速查)
- [更新日志](#更新日志)

## 核心特性

- **分层解耦** — 驱动层与硬件端口层完全分离。换 MCU、换模组，只改 `at_port.c`，驱动逻辑零改动
- **配置化通道** — 通过 `at_channel_t` 结构体注册缓冲区与 URC 关键字表，按需分配内存，支持多通道
- **指令调用方拼接** — 框架不参与 AT 命令格式化，调用方自行拼接完整命令行，最大程度灵活，支持 compound literal 一行搞定
- **URC 轮询检测** — 注册关键字后驱动自动扫描匹配，应用层通过 `at_urc_check()` 轮询即可获取事件，无需中断回调
- **接收缓冲区动态切换** — `at_recv_buf_swap()` 支持 OTA 等大数据场景临时扩容，用完归还，零额外内存开销
- **会话锁保护** — `at_session_lock()` 递归锁保证多步指令事务在 RTOS 环境下的原子性，防止任务切换导致数据串扰
- **URC 局部消费** — `at_recv_remove()` 支持按偏移量移除指定数据，多条 URC 混在一个缓冲区中时可各自独立消费，互不干扰
- **轻量无依赖** — 纯 C 实现，静态内存分配，无动态申请，驱动仅 4 个核心文件

### 资源占用

| 组件 | Flash | RAM | 说明 |
|------|-------|-----|------|
| 驱动核心 (`at_driver` + `at_port`) | **~1.3KB** | **16 bytes** | 通道数组，不含用户接收缓冲区 |
| Demo 应用层 (`main.c`) | ~1.7KB | 268 bytes | 含 256 bytes 接收缓冲区 |
| STM32 标准外设库 + C 库 + 启动 | ~5.6KB | 1,068 bytes | 含 1KB 系统栈 |
| **Demo 工程总计** | **~8.6KB** | **~1.3KB** | |

> 基于 STM32F103C8、ARM Compiler 5、MicroLIB、-O3 优化。接收缓冲区由用户按需定义，`AT_CHANNEL_MAX` 决定通道数组大小。

## 文件结构

```
EmbATlink/
├── at_driver.c      # 核心驱动层：AT 指令收发、响应匹配、URC 扫描、缓冲区管理
├── at_driver.h      # 核心驱动头文件：所有对外 API 声明与数据结构定义
├── at_port.c        # 硬件端口层：串口收发、延时、Tick、临界区（需用户适配）
└── at_port.h        # 硬件端口层头文件：宏定义、端口函数声明
```

## 系统架构

```
┌──────────────────────────────────────────────────┐
│  应用层  (main.c — 业务逻辑 / AT 指令调度)         │
│  · 发送 AT 指令 & 处理响应                        │
│  · 轮询 URC 数据 & 分发事件                       │
├──────────────────────────────────────────────────┤
│  核心驱动层  (at_driver.c/h)                      │
│  · AT 指令发送 & 响应匹配                          │
│  · URC 关键字扫描                                 │
│  · 接收缓冲区管理 / swap                          │
│  · 会话锁 (递归互斥)                               │
├──────────────────────────────────────────────────┤
│  硬件端口层  (at_port.c/h — 唯一需要适配的部分)     │
│  · 串口发送 / 接收中断对接                         │
│  · 延时 / Tick (SysTick 中断自增)                 │
│  · 临界区 lock / unlock                           │
└──────────────────────────────────────────────────┘
```

## 快速开始

### 1. 通道注册

根据实际使用场景分配接收缓冲区大小，通过 `at_channel_t` 注册到驱动：

```c
#include "at_driver.h"

/* URC 关键字表 — 注册你关心的关键字，框架自动扫描匹配 */
enum {
    AT_URC_RECV = 0,     /* +RECV: 数据到达通知     */
    AT_URC_STAT,         /* +STAT: 状态变化通知     */
    AT_URC_LAST,
};

static const char *at_urc_keys[AT_URC_LAST] = {
    [AT_URC_RECV] = "+RECV:",
    [AT_URC_STAT] = "+STAT:",
};

/* 通道注册 */
uint8_t recv_buf[256];

at_channel_t at_cfg = {
    .recv_buf  = recv_buf,
    .recv_size = sizeof(recv_buf),
    .urc_keys  = at_urc_keys,
    .urc_count = ARRAY_SIZE(at_urc_keys),
};

at_channel_init(0, &at_cfg);
```

> **说明**：`urc_keys` 中的关键字只要出现在接收数据中就会被扫描命中。上面的 `+RECV:`、`+STAT:` 只是示例，你可以换成任意模组的 URC 关键字，如 `+IPD`、`+MQTT`、`+CONNECT` 等。检测 URC 只需调用 `at_urc_check()`，传入通道号和枚举下标即可：
>
> ```c
> if (at_urc_check(channel, AT_URC_RECV) == AT_OK) {
>     /* 命中 +RECV: 关键字，执行相应处理 */
>     printf("[URC] Data received\r\n");
> }
> ```
>
> 以上示例中 `channel = 0`，即与 `at_channel_init()` 传入的通道号一致。

### 2. 发送 AT 指令

驱动采用 **调用方拼接完整命令** 的设计，`at_cmd_config_t` 支持 compound literal 写法，一行完成配置：

#### 固定指令（发送和预期响应均不变）

```c
/* 发送 "AT"，期望响应 "OK"，重试 20 次，轮询间隔 20ms，超时 500ms */
at_cmd_exec(channel, NULL, &(at_cmd_config_t){"AT", "OK", 20, 20, 500});
```

#### 发送指令动态变化

当指令参数需要运行时确定时，使用 `snprintf` 拼接：

```c
char cmd[32];

snprintf(cmd, sizeof(cmd), "ATE%d", 0);  /* 拼接后为 "ATE0" — 关闭回显 */
at_cmd_exec(channel, NULL, &(at_cmd_config_t){cmd, "OK", 20, 20, 500});

snprintf(cmd, sizeof(cmd), "ATE%d", 1);  /* 拼接后为 "ATE1" — 开启回显 */
at_cmd_exec(channel, NULL, &(at_cmd_config_t){cmd, "OK", 20, 20, 500});
```

#### 预期响应动态变化

同理，若期望匹配的响应关键字也需要动态构造，对 `expect` 参数使用相同方式：

```c
char cmd[32], expect[32];

snprintf(cmd,    sizeof(cmd),    "AT+MODE=%d", mode);  /* 拼接后为 "AT+MODE=1" */
snprintf(expect, sizeof(expect), "+MODE:%d",  mode);  /* 拼接后为 "+MODE:1"   */
at_cmd_exec(channel, NULL, &(at_cmd_config_t){cmd, expect, 3, 20, 1000});
```

#### 响应参数提取

AT 指令的响应中通常携带状态值和数据。`at_resp_param_get()` 用于从响应字符串中提取第 N 个逗号分隔的参数：

```c
/* 获取接收缓冲区 */
uint8_t *resp;
uint16_t len;
at_recv_get(channel, &resp, &len);

/*
 * 示例：发送 "AT+GETMODE" 查询当前模式
 * 响应: "+MODE:0,1,2\r\n"
 *
 * 提取 index=0 得到 "0"，index=1 得到 "1"，index=2 得到 "2"
 */
char param[8];
int  modes[3];

for (int i = 0; i < 3; i++) {
    at_resp_param_get((const char *)resp, len, i, param, sizeof(param));
    modes[i] = atoi(param);  /* 转为整数存入数组 */
}
printf("Modes: %d, %d, %d\r\n", modes[0], modes[1], modes[2]);
```

对于携带 JSON 数据的响应，也可通过指定 index 直接提取 JSON 字符串（双引号内的逗号不会被误分割）：

```c
/*
 * 示例响应: "+DATA:0,5,{\"status\":\"online\"}\r\n"
 * 提取 index=2 获取 JSON 字符串
 */
char json[128];
at_resp_param_get((const char *)resp, len, 2, json, sizeof(json));
printf("Data: %s\r\n", json);   /* 打印 JSON 字符串，例如 {"status":"online"} */
```

### 3. 串口接收对接

通过 `at_recv_push()` 将串口接收到的数据注入驱动缓冲区。支持**中断接收、DMA 接收、主循环轮询**三种模式。

#### 中断逐字节接收

在串口接收中断服务函数中逐字节推入驱动：

```c
/*
 * usart_rx_isr() — 串口接收中断服务函数
 * 参数：usart — 串口外设指针，如 USART1
 * 每收到一个字节触发一次，调用 at_recv_push() 推入驱动缓冲区
 */
void usart_rx_isr(usart_t *usart)
{
    if (usart == USART1) {
        uint8_t byte = usart_receive_byte(usart);  /* 从数据寄存器读取一个字节 */
        at_recv_push(0, &byte, 1);                 /* 推入通道 0 的接收缓冲区   */
    }
}
```

#### DMA 批量接收

使用 DMA 接收时，在 DMA 完成中断中批量推入数据。需额外开辟一块 DMA 专用缓冲区：

```c
uint8_t dma_buf[256];   /* DMA 专用接收缓冲区，DMA 硬件直接写入此区域 */

/*
 * dma_rx_complete_isr() — DMA 接收完成中断
 * 参数：dma_ch — DMA 通道句柄
 * 当 DMA 收到指定长度数据或空闲超时时触发，一次性推入全部已接收数据
 */
void dma_rx_complete_isr(dma_channel_t *dma_ch)
{
    uint16_t recv_len = dma_get_recv_count(dma_ch); /* 获取实际接收字节数 */
    at_recv_push(0, dma_buf, recv_len);             /* 批量推入通道 0    */
}
```

#### 主循环轮询接收

不依赖中断，在主循环中轮询 UART 状态寄存器收数据，适合裸机场景：

```c
/* 主循环中轮询 UART */
while (1) {
    if (usart_rx_ready(USART1)) {               /* 检查接收寄存器是否有数据 */
        uint8_t byte = usart_receive_byte(USART1);
        at_recv_push(0, &byte, 1);
    }
    /* ... 其他业务逻辑 ... */
}
```

### 4. URC 事件检测

URC（Unsolicited Result Code）采用 **轮询检测** 机制。注册关键字后，驱动在每次接收数据时自动扫描匹配，用户通过 `at_urc_check()` 查询是否命中：

```c
/* 检测是否收到状态变更 URC */
if (at_urc_check(channel, AT_URC_STAT) == AT_OK) {
    /* 命中 +STAT: 关键字，执行相应处理 */
    printf("[URC] Status changed\r\n");
    at_recv_reset(channel);   /* 清空缓冲区，准备下一次接收 */
}
```

> **说明**：`at_urc_check()` 是只读操作，不会修改缓冲区状态。

处理 URC 后，有两种方式清理缓冲区：

- **全部清空**（`at_recv_reset`）：适合接收完一条完整的 URC 消息后整体重置
- **局部移除**（`at_recv_remove`）：当缓冲区中混合了多条数据，只想消费自己关心的部分

#### 局部移除的使用场景

假设你的设备同时订阅了多个主题，缓冲区中混合了不同来源的数据（AAA、BBB、CCC 分别代表三类数据块）：

```
缓冲区内容: [AAA]      [BBB]        [CCC]
              ↑         ↑             ↑
          主题1数据  网络状态URC  主题2数据
```

- **如果使用全部清空**：一次 `at_recv_reset()` 后 BBB、CCC 都没了。另一个模块需要检测网络状态变化时已经找不到 BBB 了。要么提前拷贝数据（浪费内存），要么不清除数据（下次循环又会重复进入判断）。
- **如果使用局部移除**：你只消费自己关心的数据。例如模块 A 只处理主题 URC（AAA 和 CCC），模块 B 只处理网络状态 URC（BBB），各自用 `at_recv_remove()` 移除自己的部分，互不影响。

```c
/*
 * 场景：模块 A 负责处理主题数据，只关心 AAA 和 CCC
 * 检测到 +RECV: 后，定位并移除对应的数据块
 */
if (at_urc_check(channel, AT_URC_RECV) == AT_OK) {
    uint8_t *buf;
    uint16_t len;
    at_recv_get(channel, &buf, &len);

    /*
     * 用户需要自行计算数据的位置和长度。
     * 例如已知 AAA 在缓冲区偏移 0 处，长度 15，消费后移除：
     */
    at_recv_remove(channel, 0, 15);
    /* 移除后，AAA 后面的全部数据（BBB + CCC）整体前移填补 AAA 的空洞 */
}

/* 之后模块 B 检测网络状态 URC 时，BBB 数据仍然在缓冲区中，不受影响 */
if (at_urc_check(channel, AT_URC_STAT) == AT_OK) {
    /* 处理网络状态变更... */
    at_recv_remove(channel, offset_of_bbb, length_of_bbb);
}
```

> **注意**：使用 `at_recv_remove()` 需要用户自行计算 `offset` 和 `bytes`，建议结合 `strstr()` 等函数定位关键字位置后再确定移除范围。

### 5. 接收缓冲区动态切换

`at_recv_buf_swap()` 用于临时替换接收缓冲区，适用于 OTA 等需要大容量接收的场景。该函数**对称调用**——第一次切换到大缓冲区，第二次切换回原缓冲区：

```c
/* 正常使用：256 字节缓冲区 */
uint8_t normal_buf[256];

/* OTA 场景：动态申请 10KB 缓冲区 */
uint8_t *ota_buf = malloc(10240);
uint16_t ota_size = 10240;
uint16_t ota_len = 0;

/* 切换到 OTA 大缓冲区（同时保存原缓冲区信息） */
at_recv_buf_swap(channel, &ota_buf, &ota_size, &ota_len);

/* ... 执行 OTA 数据接收 ... */

/* OTA 完成，切换回原缓冲区 */
at_recv_buf_swap(channel, &ota_buf, &ota_size, &ota_len);

/*
 * free 之前确保 OTA 数据已处理完毕（如写入 Flash、校验、转换等）。
 * swap-back 后 ota_buf 指向原 normal_buf，不可继续当作大缓冲区使用。
 */
free(ota_buf);   /* 此时 ota_buf 指向原 normal_buf，注意不要 free 错误 */
```

> **注意**：swap 后传入的指针会**交换为旧缓冲区的指针和大小**，再次调用即可恢复。调用者需保证交换期间新旧缓冲区均有效。

### 6. 会话锁（RTOS 多步事务保护）

当多条 AT 指令构成一个完整事务时（如先进入透传模式，再发送数据），需要使用 `at_session_lock()` / `at_session_unlock()` 保护，防止 RTOS 任务切换导致其他任务的 AT 指令被模组误当作透传数据：

```c
/* 多步事务：进入透传 + 发送数据 */
at_session_lock(channel);

at_cmd_exec(channel, NULL, &(at_cmd_config_t){"AT+QIOPEN", "CONNECT", 3, 20, 5000});
/* 进入透传模式后，后续数据直接发送 */
at_cmd_exec(channel, NULL, &(at_cmd_config_t){payload_data, NULL, 1, 20, 1000});

at_session_unlock(channel);
```

> `at_session_lock()` 是**递归锁**，同一任务可嵌套加锁。其他任务在锁未释放时会被阻塞，直到持有锁的任务执行完 `at_session_unlock()` 后才可获取锁。裸机环境下无任务切换，通常无需使用。

## Demo 工程说明

Demo 工程位于 `demo/stm32f103c8/`，通过 **USART1（串口 1，PA9-TX / PA10-RX）** 与 PC 端串口助手通信，**MCU 作为主机发送 AT 指令，串口助手模拟从机模组回复响应**。无需真实无线模组即可验证驱动逻辑。

### 平台时基设计

Demo 采用以下时基方案：

| 功能 | 实现方式 | 说明 |
|------|---------|------|
| 系统 Tick (`at_port_get_tick_ms`) | SysTick 中断自增 | SysTick 每 1ms 触发一次中断，全局变量自增，供驱动超时判断 |
| 延时 (`at_port_delay_ms`) | 轮询 Tick 值 | 记录起始 tick，while 循环等待 tick 达到目标值 |

> **为什么用 SysTick while 轮询而不是 DWT？** DWT（Data Watchpoint and Trace）是 Cortex-M3/M4/M7 特有的调试单元，Cortex-M0/M0+ 和 RISC-V 等平台没有 DWT。SysTick while 轮询仅依赖一个通用定时中断，任何 MCU 都能实现，移植性最好。对于 demo 场景完全够用。如果你的平台有 DWT 且需要更高精度的短延时，将 `at_port_delay_ms` 改为 DWT 实现即可，不影响驱动层。

实现示例：

```c
/* SysTick 中断服务函数中递增全局 tick */
static volatile uint32_t sys_tick_ms = 0;

void SysTick_Handler(void)
{
    sys_tick_ms++;
}

/* 获取系统毫秒时间戳 */
uint32_t at_port_get_tick_ms(void)
{
    return sys_tick_ms;
}

/* 毫秒延时 — 轮询等待 tick 到达目标值 */
void at_port_delay_ms(uint32_t delay_ms)
{
    uint32_t start = at_port_get_tick_ms();
    while ((at_port_get_tick_ms() - start) < delay_ms) {
        /* 等待 tick 到达，可在此处执行低功耗指令（如 __WFI()） */
    }
}
```

### 串口模拟说明

Demo 不使用真实无线模组，而是通过**串口助手模拟**模组响应：

- MCU 通过 **USART1（串口 1）** 发送 AT 指令
- 串口助手手动或脚本回复预期响应（如 `OK`、`+RECV:` 等），方便验证驱动逻辑
- URC 事件同样由串口助手主动推送模拟，例如手动发送 `+STAT:0,1` 来测试状态变更的 URC 检测流程

### 演示测试用例

以下是在 Demo 工程中可操作的测试用例，通过串口助手模拟模组响应来验证各功能模块：

#### 基础 AT 测试

| 序号 | MCU 发送 | 串口助手回复 | 预期结果 |
|------|---------|-------------|---------|
| 1 | `AT\r\n` | `OK\r\n` | 返回 AT_OK |
| 2 | `ATE0\r\n` | `OK\r\n` | 关闭回显，返回 AT_OK |
| 3 | `AT+GETMODE\r\n` | `+MODE:1,2,3\r\nOK\r\n` | 提取参数得到 modes[]={1,2,3} |

#### URC 测试

| 序号 | 操作 | 串口助手主动发送 | 预期结果 |
|------|------|----------------|---------|
| 1 | 主循环轮询 | `+RECV:Hello\r\n` | `at_urc_check(AT_URC_RECV)` 返回 AT_OK |
| 2 | 主循环轮询 | `+STAT:0,1\r\n` | `at_urc_check(AT_URC_STAT)` 返回 AT_OK |
| 3 | 混合数据 | `+RECV:AAA\r\n+STAT:BBB\r\n+RECV:CCC\r\n` | 模块 A 消费 AAA 和 CCC，模块 B 消费 BBB，互不干扰 |

## 移植指南

移植只需修改 `at_port.c` / `at_port.h`，调整 2 个宏并实现 6 个端口函数。

### AT_CHANNEL_MAX 宏定义

| 项目 | 说明 |
|------|------|
| **功能** | 配置最大 AT 通道数量 |
| **位置** | `at_port.h` |
| **实现要点** | 根据实际连接的模组数量调整。例如仅使用 1 路模组定义为 `1`，使用 2 路模组定义为 `2`。驱动内部使用此值静态分配通道数组，按需配置可节省 RAM |

### AT_LOG 日志宏

| 项目 | 说明 |
|------|------|
| **功能** | 驱动内部分级日志输出 |
| **位置** | `at_port.h` |
| **实现要点** | 库文件中默认定义为空（关闭），用户可在自己的 `at_port.h` 中按需覆盖。四个级别：`AT_LOG_D`（调试）、`AT_LOG_I`（信息）、`AT_LOG_W`（警告）、`AT_LOG_E`（错误）。可适配 printf 带颜色、自定义日志函数等。**内部不带换行符**，由用户自行处理行尾 |

```c
/* 示例：对接带颜色分级打印 */
#define AT_LOG_D(...)  print_debug(__VA_ARGS__)
#define AT_LOG_I(...)  print_info(__VA_ARGS__)
#define AT_LOG_W(...)  print_warn(__VA_ARGS__)
#define AT_LOG_E(...)  print_error(__VA_ARGS__)
```

### at_port_delay_ms(delay_ms)

| 项目 | 说明 |
|------|------|
| **功能** | 毫秒级阻塞延时 |
| **参数** | `delay_ms` — 延时长度，单位毫秒 |
| **实现要点** | 裸机下通过 while 轮询系统 tick 实现；RTOS 下可替换为 `vTaskDelay()` 等系统延时，释放 CPU |

### at_port_get_tick_ms()

| 项目 | 说明 |
|------|------|
| **功能** | 获取系统启动以来的毫秒时间戳 |
| **参数** | 无 |
| **返回值** | 当前系统 tick 值（ms） |
| **实现要点** | 在 SysTick 或其他定时器中断中自增全局变量，此函数返回该变量。驱动层用此值做超时判断，不要求与 wall-clock 同步，只要单调递增即可 |

### at_port_send(channel, buf, len)

| 项目 | 说明 |
|------|------|
| **功能** | 通过指定通道发送数据 |
| **参数** | `channel` — 通道号；`buf` — 待发送数据指针；`len` — 发送字节数 |
| **实现要点** | 阻塞式发送，逐字节写入 UART 数据寄存器并等待发送完成标志。若支持 DMA 发送，可在此处启动 DMA。发送期间需确保 buf 指向的数据有效 |

### at_port_send_line_ending(channel)

| 项目 | 说明 |
|------|------|
| **功能** | 发送 AT 命令行尾符 |
| **参数** | `channel` — 通道号 |
| **实现要点** | 默认发送 `\r\n`，内部直接调用 `at_port_send()`。不同模组可能要求 `\r` 或 `\n`，通过 `switch(channel)` 区分即可 |

### at_port_recv_done(channel, buf, len)

| 项目 | 说明 |
|------|------|
| **功能** | 检测 AT 响应行是否接收完成 |
| **参数** | `channel` — 通道号；`buf` — 当前接收缓冲区；`len` — 当前已接收数据长度 |
| **返回值** | `1` 表示接收完成（完整行或 `>` 提示符），`0` 表示未完成 |
| **实现要点** | 默认判断条件：以 `\r\n` 结尾（完整行），或以 `>` 结尾（透传提示符）。不同模组的行尾可能不同，按需定制 |

### at_port_lock(channel) / at_port_unlock(channel)

| 项目 | 说明 |
|------|------|
| **功能** | 进入 / 退出 AT 收发临界区 |
| **参数** | `channel` — 通道号 |
| **实现要点** | RTOS 下实现为递归互斥锁（如 FreeRTOS 的 `xSemaphoreTakeRecursive` / `xSemaphoreGiveRecursive`），防止多任务同时操作同一串口导致数据错乱。裸机下可留空 |

## API 速查

| 函数 | 功能 |
|------|------|
| `at_channel_init(ch, cfg)` | 注册通道（缓冲区 + URC 关键字） |
| `at_cmd_exec(ch, out, cfg)` | 发送 AT 指令并等待响应 |
| `at_recv_push(ch, data, len)` | 注入接收数据（中断 / DMA 回调调用） |
| `at_recv_reset(ch)` | 重置接收缓冲区 |
| `at_recv_remove(ch, offset, bytes)` | 从缓冲区指定位置移除字节 |
| `at_recv_get(ch, &buf, &len)` | 获取缓冲区指针及有效数据长度 |
| `at_resp_param_get(resp, len, idx, buf, size)` | 从响应中提取第 N 个逗号分隔参数 |
| `at_urc_check(ch, urc_index)` | 检测指定 URC 关键字是否命中（只读） |
| `at_recv_buf_swap(ch, &buf, &size, &len)` | 临时切换接收缓冲区（对称调用） |
| `at_session_lock(ch)` / `at_session_unlock(ch)` | 会话锁保护多步事务原子性 |

## 更新日志

### v2.0 — 架构重构与 API 全面升级

本次大版本对框架进行了自底向上的重构，API 不兼容 v1.x。

**架构变更**
- 驱动层扁平化至项目根目录，Demo 从 HAL+CubeMX 切换为 StdPeriph Library（体积精简 95%+）
- 修复文件命名：`at_deriver.h` → `at_driver.h`

**API 不兼容变更**
- 用 `at_channel_t` 通道结构体替代旧版全局宏，缓冲区与 URC 关键字按通道配置
- `at_cmd_config_t` 字段全面重命名，初始化由 `at_init()` 改为 `at_channel_init(channel, cfg)`
- 所有端口函数新增 `channel` 参数，支持多通道适配

**新增**
- `at_recv_buf_swap()` 支持 OTA 大数据场景零额外内存开销
- `at_recv_remove()` 支持多条 URC 混在同一缓冲区时各自独立消费
- `at_resp_param_get()` 支持 JSON 等含逗号字符串参数的安全提取
- `at_session_lock/unlock()` 递归会话锁保护 RTOS 下多步事务原子性
- 日志宏默认关闭，用户侧按需覆盖，消除强制 printf 依赖
- 新增 `at_port_send_line_ending()` / `at_port_recv_done()` 按通道定制

**文档**
- README 完全重写，新增系统架构图、API 速查表、Demo 运行日志、移植指南

### v1.x

初始版本，详见 [v1.0 Release](https://github.com/ZeroOneLab/EmbATlink/releases/tag/v1.0)。

## 许可证

本项目基于 [MIT License](LICENSE) 开源。

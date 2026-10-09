# EmbATlink

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

一款轻量级、分层解耦的嵌入式 AT 指令驱动框架，专为资源受限的 MCU 设计。

**EmbATlink 是什么？** 一个纯 C 的 AT 指令收发引擎，裸机与 RTOS 通用。你把命令行拼好、把关心的 URC 关键字登记好，收发、超时、重试、URC 识别就都交给框架。

**为什么用它？** 手写 AT 通信时，超时与重试、回显与半包、URC 与响应互相穿插——这些边界最费心力，也最容易留下偶发 bug。框架把它们做成显式、可配置的确定行为。

**用在哪里？** 任何通过串口与无线模组（WiFi / 蓝牙 / 4G / NB-IoT 等）通信的嵌入式项目。裸机与 RTOS 均可。

**怎么集成？** 驱动层与硬件完全解耦：实现 6 个端口函数、设一个 `AT_CHANNEL_MAX`，驱动层代码零改动。

## 目录

- [核心特性](#核心特性)
- [资源占用](#资源占用)
- [文件结构](#文件结构)
- [系统架构](#系统架构)
- [移植指南](#移植指南)
- [快速开始](#快速开始)
- [Demo 工程说明](#demo-工程说明)
- [API 速查](#api-速查)
- [注意事项](#注意事项)
- [更新日志](#更新日志)

## 核心特性

- **分层解耦** — 驱动层与硬件端口层分离，换 MCU、换模组只改端口层，驱动逻辑零改动
- **裸机 / RTOS 通用** — 不依赖任何操作系统；RTOS 下可用会话锁保证多步事务的原子性
- **多通道** — 每个通道对应一路串口 / 一个模组，各自独立收发、互不干扰，通道数可配置
- **文本与二进制统一接口** — 命令行与定长数据走同一条调用路径，数据中的 `0x00` 不会被截断
- **收发判定可配** — 每条指令自行声明行尾与响应结束标志（换行符或 `>` 提示符），不做隐式兜底匹配
- **超时与重试内建** — 每条指令单独设定重试次数、轮询间隔与超时；能区分"模组没回应"与"模组答了别的"，底层发送失败也不会伪装成超时
- **URC 与指令收发互不干扰** — 模组主动上报的事件（URC）被独立识别、独立排队，既不混进响应内容，也不受指令重试影响
- **原子指令序列** — 多条指令可打包成一个事务，整段加锁、整段重试，失败能定位到具体步骤
- **大响应可临时扩容** — 接收 OTA 等大块数据时可临时换上更大的缓冲，用完归还
- **轻量无依赖** — 纯 C 实现，静态内存分配，无动态申请，驱动仅 4 个文件

### 资源占用

| 组件 | Flash | RAM | 说明 |
|------|-------|-----|------|
| 驱动核心 (`at_driver` + `at_port`) | **2,320 bytes** | **32 bytes** | 通道运行态数组，不含用户接收 / URC 缓冲区 |
| Demo 应用层 (`main.c`) | 4,056 bytes | 516 bytes | 含 256 bytes 接收缓冲 + 256 bytes URC 缓冲，以及带时间戳的 `log_printf()` |
| STM32 标准外设库 + C 库 + 启动 | ~5.7KB | 1,068 bytes | 含 1KB 系统栈 |
| **Demo 工程总计** | **12,180 bytes** | **1,616 bytes** | |

> 基于 STM32F103C8、ARM Compiler 5 (V5.06 update 7)、MicroLIB、-O3 实测（取自 `Project.map` 的镜像组件尺寸）。接收缓冲与 URC 缓冲由用户按需定义，`AT_CHANNEL_MAX` 决定通道运行态数组大小。

## 文件结构

```
EmbATlink/
├── at_driver.c      # 核心驱动层：AT 指令收发、响应匹配、URC 扫描、缓冲区管理
├── at_driver.h      # 核心驱动头文件：所有对外 API 声明与数据结构定义
├── at_port.c        # 硬件端口层：串口收发、延时、Tick、临界区（需用户适配）
├── at_port.h        # 硬件端口层头文件：宏定义、端口函数声明
└── demo/            # 演示工程（STM32F103C8，串口助手模拟模组）
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
│  · URC 记录扫描 & 队列                            │
│  · 接收缓冲区管理 / swap                          │
│  · 会话锁 (递归互斥)                               │
├──────────────────────────────────────────────────┤
│  硬件端口层  (at_port.c/h — 唯一需要适配的部分)     │
│  · 串口发送 / 接收中断对接                         │
│  · 延时 / Tick (SysTick 中断自增)                 │
│  · 临界区 lock / unlock                           │
└──────────────────────────────────────────────────┘
```

## 移植指南

驱动的硬件相关部分全部集中在端口层。**移植就是把这一层接到你的平台**：改 `at_port.c` / `at_port.h`，配置 `AT_CHANNEL_MAX` 与日志宏，实现 6 个端口函数。

这一步做完，驱动就能编译进你的工程。想先看现成效果，可以直接烧 `demo/stm32f103c8/`（见 [Demo 工程说明](#demo-工程说明)）。

### AT_CHANNEL_MAX 宏定义

| 项目 | 说明 |
|------|------|
| **功能** | 配置最大 AT 通道数量 |
| **位置** | `at_port.h` |
| **实现要点** | 按实际连接的模组数量调整：1 路模组填 `1`，2 路填 `2`。驱动用此值静态分配通道运行态数组，按需配置可省 RAM |

### AT_LOG 日志宏

| 项目 | 说明 |
|------|------|
| **功能** | 驱动内部分级日志输出 |
| **位置** | `at_port.h` |
| **实现要点** | 库文件中默认定义为空（关闭），可在自己的 `at_port.h` 里按需覆盖。三个级别：`AT_LOG_D`（收发成功等协议级调试）、`AT_LOG_W`（单次重试失败等警告）、`AT_LOG_E`（重试耗尽的最终错误）。可对接带颜色的分级打印。**宏内不带换行符**，行尾由用户处理 |

```c
/* 示例：对接带颜色分级打印 */
#define AT_LOG_D(...)  print_debug(__VA_ARGS__)
#define AT_LOG_W(...)  print_warn(__VA_ARGS__)
#define AT_LOG_E(...)  print_error(__VA_ARGS__)
```

### at_port_init(channel)

| 项目 | 说明 |
|------|------|
| **功能** | 初始化通道硬件资源（互斥锁、中断、NVIC 等） |
| **参数** | `channel` — 通道号 |
| **实现要点** | 由 `at_channel_init()` 内部调用，用户无需手动调用。在 `at_port.c` 中用 `switch(channel)` 分别初始化各通道。RTOS 下在此创建递归互斥锁；裸机下可留空。示例：`case 0: s_at_mutex[0] = xSemaphoreCreateRecursiveMutex(); HAL_NVIC_EnableIRQ(USART2_IRQn); break;` |

### at_port_delay_ms(delay_ms)

| 项目 | 说明 |
|------|------|
| **功能** | 毫秒级阻塞延时 |
| **参数** | `delay_ms` — 延时长度，单位毫秒 |
| **实现要点** | 裸机下 while 轮询系统 tick；RTOS 下可换成 `vTaskDelay()` 等系统延时，让出 CPU |

### at_port_get_tick_ms()

| 项目 | 说明 |
|------|------|
| **功能** | 获取系统启动以来的毫秒时间戳 |
| **参数** | 无 |
| **返回值** | 当前系统 tick 值（ms） |
| **实现要点** | 在 SysTick 或其他定时器中断中自增全局变量，函数返回该变量。驱动用它做超时判断，不要求与 wall-clock 同步，单调递增即可 |

### at_port_send(channel, buf, len)

| 项目 | 说明 |
|------|------|
| **功能** | 通过指定通道发送一段数据 |
| **参数** | `channel` — 通道号；`buf` — 待发送数据指针；`len` — 发送字节数 |
| **返回值** | `0` 成功；非 0 表示底层发送失败（通道未绑定 / TXE·TC 等待超时） |
| **实现要点** | 阻塞式发送，逐字节写入 UART 数据寄存器并等发送完成标志，**返回前必须等 TC**（最后一位真正出线）。若支持 DMA 发送，可在此启动 DMA 并等到传输结束再返回。发送期间需保证 `buf` 有效 |

> **注意**：驱动把一条指令拆成两次 `at_port_send()` 调用（主体 + `suffix`），两段之间不得插入延时。若端口实现省掉 TC 等待就返回，两段之间会出现半行停顿，部分模组会把它当成分隔符而产生异常响应。

### at_port_lock(channel) / at_port_unlock(channel)

| 项目 | 说明 |
|------|------|
| **功能** | 进入 / 退出 AT 收发临界区 |
| **参数** | `channel` — 通道号 |
| **实现要点** | RTOS 下实现为递归互斥锁（如 FreeRTOS 的 `xSemaphoreTakeRecursive` / `xSemaphoreGiveRecursive`），防止多任务同时操作同一串口导致数据错乱。裸机下可留空 |

> **注意**：锁句柄未创建时（例如通道尚未 `at_port_init()`）不要把 NULL 传进内核，建议取锁前判空直接返回。

## 快速开始

端口层接好后，按下面的顺序就能用起来：注册通道 → 发送指令 → 接串口数据 → 处理 URC。

### 1. 通道注册

按实际场景分配接收缓冲区大小，通过 `at_channel_t` 注册到驱动：

```c
#include "at_driver.h"

/* URC 记录表 — 登记关心的记录头 / 记录尾，框架自动扫描整条搬取 */
enum {
    AT_URC_RECV = 0,     /* +RECV: 数据到达通知     */
    AT_URC_STAT,         /* +STAT: 状态变化通知     */
    AT_URC_LAST,
};

static const at_urc_key_t at_urc_keys[AT_URC_LAST] = {
    [AT_URC_RECV] = { "+RECV:", "\r\n" },
    [AT_URC_STAT] = { "+STAT:", "\r\n" },
};

/* 通道注册：接收缓冲（命令与响应）与 URC 缓冲各自独立 */
uint8_t recv_buf[256];
uint8_t urc_buf[128];

at_channel_t at_cfg = {
    .recv_buf  = recv_buf,
    .recv_size = sizeof(recv_buf),
    .urc_buf   = urc_buf,
    .urc_size  = sizeof(urc_buf),
    .urc_keys  = at_urc_keys,
    .urc_count = sizeof(at_urc_keys) / sizeof(at_urc_keys[0]),
};

/* 注册到通道 0：后续所有 API 的第一个参数 0 均指此通道 */
at_channel_init(0, &at_cfg);
```

> **代码放在哪**：上面这段注册属于**你自己的模组驱动**（如 `esp8266.c`），写在你的 `.c` 文件里、初始化时调用一次即可；缓冲数组与记录表跟着该文件一起编译，`at_driver.*` 不需要任何改动。
>
> **通道号**：`at_channel_init()` 的第一个参数是通道号，代表把这份配置注册到第几路模组；后续所有 API 的第一个参数都是通道号。通道 0 绑 USART1、通道 1 绑 USART2，各自独立维护缓冲与记录表。
>
> **不用 URC 的通道**：`urc_buf` / `urc_keys` 同进同出，都不填即该通道只做命令收发（`at_urc_get()` 返回 `AT_ERR_NOT_FOUND`）。
>
> **记录表怎么填**：记录头出现在接收数据里、且记录尾到齐，整条记录才会被搬进 URC 缓冲。示例中的 `+RECV:`、`+STAT:` 可换成任意模组的关键字，如 `+IPD`、`+MQTTSUBRECV:`。取用方式见 [URC 事件获取](#5-urc-事件获取)。

### 2. 发送 AT 指令

`at_cmd_config_t` 用位置初始化，字段顺序固定：

```
{ cmd, cmd_len, suffix, expect, done_end, retry, poll_ms, timeout_ms }
```

| # | 字段 | 说明 |
|---|------|------|
| 1 | `cmd` | 命令主体字节：ASCII 命令行，或定长数据（`const void *`） |
| 2 | `cmd_len` | `cmd` 字节数，**必填且 > 0**（库不调用 `strlen` 推断） |
| 3 | `suffix` | 主体之后**单独发送**的尾部；`""` = 不发送；不可为 `NULL` |
| 4 | `expect` | 期望响应关键字；`NULL` = 只发不等 |
| 5 | `done_end` | 响应结束标记；**`expect` 非 NULL 时必填且非空**，`expect` 为 NULL 时可传 `NULL` |
| 6 | `retry` | 最大尝试次数，**>= 1** |
| 7 | `poll_ms` | 响应轮询间隔 (ms) |
| 8 | `timeout_ms` | 单次等待超时 (ms) |

> **发送分两段**：先发 `cmd` 的 `cmd_len` 字节，再单独发一次 `suffix`。不拼接，所以行尾不必另备可写缓冲，字符串字面量可以直接传。
>
> **缺省值只在宏层**：结构体成员一律严格必填，只有 `AT_STR_CMD_DEF` 这类宏才带 `"\r\n"` 默认值——漏填会在第一次调用就报 `AT_ERR_PARAM`，不会退化成偶发超时。

连续下发多条指令时，用宏复用同一个配置变量：

```c
const char     *cmd = NULL;
at_cmd_config_t cfg;

cmd = "AT+MQTTCLEAN=0";
AT_STR_CMD_DEF(cfg, cmd, "OK", 1, 20, 1000);
at_cmd_exec(0, &cfg, NULL);

cmd = "AT+MQTTUSERCFG=0,1,\"\",\"\",\"\",0,0,\"\"";
AT_STR_CMD_DEF(cfg, cmd, "OK", 2, 20, 1000);
at_cmd_exec(0, &cfg, NULL);
```

### 3. 指令配置宏

宏只负责填默认值，不改变字段语义。四个宏覆盖全部场景：

```c
/* 最常用：主体是字符串，尾部与结束符都是 "\r\n" */
at_cmd_config_t cfg;
AT_STR_CMD_DEF(cfg, "AT", "OK", 3, 20, 200);
at_cmd_exec(0, &cfg, NULL);

/* 全开放：尾部或结束符不是 "\r\n" 时显式写 */
AT_STR_CMD(cfg, "AT+MQTTPUBRAW=0,\"t\",10,0,0", "\r\n", "OK", ">", 2, 20, 800);

/* 定长数据：长度由调用方给出，通常不补尾部 */
const uint8_t payload[4] = { 0x01, 0x02, 0x00, 0x04 };
AT_BIN_CMD(cfg, payload, sizeof(payload), "", "+MQTTPUB:OK", "\r\n", 1, 20, 500);

/* 只发不等：不声明结束符，固定等待后取回内容 */
AT_STR_SEND_DEF(cfg, "AT+RST", 500);
```

> `cmd_len` 由宏用 `strlen()` 取得（`AT_BIN_CMD` 由调用方直接给），所以 `AT_STR_CMD*` 的主体必须是可求值的字符串。传指针变量也没问题——`strlen` 在运行期算长度，不像 `sizeof` 会在指针上静默取到 4。

#### 固定指令（发送和预期响应均不变）

```c
/* 通道 0，发送 "AT" + "\r\n"，期望响应 "OK"，最多 3 次，轮询 20ms，单次超时 200ms */
at_cmd_exec(0, &(at_cmd_config_t){"AT", 2, "\r\n", "OK", "\r\n", 3, 20, 200}, NULL);
```

#### 发送指令动态变化

参数需要运行时确定时，用 `snprintf` 拼好再交给宏，长度由宏自己算：

```c
char cmd[32];

snprintf(cmd, sizeof(cmd), "ATE%d", 0);  /* 拼出 "ATE0" — 关闭回显 */
AT_STR_CMD_DEF(cfg, cmd, "OK", 3, 20, 200);
at_cmd_exec(0, &cfg, NULL);
```

#### 发送定长数据

文本与定长数据走同一条路径，`cmd_len` 由调用方给定，数据中的 `0x00` 不会被截断：

```c
const uint8_t payload[4] = {0x01, 0x02, 0x00, 0x04};

/* 原样发 4 字节，不追加尾部（suffix 传 ""），期望 "+MQTTPUB:OK" */
AT_BIN_CMD(cfg, payload, sizeof(payload), "", "+MQTTPUB:OK", "\r\n", 1, 20, 500);
at_cmd_exec(0, &cfg, NULL);
```

#### 等待提示符

透传 / 大数据发送需要先等 `>` 提示符，此时 `expect` 与 `done_end` 都填 `">"`：

```c
/* 收到 '>' 即视为响应完成，随后再发负载 */
AT_STR_CMD(cfg, "AT+MQTTPUBRAW=0,\"topic\",10,0,0", "\r\n", ">", ">", 2, 20, 800);
at_cmd_exec(0, &cfg, NULL);
```

#### 预期响应动态变化

若期望匹配的响应关键字也要动态构造，对 `expect` 参数用同样方式：

```c
char cmd[32], expect[32];

snprintf(cmd,    sizeof(cmd),    "AT+MODE=%d", mode);  /* 拼出 "AT+MODE=1" */
snprintf(expect, sizeof(expect), "+MODE:%d",  mode);  /* 拼出 "+MODE:1"   */
AT_STR_CMD_DEF(cfg, cmd, expect, 3, 20, 1000);
at_cmd_exec(0, &cfg, NULL);
```

#### 响应参数提取

`at_cmd_exec()` 把响应拷进调用方提供的 `at_resp_t`，再用 `at_resp_param_get()` 提取第 N 个逗号分隔参数：

```c
uint8_t  resp_buf[128];
at_resp_t resp = { resp_buf, sizeof(resp_buf), 0 };

/* 通道 0，发送 "AT+MQTTCONN?"，期望响应 "+MQTTCONN:"，重试 1 次，轮询 20ms，超时 800ms */
AT_STR_CMD_DEF(cfg, "AT+MQTTCONN?", "+MQTTCONN:", 1, 20, 800);
if (at_cmd_exec(0, &cfg, &resp) != AT_OK)
    return -1;

/*
 * 响应 "+MQTTCONN:0,4,1,\"host\",1883\r\n"
 * 提取 index=1 得到 "4"
 */
char state[8];
at_resp_param_get(&resp, "+MQTTCONN:", 1, state, sizeof(state));
```

第二个参数 `line_key` 是**行首关键字**：先定位到自己的那一行再切字段。传 `NULL` 表示不限行，从数据开头找。

含逗号的 JSON 字符串可以整段取出，双引号内的逗号不会被误分割：

```c
/*
 * 示例响应: "+DATA:0,5,{\"status\":\"online\"}\r\n"
 * 提取 index=2 即得 JSON 字符串
 */
char json[128];
at_resp_param_get(&resp, "+DATA:", 2, json, sizeof(json));
printf("Data: %s\r\n", json);   /* {"status":"online"} */
```

### 4. 串口接收对接

通过 `at_recv_push()` 把串口收到的数据注入驱动，**中断接收、DMA 接收、主循环轮询**三种方式任选其一。

#### 中断逐字节接收

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

在 DMA 完成中断里批量推入，需另备一块 DMA 专用缓冲区：

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

不依赖中断，在主循环里轮询 UART 状态寄存器，适合裸机场景：

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

> **单生产者约定**：同一通道同一时刻只允许一个注入方，三种方式任选其一，禁止混用或并发。

### 5. URC 事件获取

URC（Unsolicited Result Code）是模组**主动上报**的消息，不与指令响应一一对应。驱动在收发过程中会把这些记录识别出来并单独排队，应用按登记的下标逐条取用，取走即消费：

```c
uint8_t  body[128];
uint16_t len;

/* urc_index 就是 at_urc_keys[] 里的下标；没有该表项的记录时返回 AT_ERR_NOT_FOUND */
len = sizeof(body);                                  /* 入参：buf 容量 */
if (at_urc_get(0, AT_URC_STAT, body, &len) == AT_OK) {
    /* 出参：len = 记录体长度；body 即记录体（记录头 "+STAT:" 与记录尾 "\r\n" 已被剥离） */
    printf("[URC] +STAT <<%s>>\r\n", (char *)body);
}
```

想一次排空某个表项，循环取到 `AT_ERR_NOT_FOUND` 即可：

```c
while (1) {
    len = sizeof(body);
    if (at_urc_get(0, AT_URC_STAT, body, &len) != AT_OK)
        break;
    handle_urc(body, len);
}
```

> **说明**：
> - 只摘除**命中的那一条**记录（其后内容前移到该位置），别的表项原位保留——只想处理某一类 URC 的模块直接指名取用即可，不会误消费别人的记录。
> - 但同一表项仍应只由**一处**消费：队列是同一份，多处同时取同一表项会互相抢记录；要分发给多个任务，请由一处统一取出再转发。
> - 记录体长度不小于传入容量（留不出结尾 `'\0'`）时该记录被丢弃并返回 `AT_ERR_BUF_FULL`，所以 `buf` 要按最长记录体准备、并多留一个字节。
> - 缓存满时**丢最旧记录**（按记录头里的长度整条跳过），不会丢半条，也不会清空整个缓存。
> - 接收缓冲里的原始文本不会因搬进 URC 缓存而消失，所以从 `resp` 取字段时要带上 `line_key`（见 [响应参数提取](#响应参数提取)）。

### 6. 接收缓冲区动态切换

`at_recv_buf_swap()` 用于临时替换接收缓冲区，适用于 OTA 等大容量接收场景。该函数**对称调用**——第一次切到大缓冲区，第二次切回原缓冲区：

```c
/* 正常使用：256 字节缓冲区 */
uint8_t normal_buf[256];

/* OTA 场景：动态申请 10KB 缓冲区 */
uint8_t *ota_buf = malloc(10240);
uint16_t ota_size = 10240;
uint16_t ota_len = 0;

/* 切换到 OTA 大缓冲区（同时保存原缓冲区信息） */
at_recv_buf_swap(0, &ota_buf, &ota_size, &ota_len);

/* ... 执行 OTA 数据接收 ... */

/* OTA 完成，切换回原缓冲区 */
at_recv_buf_swap(0, &ota_buf, &ota_size, &ota_len);

/*
 * free 之前确保 OTA 数据已处理完毕（如写入 Flash、校验、转换等）。
 * swap-back 后 ota_buf 指向原 normal_buf，不可继续当作大缓冲区使用。
 */
free(ota_buf);   /* 此时 ota_buf 指向原 normal_buf，注意不要 free 错误 */
```

> **注意**：swap 后传入的指针会**交换为旧缓冲区的指针和大小**，再次调用即可恢复。调用者需保证通道空闲（建议先 `at_session_lock()`，换回后再解锁），且交换期间新旧缓冲区均有效。

### 7. 会话锁与原子指令序列

多条指令构成一个事务时（如先进入透传模式，再发送数据），需要保护起来，防止 RTOS 任务切换让其他任务的 AT 指令被模组误当作透传数据：

```c
/* 多步事务：进入透传 + 发送数据 */
at_session_lock(0);

at_cmd_exec(0, &(at_cmd_config_t){"AT+QIOPEN", 9, "\r\n",
                                  "CONNECT", "\r\n", 3, 20, 5000});
/* 进入透传模式后，后续数据直接发送（suffix 传 ""，不追加任何尾部） */
at_cmd_exec(0, &(at_cmd_config_t){payload_data, payload_len, "",
                                  NULL, NULL, 1, 20, 1000});

at_session_unlock(0);
```

`at_session_lock()` 是**递归锁**，同一任务可嵌套加锁；其他任务会被阻塞到锁释放。裸机下无任务切换，通常无需使用。

更省事的做法是交给 `at_cmd_seq_exec()`：它把一组指令打包成原子事务，内部自动加锁、整段重试，并用 `failed_index` 指出失败步骤。

```c
const char      *data = "HELLO";
char             pass_cmd[32];
at_cmd_config_t  seq[2];
uint8_t          failed_index = 0;

snprintf(pass_cmd, sizeof(pass_cmd), "AT+QIOPEN=%d", (int)strlen(data));

seq[0] = (at_cmd_config_t){ pass_cmd, (uint16_t)strlen(pass_cmd), "\r\n",
                            "CONNECT", "\r\n", 2, 20, 3000 };   /* 进入透传 */
seq[1] = (at_cmd_config_t){ data, (uint16_t)strlen(data), "",
                            NULL, NULL, 1, 20, 1000 };          /* 发送数据 */

if (at_cmd_seq_exec(0, seq, 2, 3, &failed_index) == AT_OK) {
    /* 整段成功：全部步骤按序完成 */
} else {
    /* 整段失败：failed_index 为失败步骤下标（成功时为 seq_len） */
}
```

## Demo 工程说明

Demo 位于 `demo/stm32f103c8/`，通过 **USART1（PA9-TX / PA10-RX）** 与 PC 端串口助手通信：**MCU 作主机发 AT 指令，串口助手模拟从机模组回响应**，无需真实无线模组即可跑通全流程。

Demo 用 SysTick 做时基：中断里自增一个毫秒计数器作为 `at_port_get_tick_ms()`，`at_port_delay_ms()` 则轮询该计数器。

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
```

> **为什么不用 DWT 做延时？** DWT 只有 Cortex-M3/M4/M7 才有，Cortex-M0/M0+ 与 RISC-V 等平台没有；SysTick 轮询只依赖一个通用定时中断，任何 MCU 都能照搬，移植性最好。平台若需要更高精度的短延时，把 `at_port_delay_ms()` 换成 DWT 实现即可，不影响驱动层。

### 怎么跑

1. 烧录后打开串口助手（115200-8-N-1）
2. 上电先打印一张**响应对照表**，随后自动跑完 8 个演示步骤，每步都会打印"发什么、等你回什么"
3. 照着对照表在串口助手里逐条回复；**在等待窗口内回复**才会判成功
4. 8 步跑完进入 **URC 轮询主循环**，此后可随时推送 URC 记录测试队列

完整的串口收发记录见 [demo运行日志.txt](demo运行日志.txt)（`«` 为 MCU 打印，`»` 为串口助手发送）。

> **等待预算**：演示统一 `retry = 2`、`timeout_ms = 10000`，即单次等 10 秒、最多试 2 次，**一条命令约 20 秒操作时间**。
> 打印里统一用 `(CRLF)` 表示回车换行（而非 `\r\n`），避免照抄成字面的反斜杠字符。

#### 步骤 1～8：命令与响应

| 步骤 | MCU 发送 | 串口助手回复 | 演示的功能点 |
|------|---------|-------------|------------|
| 1 | `AT`(CRLF) | `OK`(CRLF) | 基础自检：`AT_STR_CMD_DEF` 宏，`cmd_len` 由宏用 `strlen` 取得 |
| 2 | `ATE0`(CRLF) | `OK`(CRLF) | 运行时拼接命令行（`snprintf` 拼出 `ATE0`），宏照样算长度 |
| 3 | `AT+GETMODE`(CRLF) | `+MODE:1,2,3`(CRLF) | 响应拷进 `at_resp_t`，`at_resp_param_get()` 按行首 `+MODE:` 定位并取出 1、2、3 |
| 4 | `AT+CPIN?`(CRLF) | `ERROR`(CRLF) | 模组答了别的 → `AT_ERR_NO_MATCH`（不是超时），实际应答经 `resp` 打印出来 |
| 5 | `AT+PASSTHRU=5`(CRLF) | `>`（提示符，**无** CRLF） | 等提示符：`done_end = ">"` 显式声明，不靠端口层猜 `>` |
| 6 | `AT+VERSION`(CRLF) | 任意文本（如 `V3.0`） | 未知响应：`AT_STR_SEND_DEF`（`expect` / `done_end` 均为 `NULL`）固定等待后取回全部内容 |
| 7 | 4 字节定长数据（含 `0x00`，**无** CRLF） | `AT+BINECHO=4`(CRLF) | 文本与数据同一入口：`AT_BIN_CMD` 长度由调用方给定，`0x00` 不截断 |
| 8 | `AT+PASSTHRU=5`(CRLF) → `HELLO`（无 CRLF） | `CONNECT`(CRLF) | `at_cmd_seq_exec()` 原子事务：步骤 2 只发不等，整段自动加锁 + 整段重试 |

> **关于等提示符**：`>`、`\r\n>`、`>\r\n`、`\r\nOK\r\n\r\n>` 这几种到达形式都能正确判定；`>` 与后续行尾分两次到达也没问题。
>
> 要避开的坑是**回显**：若模组仍开着回显，而命令行本身就含 `>`（如 `AT+CIPSEND>`），回显行里的 `>` 会被当成结束符提前命中。所以调这类指令前先 `ATE0` 关回显——Demo 的步骤 2 正是这么做的。

补充用例（把串口助手的回复换掉即可复现）：

| MCU 发送 | 串口助手回复 | 预期结果 |
|---------|-------------|---------|
| `AT`(CRLF) | 完全不回复 | `AT_ERR_TIMEOUT`；每次尝试打印 `[AT:0][1/2] ... TIME OUT ...`，2 次耗尽后打印 `[AT:0] command failed after 2 attempt(s): TIME OUT` |
| `AT+GETMODE`(CRLF) | 只回 `+MODE:1`（不补 CRLF） | `AT_ERR_TIMEOUT`：结束符未到齐一律判超时，不做残数据兜底匹配 |
| `AT+VERSION`(CRLF) | 超过 64 字节的长响应 | `AT_OK`，`resp.len` 即实际拷回长度，超出部分被截断 |
| `AT+PASSTHRU=5`(CRLF) | 只回 `CONNECT` 且一直不补回车 | 步骤 1 超时 → 整段重试 3 次 → 失败，`failed_index = 0` |

#### URC 测试

主循环每轮对每个表项各取一条记录，取到就把记录体原样打印（解析交给应用）：

| 序号 | 串口助手主动发送 | 预期结果 |
|------|----------------|---------|
| 1 | `+RECV:Hello`(CRLF) | 打印 `[URC] idx:0 <<Hello>>` |
| 2 | `+STAT:0,1`(CRLF) | 打印 `[URC] idx:1 <<0,1>>` |
| 3 | `+RECV:AAA`(CRLF)`+STAT:0,1`(CRLF)`+RECV:CCC`(CRLF) | 三条都入队；同一轮取走 `AAA` 与 `0,1`，`CCC` 留到下一轮 |
| 4 | 只发半条 `+RECV:AAA`（不补 CRLF） | 取不到记录：记录尾未到齐，驱动停在记录头等后续字节，日志无输出 |
| 5 | 命令发出后、响应结束前插入 `+RECV:X`(CRLF) | 记录进队列，但原文仍在 `resp` 里——取字段时要带 `line_key` |
| 6 | 连续推送多条完全相同的记录 | 每条都完整取出，不会出现空记录 |
| 7 | 一条记录体远超 `urc_size`（256 字节） | 整条丢弃并打印 `URC body ... exceeds cache`，扫描继续 |
| 8 | 记录体超过取用缓冲（128 字节）但仍在 `urc_size` 内 | `at_urc_get()` 返回 `AT_ERR_BUF_FULL` 并丢弃该条（Demo 只判 `AT_OK`，故日志无输出） |

## API 速查

| 函数 | 功能 |
|------|------|
| `at_channel_init(ch, cfg)` | 注册通道（接收缓冲 + URC 缓冲 + URC 记录表 + 调用 at_port_init） |
| `at_cmd_exec(ch, cfg, resp)` | 发送 AT 指令并等待响应（文本 / 定长数据同一入口） |
| `at_recv_push(ch, data, len)` | 注入接收数据（中断 / DMA 回调 / 主循环轮询） |
| `at_recv_get(ch, &buf, &len)` | 取 cmd 缓存只读视图（起点 + 本次已收长度） |
| `at_urc_get(ch, idx, buf, &size)` | 按记录表下标取一条记录体（`size` 入为容量、出为长度） |
| `at_resp_param_get(resp, line_key, idx, buf, size)` | 按行首关键字定位后提取第 N 个逗号分隔参数 |
| `at_buf_strstr(buf, len, key)` | 带长度的子串查找（缓冲可非 `'\0'` 结尾） |
| `at_cmd_seq_exec(ch, seq, len, retry, &idx)` | 按序执行一组指令（原子事务，整段重试） |
| `at_recv_buf_swap(ch, &buf, &size, &len)` | 临时切换接收缓冲区（对称调用） |
| `at_session_lock(ch)` / `at_session_unlock(ch)` | 会话锁保护多步事务原子性 |

### 指令配置宏

| 宏 | 用途 |
|----|------|
| `AT_STR_CMD(cfg, body, tail, want, end, times, poll, wait)` | 字符串主体，尾部与结束符全部显式给出 |
| `AT_STR_CMD_DEF(cfg, body, want, times, poll, wait)` | 字符串主体，尾部与结束符默认 `"\r\n"`（最常用） |
| `AT_BIN_CMD(cfg, data, data_len, tail, want, end, times, poll, wait)` | 定长数据主体，长度由调用方给出 |
| `AT_STR_SEND_DEF(cfg, body, wait)` | 只发不等（`expect = NULL`、`done_end = NULL`），固定等待后取回内容 |
| 位置初始化 | `{ cmd, cmd_len, suffix, expect, done_end, retry, poll_ms, timeout_ms }` |

### `at_cmd_exec()` 返回值

| 返回值 | 含义 |
|--------|------|
| `AT_OK` | 命中 `expect`（`expect` 为 NULL 时表示已按 `timeout_ms` 等待完毕） |
| `AT_ERR_TIMEOUT` | 等待期内始终未收到完整行（未出现 `done_end`）——模组没回应 |
| `AT_ERR_NO_MATCH` | 收到了完整行但没有一行命中 `expect`——模组回应了别的内容，日志里的 `RECV` 即实际应答 |
| `AT_ERR_IO` | 底层发送失败（主体或尾部），**不重试** |
| `AT_ERR_PARAM` | `config` / `cmd` 为 NULL、`cmd_len == 0`、`suffix` 为 NULL、`expect` 非 NULL 而 `done_end` 为 NULL 或空串、`retry == 0`、通道号越界 |
| `AT_ERR_NO_BUFFER` | 通道缓冲区未注册 |

### `at_urc_get()` 返回值

| 返回值 | 含义 |
|--------|------|
| `AT_OK` | 取到记录体，已补 `'\0'`，长度写入 `*size` |
| `AT_ERR_NOT_FOUND` | 该通道未启用 URC，或队列里没有本表项的记录 |
| `AT_ERR_BUF_FULL` | 记录体不小于传入容量（留不出结尾 `'\0'`），该记录已丢弃，`*size` 不变 |
| `AT_ERR_PARAM` | `buf` / `size` 为 NULL、`*size == 0`，或 `urc_index` 越界 |

## 注意事项

- **响应区按最后一个 `done_end` 切分**：`resp` 装的是接收缓冲从起点到最后一个结束符末尾的原始字节（取最后一个，是为了开回显时不被回显行的行尾截断）。末尾若还有没等到结束符的残数据，不算进 `resp`；`resp` / `at_recv_get()` 里也可能夹带 URC 记录原文，按字段取值请带 `line_key`。
- **URC 缓存满时丢最旧**：空间不足时按记录头里的长度整条丢弃最旧记录（不丢半条、不清空缓存）。应用长时间不调用 `at_urc_get()`，早期记录会被后来的挤掉。**单条记录体上限 = `urc_size` − 3**（3 字节记录头）。
- **`at_resp_param_get()` 以冒号定位参数区**：数据里没有冒号一律返回 `AT_ERR_NOT_FOUND`。URC 记录体已剥掉 `head` / `tail`，通常不含冒号，需要先补回记录头再切字段。
- **`at_recv_buf_swap()` 需在通道空闲时调用**：它会改写接收缓冲指针与写游标，调用方须先 `at_session_lock()`，换回后再解锁。

## 更新日志

### v3.0 — 收发语义显式化与 URC 独立缓冲

围绕"发什么尾部 / 什么算响应结束 / URC 怎么取"做了整体重构，**API 不兼容 v2.x**。

**破坏性变更**

`at_cmd_config_t` 由 6 个字段变为 8 个，顺序固定为
`{ cmd, cmd_len, suffix, expect, done_end, retry, poll_ms, timeout_ms }`：

| v2.x | v3.0 | 说明 |
|------|------|------|
| `const char *cmd` | `const void *cmd` + `uint16_t cmd_len` | 长度由调用方显式给定，不再用 `strlen` 推断，二进制数据可直接传 |
| `uint16_t cmd_len`（0 = 补行尾） | `const char *suffix` | 主体之后单独发送的尾部，`""` 表示不发送 |
| —— | `const char *done_end` | 响应结束标记，`expect` 非 NULL 时必填；替代原先 `at_port_recv_done()` 的通道级隐式判定 |
| `const char *expect` | 不变 | 仍为单关键字子串匹配；为 NULL 时只发不等 |

- 位置初始化时元素数量不符会直接编译报错，旧调用点不会静默错位
- 旧式 `{ "AT", "OK", 3, 20, 200 }` 需改为 `{ "AT", 2, "\r\n", "OK", "\r\n", 3, 20, 200 }`

`at_channel_t` 新增 `urc_buf` / `urc_size`，URC 记录体改存独立缓冲；两者与 `urc_keys` 同进同出，都不填即该通道无 URC 能力。`urc_keys` 由"关键字字符串数组"升级为"记录表"，每条用 `at_urc_key_t` 声明记录头（`head`）与记录尾（`tail`）。

`at_cmd_exec()` 重新引入 `resp` 出参，本次响应拷回调用方缓冲，不需要时传 `NULL`。

`at_resp_param_get()` 改为接收 `at_resp_t`（不再手动传 `resp_len`），并新增 `line_key` 参数：先用行首关键字定位到自己的那一行，再切字段。

`at_urc_get()` 的 `urc_index` 由**出参改为入参**，返回值统一为 `at_status_t`，`size` 改为一入一出（传入 `buf` 容量，传出记录体长度）。

新增指令配置宏（`AT_CMD_CFG` 已删除）：

| 宏 | 用途 |
|----|------|
| `AT_STR_CMD(cfg, body, tail, want, end, times, poll, wait)` | 字符串主体，尾部与结束符显式给出 |
| `AT_STR_CMD_DEF(cfg, body, want, times, poll, wait)` | 字符串主体，尾部与结束符默认 `"\r\n"` |
| `AT_BIN_CMD(cfg, data, data_len, tail, want, end, times, poll, wait)` | 定长数据主体 |
| `AT_STR_SEND_DEF(cfg, body, wait)` | 只发不等（`expect = NULL`） |

**新增**

- `AT_ERR_IO`：端口层发送失败上报，`at_cmd_exec()` 立即终止重试
- `at_port_send()` 改为返回 `int`（0 成功 / 非 0 失败），底部 UART 的 `TXE` / `TC` 等待失败不再被吞掉
- `at_urc_get()`：按记录表下标取记录，只摘除命中的那一条
- `at_resp_param_get()`：行首关键字定位，响应里夹带 URC 文本也能取到自己的字段

**改进**

- URC 记录在收发过程中被搬进独立缓存，指令重试、清空接收缓冲都不影响已入队的记录
- 结束判定取最后一个 `done_end`（开回显时不会被回显行的行尾截断）；取消按 `>` 猜提示符的隐式魔法值，等提示符改为显式写 `done_end = ">"`
- `done_end` 与 `expect` 解耦：`expect` 为 NULL 时 `done_end` 可传 `NULL`，收到多少内容就回多少
- 记录头首字节预筛，非记录字节的扫描开销大幅降低；超长记录整条跳过而不中断扫描
- 返回值细分 `AT_ERR_TIMEOUT`（没收到完整行）与 `AT_ERR_NO_MATCH`（收到完整行但不匹配），便于区分"模组没理你"和"模组答了别的"
- 超时判定改为半开区间，轮询循环保证至少查询一次；`retry` 改用 `uint16_t`，修复 `retry > 255` 时回绕导致的死循环
- 全部公开 API 统一校验通道号，越界返回 `AT_ERR_PARAM`
- 日志收敛为三级：`AT_LOG_D`（收发成功）、`AT_LOG_W`（单次重试失败）、`AT_LOG_E`（重试耗尽的最终错误）
- 日志不再区分文本与二进制，统一为 `TX len:N <<内容>>` / `RX len:N <<内容>>`，长度始终准确

**修复**

- `at_urc_push()` 之前把记录头与记录体写在 URC 缓存的**头部**而不是队尾，只有"入队时队列为空"才凑巧正确；队列里已有记录时再入队会在 `urc_len` 记数的位置留下未写入的零字节，取出时表现为记录体为空的"幽灵记录"（连续推送相同 URC 时会打印出 `<<>>`）。现改为按 `urc_len` 追加到队尾。

**删除**

- `at_port_send_line_ending()` / `at_port_recv_done()`：行尾与结束判定分别由 `suffix` / `done_end` 显式声明，端口层不再需要
- `at_recv_reset()`：驱动在每次发送前自动清空
- `at_recv_remove()` / `at_urc_check()`：URC 记录改由独立缓存整条取用
- `at_buf_sweep()` / `at_run_next()` / `at_window_scan()` / `at_record_span()`：这些函数存在的唯一理由是"URC 与响应共用一个缓冲"，独立缓存后不再需要
- `at_channel_t` 的运行态字段（`rx_len` / `channel` 等）移出结构体，改为文件内静态数组按通道号索引——`at_channel_t` 只剩配置

### v2.2 — 原子指令序列与日志增强

**新增**
- `at_cmd_seq_exec()` 按序执行一组 AT 指令：内部自动加会话锁（原子事务），任一步失败整段从第一条重试，并通过 `failed_index` 定位失败步骤。省去手动 lock/unlock 与外部重试循环，适用于"进入透传 + 发送数据"等多步指令场景

**改进**
- 驱动日志统一以 `<<...>>` 包裹指令与接收数据（如 `CMD:<<AT>>  RECV:<<OK>>`），原始数据更易辨认；接收数据打印增加长度限制 `%.*s`，避免越界
- `at_resp_param_get()` 定位冒号时不再把 `\r\n` 当作搜索边界，修复响应以换行开头时被截断导致提取失败的问题

**文档**
- README 新增"指令序列（原子事务，整段重试）"章节与 API 速查条目
- 全部源文件版本号更新为 v2.2

**工程**
- Demo 工程同步更新驱动源码，并新增 `at_cmd_seq_exec` 演示用例

### v2.1 — API 简化与端口层增强

**API 变更（不兼容）**
- `at_cmd_exec()` 移除 `out_resp` 参数，需要响应数据时直接调用 `at_recv_get()` 获取，职责更清晰

**新增**
- `at_port_init()` 端口函数，由 `at_channel_init()` 内部调用，用于初始化通道硬件资源（互斥锁、中断、NVIC 等），保持高内聚低耦合

**文档**
- 通道注册示例改用 `sizeof` 替代 `ARRAY_SIZE` 宏，更纯粹
- 补充通道号参数说明，明确 `channel` 参数的作用
- 新增"未知响应内容获取"示例，演示 `expect=NULL` 的使用场景
- 所有示例代码统一使用具体通道号（如 `0`）替代变量 `channel`
- Demo 工程新增 `AT+VERSION` 未知响应演示

**工程**
- 驱动源文件移至项目根目录，去除冗余 `EmbATlink/` 嵌套层
- 新增 `.gitignore`，忽略 `.o` / `.d` 等编译产物
- 更新资源占用数据

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

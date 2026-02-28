# EmbATlink

![Image](https://p9-flow-imagex-sign.byteimg.com/tos-cn-i-a9rns2rl98/rc/online_import/05e53c13acbc4549a5dd17d6841c6312~tplv-noop.jpeg?rk3s=49177a0b&x-expires=1772245517&x-signature=tmIZc%2BTVYHm%2FPAkHosflgirmY%2Fo%3D&resource_key=7053ea1f-32c0-4cda-aedb-68f744661b00&resource_key=7053ea1f-32c0-4cda-aedb-68f744661b00)

![Image](https://p9-flow-imagex-sign.byteimg.com/tos-cn-i-a9rns2rl98/rc/online_import/9a6cfbc80b4444ac809c5b16b0d45127~tplv-noop.jpeg?rk3s=49177a0b&x-expires=1772245517&x-signature=kQHHkYTNpCsf7H5ZjoNCQQ93EAs%3D&resource_key=10bda3d3-347b-4bb4-8119-1a0389894db0&resource_key=10bda3d3-347b-4bb4-8119-1a0389894db0)

![Image](https://p9-flow-imagex-sign.byteimg.com/tos-cn-i-a9rns2rl98/rc/online_import/bbac07da52a14ae88dfb95ab5553f1c4~tplv-noop.jpeg?rk3s=49177a0b&x-expires=1772245517&x-signature=%2FHXEtBjeTdQd1ull6Kkak89JD%2B8%3D&resource_key=c9e81d62-fd79-4f1c-85e9-f25eba8b6078&resource_key=c9e81d62-fd79-4f1c-85e9-f25eba8b6078)

一款**高通用性、分层解耦**的AT指令驱动框架，专为嵌入式MCU设计，解决物联网开发中多模组AT指令代码冗余、跨平台移植困难、多路设备并行通信难等问题，支持主动指令发送+被动事件监听，适配STM32等主流MCU平台，裸机/RTOS环境均兼容。

## 关键词

AT command, AT driver, EmbATlink, 串口驱动, 物联网AT指令, 分层解耦AT驱动

## 命名说明

本项目命名为`EmbATlink`，其中：

- `Emb` = Embedded（嵌入式），代表框架的应用场景为嵌入式MCU开发；

- `AT` = AT指令，是物联网模组通用的控制通信协议；

- `link` = 链路/连接，代表框架作为主机与AT模组之间的通信桥梁；

因此`EmbATlink`本质是**标准化、可扩展的嵌入式AT指令驱动框架**，区别于传统项目中零散的AT指令收发代码。

## 🌟 核心特性

✅ **分层解耦设计**：核心逻辑与硬件操作完全分离，移植仅需修改硬件抽象层，无需改动核心代码

✅ **多设备支持**：通过LUN（逻辑单元号）标识多路AT设备，每个LUN对应独立串口/模组

✅ **双通信模式**：支持**主动格式化发送**AT指令+匹配响应，**被动监听**模组主动上报事件（URC），覆盖所有AT通信场景

✅ **配置化指令管理**：通过**枚举+配置表**统一管理AT指令和监听关键字，新增/修改指令无需改动核心逻辑

✅ **鲁棒性保护**：内置指令重发、超时检测、接收缓冲区溢出防护，支持RTOS临界区保护

✅ **格式化指令发送**：支持可变参数的AT指令拼接，无需手动处理`\r\n`结束符，简化开发

✅ **易调试**：内置分级日志打印（INFO/WARN/ERROR），通信失败时精准输出错误原因（超时/匹配失败/缓冲区溢出）

✅ **轻量无依赖**：静态内存分配，无动态内存申请，资源占用低，适合小型嵌入式系统

✅ **灵活适配**：支持串口中断/DMA+空闲中断两种接收方式，可根据项目需求灵活选择

## 📂 文件结构

驱动仅包含4个核心文件，结构清晰，易于集成到任意嵌入式工程中：

```C

EmbATlink/
├── at_driver.c      # 核心驱动层：AT指令收发、响应匹配、被动监听、日志打印等通用逻辑
├── at_driver.h      # 核心驱动头文件：对外暴露所有API接口
├── at_port.c        # 硬件抽象层：串口操作、延时、指令表/监听表配置（需用户适配）
└── at_port.h        # 硬件抽象层头文件：宏定义、指令/监听枚举、硬件层函数声明
```

## 🏗️ 系统架构

采用**三层架构**实现**硬件无关性**，核心逻辑跨平台复用，移植成本极低，与传统零散AT代码相比，大幅提升可维护性和扩展性：

```C

应用层（模组业务逻辑：Wi-Fi/4G/BLE控制、外设联动）
    ↕️
核心驱动层（at_driver.c/h）：通用AT协议逻辑（指令发送、响应匹配、被动监听、超时/重发）
    ↕️
硬件抽象层（at_port.c/h）：平台相关硬件操作+配置化管理（串口、延时、Tick、临界区、指令表/监听表）
```

## 🚀 快速开始

### 步骤1：硬件抽象层适配（仅需修改`at_port.c/h`）

这是唯一需要用户修改的部分，根据自身MCU平台实现硬件相关函数并完成**指令表/监听表配置**，以STM32为例：

1. **修改宏定义**（`at_port.h`）

    - `AT_LUN_MAX`：设置实际使用的AT设备数量（如1路则定义为1）

    - `AT_RECV/SEND_BUFFER_SIZE`：调整收发缓冲区大小，适配不同模组的响应长度

    - 日志宏：默认已开启`printf`打印，无需额外修改（如需关闭，注释即可）

2. **实现基础函数**（`at_port.c`）

    - **先说明LUN含义**：LUN（逻辑单元号）是用于区分多路AT设备的标识，**一个LUN对应一组独立的串口引脚/一个AT模组**；不同LUN的设备通信互不干扰，支持并行工作。

    - `at_port_delay_ms`：对接平台毫秒级延时函数（如`HAL_Delay`）

    - `at_port_get_tick_ms`：对接系统毫秒级时钟（如STM32的`HAL_GetTick`）

    - `at_port_init`：初始化串口接收（中断/DMA），配置对应GPIO

    - `at_port_uart_transmit`：实现串口数据发送

    - （可选）`at_port_enter/exit_critical`：RTOS下实现临界区保护，裸机可留空

**举例**：

```C

/**
 * @brief   AT设备串口数据发送
 * @param   lun: AT设备逻辑单元号
 * @param   buf: 发送数据缓冲区
 * @param   len: 发送数据长度
 * @retval  无
 */
void at_port_uart_transmit(uint8_t lun, const char *buf, uint16_t len)
{
    switch (lun)
    {
    case AT_LUN_SYS: // 0号AT设备：系统串口对接Wi-Fi模组
        HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, 0x100);
        break;
    case AT_LUN_USER: // 1号AT设备：用户串口对接4G模组
        HAL_UART_Transmit(&huart2, (uint8_t *)buf, len, 0x100);
        break;
    default:
        break;
    }
}
```

1. **AT指令枚举与配置表定义**（`at_port.h`+`at_port.c`，核心配置）

框架采用**枚举作为索引**+**配置表存储属性**的方式管理AT指令和被动监听关键字，**枚举成员与配置表下标一一对应**，新增/修改指令仅需修改枚举和配置表，无需改动核心驱动逻辑。

#### 3.1 主动发送指令枚举（`at_port.h`）

定义所有需要**主动发送**的AT指令枚举，`AT_CMD_LAST`为指令总数（必须放在最后，用于数组计数，不可删除/修改），`AT_CMD_DEFAULT`为默认占位符：

```C

typedef enum
{
    AT_CMD_DEFAULT = 0x00, /* 默认指令(占位符，不可删除) */
    AT,                    /* AT基础指令：检测模组在线 */
    ATE,                   /* ATE指令：配置模组回显 */
    AT_CMD_LAST,           /* 指令数量(保留放在最后，不可删除) */
} at_cmd_id_e;
```

#### 3.2 主动发送指令表（`at_port.c`）

类型为`at_cmd_config_t`的数组，**下标与** **`at_cmd_id_e`** **枚举成员严格对应**，存储每条AT指令的通信属性，结构体成员含义见注释：

```C

/* AT指令表：下标与at_cmd_id_e枚举成员一一对应 */
const at_cmd_config_t at_cmd_table[AT_CMD_LAST] = {
    [AT_CMD_DEFAULT] = {"AT_CMD_DEFAULT", NULL, 0, 0, 0},      // 默认指令（占位，属性无意义）
    [AT] = {"AT", "OK", 100, 20, 200},                          // 对应枚举AT：基础指令
    [ATE] = {"ATE", NULL, 1, 20, 2000},                         // 对应枚举ATE：回显配置指令
};
```

**`at_cmd_config_t`** **结构体成员含义**：

|成员名|类型|含义|
|---|---|---|
|`cmd_str`|`const char *`|AT指令基础字符串（如`AT`/`ATE`，可变参数后续格式化拼接）|
|`expected_rsp`|`const char *`|指令预期响应（如`OK`），NULL表示无需匹配响应，超时后直接返回接收数据|
|`send_count`|`uint8_t`|指令最大重发次数，超时/匹配失败时自动重发|
|`check_interval_ms`|`uint16_t`|响应检测间隔，每隔N毫秒检查一次是否收到响应|
|`recv_timeout_ms`|`uint16_t`|单次接收超时时间，超过N毫秒未收到响应则判定为超时|
#### 3.3 被动监听关键字枚举（`at_port.h`）

定义所有需要**被动监听**的关键字枚举（模组主动上报/上位机指令），`AT_MONITOR_LAST`为关键字总数（必须放在最后），`AT_MONITOR_DEFAULT`为默认占位符：

```C

typedef enum
{
    AT_MONITOR_DEFAULT = 0x00, /* 默认监控指令(占位符，不可删除) */
    SYS_LED0_OFF,              /* 监听关键字：LED0关闭指令 */
    SYS_LED0_ON,               /* 监听关键字：LED0开启指令 */
    SYS_LED0_TOGGLE,           /* 监听关键字：LED0翻转指令 */
    AT_MONITOR_LAST,           /* 监听关键字数量(保留放在最后，不可删除) */
} at_monitor_key_e;
```

#### 3.4 被动监听关键字表（`at_port.c`）

字符串数组，**下标与** **`at_monitor_key_e`** **枚举成员严格对应**，存储需要监听的关键字，驱动会自动匹配串口接收数据中是否包含该关键字：

```C

/* AT监听指令表：下标与at_monitor_key_e枚举成员一一对应 */
const char *at_monitor_key_table[AT_MONITOR_LAST] = {
    [AT_MONITOR_DEFAULT] = "AT_MONITOR_DEFAULT",     // 默认指令（占位，无意义）
    [SYS_LED0_OFF] = "AT+LED0=0",                    // 对应枚举SYS_LED0_OFF：LED0关闭关键字
    [SYS_LED0_ON] = "AT+LED0=1",                     // 对应枚举SYS_LED0_ON：LED0开启关键字
    [SYS_LED0_TOGGLE] = "AT+LED0=2",                 // 对应枚举SYS_LED0_TOGGLE：LED0翻转关键字
};
```

1. **串口硬件要求**：根据模组手册配置串口波特率/数据位/校验位/停止位，建议开启串口硬件流控（若模组支持）。

### 步骤2：工程集成

1. 将4个核心文件添加到工程中，新建分组`AT_Driver`管理

2. 在编译器中添加EmbATlink文件夹的头文件路径

3. 重定向`printf`到串口（如STM32重定向`fputc`），并勾选编译器微库（Use MicroLIB）

### 步骤3：驱动初始化

在工程主函数中完成平台初始化后，调用AT驱动初始化函数，示例：

```C

#include "at_driver.h"
int main(void)
{
    // 平台初始化（时钟、GPIO、串口、延时等）
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init(); // 对接AT模组的串口初始化
    
    // 初始化EmbATlink驱动
    at_init();
    
    while(1)
    {
        // 业务逻辑：AT指令发送/被动事件监听
    }
}
```

1. **串口接收回调对接**：将硬件串口的接收回调指向驱动的接收处理函数，以STM32中断接收为例：

```C

#include "at_driver.h"
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    // 驱动接收核心入口：传递LUN、接收数据、长度
    at_uart_recv_handler(AT_LUN_SYS, at_rx_data[AT_LUN_SYS], 1);
    // 重新开启串口中断接收
    HAL_UART_Receive_IT(&huart1, at_rx_data[AT_LUN_SYS], 1);
  }
}
```

## 📚 核心API

驱动对外暴露**基础初始化函数**和**核心通信函数**，封装了所有AT指令收发的底层细节，直接调用即可满足99%的物联网开发需求。

### 基础初始化函数

```C

void at_init(void);                          // 初始化AT驱动，清空缓冲区/初始化硬件
void at_clear_recv_buffer(uint8_t lun);      // 清空指定LUN的接收缓冲区
char *at_get_recv_buffer(uint8_t lun);       // 获取指定LUN的接收缓冲区指针
uint8_t at_get_monitor_match_index(uint8_t lun); // 获取被动监听的事件匹配索引（返回枚举at_monitor_key_e值）
```

### 核心通信函数

|函数|功能|适用场景|
|---|---|---|
|`at_uart_recv_handler`|AT驱动接收核心入口，硬件串口接收后调用|所有场景，必须对接硬件串口回调|
|`at_cmd_format_send_and_recv`|格式化发送AT指令，自动拼接参数+匹配响应|主动控制模组（如连接Wi-Fi、配置4G）|
## 💻 使用示例

### 示例1：基础AT指令发送（检测模组在线）

向0号AT设备发送`AT`指令，检测模组是否正常响应`OK`，**函数入参的** **`cmd_id`** **为** **`at_cmd_id_e`** **枚举成员**，与指令表严格对应：

```C

#include "at_driver.h"
#include <stdio.h>
#define AT_LUN_WIFI 0 // Wi-Fi模组对应0号LUN
int main(void)
{
    // 平台+驱动初始化...
    at_init();
    char *recv_buffer = NULL;
    uint8_t ret;
    
    // 发送AT指令，cmd_id为枚举AT，与指令表[AT]对应
    ret = at_cmd_format_send_and_recv(AT_LUN_WIFI, &recv_buffer, AT, "");
    if (ret == 0)
    {
        printf("AT指令发送成功，响应：%s\r\n", recv_buffer);
    }
    else if (ret == 1)
    {
        printf("AT指令发送超时\r\n");
    }
    else if (ret == 2)
    {
        printf("AT指令响应匹配失败\r\n");
    }
    
    while(1);
}
```

### 示例2：格式化发送带参数的AT指令（配置模组回显）

向0号AT设备发送`ATE1`指令（回显开启），通过可变参数实现指令拼接，**`cmd_id`** **为枚举ATE，与指令表[ATE]对应**：

```C

// 发送ATE1指令，cmd_id为枚举ATE，与指令表[ATE]对应，参数1通过格式化拼接
ret = at_cmd_format_send_and_recv(AT_LUN_WIFI, &recv_buffer, ATE, "%d", 1);
if (ret == 0)
{
    printf("ATE1配置成功，响应：%s\r\n", recv_buffer);
}
```

### 示例3：被动事件监听（模组状态上报/外设控制）

监听上位机/模组的主动上报指令，实现LED灯的远程控制。**`at_get_monitor_match_index`** **返回值为** **`at_monitor_key_e`** **枚举成员**，`switch`的`case`与枚举成员一一对应，最终关联到**监听关键字表**的匹配结果：

1. 驱动自动匹配串口接收数据与`at_monitor_key_table`中的关键字；

2. 匹配成功后，`at_get_monitor_match_index`返回对应的`at_monitor_key_e`枚举值（即`monitor_idx`）；

3. 根据`monitor_idx`（枚举值）执行对应业务逻辑，`switch`的`case`为枚举成员，与**监听枚举+监听表**严格关联。

```C

#include "at_driver.h"
#include "gpio.h"
#define AT_LUN_SYS 0
int main(void)
{
    // 平台+驱动初始化...
    at_init();
    HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_SET); // 初始关闭LED
    
    while(1)
    {
        // 获取被动监听的事件索引：返回at_monitor_key_e枚举值
        uint8_t monitor_idx = at_get_monitor_match_index(AT_LUN_SYS);
        if (monitor_idx != 0xFF) // 匹配到有效事件（0xFF为无匹配）
        {
            char *recv_data = at_get_recv_buffer(AT_LUN_SYS);
            printf("监听到事件：%s\r\n", recv_data);
            // switch的case与at_monitor_key_e枚举成员一一对应，关联监听表的匹配结果
            switch (monitor_idx)
            {
            case SYS_LED0_ON: // 对应枚举SYS_LED0_ON，监听表[AT+LED0=1]
                HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_RESET);
                break;
            case SYS_LED0_OFF: // 对应枚举SYS_LED0_OFF，监听表[AT+LED0=0]
                HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_SET);
                break;
            case SYS_LED0_TOGGLE: // 对应枚举SYS_LED0_TOGGLE，监听表[AT+LED0=2]
                HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin);
                break;
            default:
                break;
            }
            at_clear_recv_buffer(AT_LUN_SYS); // 清空缓冲区，准备下一次监听
        }
        HAL_Delay(10);
    }
}
```

**核心关联关系总结**：

```C
串口接收数据 → 驱动匹配at_monitor_key_table（监听表） → 返回at_monitor_key_e枚举值（monitor_idx） → switch(monitor_idx)执行对应逻辑
```

## 🚨 故障排除

驱动使用中常见问题及解决方法，按优先级排查：

|问题现象|可能原因|解决方法|
|---|---|---|
|AT指令发送超时，返回ret=1|串口硬件未初始化/接线错误|检查串口GPIO配置、模组TX/RX与MCU交叉连接|
|指令发送成功但响应匹配失败，返回ret=2|预期响应配置错误/模组返回格式不同|检查`at_cmd_table`中`expected_rsp`是否与模组手册一致|
|被动监听无响应，monitor_idx始终为0xFF|串口接收回调未对接/缓冲区溢出/枚举-表不匹配|1. 检查`at_uart_recv_handler`是否正确调用；2. 增大`AT_RECV_BUFFER_SIZE`；3. 检查`at_monitor_key_e`枚举与监听表下标是否一一对应|
|串口乱码/日志打印异常|波特率不匹配/printf未重定向|核对MCU与模组的串口波特率，检查`fputc`重定向和微库勾选|
|RTOS下通信乱码/指令执行异常|无临界区保护|在`at_port.c`中实现`at_port_enter/exit_critical`，用信号量/关中断保护|
|缓冲区溢出，日志打印[ERR] RECV BUFFER OVERFLOW|模组响应数据过长|增大`AT_RECV_BUFFER_SIZE`宏定义的数值|
|指令发送报错/无匹配|枚举与配置表下标不对应|检查`at_cmd_id_e`/`at_monitor_key_e`枚举成员与配置表下标是否严格一致，不可跳过/乱序|
## 📝 调试方法

驱动内置**分级日志打印**（INFO/WARN/ERROR），无需额外添加调试代码，通信过程全程可追溯，快速定位问题：

1. 日志默认开启：无需修改，确保`printf`重定向成功即可

2. 日志示例：

    - 指令发送成功：`[AT][SUCC] CMD:AT`

    - 指令发送超时重发：`[AT][RETRY][1] CMD:AT, TIME OUT`

    - 响应匹配失败：`[AT][ERR][1] CMD:AT, RECV: ERROR`

    - 缓冲区溢出：`[ERR] RECV BUFFER OVERFLOW (LUN:0)`

日志直接指出**错误类型、指令内容、设备LUN**，大幅降低物联网模组的调试难度。

## 📄 许可证

本项目基于**MIT开源协议**发布，详见 LICENSE 文件。

## 🔗 相关链接

- 详细使用教程：[EmbATlink：高通用性的AT指令驱动框架](https://www.yuque.com/u54102073/rs06a5/lgcn1lumuhqfvcgg?singleDoc#)

- 嵌入式技术交流群：181921938

如果这个项目对你的物联网开发有帮助，请给它一个 ⭐ ！

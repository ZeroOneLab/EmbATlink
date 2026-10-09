/**
 * @file    main.c
 * @brief   EmbATlink Demo -- STM32F103C8  (USART1 模拟从机模组)
 * @version v3.0
 * @date    2026-10-08
 *
 * 硬件连接：
 *   PA9  -- USART1 TX  -> 串口助手 RX
 *   PA10 -- USART1 RX  <- 串口助手 TX
 *
 * 使用方式：
 *   1. 烧录后打开串口助手 (115200-8-N-1)
 *   2. 上电先打印一张"响应对照表"，8 个演示步骤会依次发命令，
 *      按对照表在串口助手里逐条回复响应（每条指令等待 10 秒、最多试 2 次，
 *      即单条命令给了约 20 秒操作时间），表里的 (CRLF) 表示回车换行
 *   3. 8 步走完进入 URC 轮询，串口助手可随时推送 URC 数据测试记录队列
 *
 * 日志格式：[hh:mm:ss.ms] message\r\n
 *   日志自带时间戳前缀（时间源为 SysTick 中断自增的 sys_tick_ms）
 */

#include "stm32f10x.h"
#include "at_driver.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* ════════════════════════════════════════════════════════════════
 *  全局变量（供 at_port.c / stm32f10x_it.c / log_printf extern 使用）
 * ════════════════════════════════════════════════════════════════ */

volatile uint32_t sys_tick_ms = 0;   /* 系统毫秒 tick，SysTick 中断自增 */

/* ════════════════════════════════════════════════════════════════
 *  硬件底层
 * ════════════════════════════════════════════════════════════════ */

/**
 * @brief   通过 USART1 发送原始数据
 * @param   data  数据指针
 * @param   len   数据长度
 */
void usart1_send(const char *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
        USART_SendData(USART1, (uint8_t)data[i]);
    }
}

/**
 * @brief   printf 重定向到底层 usart1_send（供 stdio 函数内部调用）
 */
int fputc(int ch, FILE *f)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}

/**
 * @brief   带时间戳的日志打印函数
 * @param   fmt  格式化字符串
 * @param   ...  可变参数
 *
 * 输出格式：[hh:mm:ss.ms] message\r\n
 * 时间源为 sys_tick_ms（SysTick 中断每 1ms 自增）。
 * 所有 printf 调用替换为此函数后，串口输出自动带时间戳与换行。
 * 同时被 at_port.h 中的 AT_LOG_* 宏调用，驱动内部日志也带时间戳。
 */
void log_printf(const char *fmt, ...)
{
    char buf[256];
    char ts[16];
    uint32_t t = sys_tick_ms;

    /* 格式化时间戳 [hh:mm:ss.ms] */
    uint32_t h  = (t / 3600000) % 24;
    uint32_t m  = (t / 60000) % 60;
    uint32_t s  = (t / 1000) % 60;
    uint32_t ms = t % 1000;
    snprintf(ts, sizeof(ts), "[%02lu:%02lu:%02lu.%03lu]", h, m, s, ms);

    /* 格式化消息体 */
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    /* 输出：时间戳 + 消息 + 自动换行 */
    usart1_send(ts, strlen(ts));
    if (len > 0)
        usart1_send(buf, (uint16_t)len);
    usart1_send("\r\n", 2);
}

/**
 * @brief   毫秒级阻塞延时（while 轮询 sys_tick_ms）
 * @note    供 at_port.c 中 at_port_delay_ms() extern 调用
 */
void delay_ms(uint32_t ms)
{
    uint32_t start = sys_tick_ms;
    while ((sys_tick_ms - start) < ms);
}

/**
 * @brief   初始化 USART1 (PA9-TX, PA10-RX) + 开启接收中断
 */
static void usart1_init(uint32_t baudrate)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef  nvic;

    /* 时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    /* PA9 -- TX */
    gpio.GPIO_Pin   = GPIO_Pin_9;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* PA10 -- RX (浮空输入) */
    gpio.GPIO_Pin  = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    /* USART 参数 */
    usart.USART_BaudRate            = baudrate;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &usart);

    /* 使能接收中断 */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    /* NVIC */
    nvic.NVIC_IRQChannel                   = USART1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 0;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    USART_Cmd(USART1, ENABLE);
}

/* ════════════════════════════════════════════════════════════════
 *  AT Demo
 * ════════════════════════════════════════════════════════════════ */

/* URC 记录表下标（at_urc_get() 的 urc_index 参数即此枚举值） */
enum {
    AT_URC_RECV = 0,     /* +RECV: 数据到达通知 */
    AT_URC_STAT,         /* +STAT: 状态变化通知 */
};

/* URC 记录表（head + tail 收全才整条入队） */
static const at_urc_key_t at_urc_keys[] = {
    [AT_URC_RECV] = { "+RECV:", "\r\n" },
    [AT_URC_STAT] = { "+STAT:", "\r\n" },
};

static uint8_t recv_buf[256];   /* 命令与响应缓冲 */
static uint8_t urc_buf[256];    /* URC 记录体缓冲 */

int main(void)
{
    /* ── 系统初始化 ── */
    SystemInit();
    SysTick_Config(SystemCoreClock / 1000);  /* 1ms 中断 */
    usart1_init(115200);

    log_printf("+===========================================+");
    log_printf("|    EmbATlink v3.0  |  STM32F103C8         |");
    log_printf("|    USART1 PA9-PA10 @  115200 bps          |");
    log_printf("|    github.com/ZeroOneLab/EmbATlink        |");
    log_printf("+===========================================+");

    log_printf("[BOOT] System Init OK");

    /* ── 响应对照表 ── */
    log_printf("+-----------------------------------------------------------+");
    log_printf("| MCU sends                   | You reply (CRLF)            |");
    log_printf("+-----------------------------------------------------------+");
    log_printf("| AT                          | OK (CRLF)                   |");
    log_printf("| ATE0                        | OK (CRLF)                   |");
    log_printf("| AT+GETMODE                  | +MODE:1,2,3 (CRLF)          |");
    log_printf("| AT+CPIN?                    | ERROR (CRLF)                |");
    log_printf("| AT+PASSTHRU=5               | >  (no CRLF)                |");
    log_printf("| AT+VERSION                  | any text (e.g. V3.0)        |");
    log_printf("| 4 bytes (no CRLF)           | AT+BINECHO=4 (CRLF)         |");
    log_printf("| AT+PASSTHRU=5               | CONNECT (CRLF)              |");
    log_printf("| HELLO (no CRLF)             | (nothing)                   |");
    log_printf("+-----------------------------------------------------------+");
    log_printf("  (commands end with CRLF; rows marked no CRLF are raw data)");
    log_printf("  (each command waits up to 10s for your reply, retried 2 times)");

    /* ── 通道注册（通道 0：绑定 USART1，后续所有 API 第一个参数 0 均指此通道） ── */
    at_channel_t at_cfg = {
        .recv_buf  = recv_buf,
        .recv_size = sizeof(recv_buf),
        .urc_buf   = urc_buf,
        .urc_size  = sizeof(urc_buf),
        .urc_keys  = at_urc_keys,
        .urc_count = sizeof(at_urc_keys) / sizeof(at_urc_keys[0]),
    };

    if (at_channel_init(0, &at_cfg) != AT_OK)
        log_printf("[INIT] Channel 0 FAIL");
    else
        log_printf("[INIT] Channel 0 ready");

    /* ── 演示 1：发送 "AT"，期望 "OK" ── */
    log_printf("\r\n");
    log_printf("[DEMO] send \"AT\"  -> reply: OK(CRLF)");
    {
        at_cmd_config_t cfg;

        AT_STR_CMD_DEF(cfg, "AT", "OK", 2, 20, 10000);
        if (at_cmd_exec(0, &cfg, NULL) != AT_OK)
            log_printf("[DEMO] FAIL: no response");
    }

    /* ── 演示 2：命令行动态拼接（关闭回显） ── */
    log_printf("\r\n");
    log_printf("[DEMO] send \"ATE0\"  -> reply: OK(CRLF)");
    {
        char cmd[16];
        at_cmd_config_t cfg;

        snprintf(cmd, sizeof(cmd), "ATE%d", 0);                  /* 拼出 "ATE0" */
        AT_STR_CMD_DEF(cfg, cmd, "OK", 2, 20, 10000);
        if (at_cmd_exec(0, &cfg, NULL) != AT_OK)
            log_printf("[DEMO] FAIL: no response");
    }

    /* ── 演示 3：从响应中提取参数 ── */
    log_printf("\r\n");
    log_printf("[DEMO] send \"AT+GETMODE\"  -> reply: +MODE:1,2,3(CRLF)");
    {
        uint8_t resp_buf[64];
        at_resp_t resp = { resp_buf, sizeof(resp_buf), 0 };
        at_cmd_config_t cfg;
        char param[8];
        int  modes[3] = { -1, -1, -1 };                          /* 未取到保持 -1 */

        AT_STR_CMD_DEF(cfg, "AT+GETMODE", "+MODE:", 2, 20, 10000);
        if (at_cmd_exec(0, &cfg, &resp) == AT_OK) {
            /* 参数区定位到 "+MODE:" 所在行：resp 里可能还夹着别的行 */
            for (uint8_t i = 0; i < 3; i++) {
                if (at_resp_param_get(&resp, "+MODE:", i, param, sizeof(param)) > 0)
                    modes[i] = atoi(param);
            }
            log_printf("[DEMO] Modes: %d, %d, %d", modes[0], modes[1], modes[2]);
        } else {
            log_printf("[DEMO] FAIL: no +MODE response");
        }
    }

    /* ── 演示 4：模组答了别的 -> AT_ERR_NO_MATCH（不是超时） ── */
    log_printf("\r\n");
    log_printf("[DEMO] send \"AT+CPIN?\"  -> reply: ERROR(CRLF)");
    {
        uint8_t resp_buf[64];
        at_resp_t resp = { resp_buf, sizeof(resp_buf), 0 };
        at_cmd_config_t cfg;

        AT_STR_CMD_DEF(cfg, "AT+CPIN?", "+CPIN:", 2, 20, 10000);
        if (at_cmd_exec(0, &cfg, &resp) == AT_OK)
            log_printf("[DEMO] OK: module replied +CPIN");
        else
            log_printf("[DEMO] Response: %.*s", (int)resp.len, (const char *)resp.buf);
    }

    /* ── 演示 5：等提示符（done_end = ">"，提示符后面没有 \r\n） ── */
    log_printf("\r\n");
    log_printf("[DEMO] send \"AT+PASSTHRU=5\"  -> reply: > (no CRLF)");
    {
        at_cmd_config_t cfg;

        AT_STR_CMD(cfg, "AT+PASSTHRU=5", "\r\n", ">", ">", 2, 20, 10000);
        if (at_cmd_exec(0, &cfg, NULL) != AT_OK)
            log_printf("[DEMO] FAIL: no prompt");
    }

    /* ── 演示 6：未知响应（expect = NULL，固定等待后取回内容） ── */
    log_printf("\r\n");
    log_printf("[DEMO] send \"AT+VERSION\"  -> reply: any text (e.g. V3.0)");
    {
        uint8_t resp_buf[64];
        at_resp_t resp = { resp_buf, sizeof(resp_buf), 0 };
        at_cmd_config_t cfg;

        AT_STR_SEND_DEF(cfg, "AT+VERSION", 10000);
        at_cmd_exec(0, &cfg, &resp);
        log_printf("[DEMO] Response: %.*s", (int)resp.len, (const char *)resp.buf);
    }

    /* ── 演示 7：定长数据主体（长度由调用方给，0x00 不截断） ── */
    log_printf("\r\n");
    log_printf("[DEMO] send 4 bytes  -> reply: AT+BINECHO=4(CRLF)");
    {
        const uint8_t payload[4] = { 0x01, 0x02, 0x00, 0x04 };
        at_cmd_config_t cfg;

        AT_BIN_CMD(cfg, payload, sizeof(payload), "", "AT+BINECHO=4", "\r\n", 2, 20, 10000);
        if (at_cmd_exec(0, &cfg, NULL) != AT_OK)
            log_printf("[DEMO] FAIL: echo not matched");
    }

    /* ── 演示 8：原子指令序列（整段自动加锁 + 整段重试） ──
     *   步骤 1：AT+PASSTHRU=5  进入 5 字节透传模式（期望回复 CONNECT）
     *   步骤 2：HELLO          发送透传数据（expect = NULL，只发不等）
     */
    log_printf("\r\n");
    log_printf("[DEMO] SEQ: send \"AT+PASSTHRU=5\" -> reply: CONNECT(CRLF), then send \"HELLO\"");
    {
        const char *const data = "HELLO";                        /* 透传数据 */
        char              pass_cmd[32];
        at_cmd_config_t   seq[2];

        snprintf(pass_cmd, sizeof(pass_cmd), "AT+PASSTHRU=%d", (int)strlen(data));

        seq[0] = (at_cmd_config_t){ pass_cmd, (uint16_t)strlen(pass_cmd), "\r\n",
                                    "CONNECT", "\r\n", 2, 20, 10000 };
        /* 透传数据：expect 为 NULL，done_end 随之可为 NULL（收到多少回多少） */
        seq[1] = (at_cmd_config_t){ data, (uint16_t)strlen(data), "",
                                    NULL, NULL, 2, 20, 5000 };

        uint8_t failed_index = 0;
        if (at_cmd_seq_exec(0, seq, 2, 3, &failed_index) == AT_OK)
            log_printf("[DEMO] SEQ EXEC OK: all steps done");
        else
            log_printf("[DEMO] SEQ EXEC FAIL: step %u failed (retried 3 times)",
                       (unsigned)failed_index);
    }

    /* ── URC 测试提示（8 步演示已跑完，进入 URC 轮询） ── */
    log_printf("\r\n");
    log_printf("  URC Tests:  (type these in the serial tool at any time)");
    log_printf("  +RECV:Hello(CRLF)                               -> data URC");
    log_printf("  +STAT:0,1(CRLF)                                 -> status URC, event=1");
    log_printf("  +STAT:1,0(CRLF)                                 -> status URC, event=0");
    log_printf("  +RECV:AAA(CRLF)+STAT:0,1(CRLF)+RECV:CCC(CRLF)   -> three records in a row");

    log_printf("[LOOP] Polling URC...");

    /* ── 主循环：取 URC 记录 ── */
    while (1) {
        uint8_t  body[128];
        uint16_t len;

        /* +RECV: 数据到达通知 */
        len = sizeof(body);
        if (at_urc_get(0, AT_URC_RECV, body, &len) == AT_OK)
            log_printf("[URC] idx:%d <<%s>>", AT_URC_RECV, (char *)body);

        /* +STAT: 状态变化通知 */
        len = sizeof(body);
        if (at_urc_get(0, AT_URC_STAT, body, &len) == AT_OK)
            log_printf("[URC] idx:%d <<%s>>", AT_URC_STAT, (char *)body);

        /* 等待 500ms 后继续检查 URC */
        delay_ms(500);
    }
}

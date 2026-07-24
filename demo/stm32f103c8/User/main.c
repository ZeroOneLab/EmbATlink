/**
 * @file    main.c
 * @brief   EmbATlink Demo -- STM32F103C8  (USART1 模拟从机模组)
 *
 * 硬件连接：
 *   PA9  -- USART1 TX  -> 串口助手 RX
 *   PA10 -- USART1 RX  ←  串口助手 TX
 *
 * 使用方式：
 *   1. 烧录后打开串口助手 (115200-8-N-1)
 *   2. MCU 自动发送 AT 指令，串口助手手动回复响应
 *   3. 串口助手可主动推送 URC 数据测试 URC 检测
 *
 * 日志格式：[hh:mm:ss.ms] message
 *   所有日志自动带时间戳前缀，时间源为 SysTick 中断自增的 sys_tick_ms
 */

#include "stm32f10x.h"
#include "at_driver.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

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
 * 所有 printf 调用替换为此函数后，串口输出自动带时间戳。
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

/* URC 关键字表 */
enum {
    AT_URC_RECV = 0,     /* +RECV: 数据到达通知 */
    AT_URC_STAT,         /* +STAT: 状态变化通知 */
    AT_URC_LAST,
};

static const char *at_urc_keys[AT_URC_LAST] = {
    [AT_URC_RECV] = "+RECV:",
    [AT_URC_STAT] = "+STAT:",
};

/* 接收缓冲区 */
static uint8_t recv_buf[256];

/**
 * @brief   消费缓冲区中指定 URC 关键字的所在行（逐行移除，不丢其他数据）
 * @param   key  URC 关键字字符串，如 "+RECV:"
 */
static void consume_urc(const char *key)
{
    uint8_t *buf;
    uint16_t len;
    at_recv_get(0, &buf, &len);
    if (len < 2) return;

    /* 定位关键字位置 */
    const char *p  = (const char *)buf;
    const char *pos = strstr(p, key);
    if (!pos) return;

    uint16_t off = (uint16_t)(pos - p);

    /* 找到行尾 \r\n */
    uint16_t end = off;
    while (end < len - 1 && !(buf[end] == '\r' && buf[end + 1] == '\n'))
        end++;
    if (end >= len - 1) return;

    /* 打印该行内容（不含 \r\n） */
    log_printf("[URC] %.*s", end - off, buf + off);

    /* 仅移除该行，后续数据前移 */
    at_recv_remove(0, off, (end + 2) - off);
}

int main(void)
{
    /* ── 系统初始化 ── */
    SystemInit();
    SysTick_Config(SystemCoreClock / 1000);  /* 1ms 中断 */
    usart1_init(115200);

    log_printf("+==========================================+");
    log_printf("|    EmbATlink v2.0  |  STM32F103C8       |");
    log_printf("|    USART1 PA9-PA10 @  115200 bps 		  |");
    log_printf("|    github.com/ZeroOneLab/EmbATlink      |");
    log_printf("+==========================================+");

    log_printf("[BOOT] System Init OK");

    /* ── 通道注册 ── */
    at_channel_t at_cfg = {
        .recv_buf  = recv_buf,
        .recv_size = sizeof(recv_buf),
        .urc_keys  = at_urc_keys,
        .urc_count = ARRAY_SIZE(at_urc_keys),
    };
    at_channel_init(0, &at_cfg);
    log_printf("[INIT] Channel 0 ready");

    /* ── 自检 ── */
    log_printf("[DEMO] AT -> OK\\r\\n");
    {
		/* 发送 "AT"，期望响应 "OK"，重试 5 次，轮询间隔 20ms，超时 3000ms */
        if(at_cmd_exec(0, NULL, &(at_cmd_config_t){"AT", "OK", 5, 20, 3000}) != AT_OK)
            log_printf("[DEMO] AT FAIL");
    }

    /* ── 演示 ATE0（关闭回显） ── */
    log_printf("[DEMO] ATE0 -> OK\\r\\n");
    {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "ATE%d", 0);  /* "ATE0" */
		/* 发送 "ATE0"，期望响应 "OK"，重试 5 次，轮询间隔 20ms，超时 3000ms */
        if(at_cmd_exec(0, NULL, &(at_cmd_config_t){cmd, "OK", 5, 20, 3000}) != AT_OK)
            log_printf("[DEMO] ATE0 FAIL");
    }

    /* ── 演示 AT+GETMODE（查询模式） ── */
    log_printf("[DEMO] AT+GETMODE -> +MODE:0,1,2\\r\\n");
    {
		/* 发送 "AT+GETMODE"，期望响应 "+MODE:0,1,2"，重试 5 次，轮询间隔 20ms，超时 3000ms */
        if ( at_cmd_exec(0, NULL, &(at_cmd_config_t){"AT+GETMODE", "+MODE:", 5, 20, 3000})== AT_OK) {
            uint8_t *resp;
            uint16_t len;
            at_recv_get(0, &resp, &len);

            char param[8];
            int  modes[3];
            for (int i = 0; i < 3; i++) {
                if (at_resp_param_get((const char *)resp, len, i, param, sizeof(param)) > 0)
                    modes[i] = atoi(param);
            }
            log_printf("[DEMO] Modes: %d, %d, %d", modes[0], modes[1], modes[2]);
        } else {
            log_printf("[DEMO] AT+GETMODE FAIL");
        }
    }

    /* ── 提示 URC 测试 ── */
    log_printf("  URC Tests:\r\n");
    log_printf("  +RECV:Hello\\r\\n                        -> data URC");
    log_printf("  +STAT:0,1\\r\\n                          -> status URC");
    log_printf("  +RECV:AAA\\r\\n+STAT:BBB\\r\\n+RECV:CCC\\r\\n   -> mixed URC");

    log_printf("[LOOP] Polling URC...");
    at_recv_reset(0);

    while (1) {
        while (at_urc_check(0, AT_URC_RECV) == AT_OK)
            consume_urc("+RECV:");

        while (at_urc_check(0, AT_URC_STAT) == AT_OK)
            consume_urc("+STAT:");

        /* 等待 500ms 后继续检查 URC */
        at_recv_reset(0);
		delay_ms(500);
    }
}

/**
 * @file    at_deriver.c
 * @version v1.0
 * @date    2026-02-28
 * @author  ZeroOneLab
 * @website https://github.com/ZeroOneLab/EmbATlink.git
 *
 * @license MIT License
 * Copyright (c) 2026 ZeroOneLab
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "at_driver.h"

#include <stdarg.h>
#include <string.h>
#include <stddef.h>

char at_recv_buffer[AT_LUN_MAX][AT_RECV_BUFFER_SIZE];        /* 接收缓冲区 */
static char at_send_buffer[AT_LUN_MAX][AT_SEND_BUFFER_SIZE]; /* 发送缓冲区 */

uint16_t volatile at_rx_status[AT_LUN_MAX];        /* 接收状态 */
static uint8_t at_monitor_match_index[AT_LUN_MAX]; /* 被动匹配索引 */

extern const at_cmd_config_t at_cmd_table[AT_CMD_LAST];   /* AT指令配置表 */
extern const char *at_monitor_key_table[AT_MONITOR_LAST]; /* 被动匹配关键字表 */

/**
 * @brief    AT初始化
 * @param    无
 * @retval   无
 */
void at_init(void)
{
    for (uint8_t i = 0; i < AT_LUN_MAX; i++)
    {
        memset(at_recv_buffer[i], 0, AT_RECV_BUFFER_SIZE);
        memset(at_send_buffer[i], 0, AT_SEND_BUFFER_SIZE);
        at_rx_status[i] = 0U;
        at_monitor_match_index[i] = 0xFFU;
    }

    for (uint8_t i = 0; i < AT_LUN_MAX; i++)
    {
        at_port_init(i);
    }
}

/**
 * @brief    清除接收缓冲区
 * @param    lun:串口编号
 * @retval   无
 */
void at_clear_recv_buffer(uint8_t lun)
{
    memset(at_recv_buffer[lun], 0, AT_RECV_BUFFER_SIZE);
    at_rx_status[lun] = 0;
}

/**
 * @brief    被动匹配
 * @param    lun:串口编号
 * @retval   0xff:匹配失败 其他:匹配成功
 */
static uint8_t at_monitor_data_match(uint8_t lun)
{
    for (uint8_t i = 0; i < AT_MONITOR_LAST; i++)
    {
        if (strstr((const char *)at_recv_buffer[lun], at_monitor_key_table[i]) != NULL)
            return i; // 匹配成功，返回索引
    }
    return 0xFF; // 匹配失败
}

/**
 * @brief    接收数据
 * @param    lun:串口编号
 * @param    data:接收数据
 * @param    len:接收数据长度
 * @retval   无
 */
void at_uart_recv_handler(uint8_t lun, const uint8_t *data, uint16_t len)
{
    uint16_t data_len = at_rx_status[lun] & 0x7FFF; /* 接收数据长度 */

    if ((data_len + len) >= AT_RECV_BUFFER_SIZE) /* 接收缓冲区溢出 */
    {
        at_rx_status[lun] = 0;
        AT_LOG_E("[ERR] RECV BUFFER OVERFLOW (LUN:%d)\r\n", lun);
        return;
    }

    memcpy(at_recv_buffer[lun] + data_len, data, len); /* 接收数据 */
    at_rx_status[lun] += len;
    data_len += len;

    /* 接收完成判断：1. 收到'>'（透传模式） 2. 收到\r\n（指令响应结束） */
    if ((at_recv_buffer[lun][0] == '>') ||
        (data_len > 2U && (at_recv_buffer[lun][data_len - 1] == '\n' && at_recv_buffer[lun][data_len - 2] == '\r')))
    {
        at_monitor_match_index[lun] = at_monitor_data_match(lun);
        at_rx_status[lun] |= 0x8000U; /* 标记接收完成 */
    }
}

/**
 * @brief    获取接收缓冲区
 * @param    lun:串口编号
 * @retval   接收缓冲区
 */
char *at_get_recv_buffer(uint8_t lun)
{
    return &at_recv_buffer[lun][0];
}

/**
 * @brief    获取被动匹配索引
 * @param    lun:串口编号
 * @retval   被动匹配索引
 */
uint8_t at_get_monitor_match_index(uint8_t lun)
{
    uint8_t temp = at_monitor_match_index[lun];
    at_monitor_match_index[lun] = 0xFF;
    return temp;
}

/**
 * @brief    发送AT指令
 * @param    lun:串口编号
 * @param    cmd:AT指令
 * @param    out_recv:接收数据
 * @param    cmd_config:AT指令配置
 * @retval   1:超时 2:匹配失败 0:成功
 */
static uint8_t at_cmd_send_and_wait(uint8_t lun, char *cmd, char **out_recv, const at_cmd_config_t *cmd_config)
{
    uint8_t send_cnt = 0;
    uint8_t res = 1; /* 1:超时 2:匹配失败 0:成功 */
    at_port_enter_critical(lun);
    for (uint8_t i = 0; i < cmd_config->send_count; i++) /* 发送数据 */
    {
        at_clear_recv_buffer(lun);

        // AT_LOG("CMD:%s\r\n", cmd);   /* 打印发送的命令 */

        at_port_uart_transmit(lun, cmd, strlen(cmd)); /* 发送数据 */
        at_port_uart_transmit(lun, "\r\n", 2);        /* 发送回车换行 */
        send_cnt++;

        if (cmd_config->expected_rsp == NULL) /* 不需要对比数据, 超时后，直接返回接收的数据 */
        {
            at_port_delay_ms(cmd_config->recv_timeout_ms); /* 超时后 */
            if (out_recv != NULL)
                *out_recv = &at_recv_buffer[lun][0]; /* 传递接收数据 */
            res = 0;
            break;
        }

        /* 等待响应并检查 */
        uint32_t recv_tick = at_port_get_tick_ms(); /* 接收超时时间 */

        while (at_port_get_tick_ms() - recv_tick <= cmd_config->recv_timeout_ms) /* 等待超时 */
        {
            at_port_delay_ms(cmd_config->check_interval_ms); /* 延时等待 */

            if ((at_rx_status[lun] & 0x8000) == 0) /* 未接收到数据 */
                continue;

            at_rx_status[lun] &= 0x7FFF; /* 清除接收完成标志 */

            /* 响应匹配检查 */
            if (strstr((const char *)at_recv_buffer[lun], cmd_config->expected_rsp) != NULL) /* 接收数据中包含指定数据 */
            {
                if (out_recv != NULL)
                    *out_recv = &at_recv_buffer[lun][0]; /* 传递接收数据 */

                res = 0; /* 匹配成功 */
                break;
            }

            res = 2; /* 匹配失败 */
        }

        if (res == 0)
            AT_LOG_I("[AT][SUCC] CMD:%s\r\n", cmd);
        if (res == 0)
            break;
        else if (res == 1)
            AT_LOG_W("[AT][RETRY][%hhu] CMD:%s, TIME OUT\r\n", send_cnt, cmd);
        else if (res == 2)
            AT_LOG_E("[AT][ERR][%hhu] CMD:%s, RECV: %s\r\n", send_cnt, cmd, at_recv_buffer[lun]);
    }

    at_port_exit_critical(lun);

    return res;
}

/**
 * @brief    格式化发送AT指令并接收数据
 * @param    lun:串口编号
 * @param    recv_buffer:接收数据
 * @param    cmd_id:AT指令ID
 * @param    format:格式化字符串
 * @retval   1:超时 2:匹配失败 0:成功
 */
uint8_t at_cmd_format_send_and_recv(uint8_t lun, char **recv_buffer, at_cmd_id_e cmd_id, char *format, ...)
{
    uint8_t res = 0;

    memset(at_send_buffer[lun], 0, sizeof(at_send_buffer[lun]));
    va_list arg;
    va_start(arg, format);
    snprintf(at_send_buffer[lun], sizeof(at_send_buffer[lun]), "%s", at_cmd_table[cmd_id].cmd_str);
    vsnprintf(at_send_buffer[lun] + strlen(at_send_buffer[lun]), sizeof(at_send_buffer[lun]) - strlen(at_send_buffer[lun]), format, arg);
    va_end(arg);

    res = at_cmd_send_and_wait(lun, at_send_buffer[lun],
                               recv_buffer,
                               &at_cmd_table[cmd_id]);

    return res;
}

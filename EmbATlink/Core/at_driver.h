/**
 * @file    at_deriver.h
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

#ifndef AT_DRIVER_H
#define AT_DRIVER_H

#include <stdint.h>

#include "at_port.h"

typedef struct
{
    const char *cmd_str;        /* AT指令基础字符串 */
    const char *expected_rsp;   /* 预期响应字符串，如为NULL，则代表不匹配 */
    uint8_t send_count;         /* 最大重发次数 */
    uint16_t check_interval_ms; /* 响应检测间隔(ms) */
    uint16_t recv_timeout_ms;   /* 单次接收超时时间(ms) */
} at_cmd_config_t;

/**
 * @brief    AT初始化
 * @param    无
 * @retval   无
 */
void at_init(void);

/**
 * @brief    清除接收缓冲区
 * @param    lun:串口编号
 * @retval   无
 */
void at_clear_recv_buffer(uint8_t lun);

/**
 * @brief    接收数据
 * @param    lun:串口编号
 * @param    data:接收数据
 * @param    len:接收数据长度
 * @retval   无
 */
void at_uart_recv_handler(uint8_t lun, const uint8_t *data, uint16_t len);

/**
 * @brief    获取接收缓冲区
 * @param    lun:串口编号
 * @retval   接收缓冲区
 */
char *at_get_recv_buffer(uint8_t lun);

/**
 * @brief    获取被动匹配索引
 * @param    lun:串口编号
 * @retval   被动匹配索引
 */
uint8_t at_get_monitor_match_index(uint8_t lun);

/**
 * @brief    格式化发送AT指令并接收数据
 * @param    lun:串口编号
 * @param    recv_buffer:接收数据
 * @param    cmd_id:AT指令ID
 * @param    format:格式化字符串
 * @retval   1:超时 2:匹配失败 0:成功
 */
uint8_t at_cmd_format_send_and_recv(uint8_t lun, char **recv_buffer, at_cmd_id_e cmd_id, char *format, ...);

#endif

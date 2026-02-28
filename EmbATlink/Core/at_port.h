/**
 * @file    at_port.h
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

#ifndef AT_PORT_H
#define AT_PORT_H

#include <stdint.h>
#include <stdio.h>

#define AT_LUN_SYS 0  /* 系统串口 */
#define AT_LUN_USER 1 /* 用户串口 */

#define AT_LUN_MAX 1            /* AT设备逻辑单元号最大值 */
#define AT_RECV_BUFFER_SIZE 512 /* 接收缓冲区大小 */
#define AT_SEND_BUFFER_SIZE 512 /* 发送缓冲区大小 */
#define AT_LOG_I(fmt, ...) printf(fmt, ##__VA_ARGS__)
#define AT_LOG_E(fmt, ...) printf(fmt, ##__VA_ARGS__)
#define AT_LOG_W(fmt, ...) printf(fmt, ##__VA_ARGS__)

typedef enum
{
    AT_CMD_DEFAULT = 0x00, /* 默认指令(保留放在前后) */
    /* 用户自己定义AT指令 */
    AT_CMD_LAST,           /* AT指令数量(保留放在最后) */
} at_cmd_id_e;

typedef enum
{
    AT_MONITOR_DEFAULT = 0x00, /* 默认监控指令(保留放在前后) */
    /* 用户自己定义AT指令 */
    AT_MONITOR_LAST,           /* AT监控指令数量(保留放在最后) */
} at_monitor_key_e;

extern uint8_t at_rx_data[AT_LUN_MAX][1];

/**
 * @brief 延时函数
 * @param xms: 延时时间
 * @retval 无
 */
void at_port_delay_ms(uint32_t xms);

/**
 * @brief 获取系统时间
 * @param 无
 * @retval 系统时间
 */
uint32_t at_port_get_tick_ms(void);

/**
 * @brief AT串口初始化
 * @param lun: 串口编号
 * @retval 无
 */
void at_port_init(uint8_t lun);

/**
 * @brief 进入临界区
 * @param lun: 串口编号
 * @retval 无
 */
void at_port_enter_critical(uint8_t lun);

/**
 * @brief 退出临界区
 * @param lun: 串口编号
 * @retval 无
 */
void at_port_exit_critical(uint8_t lun);

/**
 * @brief AT串口发送数据
 * @param lun: 串口编号
 * @param buf: 发送数据缓冲区
 * @param len: 发送数据长度
 * @retval 无
 */
void at_port_uart_transmit(uint8_t lun, const char *buf, uint16_t len);

#endif

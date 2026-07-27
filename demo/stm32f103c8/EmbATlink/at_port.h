/**
 * @file    at_port.h
 * @version v2.1
 * @date    2026-07-27
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

/** 最大 AT 通道数 */
#define AT_CHANNEL_MAX 1


void log_printf(const char *fmt, ...);

/** 驱动日志宏 — 自动带时间戳前缀 */
#define AT_LOG_D(...)   log_printf(__VA_ARGS__)
#define AT_LOG_I(...)   log_printf(__VA_ARGS__)
#define AT_LOG_W(...)   log_printf(__VA_ARGS__)
#define AT_LOG_E(...)   log_printf(__VA_ARGS__)

/**
 * @brief   初始化通道硬件资源（互斥锁、中断、NVIC 等）
 * @param   [in] channel:  通道号
 * @retval  无
 * @note    由 at_channel_init() 内部调用，用户无需手动调用。
 *          在 at_port.c 中通过 switch(channel) 实现各通道的硬件初始化。
 */
void at_port_init(uint8_t channel);

/**
 * @brief   延时
 * @param   [in] delay_ms:  延时时长 (ms)
 * @retval  无
 */
void at_port_delay_ms(uint32_t delay_ms);

/**
 * @brief   获取系统毫秒时间戳
 * @param   无
 * @retval  当前系统 tick (ms)
 */
uint32_t at_port_get_tick_ms(void);

/**
 * @brief   进入 AT 收发临界区（获取递归互斥锁）
 * @param   [in] channel:  通道号
 * @retval  无
 */
void at_port_lock(uint8_t channel);

/**
 * @brief   退出 AT 收发临界区（释放递归互斥锁）
 * @param   [in] channel:  通道号
 * @retval  无
 */
void at_port_unlock(uint8_t channel);

/**
 * @brief   通过通道发送数据
 * @param   [in] channel:  通道号
 * @param   [in] buf:      发送数据指针
 * @param   [in] len:      发送长度
 * @retval  无
 */
void at_port_send(uint8_t channel, const char *buf, uint16_t len);

/**
 * @brief   发送 AT 命令行尾符（按通道可定制）
 * @param   [in] channel:  通道号
 * @note    默认 \\r\\n；不同模组可通过 switch-case 扩展（如 \\r 等）
 *          内部直接调用 at_port_send，无需 driver 层二次发送。
 */
void at_port_send_line_ending(uint8_t channel);

/**
 * @brief   检测 AT 响应行是否接收完成（按通道可定制）
 * @param   [in] channel:  通道号
 * @param   [in] buf:      接收缓冲区
 * @param   [in] len:      当前已接收长度
 * @retval  1  接收完成（完整行或 '>' 提示符）
 * @retval  0  未完成
 * @note    默认判定：'\\r\\n' 结尾 或 '>' 开头。不同模块可通过 channel 扩展。
 */
uint8_t at_port_recv_done(uint8_t channel, const char *buf, uint16_t len);

#endif

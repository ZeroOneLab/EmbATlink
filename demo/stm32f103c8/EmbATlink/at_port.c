/**
 * @file    at_port.c
 * @version v2.0
 * @date    2026-07-24
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

#include "at_port.h"

/* ── 时间 ── */

/* 毫秒延时 */
void at_port_delay_ms(uint32_t dly_ms)
{
    extern void          delay_ms(uint32_t ms); /* main.c: while 轮询 tick 延时 */
    delay_ms(dly_ms);
}

/* 获取系统毫秒时间戳 */
uint32_t at_port_get_tick_ms(void)
{
    extern volatile uint32_t sys_tick_ms;   /* main.c: SysTick 中断自增      */
    return sys_tick_ms;
}

/* ── 数据收发 ── */

/* 通过通道发送数据 */
void at_port_send(uint8_t channel, const char *buf, uint16_t len)
{
    switch (channel)
    {
    case 0:
		{
            extern void usart1_send(const char *data, uint16_t len);
            usart1_send(buf, len);
        }
        break;
    case 1:
        break;
    default:
        break;
    }
}

/* 发送 AT 命令行尾符 —— 按通道可定制 */
void at_port_send_line_ending(uint8_t channel)
{
    switch (channel)
    {
    case 0:
        at_port_send(channel, "\r\n", 2);
        break;
    case 1:
        break;
    default:
        break;
    }
}

/* 检测 AT 响应行是否接收完成（\r\n 结尾 或 > 提示符） */
uint8_t at_port_recv_done(uint8_t channel, const char *buf, uint16_t len)
{
    switch (channel)
    {
    case 0:
		if (len >= 1U && buf[len - 1] == '>')
            return 1;
        if (len >= 2U && buf[len - 1] == '\n' && buf[len - 2] == '\r')
            return 1;
        break;
    case 1:
        break;
    default:
        break;
    }

    return 0;
}

/* ── 同步（裸机下无 RTOS，锁为空） ── */

/* 进入 AT 临界区（获取递归互斥锁） */
void at_port_lock(uint8_t channel)
{
    switch (channel)
    {
    case 0:
        break;
    case 1:
        break;
    default:
        break;
    }
}

/* 退出 AT 临界区（释放递归互斥锁） */
void at_port_unlock(uint8_t channel)
{
    switch (channel)
    {
    case 0:
        break;
    case 1:
        break;
    default:
        break;
    }
}

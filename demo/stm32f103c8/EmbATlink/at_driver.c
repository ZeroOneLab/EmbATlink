/**
 * @file    at_driver.c
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

#include "at_driver.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static at_channel_t s_at_channels[AT_CHANNEL_MAX];

/* ── 工具函数 ── */

/* 在指定长度缓冲区中搜索子串 */
static char *mem_strstr(const char *haystack, uint16_t haystack_len, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || haystack_len < (uint16_t)needle_len)
        return NULL;

    uint16_t search_len = haystack_len - (uint16_t)needle_len + 1;
    for (uint16_t i = 0; i < search_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0)
            return (char *)(haystack + i);
    }
    return NULL;
}

/* 发送命令行并阻塞等待响应（支持重试） */
static at_status_t send_and_wait(at_channel_t *ch, const char *cmd, char **out_resp,
                                 const at_cmd_config_t *config)
{
    at_status_t result = AT_ERR_TIMEOUT;

    if (ch->recv_buf == NULL)
        return AT_ERR_NO_BUFFER;

    for (uint8_t i = 0; i < config->retry; i++) {
        at_recv_reset(ch->channel);

        at_port_send(ch->channel, cmd, strlen(cmd));
        at_port_send_line_ending(ch->channel);

        if (config->expect == NULL) {
            at_port_delay_ms(config->timeout_ms);
            AT_LOG_D("[AT:%d][SUCC] CMD:%s", ch->channel, cmd);
            if (out_resp != NULL)
                *out_resp = (char *)ch->recv_buf;
            result = AT_OK;
            break;
        }

        result = AT_ERR_TIMEOUT;
        uint32_t start_ms = at_port_get_tick_ms();

        /* 轮询等待 ISR 通过 at_recv_push 写入的数据 */
        while (at_port_get_tick_ms() - start_ms <= config->timeout_ms) {
            at_port_delay_ms(config->poll_ms);

            /* 行完成 → 匹配期望关键字 */
            if (!at_port_recv_done(ch->channel, (const char *)ch->recv_buf, ch->rx_len))
                continue;

            if (mem_strstr((const char *)ch->recv_buf, ch->rx_len, config->expect) != NULL) {
                if (out_resp != NULL)
                    *out_resp = (char *)ch->recv_buf;
                result = AT_OK;
                break;
            }

            /* 本行不匹配，继续接收下一行（AT 响应可能多行） */
            result = AT_ERR_NO_MATCH;
        }

        if (result == AT_OK) {
            AT_LOG_D("[AT:%d][SUCC] CMD:{%s}", ch->channel, cmd);
            break;
        } else if (result == AT_ERR_TIMEOUT) {
            if (i < config->retry - 1)
                AT_LOG_W("[AT:%d][WARN][%hhu] CMD:{%s}  TIME OUT", ch->channel, i + 1, cmd);
        } else {
            if (i < config->retry - 1)
                AT_LOG_W("[AT:%d][WARN][%hhu] CMD:{%s}  RECV: %s", ch->channel, i + 1, cmd,
                         ch->recv_buf);
        }
    }

    if (result != AT_OK)
        AT_LOG_E("[AT:%d][ERR] CMD:%s", ch->channel, cmd);

    return result;
}

/* ════════════════════════════════════════════════════════════════
 *  初始化
 * ════════════════════════════════════════════════════════════════ */

/* 初始化 AT 通道：注册 buf / URC */
at_status_t at_channel_init(uint8_t channel, const at_channel_t *cfg)
{
    if (channel >= AT_CHANNEL_MAX || cfg == NULL)
        return AT_ERR_PARAM;
    if (cfg->recv_buf == NULL)
        return AT_ERR_PARAM;

    at_channel_t *ch = &s_at_channels[channel];

    /* 先清零再拷贝，防止 cfg 未完全初始化导致残留 */
    memset(ch, 0, sizeof(at_channel_t));
    memcpy(ch, cfg, sizeof(at_channel_t));
    ch->channel = channel;
    ch->rx_len  = 0;

    memset(cfg->recv_buf, 0, cfg->recv_size);

    return AT_OK;
}

/* ════════════════════════════════════════════════════════════════
 *  指令收发（核心）
 * ════════════════════════════════════════════════════════════════ */

/* 发送完整 AT 命令行并等待响应 */
at_status_t at_cmd_exec(uint8_t channel, char **out_resp,
                        const at_cmd_config_t *config)
{
    if (channel >= AT_CHANNEL_MAX)
        return AT_ERR_PARAM;

    at_channel_t *ch = &s_at_channels[channel];

    if (ch->recv_buf == NULL)
        return AT_ERR_NO_BUFFER;
    if (config == NULL || config->cmd == NULL)
        return AT_ERR_PARAM;

    at_status_t ret;

    at_port_lock(channel);
    ret = send_and_wait(ch, config->cmd, out_resp, config);
    at_port_unlock(channel);
    return ret;
}

/* 开启 AT 会话锁 */
void at_session_lock(uint8_t channel)
{
    at_port_lock(channel);
}

/* 关闭 AT 会话锁 */
void at_session_unlock(uint8_t channel)
{
    at_port_unlock(channel);
}

/* ════════════════════════════════════════════════════════════════
 *  数据接收 & URC
 * ════════════════════════════════════════════════════════════════ */

/* 向通道注入接收数据（ISR / 轮询 / DMA 回调均可调用） */
at_status_t at_recv_push(uint8_t channel, const uint8_t *data, uint16_t len)
{
    at_channel_t *ch = &s_at_channels[channel];

    if (ch->recv_buf == NULL || data == NULL)
        return AT_ERR_NO_BUFFER;

    if (ch->rx_len + len > ch->recv_size) {
        AT_LOG_E("[AT:%d] RX OVERFLOW! (%u + %u > %u)",
                 channel, ch->rx_len, len, ch->recv_size);
        return AT_ERR_BUF_FULL;
    }

    memcpy(ch->recv_buf + ch->rx_len, data, len);
    ch->rx_len += len;
    
    return AT_OK;
}

/* 检测指定 URC 关键字（只读，数据已通过 at_recv_push 写入） */
at_status_t at_urc_check(uint8_t channel, uint8_t urc_index)
{
    at_channel_t *ch = &s_at_channels[channel];

    if (ch->recv_buf == NULL || urc_index >= ch->urc_count)
        return AT_ERR_NO_MATCH;

    uint16_t recv_len = ch->rx_len;
    if (recv_len > 0 && mem_strstr((const char *)ch->recv_buf, recv_len, ch->urc_keys[urc_index]) != NULL)
        return AT_OK;

    return AT_ERR_NO_MATCH;
}

/* 从 AT 响应中提取第 N 个逗号分隔参数 */
int16_t at_resp_param_get(const char *resp, uint16_t resp_len,
                          uint8_t index,
                          char *out_buf, uint16_t out_size)
{
    uint16_t pos, start, end, param_len;
    uint8_t  current, in_quote;

    if (resp == NULL || resp_len == 0 || out_buf == NULL || out_size == 0)
        return AT_ERR_PARAM;

    /* 找到冒号 */
    pos = 0;
    while (pos < resp_len && resp[pos] != ':' && resp[pos] != '\r' && resp[pos] != '\n')
        pos++;

    if (pos >= resp_len || resp[pos] != ':')
        return AT_ERR_NOT_FOUND;

    /* 跳过冒号及后续空格 */
    pos++;
    while (pos < resp_len && resp[pos] == ' ')
        pos++;

    if (pos >= resp_len || resp[pos] == '\r' || resp[pos] == '\n')
        return AT_ERR_NOT_FOUND;

    /* 遍历参数 */
    current  = 0;
    in_quote = 0;
    start    = pos;

    for (; pos < resp_len; pos++) {
        char c = resp[pos];

        if (c == '"') {
            in_quote = !in_quote;
            continue;
        }

        if (c == '\r' || c == '\n' || c == '\0')
            break;

        if (c == ',' && !in_quote) {
            if (current == index) {
                end = pos;
                goto do_extract;
            }
            current++;
            start = pos + 1;
            while (start < resp_len && resp[start] == ' ')
                start++;
            pos = start - 1;
        }
    }

    /* 最后一个参数（到行尾） */
    if (current == index && start < pos) {
        end = pos;
        goto do_extract;
    }

    return AT_ERR_NOT_FOUND;

do_extract:
    /* trim 首尾空格 */
    while (end > start && resp[end - 1] == ' ')
        end--;
    while (start < end && resp[start] == ' ')
        start++;

    param_len = end - start;
    if (param_len >= out_size)
        return AT_ERR_PARAM;

    memcpy(out_buf, resp + start, param_len);
    out_buf[param_len] = '\0';
    return (int16_t)param_len;
}

/* ════════════════════════════════════════════════════════════════
 *  缓冲区管理
 * ════════════════════════════════════════════════════════════════ */

/* 获取接收缓冲区及有效数据长度 */
at_status_t at_recv_get(uint8_t channel, uint8_t **buf, uint16_t *len)
{
    at_channel_t *ch = &s_at_channels[channel];

    if (ch->recv_buf == NULL)
        return AT_ERR_NO_BUFFER;

    if (buf) *buf = ch->recv_buf;
    if (len) *len = ch->rx_len;
    return AT_OK;
}

/* 重置接收缓冲区及长度 */
at_status_t at_recv_reset(uint8_t channel)
{
    at_channel_t *ch = &s_at_channels[channel];

    if (ch->recv_buf == NULL)
        return AT_ERR_NO_BUFFER;

    memset(ch->recv_buf, 0, ch->recv_size);
    ch->rx_len = 0;
    return AT_OK;
}

/* 从接收缓冲区指定位置移除字节（后续数据前移填补空洞） */
at_status_t at_recv_remove(uint8_t channel, uint16_t offset, uint16_t bytes)
{
    at_channel_t *ch = &s_at_channels[channel];

    if (ch->recv_buf == NULL)
        return AT_ERR_NO_BUFFER;
    if (bytes == 0)
        return AT_OK;
    if (offset + bytes > ch->rx_len)
        return AT_ERR_PARAM;

    uint16_t tail = ch->rx_len - offset - bytes;
    if (tail > 0)
        memmove(ch->recv_buf + offset, ch->recv_buf + offset + bytes, tail);
    ch->rx_len -= bytes;
    /* 清除尾部残留 */
    memset(ch->recv_buf + ch->rx_len, 0, bytes);
    return AT_OK;
}

/* 临时替换接收缓冲区（供 OTA 等大数据场景复用内存） */
void at_recv_buf_swap(uint8_t channel, uint8_t **buf_p, uint16_t *size_p,
                       uint16_t *len_p)
{
    if (channel >= AT_CHANNEL_MAX || buf_p == NULL || size_p == NULL || len_p == NULL)
        return;

    at_channel_t *ch = &s_at_channels[channel];
    uint8_t  *tmp_buf  = ch->recv_buf;
    uint16_t  tmp_size = ch->recv_size;
    uint16_t  tmp_len  = ch->rx_len;

    ch->recv_buf  = *buf_p;
    ch->recv_size = *size_p;
    ch->rx_len    = *len_p;

    *buf_p  = tmp_buf;
    *size_p = tmp_size;
    *len_p  = tmp_len;
}

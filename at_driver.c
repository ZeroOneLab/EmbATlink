/**
 * @file    at_driver.c
 * @version v3.0
 * @date    2026-10-08
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
#include <string.h>

/* 每通道运行态（文件内静态） */
typedef struct {
    at_channel_t      cfg;        /**< 通道配置                              */
    volatile uint16_t rx_len;     /**< cmd 缓存写指针                        */
    volatile uint8_t  overflow;   /**< cmd 缓存溢出标志                      */
    uint16_t          scan;       /**< URC 扫描进度                          */
    uint16_t          urc_len;    /**< URC 缓存已用字节数                     */
} at_rt_t;

static at_rt_t s_rt[AT_CHANNEL_MAX];

/* 取运行态指针 rt；通道号越界直接返回 AT_ERR_PARAM */
#define AT_CH_GET(channel)                                        \
    if ((channel) >= AT_CHANNEL_MAX)                              \
        return AT_ERR_PARAM;                                      \
    at_rt_t *rt = &s_rt[(channel)]

/* ==================== 工具函数 ==================== */

/* 正向查找子串（缓冲无需 '\0' 结尾） */
char *at_buf_strstr(const uint8_t *buf, uint16_t buf_len, const char *key)
{
    size_t key_len = strlen(key);

    if (buf == NULL || key_len == 0 || buf_len < (uint16_t)key_len)
        return NULL;

    uint16_t search_len = (uint16_t)(buf_len - (uint16_t)key_len + 1);
    for (uint16_t i = 0; i < search_len; i++) {
        if (memcmp(buf + i, key, key_len) == 0)
            return (char *)(buf + i);
    }
    return NULL;
}

/* 反向查找子串，返回最后一个匹配 */
static char *at_buf_rstrstr(const uint8_t *buf, uint16_t buf_len, const char *key)
{
    size_t key_len = strlen(key);

    if (buf == NULL || key_len == 0 || buf_len < (uint16_t)key_len)
        return NULL;

    for (uint16_t i = (uint16_t)(buf_len - (uint16_t)key_len + 1u); i > 0u; i--) {
        if (memcmp(buf + i - 1u, key, key_len) == 0)
            return (char *)(buf + i - 1u);
    }
    return NULL;
}

/* ==================== URC 缓存（单游标 FIFO） ==================== */

/* 记录头长度：1 字节表项下标 + 2 字节记录体长度 */
#define URC_HDR 3u

/* 追加一条记录；满了先丢最旧；返回 0 表示本条被放弃 */
static uint8_t at_urc_push(uint8_t channel, uint8_t idx, const uint8_t *body,
                           uint16_t body_len)
{
    at_rt_t *r   = &s_rt[channel];
    uint16_t rec = (uint16_t)(body_len + URC_HDR);
    uint16_t put;

    if (rec > r->cfg.urc_size) {
        AT_LOG_W("[AT:%d] URC body %u exceeds cache %u, record dropped",
                 channel, (unsigned)body_len, (unsigned)r->cfg.urc_size);
        return 0u;                           /* 单条超过整个缓存 */
    }

    while ((uint32_t)r->urc_len + rec > r->cfg.urc_size) {
        uint16_t old;

        /* 丢最旧记录（其后内容前移） */
        old = (uint16_t)(r->cfg.urc_buf[1] | ((uint16_t)r->cfg.urc_buf[2] << 8));
        memmove(r->cfg.urc_buf, r->cfg.urc_buf + URC_HDR + old,
                (size_t)(r->urc_len - URC_HDR - old));
        r->urc_len = (uint16_t)(r->urc_len - URC_HDR - old);
        AT_LOG_W("[AT:%d] URC cache full, drop oldest record", channel);
    }

    put = r->urc_len;                        /* 追加到队尾，不能覆盖已有记录 */

    r->cfg.urc_buf[put]     = idx;           /* 记录头 */
    r->cfg.urc_buf[put + 1] = (uint8_t)(body_len & 0xFFu);
    r->cfg.urc_buf[put + 2] = (uint8_t)(body_len >> 8);
    memcpy(r->cfg.urc_buf + put + URC_HDR, body, body_len);
    r->urc_len = (uint16_t)(r->urc_len + rec);

    return 1u;
}

/* 把 cmd 缓存里已收全的 URC 记录搬进 URC 缓存 */
static void at_urc_harvest(uint8_t channel)
{
    at_rt_t            *r   = &s_rt[channel];
    const at_channel_t *cfg = &r->cfg;
    uint16_t            pos = r->scan;
    uint16_t            end = r->rx_len;

    if (cfg->recv_buf == NULL || cfg->urc_buf == NULL || cfg->urc_keys == NULL)
        return;
    if (pos > end)
        pos = 0;                             /* 写指针归零：进度回退 */
    r->scan = pos;

    while (pos < end) {
        const char *head;
        const char *tail;
        char       *p;
        uint8_t     idx;
        size_t      hlen = 0;
        uint16_t    body_at;
        uint16_t    body_end;

        for (idx = 0; idx < cfg->urc_count; idx++) {   /* 找记录头 */
            head = cfg->urc_keys[idx].head;

            if (cfg->recv_buf[pos] != (uint8_t)head[0])
                continue;                    /* 首字节不符 */
            hlen = strlen(head);
            if ((size_t)(end - pos) < hlen)
                continue;
            if (memcmp(cfg->recv_buf + pos, head, hlen) == 0)
                break;
        }
        if (idx == cfg->urc_count) {         /* 非记录头：后移一字节 */
            pos++;
            r->scan = pos;
            continue;
        }

        /* 表头之后的第一个记录尾即本记录结束 */
        tail    = cfg->urc_keys[idx].tail;
        body_at = (uint16_t)(pos + hlen);
        p       = at_buf_strstr(cfg->recv_buf + body_at,
                                (uint16_t)(end - body_at), tail);
        if (p == NULL) {
            r->scan = pos;                   /* 尾未到齐：等下一批 */
            return;
        }

        body_end = (uint16_t)(p - (char *)cfg->recv_buf);

        /* 超长记录入队失败：跳过整条 */
        (void)at_urc_push(channel, idx, cfg->recv_buf + body_at,
                          (uint16_t)(body_end - body_at));

        pos = (uint16_t)(body_end + strlen(tail));   /* 越过整条记录 */
        r->scan = pos;
    }
}

/* 取本次响应并判定 expect；resp 非空时拷出响应区，返回 0 未收全 / 1 未命中 / 2 命中 */
static uint8_t at_resp_take(const at_rt_t *r, const at_cmd_config_t *config,
                            at_resp_t *resp)
{
    uint16_t len  = r->rx_len;
    size_t   dlen = (config->done_end != NULL) ? strlen(config->done_end) : 0u;
    char    *end  = (len > 0u && dlen > 0u)
                        ? at_buf_rstrstr(r->cfg.recv_buf, len, config->done_end)
                        : NULL;
    uint16_t n    = len;

    /* 响应区 = cmd 缓存起点到最后一个结束符末尾；结束符未到则取已收全部 */
    if (end != NULL)
        n = (uint16_t)((uint16_t)(end - (char *)r->cfg.recv_buf) +
                       (uint16_t)dlen);

    if (resp != NULL && resp->buf != NULL) {
        uint16_t room = (uint16_t)(resp->size - 1u);
        uint16_t cpy  = (n < room) ? n : room;

        if (cpy > 0u)
            memcpy(resp->buf, r->cfg.recv_buf, cpy);
        resp->buf[cpy] = '\0';
        resp->len      = cpy;
    }

    if (end == NULL || config->expect == NULL)
        return 0u;                           /* expect 为 NULL：不判定 */

    return (at_buf_strstr(r->cfg.recv_buf, n, config->expect) != NULL) ? 2u : 1u;
}

/* ==================== 初始化 ==================== */

/* 初始化 AT 通道 */
at_status_t at_channel_init(uint8_t channel, const at_channel_t *cfg)
{
    if (channel >= AT_CHANNEL_MAX || cfg == NULL)
        return AT_ERR_PARAM;
    if (cfg->recv_buf == NULL || cfg->recv_size == 0)
        return AT_ERR_PARAM;
    if ((cfg->urc_keys == NULL) != (cfg->urc_buf == NULL))
        return AT_ERR_PARAM;                 /* URC 两项同进同出 */

    if (cfg->urc_keys != NULL) {
        if (cfg->urc_size < 4u || cfg->urc_count == 0)
            return AT_ERR_PARAM;

        for (uint8_t i = 0; i < cfg->urc_count; i++) {
            if (cfg->urc_keys[i].head == NULL || cfg->urc_keys[i].head[0] == '\0')
                return AT_ERR_PARAM;
            if (cfg->urc_keys[i].tail == NULL || cfg->urc_keys[i].tail[0] == '\0')
                return AT_ERR_PARAM;
        }
    }

    at_rt_t *rt = &s_rt[channel];

    memset(rt, 0, sizeof(at_rt_t));
    rt->cfg = *cfg;                          /* 配置整体拷贝，入参之后可释放 */

    memset(cfg->recv_buf, 0, cfg->recv_size);
    if (cfg->urc_buf != NULL)
        memset(cfg->urc_buf, 0, cfg->urc_size);

    at_port_init(channel);

    return AT_OK;
}

/* ==================== 指令收发 ==================== */

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

/* 按序执行一组 AT 指令（原子事务，整段重试） */
at_status_t at_cmd_seq_exec(uint8_t channel, const at_cmd_config_t *seq,
                            uint8_t seq_len, uint8_t retry,
                            uint8_t *failed_index)
{
    if (channel >= AT_CHANNEL_MAX || seq == NULL || seq_len == 0 || retry == 0)
        return AT_ERR_PARAM;

    at_status_t result = AT_ERR_GENERIC;
    uint8_t     step   = 0;

    at_session_lock(channel);

    for (uint8_t attempt = 0; attempt < retry; attempt++) {
        result = AT_OK;
        for (step = 0; step < seq_len; step++) {
            result = at_cmd_exec(channel, &seq[step], NULL);
            if (result != AT_OK)
                break;
        }
        if (result == AT_OK)
            break;

        if (attempt + 1 < retry)
            AT_LOG_W("[AT:%d][WARN][seq] attempt %u/%u, step %u failed",
                     channel, (unsigned)(attempt + 1), (unsigned)retry,
                     (unsigned)step);
    }

    at_session_unlock(channel);

    if (failed_index)
        *failed_index = (result == AT_OK) ? seq_len : step;

    if (result != AT_OK)
        AT_LOG_E("[AT:%d][ERR][seq] step %u failed after %u attempt(s)",
                 channel, (unsigned)step, (unsigned)retry);

    return result;
}

/* 发送指令并阻塞等待响应 */
at_status_t at_cmd_exec(uint8_t channel, const at_cmd_config_t *config,
                        at_resp_t *resp)
{
    at_status_t result = AT_ERR_TIMEOUT;
    uint16_t    attempt;

    AT_CH_GET(channel);

    if (rt->cfg.recv_buf == NULL)
        return AT_ERR_NO_BUFFER;
    if (config == NULL || config->cmd == NULL)
        return AT_ERR_PARAM;
    if (config->cmd_len == 0 || config->suffix == NULL)
        return AT_ERR_PARAM;
    if (config->expect != NULL &&
        (config->done_end == NULL || config->done_end[0] == '\0'))
        return AT_ERR_PARAM;                 /* expect 非空时才需要 done_end */
    if (config->retry == 0)
        return AT_ERR_PARAM;
    if (resp != NULL && (resp->buf == NULL || resp->size < 2u))
        return AT_ERR_PARAM;                 /* 需留出结尾 '\0' 的余量 */

    if (resp != NULL)
        resp->len = resp->buf[0] = '\0';

    const char *cmd_text = (const char *)config->cmd;
    uint16_t    text_len = config->cmd_len;
    const char *why      = NULL;

    at_port_lock(channel);

    for (attempt = 0; attempt < config->retry; attempt++) {
        uint32_t start_ms;
        uint8_t  m;

        at_urc_harvest(channel);             /* 先收已到齐的 URC */

        rt->rx_len   = 0;                    /* 清空 cmd 缓存 */
        rt->overflow = 0u;
        rt->scan     = 0;                    /* 扫描进度复位 */

        /* 第一段：命令主体 */
        if (at_port_send(channel, config->cmd, config->cmd_len) != 0) {
            result = AT_ERR_IO;
            why    = "SEND FAILED";
            break;
        }

        /* 第二段：尾部（"" 表示不发） */
        if (config->suffix[0] != '\0' &&
            at_port_send(channel, config->suffix,
                         (uint16_t)strlen(config->suffix)) != 0) {
            result = AT_ERR_IO;
            why    = "SEND FAILED";
            break;
        }

        /* 只发不等：固定等待后取回内容 */
        if (config->expect == NULL) {
            at_port_delay_ms(config->timeout_ms);
            at_urc_harvest(channel);
            (void)at_resp_take(rt, config, resp);
            result = AT_OK;
            break;
        }

        /* 等待响应 */
        start_ms = at_port_get_tick_ms();
        for (;;) {
            at_urc_harvest(channel);
            m = at_resp_take(rt, config, resp);

            if (rt->overflow) {
                result = AT_ERR_BUF_FULL;
                break;
            }
            if (m == 2u) {
                result = AT_OK;
                break;
            }
            if ((int32_t)(at_port_get_tick_ms() - start_ms) >=
                (int32_t)config->timeout_ms) {
                result = (m == 1u) ? AT_ERR_NO_MATCH : AT_ERR_TIMEOUT;
                break;
            }

            at_port_delay_ms(config->poll_ms);
        }

        if (result == AT_OK)
            break;

        /* 失败：原因与收发内容 */
        why = (result == AT_ERR_BUF_FULL) ? "RX OVERFLOW, RESPONSE LOST"
              : (result == AT_ERR_TIMEOUT)  ? "TIME OUT"
                                            : "NO MATCH";

        AT_LOG_W("[AT:%d][%u/%u] TX len:%u <<%.*s>>  %s  RX len:%u <<%.*s>>",
                 channel, (unsigned)(attempt + 1u), (unsigned)config->retry,
                 (unsigned)text_len, (int)text_len, cmd_text, why,
                 (unsigned)rt->rx_len, (int)rt->rx_len,
                 (const char *)rt->cfg.recv_buf);
    }

    if (result == AT_OK)
        AT_LOG_D("[AT:%d][SUCC] TX len:%u <<%.*s>>", channel,
                 (unsigned)text_len, (int)text_len, cmd_text);
    else
        AT_LOG_E("[AT:%d] command failed after %u attempt(s): %s", channel,
                 (unsigned)config->retry, why);

    at_port_unlock(channel);
    return result;
}

/* ==================== 数据接收 & URC ==================== */

/* 追加接收数据；放不下时清零并置溢出标志 */
at_status_t at_recv_push(uint8_t channel, const uint8_t *data, uint16_t len)
{
    AT_CH_GET(channel);

    if (rt->cfg.recv_buf == NULL)
        return AT_ERR_NO_BUFFER;
    if (len == 0)
        return AT_OK;
    if (data == NULL || len > rt->cfg.recv_size)
        return AT_ERR_PARAM;

    if ((uint32_t)rt->rx_len + len > rt->cfg.recv_size) {
        rt->rx_len   = 0;                    /* 放不下：整区重来 */
        rt->overflow = 1u;
    }

    memcpy(rt->cfg.recv_buf + rt->rx_len, data, len);
    rt->rx_len = (uint16_t)(rt->rx_len + len);

    return AT_OK;
}

/* 取一条指定表项的 URC 记录；size 入为 buf 容量，出为记录体长度 */
at_status_t at_urc_get(uint8_t channel, uint8_t urc_index, uint8_t *buf,
                       uint16_t *size)
{
    at_status_t status = AT_ERR_NOT_FOUND;
    uint16_t    off;
    uint16_t    room;

    AT_CH_GET(channel);

    if (buf == NULL || size == NULL || *size == 0u)
        return AT_ERR_PARAM;
    if (rt->cfg.urc_buf == NULL || rt->cfg.urc_count == 0)
        return AT_ERR_NOT_FOUND;             /* 通道未启用 URC */
    if (urc_index >= rt->cfg.urc_count)
        return AT_ERR_PARAM;

    room = *size;

    at_port_lock(channel);
    at_urc_harvest(channel);                 /* 先收已到齐的 URC */

    for (off = 0u; (uint16_t)(rt->urc_len - off) >= URC_HDR; ) {
        uint16_t body_len = (uint16_t)(rt->cfg.urc_buf[off + 1u] |
                                       ((uint16_t)rt->cfg.urc_buf[off + 2u] << 8));
        uint16_t rec      = (uint16_t)(URC_HDR + body_len);

        if (rt->cfg.urc_buf[off] != urc_index) {
            off = (uint16_t)(off + rec);     /* 非目标记录：原位保留 */
            continue;
        }

        if (body_len >= room) {              /* 留不出结尾 '\0'：整条丢弃 */
            status = AT_ERR_BUF_FULL;
        } else {
            memcpy(buf, rt->cfg.urc_buf + off + URC_HDR, body_len);
            buf[body_len] = '\0';
            *size  = body_len;
            status = AT_OK;
        }

        /* 摘除命中记录（其后内容前移） */
        rt->urc_len = (uint16_t)(rt->urc_len - rec);
        memmove(rt->cfg.urc_buf + off, rt->cfg.urc_buf + off + rec,
                (size_t)(rt->urc_len - off));
        break;
    }

    at_port_unlock(channel);
    return status;
}

/* 提取第 N 个逗号分隔参数（0-based，双引号内的逗号不作分隔） */
int16_t at_resp_param_get(const at_resp_t *resp, const char *line_key,
                          uint8_t index, char *out_buf, uint16_t out_size)
{
    const char *data;
    uint16_t    resp_len;
    uint16_t    pos;
    uint8_t     current  = 0u;
    uint8_t     in_quote = 0u;

    if (resp == NULL || resp->buf == NULL || resp->len == 0 ||
        out_buf == NULL || out_size == 0)
        return AT_ERR_PARAM;

    data     = (const char *)resp->buf;
    resp_len = resp->len;

    if (line_key != NULL) {                  /* 先定位关键字所在行 */
        const char *p = at_buf_strstr(resp->buf, resp_len, line_key);

        if (p == NULL)
            return AT_ERR_NOT_FOUND;
        data     = p;
        resp_len = (uint16_t)(resp->len - (uint16_t)(p - (const char *)resp->buf));
    }

    /* 参数区在第一个冒号之后 */
    for (pos = 0u; pos < resp_len && data[pos] != ':'; pos++)
        ;
    if (pos >= resp_len)
        return AT_ERR_NOT_FOUND;

    pos++;                                   /* 跳过冒号与前导空格 */
    while (pos < resp_len && data[pos] == ' ')
        pos++;

    for (;;) {
        uint16_t start = pos;
        uint16_t end;
        uint16_t param_len;

        while (pos < resp_len) {             /* 定界到下一个引号外逗号或行尾 */
            char c = data[pos];

            if (c == '"') {
                in_quote = (uint8_t)!in_quote;
                pos++;
                continue;
            }
            if ((c == ',' && !in_quote) || c == '\r' || c == '\n' || c == '\0')
                break;
            pos++;
        }

        if (current == index) {
            end = pos;
            while (end > start && data[end - 1u] == ' ')
                end--;                       /* 去掉尾部空格 */

            param_len = (uint16_t)(end - start);
            if (param_len == 0u)
                return AT_ERR_NOT_FOUND;     /* 空参数 */
            if (param_len >= out_size)
                return AT_ERR_PARAM;

            memcpy(out_buf, data + start, param_len);
            out_buf[param_len] = '\0';
            return (int16_t)param_len;
        }

        if (pos >= resp_len || data[pos] != ',')
            return AT_ERR_NOT_FOUND;         /* 参数不足 */

        current++;
        pos++;                               /* 跳过逗号 */
        while (pos < resp_len && data[pos] == ' ')
            pos++;
    }
}

/* ==================== 缓冲区管理 ==================== */

/* 取 cmd 缓存的只读视图 */
at_status_t at_recv_get(uint8_t channel, uint8_t **buf, uint16_t *len)
{
    AT_CH_GET(channel);

    if (rt->cfg.recv_buf == NULL)
        return AT_ERR_NO_BUFFER;

    if (buf)
        *buf = rt->cfg.recv_buf;
    if (len)
        *len = rt->rx_len;

    return AT_OK;
}

/* 临时替换接收缓冲区 */
void at_recv_buf_swap(uint8_t channel, uint8_t **buf_p, uint16_t *size_p,
                      uint16_t *len_p)
{
    uint8_t  *tmp_buf;
    uint16_t  tmp_size;
    uint16_t  tmp_len;

    if (channel >= AT_CHANNEL_MAX || buf_p == NULL || size_p == NULL || len_p == NULL)
        return;

    at_rt_t *rt = &s_rt[channel];

    if (rt->rx_len > 0u)
        AT_LOG_W("[AT:%d] swap with %u unconsumed byte(s)",
                 channel, (unsigned)rt->rx_len);

    tmp_buf  = rt->cfg.recv_buf;
    tmp_size = rt->cfg.recv_size;
    tmp_len  = rt->rx_len;

    rt->cfg.recv_buf  = *buf_p;
    rt->cfg.recv_size = *size_p;
    rt->rx_len        = 0;                   /* 游标复位 */
    rt->overflow      = 0u;
    rt->scan          = 0;

    *buf_p  = tmp_buf;
    *size_p = tmp_size;
    *len_p  = tmp_len;
}

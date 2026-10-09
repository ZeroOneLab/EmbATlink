/**
 * @file    at_driver.h
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

#ifndef AT_DRIVER_H
#define AT_DRIVER_H

#include <stddef.h>
#include <stdint.h>
#include "at_port.h"

// clang-format off

/** AT 状态码 */
typedef enum {
    AT_OK              =  0,   /**< 成功                                */
    AT_ERR_GENERIC     = -1,   /**< 未知错误                            */
    AT_ERR_TIMEOUT     = -2,   /**< 超时，响应始终未收全                  */
    AT_ERR_NO_MATCH    = -3,   /**< 响应收全但未命中 expect               */
    AT_ERR_BUF_FULL    = -4,   /**< 缓冲不足：接收溢出，或 URC 记录过长     */
    AT_ERR_NO_BUFFER   = -5,   /**< 缓冲区未注册                         */
    AT_ERR_PARAM       = -6,   /**< 参数非法                            */
    AT_ERR_NOT_FOUND   = -7,   /**< 目标不存在：URC 缓存空                */
    AT_ERR_IO          = -8,   /**< 底层发送失败                         */
} at_status_t;

/**
 * AT 指令配置（命令行由调用方拼接，驱动不参与格式化）
 *
 * 位置初始化顺序：
 *   { cmd, cmd_len, suffix, expect, done_end, retry, poll_ms, timeout_ms }
 *
 * @note 发送分两段：先发 cmd 的 cmd_len 字节，再单独发一次 suffix。
 *       不拼接，因此行尾不必另备可写缓冲，字面量可直接传。
 */
typedef struct {
    const void *cmd;        /**< 命令主体：ASCII 命令行，或定长数据            */
    uint16_t    cmd_len;    /**< cmd 字节数（必填，> 0）                     */
    const char *suffix;     /**< 主体之后单独发送；"" 不发送，NULL 非法        */
    const char *expect;     /**< 期望响应关键字；NULL = 只发不等              */
    const char *done_end;   /**< 响应结束标记；expect 非 NULL 时必填且非空      */
    uint16_t    retry;      /**< 最大尝试次数（必填，>= 1）                  */
    uint32_t    poll_ms;    /**< 响应轮询间隔 (ms)                          */
    uint32_t    timeout_ms; /**< 单次等待超时 (ms)                          */
} at_cmd_config_t;

/** URC 记录描述 */
typedef struct {
    const char *head;       /**< 记录起点关键字，如 "+MQTT:"   */
    const char *tail;       /**< 记录结束串，如 "\r\n"        */
} at_urc_key_t;

/** 响应描述（调用方提供缓冲） */
typedef struct {
    uint8_t  *buf;          /**< 缓冲指针   */
    uint16_t  size;         /**< 缓冲容量   */
    uint16_t  len;          /**< 有效长度   */
} at_resp_t;

/**
 * AT 通道配置
 *
 * @note at_channel_init() 按值整体拷贝，之后入参可释放（缓冲存储须常驻）。
 *       urc_buf / urc_keys 同进同出，都不传即该通道无 URC 能力。
 */
typedef struct {
    uint8_t            *recv_buf;   /**< 命令与响应缓冲（必填）                  */
    uint16_t            recv_size;  /**< recv_buf 容量（必填，> 0）             */
    uint8_t            *urc_buf;    /**< URC 缓存，独立于 recv_buf（可选）       */
    uint16_t            urc_size;   /**< urc_buf 容量（须容纳一条最长记录体）    */
    const at_urc_key_t *urc_keys;   /**< URC 记录表（可选）                     */
    uint8_t             urc_count;  /**< URC 记录条数（提供 URC 时 >= 1）        */
} at_channel_t;

// clang-format on

/* ==================== 指令配置宏 ==================== */

/* 尾部与结束符由 _DEF 版本写死为 "\r\n"；retry / poll_ms / timeout_ms 一律显式传入。 */

/**
 * @brief   填一条 at_cmd_config_t，尾部与结束符显式给出
 *   AT_STR_CMD(cfg, "AT", "\r\n", "OK", "\r\n", 3, 20, 200);
 * @note    cmd_len 由 strlen() 取得，body 须为字符串表达式；
 *          实参各求值一次，不要在其中放副作用。
 */
#define AT_STR_CMD(cfg, body, tail, want, end, times, poll, wait)           \
    do {                                                                    \
        const char *_at_body = (body);                                      \
        (void)sizeof((cfg).cmd);                                            \
        (cfg).cmd        = _at_body;                                        \
        (cfg).cmd_len    = (uint16_t)strlen(_at_body);                      \
        (cfg).suffix     = (tail);                                          \
        (cfg).expect     = (want);                                          \
        (cfg).done_end   = (end);                                           \
        (cfg).retry      = (uint16_t)(times);                               \
        (cfg).poll_ms    = (poll);                                          \
        (cfg).timeout_ms = (wait);                                          \
    } while (0)

/**
 * @brief   字符串主体，尾部与结束符默认 "\r\n"
 *   AT_STR_CMD_DEF(cfg, "AT", "OK", 3, 20, 200);
 */
#define AT_STR_CMD_DEF(cfg, body, want, times, poll, wait) \
    AT_STR_CMD(cfg, body, "\r\n", want, "\r\n", times, poll, wait)

/**
 * @brief   定长数据主体（透传 / 裸发），长度由调用方给出
 *   AT_BIN_CMD(cfg, payload, sizeof(payload), "", "+MQTTPUB:OK", "\r\n", 1, 20, 500);
 * @note    无默认尾部版本：给数据补 "\r\n" 通常是错的。
 */
#define AT_BIN_CMD(cfg, data, data_len, tail, want, end, times, poll, wait) \
    do {                                                                    \
        (void)sizeof((cfg).cmd);                                            \
        (cfg).cmd        = (data);                                          \
        (cfg).cmd_len    = (uint16_t)(data_len);                            \
        (cfg).suffix     = (tail);                                          \
        (cfg).expect     = (want);                                          \
        (cfg).done_end   = (end);                                           \
        (cfg).retry      = (uint16_t)(times);                               \
        (cfg).poll_ms    = (poll);                                          \
        (cfg).timeout_ms = (wait);                                          \
    } while (0)

/**
 * @brief   只发不等（expect = NULL），固定等待后取回接收内容
 *   AT_STR_SEND_DEF(cfg, "AT+RST", 500);
 * @note    不声明结束符：收到多少内容就回多少（done_end = NULL）。
 */
#define AT_STR_SEND_DEF(cfg, body, wait) \
    AT_STR_CMD(cfg, body, "\r\n", NULL, NULL, 1, 20, wait)

/* ==================== 初始化 ==================== */

/**
 * @brief   初始化 AT 通道
 * @param   [in] channel: 通道号
 * @param   [in] cfg:     通道配置（按值拷贝，之后可释放）
 * @retval  AT_OK / AT_ERR_PARAM
 * @note    recv_buf 需容纳一次最长响应；urc_buf 需容纳一条最长 URC 记录体。
 */
at_status_t at_channel_init(uint8_t channel, const at_channel_t *cfg);

/* ==================== 指令收发 ==================== */

/**
 * @brief   开启 AT 会话锁（保护多步事务）
 * @param   [in] channel: 通道号
 * @retval  无
 */
void at_session_lock(uint8_t channel);

/**
 * @brief   关闭 AT 会话锁
 * @param   [in] channel: 通道号
 * @retval  无
 */
void at_session_unlock(uint8_t channel);

/**
 * @brief   按序执行一组 AT 指令（原子事务，整段重试）
 * @param   [in]  channel:      通道号
 * @param   [in]  seq:          指令序列
 * @param   [in]  seq_len:      序列条数
 * @param   [in]  retry:        整段重试次数（>= 1）
 * @param   [out] failed_index: 失败步骤下标（可 NULL；成功时为 seq_len）
 * @retval  AT_OK / 最后一次尝试的失败码 / AT_ERR_PARAM
 * @note    每轮从第一条重新开始，内部使用递归锁。
 */
at_status_t at_cmd_seq_exec(uint8_t channel, const at_cmd_config_t *seq,
                            uint8_t seq_len, uint8_t retry,
                            uint8_t *failed_index);

/**
 * @brief   发送一条指令并阻塞等待响应
 * @param   [in]  channel: 通道号
 * @param   [in]  config:  指令配置
 * @param   [out] resp:    响应出参（可 NULL）
 * @retval  AT_OK:             命中 expect
 * @retval  AT_ERR_TIMEOUT:    超时前响应未收全
 * @retval  AT_ERR_NO_MATCH:   响应收全但未命中 expect
 * @retval  AT_ERR_BUF_FULL:   接收缓冲溢出，响应被截断
 * @retval  AT_ERR_IO:         底层发送失败（不重试）
 * @retval  AT_ERR_PARAM / AT_ERR_NO_BUFFER
 * @note    先发 cmd 的 cmd_len 字节，再单独发 suffix（"" 表示不发）。
 *          expect 非 NULL 时必须声明 done_end；expect 为 NULL 时只等 timeout_ms。
 *          收发期间到达的 URC 记录会进 URC 缓存，但其文本仍留在 resp 里，
 *          按字段取值前需先定位自己的行。
 */
at_status_t at_cmd_exec(uint8_t channel, const at_cmd_config_t *config,
                        at_resp_t *resp);

/* ==================== 数据接收 & URC ==================== */

/**
 * @brief   向通道注入接收数据（中断 / DMA 回调 / 主循环轮询均可调用）
 * @param   [in] channel: 通道号
 * @param   [in] data:    数据指针（len 为 0 时可为 NULL）
 * @param   [in] len:     数据长度
 * @retval  AT_OK / AT_ERR_PARAM / AT_ERR_NO_BUFFER
 * @note    纯追加，无 AT 逻辑；放不下时写指针归零并置溢出标志。
 *          同一通道同一时刻只允许一个注入方。
 */
at_status_t at_recv_push(uint8_t channel, const uint8_t *data, uint16_t len);

/**
 * @brief   取一条指定表项的 URC 记录（该表项中最早的一条）
 * @param   [in]  channel:   通道号
 * @param   [in]  urc_index: 要取的表项下标（urc_keys 里的枚举值）
 * @param   [out] buf:       记录体输出缓冲
 * @param   [in,out] size:   传入 buf 容量，传出记录体长度（不含结尾 '\0'）
 * @retval  AT_OK:             取到记录体，已补 '\0'，长度写入 size
 * @retval  AT_ERR_NOT_FOUND:  该通道未启用 URC，或队列里没有本表项的记录
 * @retval  AT_ERR_BUF_FULL:   记录体不小于传入容量，该记录已丢弃（size 不变）
 * @retval  AT_ERR_PARAM:      buf/size 非法，或 urc_index 越界
 * @note    只摘除命中的记录（其后内容前移到该位置），别的表项的记录原位保留。
 */
at_status_t at_urc_get(uint8_t channel, uint8_t urc_index, uint8_t *buf,
                       uint16_t *size);

/**
 * @brief   提取第 N 个逗号分隔参数（0-based）
 * @param   [in]  resp:     数据源（只取 buf 与 len）
 * @param   [in]  line_key: 行首关键字，如 "+CIFSR:STAIP"；NULL = 不限行
 * @param   [in]  index:    参数序号
 * @param   [out] out_buf:  输出缓冲
 * @param   [in]  out_size: 输出缓冲大小
 * @retval  >=0:               实际字节数（不含 '\0'）
 * @retval  AT_ERR_PARAM / AT_ERR_NOT_FOUND
 * @note    参数区取命中行（line_key 为 NULL 时取数据开头）的第一个冒号之后；
 *          逗号分隔，\r\n 终止；双引号内的逗号不作分隔符。
 */
int16_t at_resp_param_get(const at_resp_t *resp, const char *line_key,
                          uint8_t index, char *out_buf, uint16_t out_size);

/* ==================== 缓冲区管理 ==================== */

/**
 * @brief   在带长度的缓冲中查找子串（缓冲无需 '\0' 结尾）
 * @param   [in] buf:     缓冲指针
 * @param   [in] buf_len: 缓冲长度
 * @param   [in] key:     关键字（'\0' 结尾）
 * @retval  关键字首地址，未找到或参数无效返回 NULL
 */
char *at_buf_strstr(const uint8_t *buf, uint16_t buf_len, const char *key);

/**
 * @brief   取 cmd 缓存的只读视图
 * @param   [in]  channel: 通道号
 * @param   [out] buf:     缓冲指针出参（可 NULL）
 * @param   [out] len:     已收字节数出参（可 NULL）
 * @retval  AT_OK / AT_ERR_PARAM / AT_ERR_NO_BUFFER
 * @note    内容可能夹带 URC 记录文本；指针在下次 at_cmd_exec / at_recv_buf_swap
 *          之前有效。
 */
at_status_t at_recv_get(uint8_t channel, uint8_t **buf, uint16_t *len);

/**
 * @brief   临时替换接收缓冲区（大响应场景复用内存）
 * @param   [in]     channel: 通道号
 * @param   [in,out] buf_p:   传入新缓冲，传出旧缓冲
 * @param   [in,out] size_p:  传入新容量，传出旧容量
 * @param   [in,out] len_p:   传入忽略，传出旧缓冲有效长度
 * @retval  无
 * @note    再次调用即恢复原缓冲；调用方须保证通道空闲。
 */
void at_recv_buf_swap(uint8_t channel, uint8_t **buf_p, uint16_t *size_p,
                      uint16_t *len_p);

#endif

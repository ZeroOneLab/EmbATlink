/**
 * @file    at_driver.h
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

#ifndef AT_DRIVER_H
#define AT_DRIVER_H

#include <stdint.h>
#include "at_port.h"

// clang-format off

/** AT 状态码 */
typedef enum {
    AT_OK              =  0,   /**< 执行成功                     */
    AT_ERR_GENERIC     = -1,   /**< 未知错误                     */
    AT_ERR_TIMEOUT     = -2,   /**< 等待响应超时                  */
    AT_ERR_NO_MATCH    = -3,   /**< 响应不匹配                   */
    AT_ERR_BUF_FULL    = -4,   /**< 缓冲区不足                   */
    AT_ERR_NO_BUFFER   = -5,   /**< 缓冲区未注册                  */
    AT_ERR_PARAM       = -6,   /**< 参数非法                     */
    AT_ERR_NOT_FOUND   = -7,   /**< 目标不存在（参数/关键字/响应行）*/
} at_status_t;

/** AT 指令配置（调用方拼接完整 cmd/expect，驱动不参与格式化） */
typedef struct {
    const char *cmd;        /**< 完整 AT 指令（调用方拼接，不含 \\r\\n）*/
    const char *expect;     /**< 期望响应关键字，NULL 表示仅发送           */
    uint16_t    retry;      /**< 最大尝试次数                             */
    uint16_t    poll_ms;    /**< 响应轮询间隔 (ms)                        */
    uint16_t    timeout_ms; /**< 单次等待超时 (ms)                        */
} at_cmd_config_t;

/** AT 通道（调用方初始化 buf/urc_keys，rx_len/channel 由驱动管理） */
typedef struct {
    /* 调用方初始化 */
    uint8_t          *recv_buf;  /**< 接收缓冲区指针（原始字节）      */
    const char *const *urc_keys; /**< URC 关键字表（NULL 不注册）   */
    uint16_t          recv_size; /**< 接收缓冲区大小                */
    uint8_t           urc_count; /**< URC 关键字数量                */
    /* 驱动内部维护 */
    uint16_t          rx_len;    /**< 接收数据长度                  */
    uint8_t           channel;   /**< 通道号                       */
} at_channel_t;

// clang-format on

/* ════════════════════════════════════════════════════════════════
 *  初始化
 * ════════════════════════════════════════════════════════════════ */

/**
 * @brief   初始化 AT 通道（注册 buf / URC）
 * @param   [in] channel:  通道号
 * @param   [in] cfg:      通道配置（指针，调用方持有）
 * @retval  AT_OK:         成功
 * @retval  AT_ERR_PARAM:  cfg 或 recv_buf 为 NULL
 */
at_status_t at_channel_init(uint8_t channel, const at_channel_t *cfg);

/* ════════════════════════════════════════════════════════════════
 *  指令收发（核心）
 * ════════════════════════════════════════════════════════════════ */

/**
 * @brief   发送完整 AT 命令行并阻塞等待响应
 * @param   [in]  channel:   通道号
 * @param   [in]  config:    指令配置（cmd/expect/retry/poll_ms/timeout_ms）
 * @retval  AT_OK:             成功
 * @retval  AT_ERR_PARAM:      config/cmd 为 NULL 或通道未初始化
 * @retval  AT_ERR_TIMEOUT:    等待响应超时
 * @retval  AT_ERR_NO_MATCH:   响应不匹配
 * @note    调用方负责拼接完整的 cmd 字符串（含参数）。驱动不参与格式化。
 *          expect 采用子串匹配，仅用于识别响应类型。
 *          需要响应数据时，调用 at_recv_get() 获取缓冲区指针。
 *          支持 compound literal: at_cmd_exec(0, &(at_cmd_config_t){"AT","OK",3,20,200});
 */
at_status_t at_cmd_exec(uint8_t channel, const at_cmd_config_t *config);

/**
 * @brief   开启 AT 会话锁（保护多步指令事务的原子性）
 * @param   [in] channel:  通道号
 * @retval  无
 * @note    必须与 at_session_unlock 成对调用。
 */
void at_session_lock(uint8_t channel);

/**
 * @brief   关闭 AT 会话锁
 * @param   [in] channel:  通道号
 * @retval  无
 */
void at_session_unlock(uint8_t channel);

/* ════════════════════════════════════════════════════════════════
 *  数据接收 & URC
 * ════════════════════════════════════════════════════════════════ */

/**
 * @brief   向通道注入接收数据（ISR / 轮询 / DMA 回调均可调用）
 * @param   [in] channel: 通道号
 * @param   [in] data:    数据指针
 * @param   [in] len:     数据长度
 * @retval  AT_OK:             成功
 * @retval  AT_ERR_BUF_FULL:  缓冲区不足
 * @retval  AT_ERR_NO_BUFFER: 缓冲区未注册
 * @note    纯 memcpy + rx_len 累加，零 AT 逻辑。
 *          裸机下可在主循环轮询 UART 后调用；RTOS 下在 ISR 中调用。
 */
at_status_t at_recv_push(uint8_t channel, const uint8_t *data, uint16_t len);

/**
 * @brief   检测指定 URC 关键字（只读，不修改状态）
 * @param   [in] channel:   通道号
 * @param   [in] urc_index: URC 关键字表下标
 * @retval  AT_OK:            缓冲区中存在该关键字
 * @retval  AT_ERR_NO_MATCH:  未命中或缓冲区未注册
 * @note    本函数只读不写，由调用者决定何时 at_recv_reset。
 */
at_status_t at_urc_check(uint8_t channel, uint8_t urc_index);

/**
 * @brief   从 AT 响应中提取第 N 个逗号分隔参数
 * @param   [in]  resp:      响应字符串
 * @param   [in]  resp_len:  响应字符串长度
 * @param   [in]  index:     参数序号（0-based，冒号后第 1 个参数 = 0）
 * @param   [out] out_buf:   输出缓冲区
 * @param   [in]  out_size:  输出缓冲区大小
 * @retval  >=0:               实际提取的字节数（不含 '\\0'）
 * @retval  AT_ERR_PARAM:      参数非法（NULL / 长度0 / out_size 不足）
 * @retval  AT_ERR_NOT_FOUND:  参数不存在（index 越界 / 响应无冒号 / 冒号后无内容）
 * @note    参数区以冒号后第一个非空字符开始，逗号分隔，\\r\\n 终止。
 *          双引号内的逗号不作为分隔符，支持 JSON / 字符串参数。
 *          提取时自动 trim 首尾空格，参数内容原样返回（含引号）。
 */
int16_t at_resp_param_get(const char *resp, uint16_t resp_len,
                          uint8_t index,
                          char *out_buf, uint16_t out_size);

/* ════════════════════════════════════════════════════════════════
 *  缓冲区管理
 * ════════════════════════════════════════════════════════════════ */

/**
 * @brief   获取接收缓冲区及有效数据长度
 * @param   [in]  channel:  通道号
 * @param   [out] buf:      接收缓冲区指针
 * @param   [out] len:      有效数据长度
 * @retval  AT_OK:             成功
 * @retval  AT_ERR_NO_BUFFER:  缓冲区未注册
 */
at_status_t at_recv_get(uint8_t channel, uint8_t **buf, uint16_t *len);

/**
 * @brief   重置接收缓冲区及状态
 * @param   [in] channel:  通道号
 * @retval  AT_OK:            成功
 * @retval  AT_ERR_NO_BUFFER: 缓冲区未注册
 */
at_status_t at_recv_reset(uint8_t channel);

/**
 * @brief   从接收缓冲区指定位置移除字节（后续数据前移填补空洞）
 * @param   [in] channel:  通道号
 * @param   [in] offset:   移除起始位置（相对缓冲区头部偏移）
 * @param   [in] bytes:    移除字节数
 * @retval  AT_OK:             成功
 * @retval  AT_ERR_NO_BUFFER:  缓冲区未注册
 * @retval  AT_ERR_PARAM:      offset + bytes 超过有效数据长度
 * @note    移除后，后续数据 memmove 前移填补空洞，尾部清零。
 */
at_status_t at_recv_remove(uint8_t channel, uint16_t offset, uint16_t bytes);

/**
 * @brief   临时替换接收缓冲区（供 OTA 等大数据场景复用内存）
 * @param   [in]     channel:   通道号
 * @param   [in,out] buf_p:     传入新缓冲区指针，传出旧缓冲区指针
 * @param   [in,out] size_p:    传入新缓冲区大小，传出旧缓冲区大小
 * @param   [in,out] len_p:     传入新缓冲区有效长度，传出旧缓冲区有效长度
 * @retval  无
 * @note    再次调用即可恢复原缓冲区。调用者保证新旧缓冲区在交换期间有效。
 *          rx_len 同步交换，避免 swap-back 后 len 与 size 不一致导致 OVERFLOW。
 */
void at_recv_buf_swap(uint8_t channel, uint8_t **buf_p, uint16_t *size_p, uint16_t *len_p);

#endif

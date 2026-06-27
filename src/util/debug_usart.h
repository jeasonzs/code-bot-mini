/**
 * @file    debug_usart.h
 * @brief   调试串口初始化 - USART3 on PC18/PC19 (DEBUG_IFACE=SERIAL 时)
 *
 * 注:
 *  - 默认 (DEBUG_IFACE=DEBUG_IFACE_SERIAL=1): 初始化 USART3 remap 到 PC18/PC19,
 *    115200 8N1, 双工 (Tx+Rx). 配套 libc_stubs.c 的 printf 走这里.
 *  - 选 SDI 模式 (DEBUG_IFACE=0): 函数为 no-op, printf 输出被丢弃
 *    (此时应用 WCH-LinkE 的 SDI 调试, 不通过串口).
 */

#ifndef DEBUG_USART_H
#define DEBUG_USART_H

#include "pinout.h"

#if (DEBUG_IFACE == DEBUG_IFACE_SERIAL)
void Debug_USART_Init(void);
#else
static inline void Debug_USART_Init(void) {
    /* DEBUG_IFACE == SDI: 不初始化 USART3, printf 无输出 */
}
#endif

#endif /* DEBUG_USART_H */

/**
 * @file    libc_stubs.c
 * @brief   libc 桩函数 - 解决 -nostdlib 下 WCH SDK 对 memcpy / printf 的引用
 *
 * WCH USB 驱动使用 memcpy, 项目代码用 printf, 但我们用 -nostdlib 禁用了 newlib.
 * memcpy: 字节级 (USB 数据量小, 不需 SIMD 优化)
 * printf: 最小化实现 (%% %c %s %d %x), 输出到 DEBUG_USART (默认 USART3 on PC18)
 *
 * 注: TSSOP-20 封装没有 PB10/PB11, USART1 默认 TX 不可用.
 *     pinout.h 中 DEBUG_USART 宏根据 DEBUG_IFACE 选择 USART3 (SERIAL 模式).
 */

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include "ch32x035.h"
#include "pinout.h"     /* DEBUG_USART, DEBUG_USART_GPIO_PORT ... */

/* 底层: 发送一个字符到 DEBUG_USART (SERIAL 模式下为 USART3 on PC18) */
static void putc(char c) {
#if (DEBUG_IFACE == DEBUG_IFACE_SERIAL)
    if (c == '\n') {
        USART_SendData(DEBUG_USART, '\r');
        while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TC) == RESET);
    }
    USART_SendData(DEBUG_USART, (uint8_t)c);
    while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TC) == RESET);
#else
    /* DEBUG_IFACE == SDI: printf 输出丢弃, 用 WCH-LinkE SDI 调试 */
    (void)c;
#endif
}

/* 数字转字符串 (无除法版, 节省 code size) */
static void print_uint(uint32_t val, unsigned base, int is_signed) {
    char buf[12];
    int i = 0;
    int neg = 0;

    if (is_signed && (int32_t)val < 0) {
        neg = 1;
        val = -(int32_t)val;
    }

    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            unsigned d = val % base;
            buf[i++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
            val /= base;
        }
    }
    if (neg) putc('-');
    while (i--) putc(buf[i]);
}

static int vprintf_impl(const char *fmt, va_list ap) {
    while (*fmt) {
        if (*fmt != '%') {
            putc(*fmt++);
            continue;
        }
        fmt++;
        /* 简易: 不支持 width/precision/length, 只支持格式字符 */
        switch (*fmt++) {
            case 'c': {
                char c = (char)va_arg(ap, int);
                putc(c);
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                while (*s) putc(*s++);
                break;
            }
            case 'd':
            case 'i': print_uint(va_arg(ap, int), 10, 1); break;
            case 'u': print_uint(va_arg(ap, unsigned), 10, 0); break;
            case 'x':
            case 'X': print_uint(va_arg(ap, unsigned), 16, 0); break;
            case 'p': {
                putc('0'); putc('x');
                print_uint((uintptr_t)va_arg(ap, void *), 16, 0);
                break;
            }
            case '%': putc('%'); break;
            default:  /* 未知: 原样输出 */ putc('%'); putc(fmt[-1]); break;
        }
    }
    return 0;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vprintf_impl(fmt, ap);
    va_end(ap);
    return ret;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *memset(void *dest, int c, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    while (n--) {
        *d++ = (unsigned char)c;
    }
    return dest;
}
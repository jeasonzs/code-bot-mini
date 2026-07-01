/**
 * @file    ringbuf.h
 * @brief   通用环形缓冲区 (用于 USB / CDC 收发)
 *
 * 单生产者单消费者, 线程不安全 (中断 + 主循环)
 * 必须 ringbuf_lock() 保护, 或者只在中断 / 主循环 单边访问
 */

#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *buf;
    uint16_t size;     /* 缓冲区大小 (必须是 2 的幂以使用位运算加速, 但本实现不强求) */
    volatile uint16_t head;  /* 写位置 */
    volatile uint16_t tail;  /* 读位置 */
} ringbuf_t;

/* 初始化环形缓冲区 (静态分配) */
void ringbuf_init(ringbuf_t *rb, uint8_t *storage, uint16_t size);

/* 写入: 返回实际写入字节数 */
uint16_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, uint16_t len);

/* 读取: 返回实际读取字节数 */
uint16_t ringbuf_read(ringbuf_t *rb, uint8_t *data, uint16_t len);

/* 反悔: 把 tail 往回退 len 字节 (前提是 head 还没追上原 tail).
 * 用于 read 之后, 下游消费失败的回滚. 返回实际回退字节数. */
uint16_t ringbuf_unread(ringbuf_t *rb, uint16_t len);

/* 查看但不移除一个字节 (peek) */
int ringbuf_peek(ringbuf_t *rb, uint8_t *byte);

/* 当前可用数据长度 */
uint16_t ringbuf_available(const ringbuf_t *rb);

/* 当前剩余空间 */
uint16_t ringbuf_space(const ringbuf_t *rb);

/* 是否为空 */
int ringbuf_is_empty(const ringbuf_t *rb);

/* 是否已满 */
int ringbuf_is_full(const ringbuf_t *rb);

/* 清空 */
void ringbuf_clear(ringbuf_t *rb);

#endif /* RINGBUF_H */

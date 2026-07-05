/**
 * @file    ringbuf.c
 * @brief   环形缓冲区实现
 */

#include "ringbuf.h"

void ringbuf_init(ringbuf_t *rb, uint8_t *storage, uint16_t size) {
    rb->buf = storage;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
}

uint16_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, uint16_t len) {
    uint16_t written = 0;
    while (written < len && !ringbuf_is_full(rb)) {
        rb->buf[rb->head] = data[written];
        rb->head = (rb->head + 1) % rb->size;
        written++;
    }
    return written;
}

uint16_t ringbuf_read(ringbuf_t *rb, uint8_t *data, uint16_t len) {
    uint16_t read = 0;
    while (read < len && !ringbuf_is_empty(rb)) {
        data[read] = rb->buf[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
        read++;
    }
    return read;
}

uint16_t ringbuf_unread(ringbuf_t *rb, uint16_t len) {
    /* 单消费者场景: read 之后 head 没动, 把 tail 倒退 len 即可, 不会撞 head.
     * 只在 len > 0 时操作; 超过 size 的部分截断. */
    if (len == 0) return 0;
    if (len >= rb->size) len = rb->size - 1;
    rb->tail = (rb->tail + rb->size - (len % rb->size)) % rb->size;
    return len;
}

int ringbuf_peek(ringbuf_t *rb, uint8_t *byte) {
    if (ringbuf_is_empty(rb)) return -1;
    *byte = rb->buf[rb->tail];
    return 0;
}

int ringbuf_peek_at(const ringbuf_t *rb, uint16_t offset, uint8_t *byte) {
    if (rb == NULL || byte == NULL) return -1;
    if (offset >= ringbuf_available(rb)) return -1;
    uint16_t idx = (rb->tail + offset) % rb->size;
    *byte = rb->buf[idx];
    return 0;
}

uint16_t ringbuf_available(const ringbuf_t *rb) {
    return (rb->size - 1 - ringbuf_space(rb));
}

uint16_t ringbuf_space(const ringbuf_t *rb) {
    if (rb->head >= rb->tail) {
        return rb->size - 1 - (rb->head - rb->tail);
    } else {
        return rb->tail - rb->head - 1;
    }
}

int ringbuf_is_empty(const ringbuf_t *rb) {
    return rb->head == rb->tail;
}

int ringbuf_is_full(const ringbuf_t *rb) {
    return ((rb->head + 1) % rb->size) == rb->tail;
}

void ringbuf_clear(ringbuf_t *rb) {
    rb->head = 0;
    rb->tail = 0;
}

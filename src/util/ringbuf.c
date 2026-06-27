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

int ringbuf_peek(ringbuf_t *rb, uint8_t *byte) {
    if (ringbuf_is_empty(rb)) return -1;
    *byte = rb->buf[rb->tail];
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

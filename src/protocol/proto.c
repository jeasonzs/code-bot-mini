/**
 * @file    proto.c
 * @brief   协议层实现
 */

#include "proto.h"
#include "util/ringbuf.h"
#include "display/lcd_driver.h"
#include "usb/usb_endp.h"
#include "usb/hid_kbd.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* 接收 buffer: USB EP1 OUT (Vendor Bulk) -> 协议解析 */
#define RX_BUF_SIZE   512
static uint8_t  s_rx_storage[RX_BUF_SIZE];
static ringbuf_t s_rx_rb;
static uint8_t  s_frame_buf[PROTO_MAX_FRAMESIZE];

/* ===== 状态 ===== */
static uint32_t g_status = 0x01;  /* bit 0: ready */

/* ===== CRC16-CCITT ===== */
uint16_t proto_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else              crc = (crc << 1);
        }
    }
    return crc;
}

/* ===== 发送帧 (到主机) ===== */
static void proto_send_frame(uint8_t cmd, const uint8_t *payload, uint16_t len) {
    proto_header_t hdr;
    hdr.magic = PROTO_MAGIC;
    hdr.version = PROTO_VERSION;
    hdr.cmd = cmd;
    hdr.flags = 0;
    hdr.length = len;

    /* CRC over header + payload */
    uint8_t crc_buf[PROTO_HEADER_SIZE + PROTO_MAX_PAYLOAD];
    size_t hdr_size = PROTO_HEADER_SIZE - PROTO_CRC_SIZE;  /* magic..length, 6B */
    memcpy(crc_buf, &hdr, hdr_size);
    if (payload && len > 0) memcpy(crc_buf + hdr_size, payload, len);
    hdr.crc16 = proto_crc16(crc_buf, hdr_size + len);

    /* 构造完整帧 (header + payload) */
    uint16_t frame_len = (uint16_t)hdr_size + len;
    uint8_t frame[PROTO_HEADER_SIZE + PROTO_MAX_PAYLOAD];
    memcpy(frame, &hdr, hdr_size);
    if (payload && len > 0) memcpy(frame + hdr_size, payload, len);

    /* 发送: 通过 Vendor EP2 IN */
    Vendor_SendFrame(frame, frame_len);
}

/* ===== 接收帧处理 ===== */
static void handle_cmd_draw_rects(const uint8_t *payload, uint16_t len) {
    /* 格式: [count] [rect0: x,y,w,h, pixels] [rect1] ... */
    if (len < 1) return;
    uint8_t count = payload[0];
    uint16_t off = 1;
    for (uint8_t i = 0; i < count && off < len; i++) {
        if (off + 8 > len) break;
        uint16_t x = payload[off] | (payload[off+1] << 8);
        uint16_t y = payload[off+2] | (payload[off+3] << 8);
        uint16_t w = payload[off+4] | (payload[off+5] << 8);
        uint16_t h = payload[off+6] | (payload[off+7] << 8);
        off += 8;
        uint32_t pixels_size = (uint32_t)w * h * 2;
        if (off + pixels_size > len) break;
        LCD_DrawRect(x, y, w, h, &payload[off]);
        off += pixels_size;
    }
}

static void handle_cmd_hid_keystrokes(const uint8_t *payload, uint16_t len) {
    /* 格式: [delay_ms] [count] [count×8B HID reports] */
    if (len < 2) return;
    uint8_t delay_ms = payload[0];
    uint8_t count = payload[1];
    if (len < 2 + (uint16_t)count * 8) return;
    HID_Kbd_EnqueueReports(&payload[2], count, delay_ms);
}

static void handle_cmd_clear(const uint8_t *payload, uint16_t len) {
    if (len < 2) return;
    uint16_t color = payload[0] | (payload[1] << 8);
    LCD_Clear(color);
}

static void handle_cmd_set_brightness(const uint8_t *payload, uint16_t len) {
    if (len < 1) return;
    uint8_t pct = payload[0];
    if (pct > 100) pct = 100;
    LCD_BL_SetBrightness(pct);
}

static void handle_cmd_ping(void) {
    proto_send_frame(CMD_PONG, NULL, 0);
}

static void dispatch_frame(const proto_header_t *hdr, const uint8_t *payload) {
    switch (hdr->cmd) {
        case CMD_PING:
            handle_cmd_ping();
            break;
        case CMD_RESET_DISPLAY:
            LCD_Reinit();
            break;
        case CMD_SET_BRIGHTNESS:
            handle_cmd_set_brightness(payload, hdr->length);
            break;
        case CMD_CLEAR:
            handle_cmd_clear(payload, hdr->length);
            break;
        case CMD_DRAW_RECTS:
            handle_cmd_draw_rects(payload, hdr->length);
            break;
        case CMD_HID_KEYSTROKES:
            handle_cmd_hid_keystrokes(payload, hdr->length);
            break;
        default:
            /* 忽略未知命令 */
            break;
    }
}

static void process_rx_data(void) {
    /* 找帧头: 0xCB 0x02 */
    while (ringbuf_available(&s_rx_rb) >= PROTO_HEADER_SIZE) {
        /* peek first byte */
        uint8_t b;
        if (ringbuf_peek(&s_rx_rb, &b) < 0) return;
        if (b != PROTO_MAGIC) {
            /* 跳过非魔数字节 */
            uint8_t discard;
            ringbuf_read(&s_rx_rb, &discard, 1);
            continue;
        }

        /* 读取 8B 头 */
        if (ringbuf_available(&s_rx_rb) < PROTO_HEADER_SIZE) return;
        uint8_t hdr_bytes[PROTO_HEADER_SIZE];
        ringbuf_read(&s_rx_rb, hdr_bytes, PROTO_HEADER_SIZE);

        proto_header_t hdr;
        memcpy(&hdr, hdr_bytes, PROTO_HEADER_SIZE);

        /* 验证版本 */
        if (hdr.version != PROTO_VERSION) continue;

        uint16_t payload_len = hdr.length;
        if (payload_len > PROTO_MAX_PAYLOAD) continue;

        /* 等待 payload + 2B CRC */
        if (ringbuf_available(&s_rx_rb) < payload_len + PROTO_CRC_SIZE) {
            /* 数据未到齐, 放回头部 (简化: 丢弃) */
            /* TODO: 实现可重入的流式解析 */
            continue;
        }

        /* 读 payload + CRC */
        ringbuf_read(&s_rx_rb, s_frame_buf, payload_len);
        uint16_t recv_crc;
        ringbuf_read(&s_rx_rb, (uint8_t *)&recv_crc, 2);

        /* 验证 CRC (header + payload) */
        uint8_t crc_data[PROTO_HEADER_SIZE - PROTO_CRC_SIZE + PROTO_MAX_PAYLOAD];
        size_t crc_data_len = (PROTO_HEADER_SIZE - PROTO_CRC_SIZE) + payload_len;
        memcpy(crc_data, hdr_bytes, PROTO_HEADER_SIZE - PROTO_CRC_SIZE);
        memcpy(crc_data + PROTO_HEADER_SIZE - PROTO_CRC_SIZE, s_frame_buf, payload_len);
        uint16_t calc_crc = proto_crc16(crc_data, crc_data_len);
        if (calc_crc != recv_crc) {
            /* CRC 错, 丢弃 */
            continue;
        }

        dispatch_frame(&hdr, s_frame_buf);
    }
}

/* ===== API ===== */

void Protocol_Init(void) {
    ringbuf_init(&s_rx_rb, s_rx_storage, RX_BUF_SIZE);
    g_status = 0x01;  /* ready */
}

void Protocol_Poll(void) {
    /* 喂入 USB 收到的数据 (由 USB 库调用 Protocol_RxByte) */
    process_rx_data();
}

void Protocol_SendPong(void) {
    pong_payload_t pong = { .status = g_status };
    proto_send_frame(CMD_PONG, (uint8_t *)&pong, sizeof(pong));
}

void Protocol_SendTouchEvent(uint8_t event_type, uint16_t x, uint16_t y) {
    touch_event_payload_t ev = { .event_type = event_type, .x = x, .y = y };
    proto_send_frame(CMD_TOUCH_EVENT, (uint8_t *)&ev, sizeof(ev));
}

void Protocol_SendLog(const char *fmt, ...) {
    /* 通过 CDC 串口发送 (printf 已经直接走 CDC) */
    /* 这里可以额外发一份到 Vendor EP2 IN 让 host 写到文件 */
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0) {
        proto_send_frame(CMD_LOG, (uint8_t *)buf, (uint16_t)len);
    }
}

/* 由 USB EP1 OUT 回调调用, 喂入协议层 */
void Protocol_RxByte(uint8_t b) {
    ringbuf_write(&s_rx_rb, &b, 1);
}

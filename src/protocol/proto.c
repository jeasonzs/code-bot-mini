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

/* 接收 buffer: USB EP1 OUT (Vendor Bulk) -> 协议解析
 * 4KB 容纳 ~12 帧 (每帧 338B), 抵御 LCD_DrawRect 慢场景下的 USB 突发 */
#define RX_BUF_SIZE   4096
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

    /* [DBG] 入口检查, 判断是否被调用 */
    extern volatile uint32_t g_ticks_ms;
    // printf("[DBG] proto_send_frame cmd=%d len=%d tick=%d\n",
    //        cmd, (int)len, (int)g_ticks_ms);

    /* CRC over header + payload */
    uint8_t crc_buf[PROTO_HEADER_SIZE + PROTO_MAX_PAYLOAD];
    size_t hdr_size = PROTO_HEADER_SIZE - PROTO_CRC_SIZE;  /* magic..length, 6B */
    memcpy(crc_buf, &hdr, hdr_size);
    if (payload && len > 0) memcpy(crc_buf + hdr_size, payload, len);
    hdr.crc16 = proto_crc16(crc_buf, hdr_size + len);

    /* 构造完整帧 (header + payload + CRC) */
    uint16_t frame_len = (uint16_t)hdr_size + len + PROTO_CRC_SIZE;
    uint8_t frame[PROTO_HEADER_SIZE + PROTO_MAX_PAYLOAD];
    memcpy(frame, &hdr, PROTO_HEADER_SIZE);              /* 完整 8B header, 含 CRC */
    if (payload && len > 0) memcpy(frame + PROTO_HEADER_SIZE, payload, len);

    /* 发送: 通过 Vendor EP2 IN */
    int snd_rc = Vendor_SendFrame(frame, frame_len);
    // printf("[DBG] Vendor_SendFrame -> %d  frame_len=%d  cmd=%d\n",
    //        snd_rc, (int)frame_len, cmd);
}

/* ===== 接收帧处理 ===== */
static void handle_cmd_draw_rects(const uint8_t *payload, uint16_t len) {
    /* 格式: [count:2B] [rect0: x,y,w,h, pixels] [rect1] ...
     * count 是 uint16_t (2 字节) 让像素数据落在偶地址,
     * 避免 CH32X035 RISC-V misaligned lhu exception. */
    if (len < 2) return;
    uint16_t count = payload[0] | (payload[1] << 8);
    uint16_t off = 2;

    for (uint16_t i = 0; i < count && off < len; i++) {
        if (off + 8 > len) break;
        uint16_t x = payload[off] | (payload[off+1] << 8);
        uint16_t y = payload[off+2] | (payload[off+3] << 8);
        uint16_t w = payload[off+4] | (payload[off+5] << 8);
        uint16_t h = payload[off+6] | (payload[off+7] << 8);
        off += 8;
        uint32_t pixels_size = (uint32_t)w * h * 2;
        if (off + pixels_size > len) break;
        /* host 端按 LE 发送 RGB565 像素 (与 MCU 一致), 驱动内部 swap 成 BE 发给 GC9307 */
        LCD_DrawRect(x, y, w, h, (const lcd_color_t *)&payload[off]);
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

/* ===== 流式解析状态机 =====
 *
 * 每字节只处理一次, 每个状态转移 = 一次 printf (便于调试不爆).
 * 状态: ST_MAGIC -> ST_HEADER -> ST_PAYLOAD -> (dispatch / reset) -> ST_MAGIC
 *
 * 错误恢复: 一律 reset 到 ST_MAGIC, 丢弃已收集的部分字节.
 * 这样最坏情况丢 ≤ 8 字节 (header), 不试图在 hdr[] 里二次找 magic (简单).
 */
typedef enum {
    ST_MAGIC = 0,
    ST_HEADER,
    ST_PAYLOAD,
} parse_state_t;

static parse_state_t s_state = ST_MAGIC;
static uint8_t  s_hdr[PROTO_HEADER_SIZE];
static uint16_t s_pl_idx;     /* 已在 s_frame_buf / s_hdr 收集的字节数 */
static uint16_t s_pl_need;    /* payload 期望字节数 (= hdr.length) */

static inline void parser_reset(void) {
    s_state = ST_MAGIC;
    s_pl_idx = 0;
    s_pl_need = 0;
}

static void process_rx_data(void) {
    extern volatile uint32_t g_ticks_ms;

    while (1) {
        uint8_t b;
        if (ringbuf_read(&s_rx_rb, &b, 1) != 1) return;   /* ringbuf 空, 等 */

        switch (s_state) {
        case ST_MAGIC:
            if (b == PROTO_MAGIC) {
                s_hdr[0] = b;
                s_pl_idx = 1;
                s_state = ST_HEADER;
                // printf("[RX] magic ok tick=%d\n", (int)g_ticks_ms);
            }
            /* 非 magic 直接丢弃, 不打印 (会爆) */
            break;

        case ST_HEADER:
            s_hdr[s_pl_idx++] = b;
            if (s_pl_idx < PROTO_HEADER_SIZE) break;     /* 还没收齐 */

            /* 8B header 收齐, 验证 */
            proto_header_t hdr;
            memcpy(&hdr, s_hdr, PROTO_HEADER_SIZE);
            if (hdr.version != PROTO_VERSION) {
                printf("[RX] bad ver=%d cmd=%d len=%d, reset\n",
                       (int)hdr.version, (int)hdr.cmd, (int)hdr.length);
                parser_reset();
                break;
            }
            if (hdr.length > PROTO_MAX_PAYLOAD) {
                printf("[RX] bad len=%d > MAX=%d, reset\n",
                       (int)hdr.length, (int)PROTO_MAX_PAYLOAD);
                parser_reset();
                break;
            }
            s_pl_need = hdr.length;
            s_pl_idx = 0;
            s_state = ST_PAYLOAD;
            // printf("[RX] hdr ok cmd=%d len=%d tick=%d\n",
            //        (int)hdr.cmd, (int)s_pl_need, (int)g_ticks_ms);
            break;

        case ST_PAYLOAD:
            s_frame_buf[s_pl_idx++] = b;
            if (s_pl_idx < s_pl_need) break;             /* 还没收齐 */

            /* payload 收齐, CRC + dispatch */
            {
                proto_header_t hdr;
                memcpy(&hdr, s_hdr, PROTO_HEADER_SIZE);
                uint8_t crc_data[PROTO_HEADER_SIZE - PROTO_CRC_SIZE + PROTO_MAX_PAYLOAD];
                size_t crc_data_len = (PROTO_HEADER_SIZE - PROTO_CRC_SIZE) + s_pl_need;
                memcpy(crc_data, s_hdr, PROTO_HEADER_SIZE - PROTO_CRC_SIZE);
                memcpy(crc_data + PROTO_HEADER_SIZE - PROTO_CRC_SIZE, s_frame_buf, s_pl_need);
                uint16_t calc_crc = proto_crc16(crc_data, crc_data_len);
                if (calc_crc != hdr.crc16) {
                    printf("[RX] CRC fail cmd=%d calc=%d field=%d, reset\n",
                           (int)hdr.cmd, (int)calc_crc, (int)hdr.crc16);
                } else {
                    // printf("[RX] dispatch cmd=%d len=%d tick=%d\n",
                    //        (int)hdr.cmd, (int)s_pl_need, (int)g_ticks_ms);
                    dispatch_frame(&hdr, s_frame_buf);
                }
            }
            parser_reset();
            break;
        }
    }
}

/* ===== API ===== */

void Protocol_Init(void) {
    ringbuf_init(&s_rx_rb, s_rx_storage, RX_BUF_SIZE);
    parser_reset();
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

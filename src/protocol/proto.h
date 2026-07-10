/**
 * @file    proto.h
 * @brief   线缆协议层 v3 - 简化帧 + 物理隔离 EP1/EP4
 *
 * 对应协议: protocol/protocol.md (v3)
 *
 * 拓扑:
 *   - EP1 OUT (control): 1B cmd + packed struct (≤ 64B 单包, 整包 = 1 个命令)
 *   - EP2 IN  (vendor):  1B cmd + packed struct (响应: PONG/TOUCH/LOG)
 *   - EP3 IN  (HID):     8B boot keyboard report (标准 USB HID, 不走这里)
 *   - EP4 OUT (image):   raw RGB565 像素流 (无协议, 由 lcd_driver 直通 SPI DMA)
 *
 * 帧格式极简: 无 magic, 无 version, 无 length, 无 CRC (USB 硬件已有 CRC).
 * 状态机彻底删除: Protocol_Poll 直接遍历 EP1 OUT ring buffer slot 派发.
 */

#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>
#include <stddef.h>

/* ===== 命令 ID (EP1 OUT 主机→设备) ===== */
typedef enum {
    CMD_PING              = 0x01,   /* 0B payload */
    CMD_RESET_DISPLAY     = 0x10,   /* 0B payload */
    CMD_SET_BRIGHTNESS    = 0x11,   /* 1B: brightness */
    CMD_CLEAR             = 0x12,   /* 2B: RGB565 color (LE) */
    CMD_DRAW_RECT_BEGIN   = 0x20,   /* 8B: {x:2,y:2,w:2,h:2} (LE) - 打开 EP4 OUT 数据通道 */
    CMD_DRAW_RECT_END     = 0x21,   /* 0B payload - 礼貌结束, 字节数到齐才是真结束 */
    CMD_DRAW_RECT_ABORT   = 0x22,   /* 0B payload - 强制结束, 丢剩余 */
} proto_cmd_t;

/* ===== 命令 ID (EP2 IN 设备→主机) ===== */
/* 复用同一 enum (PONG/TOUCH_EVENT/LOG) 区分方向用前缀函数名 */
#define CMD_PONG              0x02
#define CMD_TOUCH_EVENT       0x30
#define CMD_LOG               0xF0

/* ===== 触摸事件类型 ===== */
typedef enum {
    TOUCH_EVENT_DOWN              = 0,
    TOUCH_EVENT_MOVE              = 1,
    TOUCH_EVENT_UP                = 2,
    TOUCH_EVENT_SWIPE_LEFT        = 3,
    TOUCH_EVENT_SWIPE_RIGHT       = 4,
    TOUCH_EVENT_LONG_PRESS        = 5,
    TOUCH_EVENT_LONG_PRESS_RELEASE = 6,
    TOUCH_EVENT_DOUBLE_CLICK      = 7,
} touch_event_t;

/* ===== EP1 OUT 命令 packed struct ===== */
typedef struct __attribute__((packed)) {
    uint8_t  cmd;            /* = CMD_SET_BRIGHTNESS */
    uint8_t  brightness;     /* 0~100 */
} cmd_set_brightness_t;     /* 2B */

typedef struct __attribute__((packed)) {
    uint8_t  cmd;            /* = CMD_CLEAR */
    uint16_t color;          /* RGB565 LE */
} cmd_clear_t;              /* 3B */

typedef struct __attribute__((packed)) {
    uint8_t  cmd;            /* = CMD_DRAW_RECT_BEGIN */
    uint16_t x, y;           /* 矩形左上角 (LE) */
    uint16_t w, h;           /* 矩形宽高 (LE) */
} cmd_draw_rect_begin_t;    /* 9B */

/* ===== EP2 IN 响应 packed struct ===== */
typedef struct __attribute__((packed)) {
    uint8_t  cmd;            /* = CMD_PONG */
    uint32_t status;         /* bit 0: ready, bit 1: touch, bit 2: hid, bit 3: error */
} cmd_pong_t;               /* 5B */

typedef struct __attribute__((packed)) {
    uint8_t  cmd;            /* = CMD_TOUCH_EVENT */
    uint8_t  event_type;     /* touch_event_t */
    uint16_t x, y;           /* 坐标 (LE) */
} cmd_touch_event_t;        /* 6B */

typedef struct __attribute__((packed)) {
    uint8_t  cmd;            /* = CMD_LOG */
    char     text[63];       /* ASCII, NUL 截断 (实际长度由 Vendor_SendFrame 的 len 参数决定) */
} cmd_log_t;                 /* 64B (固定大小, 实际用 len 截) */

/* ===== API ===== */

/* 初始化协议层 (清状态) */
void Protocol_Init(void);

/* 主循环调用: 消费 EP1 OUT ring buffer slot, 派发命令 */
void Protocol_Poll(void);

/* 主循环调用: 消费 EP5 OUT ring buffer slot, 推像素到 LCD (跟 Protocol_Poll 同构) */
void Protocol_PollPixels(void);

/* 主动发送 PONG (1Hz 心跳) */
void Protocol_SendPong(void);

/* 主动发送触摸事件 (设备 → 主机) */
void Protocol_SendTouchEvent(uint8_t event_type, uint16_t x, uint16_t y);

/* 主动发送日志 (设备 → 主机) */
void Protocol_SendLog(const char *fmt, ...);

#endif /* PROTO_H */

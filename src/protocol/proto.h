/**
 * @file    proto.h
 * @brief   线缆协议层 - 帧解析 + 命令分发
 *
 * 对应协议: protocol/protocol.md
 */

#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>
#include <stddef.h>

/* ===== 协议常量 ===== */
#define PROTO_MAGIC          0xCB
#define PROTO_VERSION        0x02
#define PROTO_HEADER_SIZE    8
#define PROTO_CRC_SIZE       2
#define PROTO_MAX_PAYLOAD    512  /* 单帧最大 payload */
#define PROTO_MAX_FRAMESIZE  (PROTO_HEADER_SIZE + PROTO_MAX_PAYLOAD + PROTO_CRC_SIZE)

/* ===== 命令 ID ===== */
typedef enum {
    CMD_PING              = 0x01,
    CMD_PONG              = 0x02,
    CMD_RESET_DISPLAY     = 0x10,
    CMD_SET_BRIGHTNESS    = 0x11,
    CMD_CLEAR             = 0x12,
    CMD_DRAW_RECTS        = 0x20,
    CMD_TOUCH_EVENT       = 0x30,
    CMD_HID_KEYSTROKES    = 0x40,
    CMD_LOG               = 0xF0,
} proto_cmd_t;

/* ===== 触摸事件类型 ===== */
typedef enum {
    TOUCH_EVENT_DOWN             = 0,
    TOUCH_EVENT_MOVE             = 1,
    TOUCH_EVENT_UP               = 2,
    TOUCH_EVENT_SWIPE_LEFT       = 3,
    TOUCH_EVENT_SWIPE_RIGHT      = 4,
    TOUCH_EVENT_LONG_PRESS       = 5,
    TOUCH_EVENT_LONG_PRESS_RELEASE = 6,
} touch_event_t;

/* ===== 帧头结构 ===== */
typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint8_t  version;
    uint8_t  cmd;
    uint8_t  flags;
    uint16_t length;
    uint16_t crc16;
} proto_header_t;

/* ===== 触摸事件 payload ===== */
typedef struct __attribute__((packed)) {
    uint8_t  event_type;
    uint16_t x;
    uint16_t y;
} touch_event_payload_t;

/* ===== HID keystrokes payload 头部 ===== */
typedef struct __attribute__((packed)) {
    uint8_t delay_ms;
    uint8_t count;
} hid_keystrokes_header_t;

/* ===== 设备状态 (CMD_PONG payload 4B) ===== */
typedef struct __attribute__((packed)) {
    uint32_t status;     /* bit 0: ready, bit 1: touch, bit 2: hid, bit 3: error */
} pong_payload_t;

/* ===== API ===== */

/* 初始化协议层 (分配接收 buffer 等) */
void Protocol_Init(void);

/* 主循环调用: 处理 USB OUT 收到的命令 */
void Protocol_Poll(void);

/* 喂入一个收到的字节 (USB EP1 OUT 回调调用) */
void Protocol_RxByte(uint8_t byte);

/* 主动发送 PONG (1Hz) */
void Protocol_SendPong(void);

/* 主动发送触摸事件 (设备 → 主机) */
void Protocol_SendTouchEvent(uint8_t event_type, uint16_t x, uint16_t y);

/* 主动发送日志 (设备 → 主机, 主要通过 CDC) */
void Protocol_SendLog(const char *fmt, ...);

/* CRC16-CCITT 计算 (init=0xFFFF, poly=0x1021, no reflect) */
uint16_t proto_crc16(const uint8_t *data, size_t len);

#endif /* PROTO_H */

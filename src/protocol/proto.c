/**
 * @file    proto.c
 * @brief   线缆协议层 v3 - 简化帧 + 物理隔离 EP1/EP5
 *
 * 关键变化 (vs v0.17):
 *   - 删 4KB s_rx_storage (s_rx_rb, s_frame_buf, s_hdr)
 *   - 删 8B 帧头 (magic/ver/cmd/flags/len/crc) 和 522B max frame
 *   - 删字节流状态机
 *   - 删 proto_crc16
 *   - Protocol_Poll 直接遍历 Data_Buffer[DealPtr*64] slot, 按 slot[0] cmd 派发
 *   - back-pressure 翻 ACK 由 Protocol_Poll 完成
 *   - 图像数据走 EP5 OUT, 由 lcd_driver 内部直通 SPI DMA, proto.c 只触发 Begin/End
 */

#include "proto.h"
#include "ch32x035_conf.h"
#include "usb/ch32x035_usbfs_device.h"  /* RingBuffer_Comm, Data_Buffer, USBFSD */
#include "display/lcd_driver.h"
#include "usb/usb_endp.h"            /* Vendor_SendFrame */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ===== 设备状态 ===== */
static uint32_t g_status = 0x01;  /* bit 0: ready */

/* ============================================================== */
/* 内部: 通过 EP2 IN 发送 cmd + struct                              */
/* ============================================================== */
static int proto_send(uint8_t cmd, const void *payload, uint16_t len) {
    uint8_t buf[64];
    if (1 + len > sizeof(buf)) return -1;
    buf[0] = cmd;
    if (payload && len > 0) memcpy(&buf[1], payload, len);
    return Vendor_SendFrame(buf, 1 + len);
}

/* ============================================================== */
/* EP1 OUT 命令派发                                                */
/* ============================================================== */
static void dispatch_control_slot(const uint8_t *slot, uint16_t rx_len) {
    if (rx_len < 1) return;
    uint8_t cmd = slot[0];

    switch (cmd) {
    case CMD_PING:
        /* 心跳: 设备回 PONG */
        proto_send(CMD_PONG, NULL, 0);
        break;

    case CMD_RESET_DISPLAY:
        LCD_Reinit();
        break;

    case CMD_SET_BRIGHTNESS: {
        if (rx_len < sizeof(cmd_set_brightness_t)) break;
        const cmd_set_brightness_t *p = (const cmd_set_brightness_t *)slot;
        LCD_BL_SetBrightness(p->brightness);
        break;
    }

    case CMD_CLEAR: {
        if (rx_len < sizeof(cmd_clear_t)) break;
        const cmd_clear_t *p = (const cmd_clear_t *)slot;
        LCD_Clear(p->color);
        break;
    }

    case CMD_DRAW_RECT_BEGIN: {
        if (rx_len < sizeof(cmd_draw_rect_begin_t)) break;
        const cmd_draw_rect_begin_t *p = (const cmd_draw_rect_begin_t *)slot;
        /* 0 状态: 仅开窗 + CS_LOW + DC_DATA, 等 EP5 OUT 灌像素 */
        LCD_BeginRect(p->x, p->y, p->w, p->h);
        break;
    }

    case CMD_DRAW_RECT_END:
        /* 0 状态: 直接 CS_HIGH. host 负责发 END 结束 draw. */
        LCD_EndRect();
        break;

    case CMD_DRAW_RECT_ABORT:
        /* 0 状态: 跟 END 一样 CS_HIGH, 语义上给 host 用于异常恢复 */
        LCD_AbortStream();
        break;

    default:
        /* 未知命令, 静默丢弃 */
        break;
    }
}

/* ============================================================== */
/* 主循环: 遍历 EP1 OUT ring buffer 派发                            */
/* ============================================================== */
void Protocol_Poll(void) {
    /* 一次循环处理完所有 ready slot, 每消费一个就 DealPtr++ + RemainPack-- */
    while (RingBuffer_Comm.RemainPack) {
        const uint8_t *slot = &Data_Buffer[RingBuffer_Comm.DealPtr * DEF_USBD_FS_PACK_SIZE];
        uint16_t rx_len = RingBuffer_Comm.PackLen[RingBuffer_Comm.DealPtr];

        dispatch_control_slot(slot, rx_len);

        /* 推进 ring buffer */
        RingBuffer_Comm.DealPtr++;
        if (RingBuffer_Comm.DealPtr == DEF_Ring_Buffer_Max_Blks) {
            RingBuffer_Comm.DealPtr = 0;
        }
        RingBuffer_Comm.RemainPack--;

        /* back-pressure: 消费到 restart 阈值下, 重新 ACK EP1 OUT */
        if (RingBuffer_Comm.StopFlag &&
            RingBuffer_Comm.RemainPack < (DEF_Ring_Buffer_Max_Blks - DEF_RING_BUFFER_RESTART)) {
            USBFSD->UEP1_CTRL_H = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_ACK;
            RingBuffer_Comm.StopFlag = 0;
        }
    }
}

/* ============================================================== */
/* 主循环: 遍历 EP5 OUT ring buffer 推像素到 LCD (跟 Protocol_Poll 同构) */
/* ============================================================== */
void Protocol_PollPixels(void) {
    /* 0 状态: 一包一包同步推到 LCD, host 发 CMD_DRAW_RECT_END 时关 CS. */
    while (RingBuffer_Comm_EP5.RemainPack) {
        const uint8_t *slot = &Data_Buffer5[RingBuffer_Comm_EP5.DealPtr * DEF_USBD_FS_PACK_SIZE];
        uint16_t rx_len = RingBuffer_Comm_EP5.PackLen[RingBuffer_Comm_EP5.DealPtr];

        LCD_WritePixelsStream(slot, rx_len);  /* 同步阻塞 ~25µs */

        /* 推进 ring buffer */
        RingBuffer_Comm_EP5.DealPtr++;
        if (RingBuffer_Comm_EP5.DealPtr == DEF_Ring_Buffer_Max_Blks) {
            RingBuffer_Comm_EP5.DealPtr = 0;
        }
        RingBuffer_Comm_EP5.RemainPack--;

        /* back-pressure: 消费到 restart 阈值下, 重新 ACK EP5 OUT */
        if (RingBuffer_Comm_EP5.StopFlag &&
            RingBuffer_Comm_EP5.RemainPack < (DEF_Ring_Buffer_Max_Blks - DEF_RING_BUFFER_RESTART)) {
            USBFSD->UEP5_CTRL_H = (USBFSD->UEP5_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_ACK;
            RingBuffer_Comm_EP5.StopFlag = 0;
        }
    }
}

/* ============================================================== */
/* API: 初始化 / 主动发送                                          */
/* ============================================================== */
void Protocol_Init(void) {
    /* EP1 OUT + EP5 OUT ring buffer 由 ch32x035_usbfs_device.c 的 USBFS_Device_Endp_Init
     * 在 USB init 时已经清零; 这里不重复. */
    g_status = 0x01;  /* ready */
}

void Protocol_SendPong(void) {
    cmd_pong_t pong = { .cmd = CMD_PONG, .status = g_status };
    proto_send(CMD_PONG, &pong.status, sizeof(pong.status));
}

void Protocol_SendTouchEvent(uint8_t event_type, uint16_t x, uint16_t y) {
    cmd_touch_event_t ev = { .cmd = CMD_TOUCH_EVENT, .event_type = event_type, .x = x, .y = y };
    proto_send(CMD_TOUCH_EVENT, &ev.event_type,
               sizeof(ev.event_type) + sizeof(ev.x) + sizeof(ev.y));
}

void Protocol_SendLog(const char *fmt, ...) {
    char buf[60];  /* 留 1B 给 cmd, 共 ≤ 64B 整包 */
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0) {
        if (len > (int)sizeof(buf)) len = (int)sizeof(buf);
        proto_send(CMD_LOG, buf, (uint16_t)len);
    }
}

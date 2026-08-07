/**
 * @file    usb_endp.c
 * @brief   USB 端点处理 - Vendor Bulk + HID Keyboard
 *
 * 端点分配 (v0.18):
 *   - EP1 OUT (bulk 64B):  控制通道 (cmd + struct, 整包 = 1 命令)
 *   - EP2 IN  (bulk 64B):  Vendor 响应 (PONG/TOUCH/LOG, cmd + struct)
 *   - EP3 IN  (intr 8B):   HID Keyboard report
 *   - EP5 OUT (bulk 64B):  图像数据流 (由 lcd_driver 内部 EP5_OUT_Callback 直通 SPI DMA)
 *
 * v0.17 → v0.18 重写:
 *   - 删 EP1_OUT_Callback byte-bounce 循环 (改用 main loop 直接消费 ring buffer)
 *   - Vendor_SendFrame 保持现状, 简化后只发 cmd + struct (无 8B header)
 *
 * v0.18+: EP2 IN 入队 (RingBuffer_Comm_EP2_IN), EP2_IN_TryDrain 出队, 见 .h.
 *        EP2_IN_Callback 钩子留空 (库直接 drain).
 */

#include "usb_endp.h"
#include "ch32x035_conf.h"
#include "protocol/proto.h"
#include "hid_kbd.h"
#include <string.h>

/* WCH USBFS 库 (vendored copy at src/usb/ch32x035_usbfs_device.{c,h}) */
#include "ch32x035_usbfs_device.h"

/* ============================================================== */
/* 发送 buffer                                                     */
/* ============================================================== */
uint8_t EP2_Tx_Buf[64]  __attribute__((aligned(4)));   /* Vendor bulk IN, 64B */
uint8_t EP3_Tx_Buf[8]   __attribute__((aligned(4)));   /* HID interrupt IN, 8B  */

/* EP1 OUT byte-bounce buffer 已删: main loop 直接 in-place 读 Data_Buffer[DealPtr*64].
 * 保留兼容空符号, 防止旧 include 引用. */
uint8_t EP1_Rx_Buf[64]  __attribute__((aligned(4)));

/* Vendor Bulk IN (EP2) 发送完成回调 — 已不再调用, 见 .h. */
void EP2_IN_Callback(void) {
    /* 空壳, 防旧 include 引用 */
}

/* HID Keyboard EP3 发送完成回调 */
void EP3_IN_Callback(void) {
}

/* Vendor 帧发送 (EP2 IN) — v0.18: 不再包装 8B header, 直接发 cmd + struct (≤ 64B 整包)
 * v0.18+: 入队 RingBuffer_Comm_EP2_IN, 空→非空时 kickstart. */
int Vendor_SendFrame(const uint8_t *data, uint16_t len) {
    if (len == 0 || len > 64) return -1;
    if (data == NULL) return -1;

    NVIC_DisableIRQ(USBFS_IRQn);

    uint8_t was_empty = (RingBuffer_Comm_EP2_IN.LoadPtr == RingBuffer_Comm_EP2_IN.DealPtr);
    uint8_t next_load = (RingBuffer_Comm_EP2_IN.LoadPtr + 1) % DEF_Ring_Buffer_Max_Blks;
    if (next_load == RingBuffer_Comm_EP2_IN.DealPtr) {
        NVIC_EnableIRQ(USBFS_IRQn);
        return -1;
    }
    /* memcpy 是必须的: 调用方栈 frame 返回后就消失, 数据要活到 drain */
    memcpy(&Data_Buffer_EP2[RingBuffer_Comm_EP2_IN.LoadPtr * DEF_USBD_FS_PACK_SIZE],
           data, len);
    RingBuffer_Comm_EP2_IN.PackLen[RingBuffer_Comm_EP2_IN.LoadPtr] = (uint8_t)len;
    RingBuffer_Comm_EP2_IN.LoadPtr = next_load;
    RingBuffer_Comm_EP2_IN.RemainPack++;

    if (was_empty) {
        EP2_IN_TryDrain();
    }

    NVIC_EnableIRQ(USBFS_IRQn);
    return 0;
}

/* ============================================================== */
/* HID 报告发送 (EP3 IN, 8B)                                      */
/* ============================================================== */
int HID_SendReport(const uint8_t *report) {
    if (report == NULL) return -1;
    if (USBFS_Endp_Busy[DEF_UEP3]) return -1;
    return USBFS_Endp_DataUp(DEF_UEP3, report, 8);
}

/* ============================================================== */
/* USB device reset 回调                                            */
/* ============================================================== */
void USBFS_RCC_Init_Callback(void) {
    /* 库自己会重新调 USBFS_Device_Endp_Init 复位所有端点. */
}

/* ============================================================== */
/* 设备初始化 (main.c USB_Device_Init_App 调用)                   */
/* ============================================================== */
void USB_Device_Init_App(void) {
    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE, PWR_VDD_3V3);
}

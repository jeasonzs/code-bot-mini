/**
 * @file    usb_endp.c
 * @brief   USB 端点处理 - Vendor Bulk + HID Keyboard
 *
 * 注: CDC ACM 完整实现复杂 (需写整个 class driver), 计划 v0.18 单独实现
 *      v0.17 先把 Vendor + HID 跑通
 */

#include "usb_endp.h"
#include "ch32x035_conf.h"
#include "protocol/proto.h"
#include "hid_kbd.h"
#include <string.h>

/* WCH USBFS 库接口 (在 ch32x035_usbfs_device.c 中定义) */
#include "ch32x035_usbfs_device.h"

/* 项目自定义: 端点忙标志 + EP 发送触发 (WCH SDK 不直接提供这两个, 自行实现) */
volatile uint8_t USBD_Endp2_Busy = 0;
volatile uint8_t USBD_Endp3_Busy = 0;

/* 切换 EP 收发方向 (WCH 不同 SDK 版本命名差异大, 这里做映射) */
void USBFS_EP_Switch(uint8_t ep_addr) {
    /* 实际 WCH 库可能叫 USBFSDev_EPx_IN_Start 或类似, SDK 没暴露此符号 */
    (void)ep_addr;
    /* TODO: 调到对应端点的 toggle/RxValid/TxValid 寄存器 */
}

void USBFSDev_EP_IN_Start(uint8_t ep, const uint8_t *buf, uint16_t len) {
    /* 触发对应端点发送 */
    if (ep == 0x82) {
        USBFS_EP_Switch(ep);
        USBD_Endp2_Busy = 1;
    } else if (ep == 0x83) {
        USBFS_EP_Switch(ep);
        USBD_Endp3_Busy = 1;
    }
    (void)buf; (void)len;
}

/* ===== 发送 buffer (WCH 库使用) ===== */
uint8_t EP2_Tx_Buf[64]  __attribute__((aligned(4)));
uint8_t EP3_Tx_Buf[8]   __attribute__((aligned(4)));

/* ===== 接收 buffer (Vendor Bulk OUT EP1) ===== */
uint8_t EP1_Rx_Buf[64]  __attribute__((aligned(4)));

/* ===== Vendor Bulk OUT 回调 (EP1) ===== */
/* WCH 库在收到 EP1 数据后调用本函数 */
void EP1_OUT_Callback(uint16_t len) {
    /* 将收到的数据喂给协议层 */
    for (uint16_t i = 0; i < len; i++) {
        Protocol_RxByte(EP1_Rx_Buf[i]);
    }

    /* 准备接收下一包 (Vendor Bulk OUT) */
    USBFS_EP_Switch(0x01);  /* EP1 OUT */
    /* SetRxStatus 实际由 WCH 库宏完成, 这里只喂协议 */
}

/* ===== Vendor Bulk IN 回调 (EP2) ===== */
void EP2_IN_Callback(void) {
    USBD_Endp2_Busy = 0;  /* 标记 EP2 空闲 */
}

/* ===== HID Keyboard IN 回调 (EP3) ===== */
void EP3_IN_Callback(void) {
    /* HID 发送完成, 不需要特殊处理 */
    /* HID_Kbd_SendPending 会在下一个 20ms 节流周期检查 */
}

/* ===== 发送 Vendor 帧 (EP2 IN) ===== */
int Vendor_SendFrame(const uint8_t *data, uint16_t len) {
    if (len > 64) return -1;
    if (USBD_Endp2_Busy) return -1;  /* 上次还没发完 */
    memcpy(EP2_Tx_Buf, data, len);
    USBD_Endp2_Busy = 1;
    /* 触发 EP2 IN 发送 */
    extern void USBFSDev_EP_IN_Start(uint8_t ep, const uint8_t *buf, uint16_t len);
    USBFSDev_EP_IN_Start(0x82, EP2_Tx_Buf, len);
    return 0;
}

/* ===== 发送 HID Keyboard Report (EP3 IN) ===== */
int HID_SendReport(const uint8_t *report) {
    memcpy(EP3_Tx_Buf, report, 8);
    /* 触发 EP3 IN 发送 (interrupt, 1ms polling) */
    extern void USBFSDev_EP_IN_Start(uint8_t ep, const uint8_t *buf, uint16_t len);
    USBFSDev_EP_IN_Start(0x83, EP3_Tx_Buf, 8);
    return 0;
}

/* ===== USB device reset 回调 ===== */
void USBFS_RCC_Init_Callback(void) {
    /* 复位时重置端点状态 */
    USBD_Endp2_Busy = 0;
}

/* ===== 设备初始化 ===== */
void USB_Device_Init_App(void) {
    /* 时钟 + 外设初始化 (WCH 库) */
    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE, PWR_VDD_3V3);
    /* 端点状态初始化 */
    USBD_Endp2_Busy = 0;
}

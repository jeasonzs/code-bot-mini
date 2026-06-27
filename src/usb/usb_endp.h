/**
 * @file    usb_endp.h
 * @brief   USB 端点处理 - Vendor Bulk + HID Keyboard
 *
 * 端点分配:
 *   - EP1 OUT (bulk 64B):  Vendor 图像/命令 (主机 → 设备)
 *   - EP2 IN  (bulk 64B):  Vendor 触摸事件/ACK (设备 → 主机)
 *   - EP3 IN  (intr 8B):   HID Keyboard report
 *
 * CDC 接口 (printf 调试) - v0.18+ 实现
 */

#ifndef USB_ENDP_H
#define USB_ENDP_H

#include <stdint.h>

/* ===== 端点发送 buffer (Vendor IN EP2 / HID IN EP3) ===== */
extern uint8_t EP2_Tx_Buf[];   /* Vendor bulk IN, 64B */
extern uint8_t EP3_Tx_Buf[];   /* HID interrupt IN, 8B */

/* ===== Vendor Bulk OUT (EP1) 接收回调 ===== */
/* 由 WCH USBFS 库在收到 EP1 数据时调用 */
void EP1_OUT_Callback(uint16_t len);

/* ===== Vendor Bulk IN (EP2) 发送完成回调 ===== */
void EP2_IN_Callback(void);

/* ===== HID Keyboard EP3 发送完成回调 ===== */
void EP3_IN_Callback(void);

/* ===== USB device reset 中断 ===== */
void USBFS_RCC_Init_Callback(void);

/* ===== 设备初始化 ===== */
void USB_Device_Init_App(void);

#endif /* USB_ENDP_H */

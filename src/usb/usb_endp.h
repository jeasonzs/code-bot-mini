/**
 * @file    usb_endp.h
 * @brief   USB 端点处理 - Vendor Bulk + HID Keyboard
 *
 * 端点分配:
 *   - EP1 OUT (bulk 64B):  Vendor 图像/命令 (主机 → 设备)
 *   - EP2 IN  (bulk 64B):  Vendor 触摸事件/ACK (设备 → 主机)
 *   - EP3 IN  (intr 8B):   HID Keyboard report
 *
 * v0.18+: 不再含 CDC 接口. v0.17 也没实现, 注释统一清理.
 */

#ifndef USB_ENDP_H
#define USB_ENDP_H

#include <stdint.h>

/* ===== 端点发送 buffer (Vendor IN EP2 / HID IN EP3) ===== */
extern uint8_t EP2_Tx_Buf[];   /* Vendor bulk IN, 64B */
extern uint8_t EP3_Tx_Buf[];   /* HID interrupt IN, 8B */

/* ===== Vendor Bulk OUT (EP1) 接收回调 ===== */
/* 由 vendored WCH USBFS 库在收到 EP1 数据时调用 (弱符号, 默认 NULL) */
void EP1_OUT_Callback(uint16_t len, const uint8_t *buf);

/* ===== Vendor Bulk IN (EP2) 发送完成回调 ===== */
void EP2_IN_Callback(void);

/* ===== HID Keyboard EP3 发送完成回调 ===== */
void EP3_IN_Callback(void);

/* ===== Vendor 帧发送 (应用层协议栈 → EP2 IN) ===== */
/* 0=成功启动, -1=忙/参数错 */
int Vendor_SendFrame(const uint8_t *data, uint16_t len);

/* ===== HID 报告发送 (8B → EP3 IN) ===== */
/* 0=成功启动, -1=忙/参数错 */
int HID_SendReport(const uint8_t *report);

/* ===== USB device reset 中断 ===== */
void USBFS_RCC_Init_Callback(void);

/* ===== 设备初始化 ===== */
void USB_Device_Init_App(void);

#endif /* USB_ENDP_H */

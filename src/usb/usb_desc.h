/**
 * @file    usb_desc.h
 * @brief   USB 描述符 - 复合设备 (Vendor + HID Keyboard + CDC ACM)
 *
 * 设备描述符和配置描述符.
 * 端点分配:
 *   - EP1 OUT (bulk 64B):  Vendor 图像/命令 (主机 → 设备)
 *   - EP2 IN  (bulk 64B):  Vendor 触摸事件/ACK (设备 → 主机)
 *   - EP3 IN  (intr 8B):   HID Keyboard report
 *   - EP4 OUT (bulk 64B):  CDC data OUT (主机 → 设备, 命令)
 *   - EP5 IN  (bulk 64B):  CDC data IN  (设备 → 主机, printf)
 *   - EP6 IN  (intr 8B):   CDC notification (DTR/DSR)
 */

#ifndef USB_DESC_H
#define USB_DESC_H

#include <stdint.h>

/* USB 描述符类型 */
#define USB_DESC_TYPE_DEVICE                    0x01
#define USB_DESC_TYPE_CONFIGURATION             0x02
#define USB_DESC_TYPE_STRING                    0x03
#define USB_DESC_TYPE_INTERFACE                 0x04
#define USB_DESC_TYPE_ENDPOINT                  0x05
#define USB_DESC_TYPE_IAD                       0x0B
#define USB_DESC_TYPE_HID                       0x21
#define USB_DESC_TYPE_REPORT                    0x22
#define USB_DESC_TYPE_CS_INTERFACE              0x24

/* USB Vendor + Product ID (WCH 兼容, 避免与现有设备冲突) */
#define USB_VID_CODE               0x1A86   /* WCH 官方 VID */
#define USB_PID_CODE               0xCB0B   /* Code Bot PID */
#define USB_BCD_DEVICE             0x0100   /* v1.0 */

/* 端点大小 */
#define USB_EP0_SIZE               64
#define USB_EP1_SIZE               64   /* Vendor OUT */
#define USB_EP2_SIZE               64   /* Vendor IN  */
#define USB_EP3_SIZE               8    /* HID IN   (interrupt) */
#define USB_EP4_SIZE               64   /* CDC OUT  */
#define USB_EP5_SIZE               64   /* CDC IN   */
#define USB_EP6_SIZE               8    /* CDC NOTIFY */

/* HID Report 描述符长度 (放在 usb_desc.c 定义) */
#define HID_REPORT_DESC_LEN        63   /* 标准 Keyboard report */

#endif /* USB_DESC_H */

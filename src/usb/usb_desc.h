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
#define HID_REPORT_DESC_LEN        64   /* 标准 Keyboard report (8B modifier+rsvd+6 keys) */

/* ============================================================== */
/* WCH USB 库期望的命名 (vendored 库 ch32x035_usbfs_device.c 用)   */
/* ============================================================== */
#define DEF_USB_VID                USB_VID_CODE
#define DEF_USB_PID                USB_PID_CODE
#define DEF_IC_PRG_VER             0x01

#define DEF_USBD_UEP0_SIZE         USB_EP0_SIZE
#define DEF_USBD_FS_PACK_SIZE      64
#define DEF_USBD_HS_PACK_SIZE      512
#define DEF_USB_EP1_FS_SIZE        USB_EP1_SIZE
#define DEF_USB_EP2_FS_SIZE        USB_EP2_SIZE
#define DEF_USB_EP3_FS_SIZE        USB_EP3_SIZE
#define DEF_USB_EP4_FS_SIZE        USB_EP4_SIZE
#define DEF_USB_EP5_FS_SIZE        USB_EP5_SIZE
#define DEF_USB_EP6_FS_SIZE        USB_EP6_SIZE

/* HID class 用 (WCH 库内部引用) */
#define DEF_USBD_REPORT_DESC_LEN   HID_REPORT_DESC_LEN

/* ============================================================== */
/* 描述符长度宏 (WCH 库 GET_DESCRIPTOR 内部引用)                    */
/* 直接从描述符首字节读, 保持单一数据源 (descriptor 自己).           */
/* 注意: 这些是运行时常量, 不能用于编译期数组大小声明.                */
/* ============================================================== */
#define DEF_USBD_DEVICE_DESC_LEN   ((uint8_t)MyDevDescr[0])
#define DEF_USBD_CONFIG_DESC_LEN   ((uint16_t)MyCfgDescr[2] | ((uint16_t)MyCfgDescr[3] << 8))
#define DEF_USBD_LANG_DESC_LEN     ((uint8_t)MyLangDescr[0])
#define DEF_USBD_MANU_DESC_LEN     ((uint8_t)MyManuInfo[0])
#define DEF_USBD_PROD_DESC_LEN     ((uint8_t)MyProdInfo[0])
#define DEF_USBD_SN_DESC_LEN       ((uint8_t)MySerNumInfo[0])

/* 描述符数组 extern 声明 (定义在 usb_desc.c) */
extern const uint8_t MyDevDescr[];
extern const uint8_t MyCfgDescr[];
extern const uint8_t MyLangDescr[];
extern const uint8_t MyManuInfo[];
extern const uint8_t MyProdInfo[];
extern const uint8_t MySerNumInfo[];
extern const uint8_t MyHIDReportDesc[];

#endif /* USB_DESC_H */

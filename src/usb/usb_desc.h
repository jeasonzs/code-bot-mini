/**
 * @file    usb_desc.h
 * @brief   USB 描述符 - 复合设备 (Vendor Bulk + HID Keyboard)
 *
 * v0.18 拓扑 (实际编译进固件):
 *   - EP1 OUT (bulk 64B):  Vendor 控制通道 (主机 → 设备)
 *   - EP2 IN  (bulk 64B):  Vendor 响应 (设备 → 主机, PONG/TOUCH/LOG)
 *   - EP3 IN  (intr 8B):   HID Keyboard report
 *   - EP5 OUT (bulk 64B):  Vendor 图像数据流 (SPI DMA 直通 LCD)
 *
 * Windows 免驱:
 *   BOS Descriptor 声明 MS OS 2.0 Platform Capability.
 *   MS OS 2.0 Descriptor Set 把 Interface 0 标记为 WinUSB 兼容,
 *   让 Windows 把 Interface 0 自动绑到 inbox winusb.sys (零安装).
 *   Interface 1 (HID Keyboard) 不受影响, 走标准 HID 键盘栈.
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
#define USB_DESC_TYPE_BOS                       0x0F   /* USB 2.1+ */
#define USB_DESC_TYPE_DEVICE_CAPABILITY         0x10
#define USB_DESC_TYPE_MS_OS_20                  0xEE   /* wValue high byte for GET_DESCRIPTOR */
#define USB_DESC_TYPE_HID                       0x21
#define USB_DESC_TYPE_REPORT                    0x22
#define USB_DESC_TYPE_CS_INTERFACE              0x24

/* USB Vendor + Product ID (WCH 兼容, 避免与现有设备冲突) */
#define USB_VID_CODE               0x1A86   /* WCH 官方 VID */
#define USB_PID_CODE               0xCB0B   /* Code Bot PID */
#define USB_BCD_DEVICE             0x0100   /* v1.0 */

/* 端点大小 */
#define USB_EP0_SIZE               64
#define USB_EP1_SIZE               64   /* Vendor OUT (control) */
#define USB_EP2_SIZE               64   /* Vendor IN  (response) */
#define USB_EP3_SIZE               8    /* HID IN     (interrupt) */
#define USB_EP4_SIZE               64   /* (保留 - CH32X035 EP4 buffer 复用 EP0, 不可用) */
#define USB_EP5_SIZE               64   /* Vendor OUT (image data) */
#define USB_EP6_SIZE               8    /* (保留) */

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
#define DEF_USBD_BOS_DESC_LEN      ((uint16_t)MyBOSDescr[2] | ((uint16_t)MyBOSDescr[3] << 8))
#define DEF_USBD_MSOS20_DESC_LEN   ((uint16_t)MyMSOS20DescrSet[8] | ((uint16_t)MyMSOS20DescrSet[9] << 8))

/* 描述符数组 extern 声明 (定义在 usb_desc.c) */
extern const uint8_t MyDevDescr[];
extern const uint8_t MyCfgDescr[];
extern const uint8_t MyLangDescr[];
extern const uint8_t MyManuInfo[];
extern const uint8_t MyProdInfo[];
extern const uint8_t MySerNumInfo[];
extern const uint8_t MyHIDReportDesc[];
extern const uint8_t MyBOSDescr[];
extern const uint8_t MyMSOS20DescrSet[];

#endif /* USB_DESC_H */

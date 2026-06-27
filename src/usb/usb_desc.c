/**
 * @file    usb_desc.c
 * @brief   USB 描述符 - 复合设备 (Vendor + HID Keyboard + CDC ACM)
 *
 * 3 接口, 6 端点, 1 配置
 */

#include "usb_desc.h"

/* ============================================================== */
/* 设备描述符                                                     */
/* ============================================================== */
const uint8_t USBD_DeviceDesc[18] = {
    0x12,                           /* bLength */
    USB_DESC_TYPE_DEVICE,           /* bDescriptorType */
    0x10, 0x01,                     /* bcdUSB 1.10 */
    0x00,                           /* bDeviceClass (在 Interface 中定义) */
    0x00,                           /* bDeviceSubClass */
    0x00,                           /* bDeviceProtocol */
    USB_EP0_SIZE,                   /* bMaxPacketSize0 */
    (uint8_t)(USB_VID_CODE),         /* idVendor low */
    (uint8_t)(USB_VID_CODE >> 8),   /* idVendor high */
    (uint8_t)(USB_PID_CODE),         /* idProduct low */
    (uint8_t)(USB_PID_CODE >> 8),   /* idProduct high */
    (uint8_t)(USB_BCD_DEVICE),       /* bcdDevice low */
    (uint8_t)(USB_BCD_DEVICE >> 8), /* bcdDevice high */
    0x01,                           /* iManufacturer (String 1) */
    0x02,                           /* iProduct (String 2) */
    0x03,                           /* iSerialNumber (String 3) */
    0x01,                           /* bNumConfigurations */
};

/* ============================================================== */
/* 配置描述符 + 接口 + 端点                                        */
/* ============================================================== */
/* 总长 = 9 (cfg) + 9 (Vendor iface) + 7 (Vendor EP) + 9 (HID iface) + 9 (HID desc) + 7 (HID EP)
 *       + 8 (IAD) + 9 (CDC Comm iface) + 5 (Header) + 5 (Call) + 4 (ACM) + 5 (Union) + 7 (Notify EP)
 *       + 9 (CDC Data iface) + 7 (Data EP OUT) + 7 (Data EP IN)
 *     = 9 + 9 + 7 + 9 + 9 + 7 + 8 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 7 + 7 = 114 字节
 */
const uint8_t USBD_ConfigDesc[] = {
    /* Configuration Descriptor */
    0x09, USB_DESC_TYPE_CONFIGURATION, 0x72, 0x00,   /* wTotalLength = 114 */
    0x03, 0x01, 0x00, 0x80, 0x32,                  /* 3 interfaces, bus-powered, 100mA */

    /* ============================================ */
    /* Interface 0: Vendor (Bulk I/O) */
    /* ============================================ */
    0x09, USB_DESC_TYPE_INTERFACE, 0x00, 0x00, 0x02,  /* alt 0, 2 endpoints */
    0xFF, 0x00, 0x00, 0x00,                        /* Vendor class, no subclass/protocol */

    /* EP1 OUT: Vendor Bulk OUT (image/cmd from host) */
    0x07, USB_DESC_TYPE_ENDPOINT, 0x01, 0x02,        /* EP1 OUT, bulk */
    USB_EP1_SIZE, 0x00, 0x00,                       /* 64B, no interval for bulk */

    /* EP2 IN: Vendor Bulk IN (touch event to host) */
    0x07, USB_DESC_TYPE_ENDPOINT, 0x82, 0x02,        /* EP2 IN, bulk */
    USB_EP2_SIZE, 0x00, 0x00,

    /* ============================================ */
    /* Interface 1: HID Keyboard */
    /* ============================================ */
    0x09, USB_DESC_TYPE_INTERFACE, 0x01, 0x00, 0x01,  /* alt 0, 1 endpoint */
    0x03, 0x01, 0x01, 0x00,                        /* HID class, boot keyboard subclass */

    /* HID Descriptor */
    0x09, USB_DESC_TYPE_HID, 0x11, 0x01,             /* bcdHID 1.11 */
    0x00, 0x01, 0x22,                               /* country=0, numDesc=1, type=report */
    HID_REPORT_DESC_LEN, 0x00,

    /* EP3 IN: HID Interrupt IN (keyboard report) */
    0x07, USB_DESC_TYPE_ENDPOINT, 0x83, 0x03,        /* EP3 IN, interrupt */
    USB_EP3_SIZE, 0x00, 0x01,                       /* 8B, interval=1ms (polling) */

    /* ============================================ */
    /* Interface 2/3: CDC ACM (printf over USB) */
    /* ============================================ */
    /* IAD Descriptor (Interface Association for CDC) */
    0x08, USB_DESC_TYPE_IAD, 0x02, 0x02,             /* iFirstInterface=2, iCount=2 */
    0x02, 0x02, 0x01, 0x00,                        /* CDC class, ACM subclass, ACM protocol */

    /* Interface 2: CDC Communication (Comm) */
    0x09, USB_DESC_TYPE_INTERFACE, 0x02, 0x00, 0x01, /* alt 0, 1 endpoint */
    0x02, 0x02, 0x01, 0x00,                        /* CDC Comm class */

    /* CDC Header Functional Descriptor */
    0x05, USB_DESC_TYPE_CS_INTERFACE, 0x00,
    0x10, 0x01,                                     /* bcdCDC 1.10 */

    /* CDC Call Management Functional Descriptor */
    0x05, USB_DESC_TYPE_CS_INTERFACE, 0x01, 0x00,     /* capabilities */
    0x01,                                            /* Data Interface = 1 (interface 3) */

    /* CDC ACM Functional Descriptor */
    0x04, USB_DESC_TYPE_CS_INTERFACE, 0x02, 0x02,

    /* CDC Union Functional Descriptor */
    0x05, USB_DESC_TYPE_CS_INTERFACE, 0x06, 0x00,     /* Master interface = 2 */
    0x03,                                            /* Slave interface 0 = 3 (data) */

    /* EP6 IN: CDC Notification (DTR/DSR) */
    0x07, USB_DESC_TYPE_ENDPOINT, 0x86, 0x03,        /* EP6 IN, interrupt */
    USB_EP6_SIZE, 0x00, 0x01,                       /* 8B, 1ms polling */

    /* Interface 3: CDC Data */
    0x09, USB_DESC_TYPE_INTERFACE, 0x03, 0x00, 0x02, /* alt 0, 2 endpoints */
    0x0A, 0x00, 0x00, 0x00,                        /* CDC Data class */

    /* EP4 OUT: CDC Bulk OUT (host → device serial data) */
    0x07, USB_DESC_TYPE_ENDPOINT, 0x04, 0x02,        /* EP4 OUT, bulk */
    USB_EP4_SIZE, 0x00, 0x00,

    /* EP5 IN: CDC Bulk IN (device → host serial data, printf) */
    0x07, USB_DESC_TYPE_ENDPOINT, 0x85, 0x02,        /* EP5 IN, bulk */
    USB_EP5_SIZE, 0x00, 0x00,
};

/* ============================================================== */
/* HID Report 描述符 (Keyboard)                                    */
/* ============================================================== */
const uint8_t USBD_HIDReportDesc[] = {
    /* 标准键盘 report (8 bytes: modifier + reserved + 6 keycodes) */
    0x05, 0x01,                     /* Usage Page (Generic Desktop) */
    0x09, 0x06,                     /* Usage (Keyboard) */
    0xA1, 0x01,                     /* Collection (Application) */
    0x05, 0x07,                     /*   Usage Page (Key Codes) */
    0x19, 0xE0,                     /*   Usage Minimum (224) */
    0x29, 0xE7,                     /*   Usage Maximum (231) */
    0x15, 0x00,                     /*   Logical Minimum (0) */
    0x25, 0x01,                     /*   Logical Maximum (1) */
    0x75, 0x01,                     /*   Report Size (1) */
    0x95, 0x08,                     /*   Report Count (8) */
    0x81, 0x02,                     /*   Input (Data,Var,Abs) -- modifier byte */
    0x95, 0x01,                     /*   Report Count (1) */
    0x75, 0x08,                     /*   Report Size (8) */
    0x81, 0x03,                     /*   Input (Const,Var,Abs) -- reserved */
    0x95, 0x05,                     /*   Report Count (5) */
    0x75, 0x01,                     /*   Report Size (1) */
    0x05, 0x08,                     /*   Usage Page (LEDs) */
    0x19, 0x01,                     /*   Usage Minimum (1) */
    0x29, 0x05,                     /*   Usage Maximum (5) */
    0x91, 0x02,                     /*   Output (Data,Var,Abs) -- LED report */
    0x95, 0x01,                     /*   Report Count (1) */
    0x75, 0x03,                     /*   Report Size (3) */
    0x91, 0x03,                     /*   Output (Const,Var,Abs) -- LED padding */
    0x95, 0x06,                     /*   Report Count (6) */
    0x75, 0x08,                     /*   Report Size (8) */
    0x15, 0x00,                     /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00,               /*   Logical Maximum (255) */
    0x05, 0x07,                     /*   Usage Page (Key Codes) */
    0x19, 0x00,                     /*   Usage Minimum (0) */
    0x29, 0xFF,                     /*   Usage Maximum (255) */
    0x81, 0x00,                     /*   Input (Data,Array) -- key array (6 bytes) */
    0xC0,                           /* End Collection */
};

/* ============================================================== */
/* 字符串描述符                                                    */
/* ============================================================== */

/* Language ID: English-US (0x0409) */
const uint8_t USBD_StringLangID[] = {
    0x04, 0x03, 0x09, 0x04
};

/* Manufacturer: "Code Bot" */
const uint8_t USBD_StringManu[] = {
    16, 0x03,        /* 16 bytes, STRING type */
    'C', 0, 'o', 0, 'd', 0, 'e', 0, ' ', 0, 'B', 0, 'o', 0, 't', 0
};

/* Product: "Code Bot Display" */
const uint8_t USBD_StringProd[] = {
    34, 0x03,
    'C', 0, 'o', 0, 'd', 0, 'e', 0, ' ', 0, 'B', 0, 'o', 0, 't', 0,
    ' ', 0, 'D', 0, 'i', 0, 's', 0, 'p', 0, 'l', 0, 'a', 0, 'y', 0
};

/* Serial: "0001" (可改为唯一序列号) */
const uint8_t USBD_StringSerial[] = {
    10, 0x03,
    '0', 0, '0', 0, '0', 0, '1', 0
};

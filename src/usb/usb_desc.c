/**
 * @file    usb_desc.c
 * @brief   USB 描述符 - 复合设备 (Vendor Bulk + HID Keyboard)
 *
 * 2 接口, 4 端点, 1 配置
 *
 * v0.18 拓扑: 控制 / 数据 物理隔离
 *   - EP1 OUT (bulk 64B): 控制通道 (cmd + struct, ≤64B 单包)
 *   - EP2 IN  (bulk 64B): Vendor 响应 (PONG/TOUCH/LOG, cmd + struct)
 *   - EP3 IN  (intr 8B):  HID Keyboard 标准 8B report
 *   - EP5 OUT (bulk 64B): 图像数据流 (raw RGB565, SPI DMA 直通 LCD)
 *
 * 注: CH32X035 EP4 没有独立 DMA 寄存器, buffer 复用 EP0 (UEP0_DMA+64),
 *     不能用作 bulk 数据端点. 改用 EP5 (有 UEP5_DMA).
 */

#include "usb_desc.h"

/* ============================================================== */
/* 设备描述符 (用 WCH 驱动期望的名字 MyDevDescr)                    */
/* ============================================================== */
const uint8_t MyDevDescr[18] = {
    0x12,                           /* bLength */
    USB_DESC_TYPE_DEVICE,           /* bDescriptorType */
    0x10, 0x02,                     /* bcdUSB 2.10 — BOS + MS OS 2.0 需要 ≥ 2.10 */
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
/* 配置描述符 + 接口 + 端点 (WCH 驱动期望 MyCfgDescr)                */
/* ============================================================== */
/* 总长 = 9 (cfg) + 9 (Vendor iface) + 7 (EP1 OUT) + 7 (EP2 IN) + 7 (EP5 OUT)
 *       + 9 (HID iface) + 9 (HID desc) + 7 (EP3 IN)
 *     = 9 + 9 + 7 + 7 + 7 + 9 + 9 + 7 = 64 = 0x40 字节
 */
const uint8_t MyCfgDescr[] = {
    /* Configuration Descriptor */
    0x09, USB_DESC_TYPE_CONFIGURATION, 0x40, 0x00,   /* wTotalLength = 64 */
    0x02, 0x01, 0x00, 0x80, 0x32,                  /* 2 interfaces, bus-powered, 100mA */

    /* ============================================ */
    /* Interface 0: Vendor (Control + Data)        */
    /*   EP1 OUT: 控制通道 (cmd + struct)           */
    /*   EP2 IN:  Vendor 响应 (PONG/TOUCH/LOG)      */
    /*   EP5 OUT: 图像数据流 (raw RGB565 → SPI DMA)  */
    /* ============================================ */
    0x09, USB_DESC_TYPE_INTERFACE, 0x00, 0x00, 0x03,  /* alt 0, 3 endpoints */
    0xFF, 0x00, 0x00, 0x00,                        /* Vendor class, no subclass/protocol */

    /* EP1 OUT: Vendor Bulk OUT (control channel, host → device) */
    0x07, USB_DESC_TYPE_ENDPOINT, 0x01, 0x02,        /* EP1 OUT, bulk */
    USB_EP1_SIZE, 0x00, 0x00,                       /* 64B, no interval for bulk */

    /* EP2 IN: Vendor Bulk IN (response, device → host) */
    0x07, USB_DESC_TYPE_ENDPOINT, 0x82, 0x02,        /* EP2 IN, bulk */
    USB_EP2_SIZE, 0x00, 0x00,

    /* EP5 OUT: Vendor Bulk OUT (image data stream, host → device) */
    0x07, USB_DESC_TYPE_ENDPOINT, 0x05, 0x02,        /* EP5 OUT, bulk */
    USB_EP5_SIZE, 0x00, 0x00,                       /* 64B, no interval for bulk */

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
};

/* ============================================================== */
/* HID Report 描述符 (Keyboard, WCH 驱动期望 MyHIDReportDesc)       */
/* ============================================================== */
const uint8_t MyHIDReportDesc[] = {
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
const uint8_t MyLangDescr[] = {
    0x04, 0x03, 0x09, 0x04
};

/* Manufacturer: "Code Bot" */
const uint8_t MyManuInfo[] = {
    18, 0x03,        /* 18 bytes, STRING type (bLength 必须是字符串总字节数, 不是字符数) */
    'C', 0, 'o', 0, 'd', 0, 'e', 0, ' ', 0, 'B', 0, 'o', 0, 't', 0
};

/* Product: "Code Bot Display" */
const uint8_t MyProdInfo[] = {
    34, 0x03,
    'C', 0, 'o', 0, 'd', 0, 'e', 0, ' ', 0, 'B', 0, 'o', 0, 't', 0,
    ' ', 0, 'D', 0, 'i', 0, 's', 0, 'p', 0, 'l', 0, 'a', 0, 'y', 0
};

/* Serial: "0001" (可改为唯一序列号) */
const uint8_t MySerNumInfo[] = {
    10, 0x03,
    '0', 0, '0', 0, '0', 0, '1', 0
};

/* ============================================================== */
/* BOS Descriptor (USB 2.1+, bcdUSB ≥ 0x0210)                       */
/*                                                                */
/* 用于声明 MS OS 2.0 Platform Capability. Host 枚举时会先请求     */
/* BOS descriptor (wValue=0x0F00), 然后看到 MS OS 2.0 capability,  */
/* 主动发 GET_DESCRIPTOR(0xEE) 请求完整 MS OS 2.0 descriptor set. */
/*                                                                */
/* 长度 = 5 (BOS header) + 26 (platform cap) = 31 bytes          */
/* ============================================================== */
const uint8_t MyBOSDescr[] = {
    /* BOS header: bLength=5, bDescriptorType=BOS(0x0F),
     * wTotalLength=31 (LE), bNumDeviceCaps=1 */
    0x05, 0x0F, 0x1F, 0x00, 0x01,
    /* MS OS 2.0 Platform Capability (DEVICE_CAPABILITY): bLength=26,
     * bDevCapabilityType=PLATFORM(0x05), UUID {D8DD60DF-4589-4CC7-9CD2-659D9E648A9F},
     * dwWindowsVersion=Win8.1 (0x06030000 LE), wMSOSDescriptorSetTotalLength=176 */
    0x1A, 0x10, 0x05, 0x00, 0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C, 0x9C, 0xD2, 0x65, 0x9D,
    0x9E, 0x64, 0x8A, 0x9F, 0x00, 0x00, 0x03, 0x06, 0xB0, 0x00,
};

/* ============================================================== */
/* MS OS 2.0 Descriptor Set (176 bytes)                           */
/*                                                                */
/* Host 在看到 BOS 里 MS OS 2.0 Platform Capability 后,            */
/* 用 standard GET_DESCRIPTOR 请求这个 set:                       */
/*   bmRequestType = 0x80  bRequest = GET_DESCRIPTOR (0x06)       */
/*   wValue = 0xEE00  wIndex = 0  wLength = 176                   */
/*                                                                */
/* 内容: 把 Interface 0 (Vendor Bulk) 标记为 WinUSB 兼容,         */
/* 并注册 DeviceInterfaceGUIDs = {11962A0C-5AC3-4108-A44B-9BD46300FD30} */
/*                                                                */
/* 这让 Windows 在没有 INF 的情况下自动把 Interface 0 绑到         */
/* inbox winusb.sys — 即「免驱」. HID Keyboard (Interface 1)      */
/* 不受此影响, 走标准 HID 键盘栈.                                  */
/* ============================================================== */
const uint8_t MyMSOS20DescrSet[] = {
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, 0xB0, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00,
    0xA6, 0x00, 0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0x9E, 0x00, 0x14, 0x00, 0x03, 0x00, 0x57, 0x49,
    0x4E, 0x55, 0x53, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x82, 0x00,
    0x04, 0x00, 0x07, 0x00, 0x2A, 0x00, 0x44, 0x00, 0x65, 0x00, 0x76, 0x00, 0x69, 0x00, 0x63, 0x00,
    0x65, 0x00, 0x49, 0x00, 0x6E, 0x00, 0x74, 0x00, 0x65, 0x00, 0x72, 0x00, 0x66, 0x00, 0x61, 0x00,
    0x63, 0x00, 0x65, 0x00, 0x47, 0x00, 0x55, 0x00, 0x49, 0x00, 0x44, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x4E, 0x00, 0x7B, 0x00, 0x31, 0x00, 0x31, 0x00, 0x39, 0x00, 0x36, 0x00, 0x32, 0x00, 0x41, 0x00,
    0x30, 0x00, 0x43, 0x00, 0x2D, 0x00, 0x35, 0x00, 0x41, 0x00, 0x43, 0x00, 0x33, 0x00, 0x2D, 0x00,
    0x34, 0x00, 0x31, 0x00, 0x30, 0x00, 0x38, 0x00, 0x2D, 0x00, 0x41, 0x00, 0x34, 0x00, 0x34, 0x00,
    0x42, 0x00, 0x2D, 0x00, 0x39, 0x00, 0x42, 0x00, 0x44, 0x00, 0x34, 0x00, 0x36, 0x00, 0x33, 0x00,
    0x30, 0x00, 0x30, 0x00, 0x46, 0x00, 0x44, 0x00, 0x33, 0x00, 0x30, 0x00, 0x7D, 0x00, 0x00, 0x00,
};

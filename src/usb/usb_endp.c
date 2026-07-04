/**
 * @file    usb_endp.c
 * @brief   USB 端点处理 - Vendor Bulk + HID Keyboard
 *
 * 端点分配:
 *   - EP1 OUT (bulk 64B):  Vendor 图像/命令 (主机 → 设备)
 *   - EP2 IN  (bulk 64B):  Vendor 触摸事件/ACK (设备 → 主机)
 *   - EP3 IN  (intr 8B):   HID Keyboard report
 *
 * CDC 接口 v0.17 不实现 (注释也删了, 别再误以为有 CDC)
 *
 * v0.17 → v0.18 重写:
 *   - 删除原 USBFS_EP_Switch / USBFSDev_EP_IN_Start stub
 *   - 改用 vendored WCH 库的 USBFS_Endp_DataUp() 真实触发 IN 传输
 *   - EP1 OUT 经 WCH 库弱符号 EP1_OUT_Callback 拿到 buf + len
 *   - busy 状态由库 USBFS_Endp_Busy[] 统一管, 这里不再有 USBD_Endp*_Busy
 */

#include "usb_endp.h"
#include "ch32x035_conf.h"
#include "protocol/proto.h"
#include "hid_kbd.h"
#include <string.h>

/* WCH USBFS 库 (vendored copy at src/usb/ch32x035_usbfs_device.{c,h}) */
#include "ch32x035_usbfs_device.h"

/* ============================================================== */
/* 发送 buffer (USBD_Endp*_Busy 旧标志已删, 库 USBFS_Endp_Busy[] 统一管) */
/* ============================================================== */
uint8_t EP2_Tx_Buf[64]  __attribute__((aligned(4)));   /* Vendor bulk IN, 64B */
uint8_t EP3_Tx_Buf[8]   __attribute__((aligned(4)));   /* HID interrupt IN, 8B  */

/* EP1 Rx 旧 buffer: 库 ISR 已把数据放好, callback 从参数 buf 读, 不用这个. 留个符号兼容旧 include. */
uint8_t EP1_Rx_Buf[64]  __attribute__((aligned(4)));

/* ============================================================== */
/* Vendor Bulk OUT (EP1) 接收回调                                  */
/* 由 vendored WCH 库在 EP1 OUT 收完一包后调用 (弱符号, 默认 NULL) */
/* ============================================================== */
void EP1_OUT_Callback(uint16_t len, const uint8_t *buf) {
    if (len == 0 || len > 64) return;
    extern volatile uint32_t g_ticks_ms;
    printf("[DBG] EP1_OUT len=%u tick=%u\n", (unsigned)len, (unsigned)g_ticks_ms);
    for (uint16_t i = 0; i < len; i++) {
        Protocol_RxByte(buf[i]);
    }
}

/* ============================================================== */
/* Vendor Bulk IN (EP2) 发送完成回调 (库 EP2_IN 分支已清 busy)     */
/* ============================================================== */
void EP2_IN_Callback(void) {
    /* 库已自动清 USBFS_Endp_Busy[DEF_UEP2] 并翻 T_TOG.
     * 协议层下一帧可以发了. 这里不需做事, 留个钩子.
     * 注: 本 fork 的 ISR 没接这个 weak 钩子 (只接了 EP1_OUT_Callback),
     *     真正的 busy 清零在 vendored 库的 USBFS_UIS_TOKEN_IN 分支里.
     *     所以这里加打印只对将来扩展有效. */
}

/* ============================================================== */
/* HID Keyboard EP3 发送完成回调                                  */
/* ============================================================== */
void EP3_IN_Callback(void) {
    /* 同 EP2_IN_Callback, 库自动处理. 钩子留给 HID 上层做发送队列. */
}

/* ============================================================== */
/* Vendor 帧发送 (EP2 IN)                                        */
/* ============================================================== */
int Vendor_SendFrame(const uint8_t *data, uint16_t len) {
    if (len == 0 || len > 64) {
        printf("[DBG] VSF: len=%d out of range\n", (int)len);
        return -1;
    }
    if (data == NULL) {
        printf("[DBG] VSF: NULL data\n");
        return -1;
    }
    if (USBFS_Endp_Busy[DEF_UEP2]) {
        printf("[DBG] VSF: busy=1, skip\n");
        return -1;                                   /* 上一帧还没发完 */
    }
    uint8_t rc = USBFS_Endp_DataUp(DEF_UEP2, data, len);
    printf("[DBG] VSF: DataUp rc=%d\n", (int)rc);
    return rc;
}

/* ============================================================== */
/* HID 报告发送 (EP3 IN, 8B)                                     */
/* ============================================================== */
int HID_SendReport(const uint8_t *report) {
    if (report == NULL) return -1;
    if (USBFS_Endp_Busy[DEF_UEP3]) return -1;
    return USBFS_Endp_DataUp(DEF_UEP3, report, 8);
}

/* ============================================================== */
/* USB device reset 回调 (vendored 库在 USB 总线复位时调用)        */
/* ============================================================== */
void USBFS_RCC_Init_Callback(void) {
    /* 库自己会重新调 USBFS_Device_Endp_Init 复位所有端点.
     * 这里只做应用层状态清理. */
}

/* ============================================================== */
/* 设备初始化 (main.c USB_Device_Init_App 调用)                  */
/* ============================================================== */
void USB_Device_Init_App(void) {
    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE, PWR_VDD_3V3);
    /* 库初始化时 USBFS_Device_Endp_Init 已被调用, EP2/EP3 TX 已使能 */
}

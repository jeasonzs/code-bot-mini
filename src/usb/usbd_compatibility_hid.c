/********************************** (C) COPYRIGHT *******************************
 * File Name  : usbd_compatibility_hid.c
 * Author     : WCH (stripped for Code Bot)
 * Description:
 *   Stripped from WCH EVT CompatibilityHID example.
 *   - HID_Report_Buffer + HID_Set_Report_Flag 由 hid_kbd.c 提供, 这里不重定义
 *   - 保留 HID_Set_Report_Deal() (WCH 库 HID class 期望的 SET_REPORT 处理)
 *   - 删除所有 TIM3 / UART2 / DMA 相关代码 (与 code-bot 的 TIM3=1ms tick + USART3=debug 冲突)
 *******************************************************************************/
#include "ch32x035_usbfs_device.h"
#include "string.h"
#include "usbd_compatibility_hid.h"

/* HID_Report_Buffer / HID_Set_Report_Flag 见 src/usb/hid_kbd.c */

/*********************************************************************
 * @fn      HID_Set_Report_Deal
 *
 * @brief   处理 HID SET_REPORT 控制传输. v0.17 不使用 HID OUT report, 直接 ACK.
 *
 * @return  none
 */
void HID_Set_Report_Deal( void )
{
    if (HID_Set_Report_Flag == SET_REPORT_WAIT_DEAL)
    {
        /* 当前未消费 HID Report buffer 内容, 直接清标志 */
        HID_Set_Report_Flag = SET_REPORT_DEAL_OVER;
        USBFSD->UEP0_TX_LEN  = 0;
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
    }
}


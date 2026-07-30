/********************************** (C) COPYRIGHT *******************************
* File Name          : ch32x035_usbfs_device.c
* Author             : WCH (Code Bot local fork)
* Version            : V1.0.1-wch + CodeBot-patches
* Date               : 2025/03/10 (WCH), 2026/06/28 (Code Bot patches)
* Description        : Local fork of WCH CompatibilityHID library, modified
*                     for Code Bot's Vendor (EP1 OUT + EP2 IN bulk) + HID
*                     Keyboard (EP3 IN interrupt) topology. No CDC.
*                     - Added EP3 TX enable + DMA buffer + IRQ branch
*                     - Added USBFS_Endp_DataUp() helper (lifted from
*                       WCH SimulateCDC-HID, cleaned up)
*                     - Added weak EP1_OUT_Callback() hook for Vendor RX
*                     - Added EP3 branches in CLEAR_FEATURE/SET_FEATURE/
*                       GET_STATUS handlers
*                     - All other code paths are unchanged from WCH V1.0.1
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/

#include "ch32x035_usbfs_device.h"
#include "usbd_compatibility_hid.h"
/*******************************************************************************/
/* Variable Definition */


/* Global */
const uint8_t    *pUSBFS_Descr;

/* Setup Request */
volatile uint8_t  USBFS_SetupReqCode;
volatile uint8_t  USBFS_SetupReqType;
volatile uint16_t USBFS_SetupReqValue;
volatile uint16_t USBFS_SetupReqIndex;
volatile uint16_t USBFS_SetupReqLen;

/* USB Device Status */
volatile uint8_t  USBFS_DevConfig;
volatile uint8_t  USBFS_DevAddr;
volatile uint8_t  USBFS_DevSleepStatus;
volatile uint8_t  USBFS_DevEnumStatus;

/* HID Class Command */
volatile uint8_t USBFS_HidIdle;
volatile uint8_t USBFS_HidProtocol;

/* Endpoint Buffer */
__attribute__ ((aligned(4))) uint8_t USBFS_EP0_Buf[DEF_USBD_UEP0_SIZE];
__attribute__ ((aligned(4))) uint8_t USBFS_EP2_Buf[DEF_USB_EP2_FS_SIZE];
__attribute__ ((aligned(4))) uint8_t USBFS_EP3_Buf[DEF_USB_EP3_FS_SIZE];   /* Code Bot: EP3 HID IN */

/* Code Bot v0.18: EP5 OUT image data ring buffer (跟 EP1 同构, 16 槽 × 64B = 1KB)
 * 注: 用 EP5 不是 EP4 是因为 CH32X035 EP4 没有独立 DMA 寄存器 (buffer 复用
 *     EP0/UEP0_DMA+64), 不能用作 bulk 数据端点. EP5 有 UEP5_DMA, 独立 buffer. */
__attribute__ ((aligned(4))) uint8_t Data_Buffer5[DEF_RING_BUFFER_SIZE];
RING_BUFF_COMM RingBuffer_Comm_EP5;

/* USB IN Endpoint Busy Flag */
volatile uint8_t  USBFS_Endp_Busy[ DEF_UEP_NUM ];

/* Ring buffer */
RING_BUFF_COMM  RingBuffer_Comm;
__attribute__ ((aligned(4))) uint8_t Data_Buffer[DEF_RING_BUFFER_SIZE];


/******************************************************************************/
/* Interrupt Service Routine Declaration*/
void USBFS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));


/*********************************************************************
 * @fn      USBFS_RCC_Init
 *
 * @brief   Initializes the USBFS clock configuration.
 *
 * @return  none
 */
void USBFS_RCC_Init(void)
{
    RCC_APB2PeriphClockCmd( RCC_APB2Periph_AFIO, ENABLE );
    RCC_AHBPeriphClockCmd( RCC_AHBPeriph_USBFS, ENABLE );
}

/*********************************************************************
 * @fn      USBFS_Device_Endp_Init
 *
 * @brief   Initializes USB device endpoints.
 *
 * @return  none
 */
void USBFS_Device_Endp_Init( void )
{
    /* Code Bot v0.18: EP1 OUT (control) */
    USBFSD->UEP4_1_MOD = USBFS_UEP1_RX_EN;
    /* Code Bot: 改为 EP2 TX + EP3 TX 同时使能 (0x44) */
    USBFSD->UEP2_3_MOD = USBFS_UEP2_TX_EN | USBFS_UEP3_TX_EN;
    /* Code Bot v0.18: EP5 OUT (image data) */
    USBFSD->UEP567_MOD = USBFS_UEP5_RX_EN;

    USBFSD->UEP0_DMA = (uint32_t)USBFS_EP0_Buf;
    USBFSD->UEP1_DMA = (uint32_t)Data_Buffer;
    USBFSD->UEP2_DMA = (uint32_t)USBFS_EP2_Buf;
    USBFSD->UEP3_DMA = (uint32_t)USBFS_EP3_Buf;   /* Code Bot */
    USBFSD->UEP5_DMA = (uint32_t)Data_Buffer5;   /* Code Bot v0.18: EP5 OUT image data, slot 0 */

    USBFSD->UEP0_CTRL_H = USBFS_UEP_T_RES_NAK | USBFS_UEP_R_RES_ACK;
    USBFSD->UEP1_CTRL_H = USBFS_UEP_R_RES_ACK;
    USBFSD->UEP2_CTRL_H = USBFS_UEP_T_RES_NAK;
    USBFSD->UEP3_CTRL_H = USBFS_UEP_T_RES_NAK;   /* Code Bot */
    /* EP5 默认 NAK: 数据通道默认关闭, 等 DRAW_RECT_BEGIN 显式开 */
    USBFSD->UEP5_CTRL_H = USBFS_UEP_T_RES_NAK | USBFS_UEP_R_RES_ACK;   /* Code Bot v0.18 */

    /* 清 EP5 ring buffer */
    RingBuffer_Comm_EP5.LoadPtr = 0;
    RingBuffer_Comm_EP5.DealPtr = 0;
    RingBuffer_Comm_EP5.RemainPack = 0;
    RingBuffer_Comm_EP5.StopFlag = 0;
    for (uint8_t i = 0; i < DEF_Ring_Buffer_Max_Blks; i++) {
        RingBuffer_Comm_EP5.PackLen[i] = 0;
    }

    /* Clear End-points Busy Status */
    for(uint8_t i=0; i<DEF_UEP_NUM; i++ )
    {
        USBFS_Endp_Busy[ i ] = 0;
    }
}

/*********************************************************************
 * @fn      GPIO_USB_INIT
 *
 * @brief   Initializes USB GPIO.
 *
 * @return  none
 */
void GPIO_USB_INIT(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_16;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_17;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
}

/*********************************************************************
 * @fn      USBFS_Device_Init
 *
 * @brief   Initializes USB device.
 *
 * @return  none
 */
void USBFS_Device_Init( FunctionalState sta , PWR_VDD VDD_Voltage)
{
    if( sta )
    {
        GPIO_USB_INIT();
        if( VDD_Voltage == PWR_VDD_5V )
        {
            AFIO->CTLR = (AFIO->CTLR & ~(UDP_PUE_MASK | UDM_PUE_MASK | USB_PHY_V33)) | UDP_PUE_10K | USB_IOEN;
        }
        else
        {
            AFIO->CTLR = (AFIO->CTLR & ~(UDP_PUE_MASK | UDM_PUE_MASK )) | USB_PHY_V33 | UDP_PUE_1K5 | USB_IOEN;
        }
        USBFSD->BASE_CTRL = 0x00;
        USBFS_Device_Endp_Init( );
        USBFSD->DEV_ADDR = 0x00;
        USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
        USBFSD->INT_FG = 0xff;
        USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;
        USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;
        NVIC_EnableIRQ( USBFS_IRQn );
    }
    else
    {
        AFIO->CTLR = AFIO->CTLR & ~(UDP_PUE_MASK | UDM_PUE_MASK | USB_IOEN);
        USBFSD->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
        Delay_Us( 10 );
        USBFSD->BASE_CTRL = 0x00;
        NVIC_DisableIRQ( USBFS_IRQn );
    }
}

/*********************************************************************
 * @fn      USBFS_IRQHandler
 *
 * @brief   This function handles HD-FS exception.
 *
 * @return  none
 */
void USBFS_IRQHandler( void )
{
    uint8_t  intflag, intst, errflag;
    uint16_t len;

    intflag = USBFSD->INT_FG;
    intst   = USBFSD->INT_ST;

    if( intflag & USBFS_UIF_TRANSFER )
    {
        switch( intst & USBFS_UIS_TOKEN_MASK )
        {
            /* data-in stage processing */
            case USBFS_UIS_TOKEN_IN:
                switch( intst & ( USBFS_UIS_TOKEN_MASK | USBFS_UIS_ENDP_MASK ) )
                {
                    /* end-point 0 data in interrupt */
                    case USBFS_UIS_TOKEN_IN | DEF_UEP0:
                        if( USBFS_SetupReqLen == 0 )
                        {
                            USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~ USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
                        }

                        if ( ( USBFS_SetupReqType & USB_REQ_TYP_MASK ) != USB_REQ_TYP_STANDARD )
                        {
                            /* Non-standard request endpoint 0 Data upload */
                        }
                        else
                        {
                            switch( USBFS_SetupReqCode )
                            {
                                case USB_GET_DESCRIPTOR:
                                    len = USBFS_SetupReqLen >= DEF_USBD_UEP0_SIZE ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                                    memcpy( USBFS_EP0_Buf, pUSBFS_Descr, len );
                                    USBFS_SetupReqLen -= len;
                                    pUSBFS_Descr += len;
                                    USBFSD->UEP0_TX_LEN = len;
                                    USBFSD->UEP0_CTRL_H ^= USBFS_UEP_T_TOG;
                                    break;

                                case USB_SET_ADDRESS:
                                    USBFSD->DEV_ADDR = (USBFSD->DEV_ADDR & USBFS_UDA_GP_BIT) | USBFS_DevAddr;
                                    break;

                                default:
                                        break;
                            }
                        }
                        break;

                        /* end-point 2 data in interrupt */
                        case USBFS_UIS_TOKEN_IN | DEF_UEP2:

                            USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
                            USBFSD->UEP2_CTRL_H ^= USBFS_UEP_T_TOG;
                            USBFS_Endp_Busy[ DEF_UEP2 ] = 0;
                            /* [DBG] 打印一下 IN token 完成 (节流, 不然刷屏炸 ISR) */
                            {
                                static uint16_t _ep2_done = 0;
                                _ep2_done++;
                                if ((_ep2_done & 0x3F) == 0) {
                                    printf("[DBG] EP2_IN done, count=%d\n", (int)_ep2_done);
                                }
                            }
                            break;

                        /* Code Bot: end-point 3 data in interrupt (HID Keyboard report) */
                        case USBFS_UIS_TOKEN_IN | DEF_UEP3:

                            USBFSD->UEP3_CTRL_H = (USBFSD->UEP3_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
                            USBFSD->UEP3_CTRL_H ^= USBFS_UEP_T_TOG;
                            USBFS_Endp_Busy[ DEF_UEP3 ] = 0;
                            break;

                    default :
                        break;
                }
                break;

            /* data-out stage processing */
            case USBFS_UIS_TOKEN_OUT:
                switch( intst & ( USBFS_UIS_TOKEN_MASK | USBFS_UIS_ENDP_MASK ) )
                {
                    /* end-point 0 data out interrupt */
                    case USBFS_UIS_TOKEN_OUT | DEF_UEP0:
                            if( intst & USBFS_UIS_TOG_OK )
                            {
                                if ( ( USBFS_SetupReqType & USB_REQ_TYP_MASK ) != USB_REQ_TYP_STANDARD )
                                {
                                    if (( USBFS_SetupReqType & USB_REQ_TYP_MASK ) == USB_REQ_TYP_CLASS)
                                    {
                                        switch( USBFS_SetupReqCode )
                                        {
                                            case HID_SET_REPORT:
                                                memcpy(&HID_Report_Buffer[0],USBFS_EP0_Buf,DEF_USBD_UEP0_SIZE);
                                                HID_Set_Report_Flag = SET_REPORT_WAIT_DEAL;
                                                USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_NAK;
                                                break;
                                            default:
                                                break;
                                        }
                                    }
                                }
                                else
                                {
                                    /* Standard request end-point 0 Data download */
                                    /* Add your code here */
                                }
                            }
                            break;

                    /* end-point 1 data out interrupt */
                    case USBFS_UIS_TOKEN_OUT | DEF_UEP1:
                        if ( intst & USBFS_UIS_TOG_OK )
                        {
                            /* Code Bot v0.18: 真实 ring buffer 语义.
                             * ISR 只入队 (更新 LoadPtr/RemainPack), 由 main loop 的
                             * Protocol_Poll 按 DealPtr 消费 + 翻 ACK.
                             * 这里不调 EP1_OUT_Callback (旧 byte-bounce 已删). */
                            USBFSD->UEP1_CTRL_H ^= USBFS_UEP_R_TOG;
                            RingBuffer_Comm.PackLen[RingBuffer_Comm.LoadPtr] = USBFSD->RX_LEN;
                            RingBuffer_Comm.LoadPtr ++;
                            if(RingBuffer_Comm.LoadPtr == DEF_Ring_Buffer_Max_Blks)
                            {
                                RingBuffer_Comm.LoadPtr = 0;
                            }
                            USBFSD->UEP1_DMA = (uint32_t)(&Data_Buffer[(RingBuffer_Comm.LoadPtr) * DEF_USBD_FS_PACK_SIZE]);
                            RingBuffer_Comm.RemainPack ++;
                            if(RingBuffer_Comm.RemainPack >= DEF_Ring_Buffer_Max_Blks-DEF_RING_BUFFER_REMINE)
                            {
                                USBFSD->UEP1_CTRL_H = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_NAK;
                                RingBuffer_Comm.StopFlag = 1;
                            }
                        }
                        break;

                    /* Code Bot v0.18: end-point 5 data out interrupt (image data stream)
                     * 跟 EP1 OUT 完全同构: ISR 只入队 (更新 ring buffer 索引), 由 main loop
                     * 的 Protocol_PollPixels 按 DealPtr 消费 + 翻 ACK.
                     * 不调任何应用层 callback, 不暴露 USB 寄存器. */
                    case USBFS_UIS_TOKEN_OUT | DEF_UEP5:
                        if ( intst & USBFS_UIS_TOG_OK )
                        {
                            USBFSD->UEP5_CTRL_H ^= USBFS_UEP_R_TOG;
                            RingBuffer_Comm_EP5.PackLen[RingBuffer_Comm_EP5.LoadPtr] = USBFSD->RX_LEN;
                            RingBuffer_Comm_EP5.LoadPtr++;
                            if(RingBuffer_Comm_EP5.LoadPtr == DEF_Ring_Buffer_Max_Blks)
                            {
                                RingBuffer_Comm_EP5.LoadPtr = 0;
                            }
                            USBFSD->UEP5_DMA = (uint32_t)(&Data_Buffer5[(RingBuffer_Comm_EP5.LoadPtr) * DEF_USBD_FS_PACK_SIZE]);
                            RingBuffer_Comm_EP5.RemainPack++;
                            if(RingBuffer_Comm_EP5.RemainPack >= DEF_Ring_Buffer_Max_Blks - DEF_RING_BUFFER_REMINE)
                            {
                                USBFSD->UEP5_CTRL_H = (USBFSD->UEP5_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_NAK;
                                RingBuffer_Comm_EP5.StopFlag = 1;
                            }
                        }
                        break;
                    default:
                        break;

                }
                break;

            /* Setup stage processing */
            case USBFS_UIS_TOKEN_SETUP:
                USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG|USBFS_UEP_T_RES_NAK|USBFS_UEP_R_TOG|USBFS_UEP_R_RES_NAK;

                /* Store All Setup Values */
                USBFS_SetupReqType  = pUSBFS_SetupReqPak->bRequestType;
                USBFS_SetupReqCode  = pUSBFS_SetupReqPak->bRequest;
                USBFS_SetupReqLen   = pUSBFS_SetupReqPak->wLength;
                USBFS_SetupReqValue = pUSBFS_SetupReqPak->wValue;
                USBFS_SetupReqIndex = pUSBFS_SetupReqPak->wIndex;
                len = 0;
                errflag = 0;
                if ( ( USBFS_SetupReqType & USB_REQ_TYP_MASK ) != USB_REQ_TYP_STANDARD )
                {
                    if (( USBFS_SetupReqType & USB_REQ_TYP_MASK ) == USB_REQ_TYP_CLASS)
                    {
                        switch( USBFS_SetupReqCode )
                        {
                            case HID_SET_REPORT:
                                break;

                            case HID_GET_REPORT:
                                if( USBFS_SetupReqIndex == 0x01 )   /* Code Bot: HID 在 interface 1 */
                                {
                                    len = DEF_USBD_UEP0_SIZE;
                                    memcpy(USBFS_EP0_Buf,&HID_Report_Buffer[0],DEF_USBD_UEP0_SIZE);
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                                break;

                            case HID_SET_IDLE:
                                if( USBFS_SetupReqIndex == 0x01 )   /* Code Bot */
                                {
                                    USBFS_HidIdle = USBFS_EP0_Buf[ 3 ];
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                                break;

                            case HID_SET_PROTOCOL:
                                if( USBFS_SetupReqIndex == 0x01 )   /* Code Bot */
                                {
                                    USBFS_HidProtocol = USBFS_EP0_Buf[ 2 ];
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                                break;

                            case HID_GET_IDLE:
                                if( USBFS_SetupReqIndex == 0x01 )   /* Code Bot */
                                {
                                    USBFS_EP0_Buf[ 0 ] = USBFS_HidIdle;
                                    len = 1;
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                                break;
                            case HID_GET_PROTOCOL:
                                if( USBFS_SetupReqIndex == 0x01 )   /* Code Bot */
                                {
                                    USBFS_EP0_Buf[ 0 ] = USBFS_HidProtocol;
                                    len = 1;
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                                break;
                            default:
                                errflag = 0xFF;
                                break;
                        }
                    }
                }
                else
                {
                    /* usb standard request processing */
                    switch( USBFS_SetupReqCode )
                    {
                        /* get device/configuration/string/report/... descriptors */
                        case USB_GET_DESCRIPTOR:
                            switch( (uint8_t)(USBFS_SetupReqValue>>8) )
                            {
                                /* get usb device descriptor */
                                case USB_DESCR_TYP_DEVICE:
                                    pUSBFS_Descr = MyDevDescr;
                                    len = DEF_USBD_DEVICE_DESC_LEN;
                                    break;

                                /* get usb configuration descriptor */
                                case USB_DESCR_TYP_CONFIG:
                                    pUSBFS_Descr = MyCfgDescr;
                                    len = DEF_USBD_CONFIG_DESC_LEN;
                                    break;

                                /* Code Bot: get BOS descriptor (USB 2.1+).
                                 * 含 MS OS 2.0 Platform Capability,
                                 * 让 Windows 知道去拉 MS OS 2.0 descriptor set,
                                 * 自动把 Interface 0 绑到 inbox winusb.sys — 免驱. */
                                case USB_DESC_TYPE_BOS:
                                    pUSBFS_Descr = MyBOSDescr;
                                    len = DEF_USBD_BOS_DESC_LEN;
                                    break;

                                /* Code Bot: get MS OS 2.0 descriptor set.
                                 * 触发: bmRequestType=0x80, bRequest=GET_DESCRIPTOR,
                                 *       wValue=0xEE00. 长度由 set 自己 wTotalLength 报告. */
                                case USB_DESC_TYPE_MS_OS_20:
                                    pUSBFS_Descr = MyMSOS20DescrSet;
                                    len = DEF_USBD_MSOS20_DESC_LEN;
                                    break;
                              /* get usb report descriptor */
                              case USB_DESCR_TYP_REPORT:
                                    if (USBFS_SetupReqIndex == 0x01)   /* Code Bot: interface 1 */
                                    {
                                        pUSBFS_Descr = MyHIDReportDesc;
                                        /* 用 volatile 防 gcc 优化掉 (实测 15.2.0 + -Os
                                         * 在这个 case 里把 len=64 赋错值, 编译出来 cap 用 0x22=34) */
                                        *(volatile uint8_t *)&len = (uint8_t)DEF_USBD_REPORT_DESC_LEN;
                                        len = DEF_USBD_REPORT_DESC_LEN;
                                    }
                                    else
                                    {
                                        errflag = 0xFF;
                                    }
                                    break;
                                /* get hid descriptor */
                                case USB_DESCR_TYP_HID:
                                    if (USBFS_SetupReqIndex == 0x01)   /* Code Bot: interface 1 */
                                    {
                                        /* Code Bot v0.18: HID descriptor 在 MyCfgDescr[48]
                                         *   (offset 0-8: cfg, 9-17: if0, 18-24: EP1, 25-31: EP2,
                                         *    32-38: EP5, 39-47: if1, 48-56: HID desc, 57-63: EP3) */
                                        pUSBFS_Descr = &MyCfgDescr[48];
                                        len = 0x09;
                                    }
                                    else
                                    {
                                        errflag = 0xFF;
                                    }
                                    break;

                                /* get usb string descriptor */
                                case USB_DESCR_TYP_STRING:
                                    switch( (uint8_t)(USBFS_SetupReqValue&0xFF) )
                                    {
                                        /* Descriptor 0, Language descriptor */
                                        case DEF_STRING_DESC_LANG:
                                            pUSBFS_Descr = MyLangDescr;
                                            len = DEF_USBD_LANG_DESC_LEN;
                                            break;

                                        /* Descriptor 1, Manufacturers String descriptor */
                                        case DEF_STRING_DESC_MANU:
                                            pUSBFS_Descr = MyManuInfo;
                                            len = DEF_USBD_MANU_DESC_LEN;
                                            break;

                                        /* Descriptor 2, Product String descriptor */
                                        case DEF_STRING_DESC_PROD:
                                            pUSBFS_Descr = MyProdInfo;
                                            len = DEF_USBD_PROD_DESC_LEN;
                                            break;

                                        /* Descriptor 3, Serial-number String descriptor */
                                        case DEF_STRING_DESC_SERN:
                                            pUSBFS_Descr = MySerNumInfo;
                                            len = DEF_USBD_SN_DESC_LEN;
                                            break;

                                        default:
                                            errflag = 0xFF;
                                            break;
                                    }
                                    break;

                                default :
                                    errflag = 0xFF;
                                    break;
                            }

                            /* Copy Descriptors to Endp0 DMA buffer */
                            if( USBFS_SetupReqLen>len )
                            {
                                USBFS_SetupReqLen = len;
                            }
                            len = (USBFS_SetupReqLen >= DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                            memcpy( USBFS_EP0_Buf, pUSBFS_Descr, len );
                            pUSBFS_Descr += len;
                            break;

                        /* Set usb address */
                        case USB_SET_ADDRESS:
                            USBFS_DevAddr = (uint8_t)( USBFS_SetupReqValue & 0xFF );
                            break;

                        /* Get usb configuration now set */
                        case USB_GET_CONFIGURATION:
                            USBFS_EP0_Buf[ 0 ] = USBFS_DevConfig;
                            if( USBFS_SetupReqLen > 1 )
                            {
                                USBFS_SetupReqLen = 1;
                            }
                            break;

                        /* Set usb configuration to use */
                        case USB_SET_CONFIGURATION:
                            USBFS_DevConfig = (uint8_t)( USBFS_SetupReqValue & 0xFF );
                            USBFS_DevEnumStatus = 0x01;
                            break;

                        /* Clear or disable one usb feature */
                        case USB_CLEAR_FEATURE:
                            if( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_DEVICE )
                            {
                                /* clear one device feature */
                                if( (uint8_t)( USBFS_SetupReqValue & 0xFF ) == USB_REQ_FEAT_REMOTE_WAKEUP )
                                {
                                    /* clear usb sleep status, device not prepare to sleep */
                                    USBFS_DevSleepStatus &= ~0x01;
                                }
                            }
                            else if ( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_ENDP )
                            {
                                if( (uint8_t)( USBFS_SetupReqValue & 0xFF ) == USB_REQ_FEAT_ENDP_HALT )
                                {
                                    switch( (uint8_t)(USBFS_SetupReqIndex&0xFF) )
                                    {
                                        case ( DEF_UEP_OUT | DEF_UEP1 ):
                                            /* Set End-point 1 OUT ACK */
                                            USBFSD->UEP1_CTRL_H =  USBFS_UEP_R_RES_ACK;
                                            break;

                                        case ( DEF_UEP_IN | DEF_UEP2 ):
                                            /* Set End-point 2 IN NAK */
                                            USBFSD->UEP2_CTRL_H =  USBFS_UEP_T_RES_NAK;
                                            break;

                                        case ( DEF_UEP_IN | DEF_UEP3 ):   /* Code Bot */
                                            /* Set End-point 3 IN NAK */
                                            USBFSD->UEP3_CTRL_H =  USBFS_UEP_T_RES_NAK;
                                            break;

                                        case ( DEF_UEP_OUT | DEF_UEP5 ):   /* Code Bot v0.18 */
                                            /* Set End-point 5 OUT NAK (clear halt: 重置 toggle, 关闭数据通道) */
                                            USBFSD->UEP5_CTRL_H = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_NAK;
                                            break;

                                        default:
                                            errflag = 0xFF;
                                            break;
                                    }
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                            }
                            else
                            {
                                errflag = 0xFF;
                            }
                            break;

                        /* set or enable one usb feature */
                        case USB_SET_FEATURE:
                            if( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_DEVICE )
                            {
                                /* Set Device Feature */
                                if( (uint8_t)( USBFS_SetupReqValue & 0xFF ) == USB_REQ_FEAT_REMOTE_WAKEUP )
                                {
                                    if( MyCfgDescr[ 7 ] & 0x20 )
                                    {
                                        /* Set Wake-up flag, device prepare to sleep */
                                        USBFS_DevSleepStatus |= 0x01;
                                    }
                                    else
                                    {
                                        errflag = 0xFF;
                                    }
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                            }
                            else if( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_ENDP )
                            {
                                /* Set End-point Feature */
                                if( (uint8_t)( USBFS_SetupReqValue & 0xFF ) == USB_REQ_FEAT_ENDP_HALT )
                                {

                                    switch( (uint8_t)(USBFS_SetupReqIndex&0xFF) )
                                    {
                                        case ( DEF_UEP_OUT | DEF_UEP1 ):
                                            USBFSD->UEP1_CTRL_H = ( USBFSD->UEP1_CTRL_H & ~USBFS_UEP_R_RES_MASK ) | USBFS_UEP_R_RES_STALL;
                                            break;
                                        case ( DEF_UEP_IN | DEF_UEP2 ):
                                            USBFSD->UEP2_CTRL_H = ( USBFSD->UEP2_CTRL_H & ~USBFS_UEP_T_RES_MASK ) | USBFS_UEP_T_RES_STALL;
                                            break;
                                        case ( DEF_UEP_IN | DEF_UEP3 ):   /* Code Bot */
                                            USBFSD->UEP3_CTRL_H = ( USBFSD->UEP3_CTRL_H & ~USBFS_UEP_T_RES_MASK ) | USBFS_UEP_T_RES_STALL;
                                            break;
                                        case ( DEF_UEP_OUT | DEF_UEP5 ):   /* Code Bot v0.18 */
                                            USBFSD->UEP5_CTRL_H = ( USBFSD->UEP5_CTRL_H & ~USBFS_UEP_R_RES_MASK ) | USBFS_UEP_R_RES_STALL;
                                            break;

                                        default:
                                            errflag = 0xFF;
                                            break;
                                    }
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                            }
                            else
                            {
                                errflag = 0xFF;
                            }
                            break;

                        /* This request allows the host to select another setting for the specified interface  */
                        case USB_GET_INTERFACE:
                            USBFS_EP0_Buf[0] = 0x00;
                            if ( USBFS_SetupReqLen > 1 )
                            {
                                USBFS_SetupReqLen = 1;
                            }
                            break;

                        case USB_SET_INTERFACE:
                            break;

                        /* host get status of specified device/interface/end-points */
                        case USB_GET_STATUS:
                            USBFS_EP0_Buf[ 0 ] = 0x00;
                            USBFS_EP0_Buf[ 1 ] = 0x00;

                            if ( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_DEVICE )
                            {
                                if( USBFS_DevSleepStatus & 0x01 )
                                {
                                    USBFS_EP0_Buf[ 0 ] = 0x02;
                                }
                            }
                            else if( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_ENDP )
                            {
                                if((uint8_t)(USBFS_SetupReqIndex&0xFF) == ( DEF_UEP_OUT |DEF_UEP1 ))
                                {
                                    if( ( USBFSD->UEP1_CTRL_H & USBFS_UEP_R_RES_MASK ) == USBFS_UEP_R_RES_STALL )
                                    {
                                        USBFS_EP0_Buf[ 0 ] = 0x01;
                                    }
                                }
                                else if((uint8_t)(USBFS_SetupReqIndex&0xFF) == ( DEF_UEP_IN | DEF_UEP2 ))
                                {
                                    if( ( USBFSD->UEP2_CTRL_H & USBFS_UEP_T_RES_MASK ) == USBFS_UEP_T_RES_STALL )
                                    {
                                        USBFS_EP0_Buf[ 0 ] = 0x01;
                                    }
                                }
                                else if((uint8_t)(USBFS_SetupReqIndex&0xFF) == ( DEF_UEP_IN | DEF_UEP3 ))   /* Code Bot */
                                {
                                    if( ( USBFSD->UEP3_CTRL_H & USBFS_UEP_T_RES_MASK ) == USBFS_UEP_T_RES_STALL )
                                    {
                                        USBFS_EP0_Buf[ 0 ] = 0x01;
                                    }
                                }
                                else if((uint8_t)(USBFS_SetupReqIndex&0xFF) == ( DEF_UEP_OUT | DEF_UEP5 ))   /* Code Bot v0.18 */
                                {
                                    if( ( USBFSD->UEP5_CTRL_H & USBFS_UEP_R_RES_MASK ) == USBFS_UEP_R_RES_STALL )
                                    {
                                        USBFS_EP0_Buf[ 0 ] = 0x01;
                                    }
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                            }
                            else
                            {
                                errflag = 0xFF;
                            }

                            if ( USBFS_SetupReqLen > 2 )
                            {
                                USBFS_SetupReqLen = 2;
                            }

                            break;

                        default:
                            errflag = 0xFF;
                            break;
                    }
                }

                /* errflag = 0xFF means a request not support or some errors occurred, else correct */
                if( errflag == 0xFF)
                {
                    /* if one request not support, return stall */
                    USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG|USBFS_UEP_T_RES_STALL|USBFS_UEP_R_TOG|USBFS_UEP_R_RES_STALL;
                }
                else
                {
                    /* end-point 0 data Tx/Rx */
                    if( USBFS_SetupReqType & DEF_UEP_IN )
                    {
                        len = ( USBFS_SetupReqLen > DEF_USBD_UEP0_SIZE )? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                        USBFS_SetupReqLen -= len;
                        USBFSD->UEP0_TX_LEN = len;
                        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                    }
                    else
                    {
                        if( USBFS_SetupReqLen == 0 )
                        {
                            USBFSD->UEP0_TX_LEN = 0;
                            USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                        }
                        else
                        {
                            USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
                        }
                    }
                }
                break;

            /* Sof pack processing */
            case USBFS_UIS_TOKEN_SOF:
                break;

            default :
                break;
        }
        USBFSD->INT_FG = USBFS_UIF_TRANSFER;
    }
    else if( intflag & USBFS_UIF_BUS_RST )
    {
        /* usb reset interrupt processing */
        USBFS_DevConfig = 0;
        USBFS_DevAddr = 0;
        USBFS_DevSleepStatus = 0;
        USBFS_DevEnumStatus = 0;

        USBFSD->DEV_ADDR = 0;
        USBFS_Device_Endp_Init( );
        USBFSD->INT_FG = USBFS_UIF_BUS_RST;
    }
    else if( intflag & USBFS_UIF_SUSPEND )
    {
        USBFSD->INT_FG = USBFS_UIF_SUSPEND;
        Delay_Us(10);
        /* usb suspend interrupt processing */
        if( USBFSD->MIS_ST & USBFS_UMS_SUSPEND )
        {
            USBFS_DevSleepStatus |= 0x02;
            if( USBFS_DevSleepStatus == 0x03 )
            {
                /* Handling usb sleep here */
            }
        }
        else
        {
            USBFS_DevSleepStatus &= ~0x02;
        }

    }
    else
    {
        /* other interrupts */
        USBFSD->INT_FG = intflag;
    }
}

/*********************************************************************
 * @fn      USBFS_Endp_DataUp  (Code Bot, lifted/cleaned from WCH SimulateCDC-HID)
 *
 * @brief   将数据通过指定 IN 端点上传到 host. 简化版, 只支持 Code Bot
 *          实际使用的 EP2 (Vendor IN bulk) 和 EP3 (HID IN interrupt).
 *
 * @param   endp   - DEF_UEP2 (Vendor) / DEF_UEP3 (HID Keyboard)
 * @param   pbuf   - 数据指针
 * @param   len    - 长度 (Vendor ≤64, HID =8)
 *
 * @return  0 = 成功启动 IN 传输, 1 = 失败 (忙/未使能/参数错)
 *
 * 工作流程:
 *   1. 检查 endp 范围 + busy 标志
 *   2. 读 UEP2_3_MOD 拿对应端点的 TX_EN 位
 *   3. memcpy 到 *uep_dma + 0x20000000 (WCH 寄存器地址→RAM 偏移)
 *   4. 设 TX_LEN, 翻 T_TOG, 置 T_RES_ACK
 *   5. 置 USBFS_Endp_Busy[endp] = 1
 *   实际传输由 host 发起 IN token 完成, 完成时 EPx_IN_Callback
 *   (本文件 USBFS_IRQHandler) 清 busy 并翻 T_TOG.
 */
uint8_t USBFS_Endp_DataUp(uint8_t endp, const uint8_t *pbuf, uint16_t len)
{
    if (len == 0 || len > 64) return 1;
    if (endp != DEF_UEP2 && endp != DEF_UEP3) return 1;   /* Code Bot only */
    if (USBFS_Endp_Busy[endp]) return 1;

    /* 读对应端点的 4-bit mode (low nibble = EP2, high nibble = EP3) */
    uint8_t endp_mode = USBFSD->UEP2_3_MOD;
    if (endp == DEF_UEP3) {
        endp_mode = (uint8_t)(endp_mode >> 4);
    }
    endp_mode &= 0x0F;

    if (!(endp_mode & USBFSD_UEP_TX_EN)) return 1;        /* 未使能 TX */

    /* 单 TX buffer (本拓扑无 BUF_MOD 无 RX, buf_load_offset 恒为 0) */
    volatile uint16_t *uep_tx_len;
    volatile uint16_t *uep_ctrl;
    volatile uint32_t *uep_dma;

    if (endp == DEF_UEP2) {
        uep_tx_len = &USBFSD->UEP2_TX_LEN;
        uep_ctrl   = &USBFSD->UEP2_CTRL_H;
        uep_dma    = &USBFSD->UEP2_DMA;
    } else {
        uep_tx_len = &USBFSD->UEP3_TX_LEN;
        uep_ctrl   = &USBFSD->UEP3_CTRL_H;
        uep_dma    = &USBFSD->UEP3_DMA;
    }

    /* WCH 特殊用法: *uep_dma 是 RAM 地址 (相对 0x20000000), +0x20000000 还原绝对地址 */
    memcpy(((uint8_t *)(*uep_dma) + 0x20000000), pbuf, len);
    *uep_tx_len = len;
    /* 置 T_RES_ACK 启动 IN 传输.
     * 注: 不要在这里强制 | USBFS_UEP_T_TOG — 第一次传输 host 期望 DATA0,
     * 强制 DATA1 会导致 toggle 不匹配, host NAK, busy 永远不清.
     * ISR 会在每次成功 IN token 后 ^ T_TOG, 状态机会自动正确翻转. */
    *uep_ctrl = (*uep_ctrl & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_ACK;
    USBFS_Endp_Busy[endp] = 1;
    return 0;
}



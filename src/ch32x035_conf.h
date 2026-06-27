/**
 * @file    ch32x035_conf.h
 * @brief   外设 include 配置 - 选择启用的外设头文件
 * @note    参考 WCH EVT 示例, 仅包含项目用到的外设
 */

#ifndef __CH32X035_CONF_H
#define __CH32X035_CONF_H

#include "ch32x035_adc.h"
#include "ch32x035_dbgmcu.h"
#include "ch32x035_dma.h"
#include "ch32x035_exti.h"
#include "ch32x035_flash.h"
#include "ch32x035_gpio.h"
#include "ch32x035_i2c.h"
#include "ch32x035_misc.h"
#include "ch32x035_pwr.h"
#include "ch32x035_rcc.h"
#include "ch32x035_spi.h"
#include "ch32x035_tim.h"
#include "ch32x035_usart.h"
#include "ch32x035_wwdg.h"

/* USB 头文件由 usb/ 子目录提供 */
#include "ch32x035_usb.h"

#endif /* __CH32X035_CONF_H */

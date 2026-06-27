/**
 * @file    touch.h
 * @brief   CST816D 触摸驱动 (I2C1, EXTI0 on PB0)
 *
 * I2C 地址: 0x15 (7-bit)
 * 寄存器布局 (Waveshare 校对):
 *   0x01 GestureID
 *   0x02 FingerNum
 *   0x03 XposH
 *   0x04 XposL
 *   0x05 YposH
 *   0x06 YposL
 *
 * 内建手势: None/SlideUp/SlideDown/SlideLeft/SlideRight/Click/DoubleClick/LongPress
 */

#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>
#include "pinout.h"

/* CST816D 寄存器 (Waveshare CST816S_register_declaration.pdf 校对) */
#define CST816D_REG_GESTURE   0x01
#define CST816D_REG_FINGER    0x02
#define CST816D_REG_XPOSH     0x03
#define CST816D_REG_XPOSL     0x04
#define CST816D_REG_YPOSH     0x05
#define CST816D_REG_YPOSL     0x06
#define CST816D_REG_CHIPID    0xA7
#define CST816D_REG_MOTION    0xEC  /* 手势使能 */

/* GestureID 值 */
#define CST816D_GESTURE_NONE          0x00
#define CST816D_GESTURE_SLIDE_UP      0x01
#define CST816D_GESTURE_SLIDE_DOWN    0x02
#define CST816D_GESTURE_SLIDE_LEFT    0x03
#define CST816D_GESTURE_SLIDE_RIGHT   0x04
#define CST816D_GESTURE_SINGLE_CLICK  0x05
#define CST816D_GESTURE_DOUBLE_CLICK  0x0B
#define CST816D_GESTURE_LONG_PRESS    0x0C

/* 触摸坐标范围 */
#define TOUCH_X_MAX         LCD_WIDTH    /* 320 */
#define TOUCH_Y_MAX         LCD_HEIGHT   /* 172 */

/* 初始化 (硬件复位 + I2C + EXTI0) */
void Touch_Init(void);

/* 主循环调用: 检查 EXTI flag 并上报触摸事件 */
void Touch_Poll(void);

/* EXTI0 中断处理 (在 ch32x035_it.c 调用) */
void Touch_EXTI_Handler(void);

#endif /* TOUCH_H */

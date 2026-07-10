/**
 * @file    touch.c
 * @brief   CST816D 触摸驱动实现
 *
 * 关键寄存器布局 (Waveshare CST816S 校对):
 *   0x01 GestureID
 *   0x02 FingerNum
 *   0x03 XposH
 *   0x04 XposL
 *   0x05 YposH
 *   0x06 YposL
 *
 * GestureID:
 *   0x00 None
 *   0x01 SlideUp
 *   0x02 SlideDown
 *   0x03 SlideLeft
 *   0x04 SlideRight
 *   0x05 Click
 *   0x0B DoubleClick
 *   0x0C LongPress
 */

#include "touch.h"
#include "lcd_driver.h"
#include "ch32x035_conf.h"
#include "protocol/proto.h"
#include <stdio.h>
#include <string.h>

/* ===== 硬件 I2C1 (PA10/PA11) ===== */
/* 注: CH32X035 没有 GPIO_Mode_AF_OD, 用 AF_PP + 外置上拉 (I2C 标配) */
#define USE_HW_I2C    1

#if USE_HW_I2C
/* I2C1 初始化 */
static void Touch_I2C_Init(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    I2C_InitTypeDef  I2C_InitTSturcture = {0};

    /* 使能 I2C1 + GPIOA 时钟 */
    RCC_APB2PeriphClockCmd(TP_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(TP_I2C_APB, ENABLE);

    /* PA10 (SCL), PA11 (SDA) - AF_PP + 外置上拉 (CH32X035 无 AF_OD) */
    GPIO_InitStructure.GPIO_Pin   = PIN_TP_SCL | PIN_TP_SDA;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(TP_I2C_PORT, &GPIO_InitStructure);

    /* I2C1 配置: 400kHz */
    I2C_InitTSturcture.I2C_ClockSpeed = TP_I2C_CLOCK_400K;
    I2C_InitTSturcture.I2C_Mode       = I2C_Mode_I2C;
    I2C_InitTSturcture.I2C_DutyCycle  = I2C_DutyCycle_2;
    I2C_InitTSturcture.I2C_OwnAddress1 = 0x00;
    I2C_InitTSturcture.I2C_Ack        = I2C_Ack_Enable;
    I2C_InitTSturcture.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(TP_I2C, &I2C_InitTSturcture);

    I2C_Cmd(TP_I2C, ENABLE);

    I2C_AcknowledgeConfig(TP_I2C, ENABLE);
}

/* 写 1 字节到寄存器 */
static int I2C_WriteReg(uint8_t dev_addr, uint8_t reg, uint8_t val) {
    uint32_t timeout = 10000;
    while (I2C_GetFlagStatus(TP_I2C, I2C_FLAG_BUSY)) {
        if (--timeout == 0) return -1;
    }
    I2C_GenerateSTART(TP_I2C, ENABLE);
    timeout = 10000;
    while (!I2C_CheckEvent(TP_I2C, I2C_EVENT_MASTER_MODE_SELECT)) {
        if (--timeout == 0) return -1;
    }
    I2C_Send7bitAddress(TP_I2C, dev_addr << 1, I2C_Direction_Transmitter);
    timeout = 10000;
    while (!I2C_CheckEvent(TP_I2C, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        if (--timeout == 0) return -1;
    }
    I2C_SendData(TP_I2C, reg);
    timeout = 10000;
    while (!I2C_CheckEvent(TP_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if (--timeout == 0) return -1;
    }
    I2C_SendData(TP_I2C, val);
    timeout = 10000;
    while (!I2C_CheckEvent(TP_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if (--timeout == 0) return -1;
    }
    I2C_GenerateSTOP(TP_I2C, ENABLE);
    return 0;
}

/* 从寄存器读 N 字节 */
static int I2C_ReadRegMulti(uint8_t dev_addr, uint8_t reg, uint8_t *buf, uint8_t len) {
    uint32_t timeout = 10000;

    /* 写寄存器地址 */
    while (I2C_GetFlagStatus(TP_I2C, I2C_FLAG_BUSY)) {
        if (--timeout == 0) return -1;
    }
    I2C_GenerateSTART(TP_I2C, ENABLE);
    timeout = 10000;
    while (!I2C_CheckEvent(TP_I2C, I2C_EVENT_MASTER_MODE_SELECT)) {
        if (--timeout == 0) return -1;
    }
    I2C_Send7bitAddress(TP_I2C, dev_addr << 1, I2C_Direction_Transmitter);
    timeout = 10000;
    while (!I2C_CheckEvent(TP_I2C, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        if (--timeout == 0) return -1;
    }
    I2C_SendData(TP_I2C, reg);
    timeout = 10000;
    while (!I2C_CheckEvent(TP_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if (--timeout == 0) return -1;
    }

    /* 重启 + 读 */
    I2C_GenerateSTART(TP_I2C, ENABLE);
    timeout = 10000;
    while (!I2C_CheckEvent(TP_I2C, I2C_EVENT_MASTER_MODE_SELECT)) {
        if (--timeout == 0) return -1;
    }
    I2C_Send7bitAddress(TP_I2C, dev_addr << 1, I2C_Direction_Receiver);
    timeout = 10000;
    while (!I2C_CheckEvent(TP_I2C, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) {
        if (--timeout == 0) return -1;
    }

    for (uint8_t i = 0; i < len; i++) {
        if (i == len - 1) {
            I2C_AcknowledgeConfig(TP_I2C, DISABLE);
        }
        timeout = 10000;
        while (!I2C_CheckEvent(TP_I2C, I2C_EVENT_MASTER_BYTE_RECEIVED)) {
            if (--timeout == 0) return -1;
        }
        buf[i] = I2C_ReceiveData(TP_I2C);
    }
    I2C_AcknowledgeConfig(TP_I2C, ENABLE);
    I2C_GenerateSTOP(TP_I2C, ENABLE);
    return 0;
}

#endif /* USE_HW_I2C */

/* ===== EXTI 配置 (PB0) ===== */

static volatile uint8_t s_touch_irq_flag = 0;

void Touch_EXTI_Handler(void) {
    s_touch_irq_flag = 1;
}

static void Touch_EXTI_Init(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    EXTI_InitTypeDef EXTI_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    /* PB0 浮空输入 (CST816D INT 是开漏输出) */
    GPIO_InitStructure.GPIO_Pin  = PIN_TP_INT;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;  /* 上拉输入 */
    GPIO_Init(TP_INT_PORT, &GPIO_InitStructure);

    /* EXTI0 = PB0 */
    GPIO_EXTILineConfig(TP_INT_EXTI_PORT, TP_INT_EXTI_PIN);

    EXTI_InitStructure.EXTI_Line    = TP_INT_EXTI_LINE;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = TP_INT_TRIGGER;  /* 下降沿 */
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = TP_INT_EXTI_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/* ===== 触摸 IC 硬件复位 ===== */

static void Touch_HW_Reset(void) {
    GPIO_ResetBits(TP_RST_PORT, PIN_TP_RST);
    delay_ms(5);
    GPIO_SetBits(TP_RST_PORT, PIN_TP_RST);
    delay_ms(50);
}

/* ===== API ===== */

void Touch_Init(void) {
    /* GPIO 时钟已在 pinout 初始化 */

    /* 硬件复位 */
    Touch_HW_Reset();

    /* I2C1 初始化 */
    Touch_I2C_Init();

    /* EXTI0 初始化 */
    Touch_EXTI_Init();

    /* 验证 IC 响应 (读 ChipID) */
    uint8_t chip_id = 0;
    if (I2C_ReadRegMulti(TP_I2C_ADDR, CST816D_REG_CHIPID, &chip_id, 1) == 0) {
        printf("[Touch] CST816D ChipID: 0x%x\n", chip_id);
        if (chip_id != 0xB6 && chip_id != 0xB5) {
            printf("[Touch] WARNING: unexpected ChipID 0x%02X\n", chip_id);
        }
    } else {
        printf("[Touch] ERROR: I2C read ChipID failed\n");
    }

    /* 启用双击 + 长按 (MotionMask bit 0 = EnDClick) */
    uint8_t motion = 0;
    if (I2C_ReadRegMulti(TP_I2C_ADDR, CST816D_REG_MOTION, &motion, 1) == 0) {
        motion |= 0x01;  /* EnDClick */
        I2C_WriteReg(TP_I2C_ADDR, CST816D_REG_MOTION, motion);
    }
}

/* ===== 触摸状态机 (跨 IRQ 累积 down/move/up) ===== */
static uint8_t  s_touch_pressed = 0;
static uint16_t s_touch_last_x  = 0;
static uint16_t s_touch_last_y  = 0;

/* 上报触摸事件 (转成协议事件类型) */
static void report_gesture(uint8_t gesture_id) {
    uint8_t proto_event;
    switch (gesture_id) {
        case CST816D_GESTURE_SLIDE_UP:   proto_event = TOUCH_EVENT_SWIPE_LEFT;        break;
        case CST816D_GESTURE_SLIDE_DOWN:  proto_event = TOUCH_EVENT_SWIPE_RIGHT;       break;
        case CST816D_GESTURE_LONG_PRESS:   proto_event = TOUCH_EVENT_LONG_PRESS;        break;
        case CST816D_GESTURE_DOUBLE_CLICK: proto_event = TOUCH_EVENT_DOUBLE_CLICK;      break;
        case CST816D_GESTURE_SINGLE_CLICK: return;  /* 短按: 已经由 DOWN/UP 表达, 不再单独报手势 */
        default: return;
    }
    printf("gseture:%d\n", proto_event);
    Protocol_SendTouchEvent(proto_event, 0, 0);  /* 手势无坐标 */
}

void Touch_Poll(void) {
    if (!s_touch_irq_flag) return;
    s_touch_irq_flag = 0;

    uint8_t buf[6];
    if (I2C_ReadRegMulti(TP_I2C_ADDR, CST816D_REG_GESTURE, buf, 6) != 0) {
        return;
    }

    uint8_t gesture = buf[0];      /* 0x01 */
    uint8_t finger  = buf[1];      /* 0x02 */
    /* CST816D 是自容触摸芯片, sensor 两轴 pad 数非对称, X/Y 寄存器范围由物理 pad 数决定:
     *   sensor X 寄存器 0..319 (沿 LCD Y, 320 pad);
     *   sensor Y 寄存器 0..171 (沿 LCD X, 172 pad).
     * 本 panel sensor 相对 LCD 旋转 90° CCW, 所以先交换两轴, 再按屏物理像素线性拉伸
     * 把 sensor 寄存器值映射到 LCD 像素坐标. */
    uint16_t tx = ((buf[2] & 0x0F) << 8) | buf[3];   /* sensor X (0..319) */
    uint16_t ty = ((buf[4] & 0x0F) << 8) | buf[5];   /* sensor Y (0..171) */
    uint16_t x  = (uint16_t)((uint32_t)ty * LCD_WIDTH  / LCD_HEIGHT);  /* sensor Y → display X (0..319) */
    uint16_t y  = (uint16_t)((uint32_t)tx * LCD_HEIGHT / LCD_WIDTH);   /* sensor X → display Y (0..171) */
    

    if (finger > 0) {
        /* 手指按住中: DOWN (首次) / MOVE (坐标变化时) */
        if (!s_touch_pressed) {
            Protocol_SendTouchEvent(TOUCH_EVENT_DOWN, x, y);
            // printf("Down x=%d, y=%d\n", x, y);
        } else if (x != s_touch_last_x || y != s_touch_last_y) {
            Protocol_SendTouchEvent(TOUCH_EVENT_MOVE, x, y);
            // printf("Move x=%d, y=%d\n", x, y);
        }
        s_touch_pressed = 1;
        s_touch_last_x  = x;
        s_touch_last_y  = y;
    } else {
        /* 手指抬起: UP (用最近一次坐标), 之后回报手势 (chip 识别出的) */
        if (s_touch_pressed) {
            Protocol_SendTouchEvent(TOUCH_EVENT_UP, s_touch_last_x, s_touch_last_y);
            s_touch_pressed = 0;
            // printf("Up x=%d, y=%d\n", x, y);
        }
    }
    report_gesture(gesture);
}

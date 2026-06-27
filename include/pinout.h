/**
 * @file    pinout.h
 * @brief   CH32X033F8P6 (TSSOP-20) 引脚分配 - v0.17 确认
 * @note    全部 18 GPIO 分配完成, 用户已确认
 *
 * 信号                     引脚     方向       备注
 * -----------------------   --------  --------   --------------------------------
 * USB DP (D+)              PC17     I/O (AF)   USB-FS D+
 * USB DM (D-)              PC16     I/O (AF)   USB-FS D-
 *
 * LCD MOSI                 PA7      O (AF)     SPI1 MOSI
 * LCD SCK                  PA5      O (AF)     SPI1 SCK
 * LCD MISO                 PA6      I (AF)     SPI1 MISO (通常不用)
 * LCD CS (NSS)             PA4      O (AF)     SPI1 NSS
 * LCD DC                   PA1      O (PP)     数据/命令选择
 * LCD RST                  PA3      O (PP)     硬件复位
 * LCD BL (PWM)             PA2      O (AF)     TIM2_CH3 PWM 背光
 *
 * Touch SDA (I2C1)         PA11     I/O (AF-OD) I2C1 SDA (硬件 I²C 1)
 * Touch SCL (I2C1)         PA10     I/O (AF-OD) I2C1 SCL (硬件 I²C 1)
 * Touch INT (EXTI0)        PB0      I (PU)     CST816D 中断, 下降沿触发
 * Touch RST                PB1      O (PP)     CST816D 硬件复位
 *
 * VDD                      pin 9    P          主电源 3.3V
 * GND                      pin 7    P          地
 * NRST                     --       I          外部复位 (含 10K 上拉 + 复位按钮)
 *
 * 备用 (未使用)             PA0, PA9, PC3
 *
 * ISP 烧录: D+ (PC17) 需要 10K 上拉到 3.3V 触发 ISP 模式
 *
 * @version v0.17
 */

#ifndef PINOUT_H
#define PINOUT_H

#include "ch32x035.h"

/* ===== 全局工具 (在 main.c 中定义) ===== */
extern volatile uint32_t g_ticks_ms;
void delay_ms(uint32_t ms);

/* ===== GPIO 端口基地址 (CH32X035 头文件已定义 GPIOA/GPIOB/GPIOC) ===== */

/* ===== GPIO 时钟使能 ===== */
#define PINOUT_GPIO_CLK_ENABLE(gpio)  do {                                      \
    if ((gpio) == GPIOA) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); \
    if ((gpio) == GPIOB) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); \
    if ((gpio) == GPIOC) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE); \
} while(0)

/* ===== 引脚编号宏 (使用位掩码, 与 WCH GPIO 库一致) ===== */
/* 注意: GPIO_Pin_x 是位掩码 (1 << x), 不是引脚序号
 * 例如: PC17 用 GPIO_Pin_17, PA2 用 GPIO_Pin_2 */
#define PIN_USB_DP            GPIO_Pin_17   /* PC17 */
#define PIN_USB_DM            GPIO_Pin_16   /* PC16 */
#define PIN_LCD_MOSI          GPIO_Pin_7    /* PA7 */
#define PIN_LCD_SCK           GPIO_Pin_5    /* PA5 */
#define PIN_LCD_MISO          GPIO_Pin_6    /* PA6 */
#define PIN_LCD_CS            GPIO_Pin_4    /* PA4 */
#define PIN_LCD_DC            GPIO_Pin_1    /* PA1 */
#define PIN_LCD_RST           GPIO_Pin_3    /* PA3 */
#define PIN_LCD_BL            GPIO_Pin_2    /* PA2 */
#define PIN_TP_SDA            GPIO_Pin_11   /* PA11 */
#define PIN_TP_SCL            GPIO_Pin_10   /* PA10 */
#define PIN_TP_INT            GPIO_Pin_0    /* PB0  (EXTI0) */
#define PIN_TP_RST            GPIO_Pin_1    /* PB1 */

/* ===== 端口访问便捷宏 ===== */
#define LCD_SPI_PORT          GPIOA
#define LCD_GPIO_CLK          RCC_APB2Periph_GPIOA
#define LCD_CTRL_PORT         GPIOA
#define TP_I2C_PORT           GPIOA
#define TP_GPIO_CLK           RCC_APB2Periph_GPIOA
#define TP_INT_PORT           GPIOB
#define TP_INT_CLK            RCC_APB2Periph_GPIOB
#define TP_RST_PORT           GPIOB
#define TP_RST_CLK            RCC_APB2Periph_GPIOB
#define USB_PORT              GPIOC
#define USB_CLK               RCC_APB2Periph_GPIOC

/* ===== GPIO 初始化辅助宏 ===== */

/* 推挽输出 (LCD 控制器信号) */
#define PINOUT_SETUP_PP(port, pin, default_level) do {                \
    GPIO_InitTypeDef cfg = {0};                                       \
    cfg.GPIO_Pin   = (pin);                                            \
    cfg.GPIO_Mode  = GPIO_Mode_Out_PP;                                 \
    cfg.GPIO_Speed = GPIO_Speed_50MHz;                                 \
    GPIO_Init((port), &cfg);                                           \
    if (default_level) GPIO_SetBits((port), (pin));                    \
    else              GPIO_ResetBits((port), (pin));                   \
} while(0)

/* 上拉输入 (TP INT) */
#define PINOUT_SETUP_IPU(port, pin) do {                              \
    GPIO_InitTypeDef cfg = {0};                                       \
    cfg.GPIO_Pin   = (pin);                                            \
    cfg.GPIO_Mode  = GPIO_Mode_IPU;                                    \
    GPIO_Init((port), &cfg);                                           \
} while(0)

/* 复用功能推挽 (SPI/I2C/AF) - 由各外设驱动配置 */

/* ===== EXTI 中断号 ===== */
#define TP_INT_EXTI_LINE     EXTI_Line0
#define TP_INT_EXTI_IRQn     EXTI7_0_IRQn
#define TP_INT_EXTI_HANDLER  EXTI7_0_IRQHandler

/* ===== 触摸 I2C 地址 ===== */
#define TP_I2C_ADDR          0x15   /* CST816D 7-bit 地址 */

/* ===== LCD SPI (SPI1) ===== */
#define LCD_SPI              SPI1
#define LCD_SPI_CLK          RCC_APB2Periph_SPI1
/* 默认 NSS 由软件控制, 不使用硬件 NSS */

/* ===== Touch I2C (I2C1) ===== */
#define TP_I2C               I2C1
#define TP_I2C_APB           RCC_APB1Periph_I2C1
/* 100kHz 标准, 400kHz 快速 (CST816D 支持) */
#define TP_I2C_CLOCK_100K    100000
#define TP_I2C_CLOCK_400K    400000

/* ===== LCD BL PWM (TIM2_CH3) ===== */
#define LCD_BL_TIMER         TIM2
#define LCD_BL_TIMER_CLK     RCC_APB1Periph_TIM2
#define LCD_BL_PWM_CHANNEL   3       /* TIM2_CH3 on PA2 */

/* ===== 触摸 INT 配置 ===== */
#define TP_INT_GPIO_PORT     GPIOB
#define TP_INT_GPIO_CLK      RCC_APB2Periph_GPIOB
#define TP_INT_PIN           GPIO_Pin_0
#define TP_INT_EXTI_PORT     GPIO_PortSourceGPIOB
#define TP_INT_EXTI_PIN      GPIO_PinSource0
#define TP_INT_TRIGGER       EXTI_Trigger_Falling

#endif /* PINOUT_H */

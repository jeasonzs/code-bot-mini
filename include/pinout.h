/**
 * @file    pinout.h
 * @brief   CH32X033F8P6 (TSSOP-20) 引脚全集 + 项目分配 - v0.17
 *
 * ============================================================================
 * NOTE_9600  ⚠ 固件 baud 选 9600, 主机可 57600 / 115200
 * ============================================================================
 *
 * 现象:  固件 DEBUG_USART_BAUDRATE = 9600,  主机 minicom 配 57600 或 115200 都能
 *        正确收到数据.  但固件用 115200 时主机怎么都乱码.
 *
 * 原因 (已查清):
 *   - WCH USART_Init 公式 BRR = (25*apbclock) / (4*baud) 整除精度有限.
 *     9600@48MHz → BRR=0x1388, 实际 9600 整除无误差.
 *     115200@48MHz → BRR=0x111, 实际 10989, 误差 4.6%, 主机侧失同步 → 乱码.
 *   - UART 接收端不要求收发 baud 严格相等, 只按自己配的 baud 采样线上的位.
 *     固件发 9600 baud, 主机配 57600 → 每个 bit 被采样 6 次 (全同值, 正确解码);
 *     主机配 115200 → 每个 bit 被采样 12 次 (同样 OK).
 *
 * 现状约束:
 *   - DEBUG_USART_BAUDRATE 保持 **9600**, 改 115200 会再乱码.
 *   - 主机可配 **57600** 或 **115200** (任选), 都稳定.
 *   - 想换固件 baud 到 57600 也可以 (公式整除干净, 0.05% 误差), 主机可配
 *     57600 / 115200 / 230400 (任意整数倍) 都行.
 *   - 想彻底干净, v0.18 自己写 BRR 算式, 不用 WCH 库.
 *
 * ============================================================================
 * TSSOP-20 完整引脚表 (按物理引脚顺序, pin 1 = PA6 端圆点标记)
 * ============================================================================
 *
 *  Pin  Pad    默认/AF0       AF 备选 (节选)                            项目分配         方向        备注
 *  ---  ------  -------------  ---------------------------------------  --------------  ----------  ------------------------------
 *   1   PA6    MISO  (SPI1)   T3C1, T1BK, O1NO, A6                     LCD MISO        I (AF)      SPI1 MISO (通常不用)
 *   2   PA7    MOSI  (SPI1)   T3C2, T1C1N_, O2PO, PB0†, TX4,           LCD MOSI        O (AF)      SPI1 MOSI
 *                            T1C2N_, O1PO, A8
 *   3   PB1    GPIO           T1C3N_, RX4, O2N1, A9                     Touch RST       O (PP)      CST816D 硬件复位
 *   4   PB7    RST  (NRST)    T1C2N, O2P2                              NRST            I           外部复位 (10K 上拉 + 复位按钮)
 *   5   PC16   UDM   (USB)    T1C4, TX4_2, I2C_, RX4_5                 USB DM          I/O (AF)    USB-FS D-
 *   6   PC17   UDP   (USB)    TX4_5, I2C_, RX4_2, T1ETR                USB DP          I/O (AF)    USB-FS D+ (ISP 烧录需 10K 上拉)
 *   7   VSS    GND            --                                       GND             P           地
 *   8   PC18   SDIO  (WCH调试) TX3_, T2C1N_, T3C2_, I2C_, T1ETR_        DEBUG_IFACE     I/O (AF)   PC18/PC19 模式见 DEBUG_IFACE (默认 USART3 TX)
 *   9   VDD    3.3V           --                                       VDD             P           主电源 3.3V
 *  10   PA9    GPIO           MISO_, RX4_1, T2BK_                      Touch INT       I (PU)      CST816D 中断, EXTI9, 下降沿
 *  11   PA11   SDA   (I2C1)   SCK_, RX1, C2P1                          Touch SDA       I/O (AF-OD) I2C1 SDA (硬件 I2C1)
 *  12   PA10   SCL   (I2C1)   TX1, MOSI                                Touch SCL       I/O (AF-OD) I2C1 SCL (硬件 I2C1)
 *  13   PC3    GPIO           T1C4, T2C1N_, C1NO, C2N1, A13             (未使用)        --          备用
 *  14   PA0    GPIO           T2C1, C1P1, A0                           Test LED        O (PP)      测试 LED, 1Hz 闪 (验证板子 OK)
 *  15   PA1    GPIO           T2C2, C1O, O1N2, O2N2, A1                LCD DC          O (PP)      数据/命令选择
 *  16   PA2    GPIO           TX2, T2C3, O2O1, T2ETR, A2               LCD BL          O (AF)      TIM2_CH3 PWM 背光
 *  17   PA3    GPIO           T2C4, T3C1_, RX2, O1O0, A3               LCD RST         O (PP)      硬件复位
 *  18   PC19   SDCK  (WCH调试) T2C1_, T3C1, I2C_, RX3_, C1P0           DEBUG_IFACE     I/O (AF)   PC18/PC19 模式见 DEBUG_IFACE (默认 USART3 RX)
 *  19   PA4    CS    (SPI1)   O2O0, T3C2, A4                          LCD CS          O (AF)      SPI1 NSS (软件片选)
 *  20   PA5    SCK   (SPI1)   TX4_1, O2NO, A5                         LCD SCK         O (AF)      SPI1 SCK
 *
 *  † PA7 AF 表里出现的 "PB0" 是 WCH AF 总线标识, 但 TSSOP-20 封装未引出 PB0,
 *    实际不能用作 GPIO. 其他引脚 AF 表中类似的不存在引脚标识都按上表忽略.
 *
 * ============================================================================
 * 已分配的串口外设 (非 GPIO 引脚, 走 AF 复用)
 * ============================================================================
 *
 *  USART1 TX (printf 单向, 115200 8N1)   PB10    O (AF)   WCH debug.c 默认配置
 *  注: 仅初始化 TX (USART_Mode_Tx). PB11 (RX) 未开, 主机无法发命令进来.
 *      如要双向, 需要自己补 GPIO_Init + USART_Init (Mode = Tx|Rx).
 *
 *  USART3 (host 调试口, 走 DEBUG_IFACE 宏切换)   PC18=TX / PC19=RX
 *  详见下面 DEBUG_IFACE 一节.
 *
 * ============================================================================
 * 已知问题 / 待修正 (v0.18 处理)
 * ============================================================================
 *
 *  1. ~~Touch INT 原配 PB0/EXTI0, PB0 不在 TSSOP-20 上~~ → v0.17 已修:
 *     改用 PA9/EXTI9 (走 EXTI15_8_IRQn 共享向量, CH32X035 的 8-15 分组).
 *
 *  2. ~~WCH debug.c 默认开 USART1 在 PB10 (TX only)~~ → v0.17 已修:
 *     TSSOP-20 没有 PB10/PB11. 改用 USART3 remap 到 PC18/PC19 (双向),
 *     由 DEBUG_IFACE 宏切换 (默认 USART3). printf 重定向见 libc_stubs.c.
 *
 *  3. ~~PC18/PC19 待加 DEBUG_IFACE 宏切换 init~~ → v0.17 已修:
 *     默认走 USART3 串口 (debug_usart.c 提供 Debug_USART_Init).
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
#define PIN_TEST_LED          GPIO_Pin_0    /* PA0  测试 LED, 闪灯 1Hz */

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

/* ===== EXTI 中断号 (PA9 → EXTI9, 走 EXTI15_8 共享向量含 EXTI8..EXTI15) ===== */
#define TP_INT_EXTI_LINE     EXTI_Line9
#define TP_INT_EXTI_IRQn     EXTI15_8_IRQn
#define TP_INT_EXTI_HANDLER  EXTI15_8_IRQHandler

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

/* ===== 触摸 INT 配置 (PA9, EXTI9, 走 EXTI9_5_IRQn) ===== */
/* 注: 原计划用 PB0/EXTI0, 但 TSSOP-20 封装未引出 PB0. 改用 PA9 (备用 pin). */
#define TP_INT_GPIO_PORT     GPIOA
#define TP_INT_GPIO_CLK      RCC_APB2Periph_GPIOA
#define TP_INT_PIN           GPIO_Pin_9
#define TP_INT_EXTI_PORT     GPIO_PortSourceGPIOA
#define TP_INT_EXTI_PIN      GPIO_PinSource9
#define TP_INT_TRIGGER       EXTI_Trigger_Falling

/* ===== 调试接口选择 (PC18/PC19) =====
 *
 *  DEBUG_IFACE_SERIAL:  PC18 = USART3_TX, PC19 = USART3_RX, 115200 8N1
 *  DEBUG_IFACE_SDI:     PC18 = WCH SDIO,  PC19 = WCH SDCK  (WCH-LinkE 调试)
 *
 *  注:
 *  - PC18/PC19 上电默认 AF0 = WCH SDI (单线调试协议, WCH-LinkE 用, 不是 ARM SWD).
 *    想用 WCH-LinkE 在线调试, 必须选 SDI, 不能选 SERIAL.
 *  - 选 SERIAL 后, 烧录/调试都要走 ISP (USB ISP 或按 boot 按钮), 不能用 WCH-LinkE.
 *  - 想切换: 改下面的 DEBUG_IFACE, 重烧固件. 不用改硬件接线.
 *  - DEBUG_IFACE_SERIAL 模式下, 同时存在的 USART1 (PB10 TX) 仍可独立用,
 *    两者互不冲突 (一个 printf log, 一个 host 命令交互).
 */
#define DEBUG_IFACE_SERIAL   1   /* USART3 on PC18/PC19 */
#define DEBUG_IFACE_SDI      0   /* WCH-LinkE 调试 */

#define DEBUG_IFACE          DEBUG_IFACE_SERIAL   /* 默认 = 串口 (USART3) */

#if (DEBUG_IFACE == DEBUG_IFACE_SERIAL)
    /* USART3 remap → PC18/PC19.
     * 注: PC18/PC19 默认 AF0 = WCH SDI (单线调试), 必须先关闭 SDI
     *     (GPIO_Remap_SWJ_Disable ENABLE), USART3 remap 才能接管.
     *     顺序不能反 — 单独调 PartialRemap{1,2}/FullRemap 都无效. */
    #define DEBUG_USART              USART3
    #define DEBUG_USART_CLK          RCC_APB1Periph_USART3
    #define DEBUG_USART_GPIO_PORT    GPIOC
    #define DEBUG_USART_GPIO_CLK     RCC_APB2Periph_GPIOC
    #define DEBUG_USART_TX_PIN       GPIO_Pin_18    /* PC18 */
    #define DEBUG_USART_RX_PIN       GPIO_Pin_19    /* PC19 */
    #define DEBUG_USART_BAUDRATE     921600   /* ⚠ DO NOT CHANGE: 见顶部 NOTE_9600_57600 */
    #define DEBUG_USART_REMAP()      do {                                              \
        GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);                          \
        GPIO_PinRemapConfig(GPIO_PartialRemap1_USART3, ENABLE);                       \
    } while(0)
#endif

#endif /* PINOUT_H */

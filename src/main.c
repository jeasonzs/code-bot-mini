/**
 * @file    main.c
 * @brief   Code Bot 主程序 - CH32X033F8P6 (TSSOP-20) 固件
 * @version v0.17
 *
 * 系统架构:
 *   - USB 复合设备 (Vendor + HID Keyboard + CDC ACM)
 *   - 主机 (codebotd) 通过 Vendor Bulk 推送渲染好的图像
 *   - 触摸 (CST816D) 通过 I2C1 + EXTI0 上报
 *   - 屏 5/7 长按图标时, 设备以 HID Keyboard 形式键入
 *   - CDC 串口用于 printf 调试
 */

#include "debug.h"
#include "ch32x035_conf.h"
#include "pinout.h"

/* 各模块头文件 (后续模块化实现) */
#include "display/lcd_driver.h"
#include "display/touch.h"
#include "protocol/proto.h"
#include "usb/usb_desc.h"
#include "usb/usb_endp.h"
#include "usb/hid_kbd.h"
#include "util/ringbuf.h"

/* 延时函数声明 (本文件定义, 其他模块使用) */
void delay_ms(uint32_t ms);

/* ============================================================== */
/* 全局状态                                                       */
/* ============================================================== */
volatile uint32_t g_ticks_ms = 0;  /* 1ms SysTick */

/* SysTick 中断 (WCH core_riscv.c 默认提供) */
void SysTick_Handler(void) {
    g_ticks_ms++;
    /* SysTick_Config 已在 system_ch32x035.c 中设置 1ms 周期 */
}

/* 延时函数 (ms) - 简单循环 */
void delay_ms(uint32_t ms) {
    uint32_t start = g_ticks_ms;
    while ((g_ticks_ms - start) < ms);
}

/* EXTI0 中断 (触摸 INT) 见 ch32x035_it.c, 这里是 WCH 启动文件 weak 默认实现的覆盖 */
/* (在 ch32x035_it.c 中提供) */

/* ============================================================== */
/* 初始化                                                         */
/* ============================================================== */

static void GPIO_Init_All(void) {
    /* 使能所有用到的 GPIO 端口时钟 */
    PINOUT_GPIO_CLK_ENABLE(LCD_CTRL_PORT);
    PINOUT_GPIO_CLK_ENABLE(LCD_SPI_PORT);
    PINOUT_GPIO_CLK_ENABLE(TP_I2C_PORT);
    PINOUT_GPIO_CLK_ENABLE(TP_INT_PORT);
    PINOUT_GPIO_CLK_ENABLE(TP_RST_PORT);
    PINOUT_GPIO_CLK_ENABLE(USB_PORT);

    /* LCD 控制信号: DC, RST, BL (推挽输出) */
    PINOUT_SETUP_PP(LCD_CTRL_PORT, PIN_LCD_DC,  1);  /* DC = data mode */
    PINOUT_SETUP_PP(LCD_CTRL_PORT, PIN_LCD_RST, 1);  /* RST inactive */
    PINOUT_SETUP_PP(LCD_CTRL_PORT, PIN_LCD_BL,  0);  /* BL off (由 TIM 控制后开) */

    /* LCD SPI 信号 (PA4/5/6/7) 由 SPI 驱动 init 时配置为 AF PP */

    /* 触摸 RST (PB1) 推挽输出 */
    PINOUT_SETUP_PP(TP_RST_PORT, PIN_TP_RST, 1);

    /* 触摸 INT (PB0) 上拉输入 */
    PINOUT_SETUP_IPU(TP_INT_PORT, PIN_TP_INT);

    /* USB DP/DM (PC16/PC17) 由 USB 驱动 init 时配置为 AF PP */

    /* 调试: LED 用 PA0 (可选) - 暂未使用 */
}

int main(void) {
    /* 1. 系统基础初始化 (在 system_ch32x035.c 中, 默认 48MHz HSI) */
    SystemInit();

    /* 2. NVIC 配置 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

    /* 3. SysTick 1ms 中断 (由 SystemCoreClockUpdate + SysTick_Config) */
    /* 注意: WCH debug.c 的 USART_Printf_Init 默认初始化 SysTick */

    /* 4. 调试串口 (USART1) 初始化 - 用于 printf */
    USART_Printf_Init(115200);
    printf("\n[CodeBot] Booting v0.17...\n");

    /* 5. GPIO 全部初始化 */
    GPIO_Init_All();

    /* 6. LCD 初始化 (GC9307) */
    printf("[CodeBot] Init LCD GC9307...\n");
    LCD_Init();
    LCD_Clear(0x0000);  /* 全屏黑 */

    /* 7. 触摸初始化 (CST816D) */
    printf("[CodeBot] Init Touch CST816D...\n");
    Touch_Init();

    /* 8. LCD 背光 PWM (TIM2_CH3) */
    LCD_BL_SetBrightness(80);  /* 80% */

    /* 9. USB 复合设备初始化 (Vendor + HID Keyboard, CDC v0.18+) */
    printf("[CodeBot] Init USB composite...\n");
    USB_Device_Init_App();

    /* 10. 协议层初始化 */
    Protocol_Init();

    printf("[CodeBot] Ready. Waiting for host...\n");

    /* ============================================================ */
    /* 主循环                                                       */
    /* ============================================================ */
    uint32_t last_ping = 0;

    while (1) {
        /* 处理 USB 事件 (Vendor OUT 命令解析) */
        Protocol_Poll();

        /* 处理触摸事件 (I2C 读 + 上报 host) */
        Touch_Poll();

        /* 发送 HID Keyboard 击键队列 */
        HID_Kbd_SendPending();

        /* 1Hz PING */
        if (g_ticks_ms - last_ping >= 1000) {
            last_ping = g_ticks_ms;
            Protocol_SendPong();
        }

        /* 短暂睡眠, 让中断处理 */
        /* __WFI();  // 可选: 进入低功耗等待中断 */
    }
}

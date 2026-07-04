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
#include "util/debug_usart.h"  /* Debug_USART_Init */

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
/* g_ticks_ms 由 TIM3 update 中断每 1ms 累加一次 (见 ch32x035_it.c).
 * SysTick 留给 debug.c 的 Delay_Ms / Delay_Us 用, 不要在这里占用. */
volatile uint32_t g_ticks_ms = 0;

/* TIM3 1ms 滴答初始化
 *   48MHz / (47+1) = 1MHz 计数时钟, 周期 999 → 1kHz update 中断 = 1ms 滴答
 *   与 LCD 背光 TIM2_PWMInit 完全相同的分频/周期参数, 改成 TIM3. */
static void Tick_TIM3_Init(void) {
    TIM_TimeBaseInitTypeDef tb = {0};
    NVIC_InitTypeDef nv = {0};

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    tb.TIM_Prescaler   = 47;
    tb.TIM_Period      = 999;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &tb);

    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM3, ENABLE);

    NVIC_Init(&nv);  /* 防止 -Wmaybe-uninitialized */
    nv.NVIC_IRQChannel                   = TIM3_IRQn;
    nv.NVIC_IRQChannelPreemptionPriority = 0;
    nv.NVIC_IRQChannelSubPriority        = 1;
    nv.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nv);
}

/* 延时函数 (ms) - 简单循环, 依赖 g_ticks_ms */
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

    /* 测试 LED: PA0 推挽输出, 默认低 (灭). GPIOA 时钟上面已开 */
    PINOUT_SETUP_PP(LCD_CTRL_PORT, PIN_TEST_LED, 1);
}

void delay(uint32_t ms)
{
    volatile uint32_t i;

    while(ms--)
    {
        for(i = 0; i < 1200; i++)
        {
            __asm__("nop");
        }
    }
}

void blink() {
    for (int i = 0; i < 2; i++) {
        GPIO_ResetBits(LCD_CTRL_PORT, PIN_TEST_LED);
        delay(500);
        GPIO_SetBits(LCD_CTRL_PORT, PIN_TEST_LED);
        delay(500);
    }
}
int main(void) {
    /* 1. 系统基础初始化 (在 system_ch32x035.c 中, 默认 48MHz HSI) */
    SystemInit();

    /* 2. NVIC 配置 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

    /* 3. 1ms 滴答定时器: 用 TIM3 (TIM2 被 LCD 背光占, TIM1 太高级).
     *    SysTick 留给 debug.c 的 Delay_Ms / Delay_Us 自由使用. */
    Tick_TIM3_Init();

    /* 4. 调试串口 (USART3 on PC18/PC19, 由 DEBUG_IFACE 宏决定) 初始化 */
    Debug_USART_Init();
    printf("\n[CodeBot] Booting v0.17...\n");

    /* 5. GPIO 全部初始化 */
    GPIO_Init_All();

    // /* 6. LCD 初始化 (GC9307) */
    printf("[CodeBot] Init LCD GC9307...\n");
    LCD_Init();
    LCD_BL_SetBrightness(80);
    LCD_DebugAlignPattern();

    /* 7. (触摸 CST816D 暂不调试) */

    /* 9. USB 复合设备初始化 (Vendor + HID Keyboard) */
    printf("[CodeBot] Init USB composite...\n");
    USB_Device_Init_App();

    /* 10. 协议层初始化 */
    Protocol_Init();

    /* 10b. HID 击键队列初始化 (必须在 USB 之后, 队列空) */
    HID_Kbd_Init();

    printf("[CodeBot] Ready. Waiting for host...\n");
    printf("[CodeBot] g_ticks_ms @ boot = %d\n", (unsigned long)g_ticks_ms);

    /* ============================================================ */
    /* 主循环                                                       */
    /* ============================================================ */
    uint32_t last_ping = 0;

    while (1) {
        /* 处理 USB 事件 (Vendor OUT 命令解析) */
        Protocol_Poll();

        /* 处理触摸事件 (I2C 读 + 上报 host) */
        // Touch_Poll();

        /* 发送 HID Keyboard 击键队列 */
        HID_Kbd_SendPending();

        /* 1Hz PING */
        if (g_ticks_ms - last_ping >= 1000) {
            last_ping = g_ticks_ms;
            printf("[DBG] PING-fired tick=%d last=%d\n",
                   (int)g_ticks_ms, (int)last_ping);
            Protocol_SendPong();
        }

        /* 短暂睡眠, 让中断处理 */
        /* __WFI();  // 可选: 进入低功耗等待中断 */
    }
}

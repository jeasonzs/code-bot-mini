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
#include "display/fonts/code_bot.h"
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

/* ============================================================== */
/* 待机画面: 黑底 + 蓝色大字 "Codebot"                              */
/*   位图 src/display/fonts/code_bot.h (60pt bold 取模).             */
/*   开机调用一次; host 心跳停止 >600ms 时再调用一次.                */
/*   走 LCD_BeginRect/WritePixelsStream/EndRect 流式 API,           */
/*   避免缓存整张 RGB565 (~30KB) 超 RAM 上限.                      */
/* ============================================================== */
static uint8_t s_standby_line_be[LCD_WIDTH * 2];  /* 640B BSS, RGB565 BE */

static void standby_expand_row(const uint8_t *bits, uint16_t width, uint8_t *out) {
    for (uint16_t x = 0; x < width; x++) {
        uint8_t b = (bits[x >> 3] >> (7 - (x & 7))) & 1;
        uint16_t px = b ? LCD_COLOR_THEME_FG : LCD_COLOR_BLACK;
        out[x * 2]     = (uint8_t)(px >> 8);  /* 高字节先 (BE), 匹配 SPI 期望 */
        out[x * 2 + 1] = (uint8_t)(px & 0xFF);
    }
}

static void LCD_DrawCodebotStandby(void) {
    LCD_Clear(LCD_COLOR_BLACK);
    LCD_BeginRect(CODEBOT_LOGO_X, CODEBOT_LOGO_Y,
                  CODEBOT_LOGO_W, CODEBOT_LOGO_H);
    for (uint16_t row = 0; row < CODEBOT_LOGO_H; row++) {
        standby_expand_row(&codebot_logo_bits[row][0],
                           CODEBOT_LOGO_W, s_standby_line_be);
        LCD_WritePixelsStream(s_standby_line_be, CODEBOT_LOGO_W * 2);
    }
    LCD_EndRect();
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
    LCD_BL_SetBrightness(50);
    LCD_DrawCodebotStandby();  /* 开机默认 Logo (取代 LCD_DebugAlignPattern, 后者保留做调试) */

    /* 7. 触摸 CST816D 初始化 (硬件复位 + I2C1 + EXTI0 on PB0) */
    printf("[CodeBot] Init Touch CST816D...\n");
    Touch_Init();

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
    bool standby_active = false;  /* standby 状态机: 一次性画 Logo, host 恢复后让位 */

    while (1) {
        /* standby 检测: >2.4s 没收到 host PING → 一次性刷 Logo, host 恢复让位 */
        if (Protocol_PingStale(g_ticks_ms, 2400)) {
            if (!standby_active) {
                LCD_DrawCodebotStandby();
                standby_active = true;
            }
        } else {
            standby_active = false;
        }

        /* 处理 USB 事件 (EP1 OUT 控制命令解析) */
        Protocol_Poll();

        /* 处理 EP5 OUT 图像数据流 (从 EP5 ring buffer 拉一包, SPI DMA 推 LCD) */
        Protocol_PollPixels();

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

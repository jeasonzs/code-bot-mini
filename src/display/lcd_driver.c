/**
 * @file    lcd_driver.c
 * @brief   GC9307 / ST7789V3 LCD 驱动 (via SPI1)
 *
 * SPI1 @ 24MHz max, 实际用 ~18MHz 稳妥
 * 屏幕: 1.47" 320x172 IPS RGB565
 * 通信: SPI mode 0 (CPOL=0, CPHA=0), MSB first
 */

#include "lcd_driver.h"
#include "ch32x035_conf.h"
#include <string.h>

/* ===== 低层 SPI 通信 ===== */

static void LCD_SPI_Init(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    SPI_InitTypeDef  SPI_InitStructure  = {0};

    /* 使能 SPI1 + GPIOA 时钟 */
    RCC_APB2PeriphClockCmd(LCD_SPI_CLK | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);

    /* SCK (PA5), MOSI (PA7) - 复用推挽 */
    GPIO_InitStructure.GPIO_Pin   = PIN_LCD_SCK | PIN_LCD_MOSI;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* MISO (PA6) - 输入浮空 (通常不用) */
    GPIO_InitStructure.GPIO_Pin   = PIN_LCD_MISO;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* CS (PA4) - 软件片选, 必须配成推挽输出并默认拉高;
     * 否则复位后 PA4 为浮空输入, GPIO_Set/ResetBits 驱动不了引脚,
     * CS 永远无法有效拉低 -> GC9307 忽略所有 SPI 数据 -> 全黑。 */
    GPIO_InitStructure.GPIO_Pin   = PIN_LCD_CS;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LCD_CTRL_PORT, &GPIO_InitStructure);
    GPIO_SetBits(LCD_CTRL_PORT, PIN_LCD_CS);

    /* SPI1 配置 */
    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_Low;       /* mode 0 */
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_1Edge;      /* mode 0 */
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;  /* 48MHz/2 = 24MHz */
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_Init(LCD_SPI, &SPI_InitStructure);

    SPI_Cmd(LCD_SPI, ENABLE);
}

/* 阻塞发送单字节 */
static void SPI_SendByte(uint8_t data) {
    while (SPI_I2S_GetFlagStatus(LCD_SPI, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(LCD_SPI, data);
    while (SPI_I2S_GetFlagStatus(LCD_SPI, SPI_I2S_FLAG_RXNE) == RESET);
    SPI_I2S_ReceiveData(LCD_SPI);  /* 丢弃 RX */
}

static inline void LCD_CS_LOW(void)  { GPIO_ResetBits(LCD_CTRL_PORT, PIN_LCD_CS); }
static inline void LCD_CS_HIGH(void) { GPIO_SetBits(LCD_CTRL_PORT, PIN_LCD_CS); }
static inline void LCD_DC_CMD(void)  { GPIO_ResetBits(LCD_CTRL_PORT, PIN_LCD_DC); }
static inline void LCD_DC_DATA(void) { GPIO_SetBits(LCD_CTRL_PORT, PIN_LCD_DC); }
static inline void LCD_RST_LOW(void) { GPIO_ResetBits(LCD_CTRL_PORT, PIN_LCD_RST); }
static inline void LCD_RST_HIGH(void){ GPIO_SetBits(LCD_CTRL_PORT, PIN_LCD_RST); }

/* 发送 LCD 命令 (DC=0) */
static void LCD_WriteCommand(uint8_t cmd) {
    LCD_CS_LOW();
    LCD_DC_CMD();
    SPI_SendByte(cmd);
    LCD_CS_HIGH();
}

/* 发送 LCD 数据 (DC=1) */
static void LCD_WriteData(uint8_t data) {
    LCD_CS_LOW();
    LCD_DC_DATA();
    SPI_SendByte(data);
    LCD_CS_HIGH();
}

/* 批量发送数据 (无 CS toggle 中间) */
static void LCD_WriteDataMulti(const uint8_t *data, uint32_t len) {
    LCD_CS_LOW();
    LCD_DC_DATA();
    for (uint32_t i = 0; i < len; i++) {
        SPI_SendByte(data[i]);
    }
    LCD_CS_HIGH();
}

/* ===== GC9307 初始化序列 (厂商提供) ===== */

static void LCD_RunInitSequence(void) {
    /* 硬件复位 */
    LCD_RST_LOW();
    delay_ms(100);
    LCD_RST_HIGH();
    delay_ms(120);

    /* 内部寄存器解锁 (Inter Register Enable 1/2) */
    LCD_WriteCommand(0xFE);
    LCD_WriteCommand(0xEF);

    /* MADCTL: 320 宽横屏 MV=1 + RGB 顺序(BGR=1 切回 RGB) -> 0x70。
     * 实测默认 BGR 解析会让红/蓝互换, 绿/白不变, 故置 BGR=1 修正。 */
    LCD_WriteCommand(0x36);
    LCD_WriteData(0x70);

    /* 像素格式: RGB565 (16-bit/pixel) */
    LCD_WriteCommand(0x3A);
    LCD_WriteData(0x05);

    /* 显示反显开: 该 IPS 屏默认反色 (0x0000 显示成白), 需 INVON 才正常 */
    LCD_WriteCommand(0x21);

    /* 内部时序 / 电荷泵配置 (厂商值) */
    LCD_WriteCommand(0x85);
    LCD_WriteData(0xC0);
    LCD_WriteCommand(0x86);
    LCD_WriteData(0x98);
    LCD_WriteCommand(0x87);
    LCD_WriteData(0x28);
    LCD_WriteCommand(0x89);
    LCD_WriteData(0x33);
    LCD_WriteCommand(0x8B);
    LCD_WriteData(0x84);
    LCD_WriteCommand(0x8D);
    LCD_WriteData(0x3B);
    LCD_WriteCommand(0x8E);
    LCD_WriteData(0x0F);
    LCD_WriteCommand(0x8F);
    LCD_WriteData(0x70);

    LCD_WriteCommand(0xE8);
    LCD_WriteData(0x13);
    LCD_WriteData(0x17);

    LCD_WriteCommand(0xEC);
    LCD_WriteData(0x57);
    LCD_WriteData(0x07);
    LCD_WriteData(0xFF);

    LCD_WriteCommand(0xED);
    LCD_WriteData(0x18);
    LCD_WriteData(0x09);

    /* VCOM 已烧录到 OTP, 此处不再写入 */
    /* LCD_WriteCommand(0xC3); LCD_WriteData(0x29); */
    /* LCD_WriteCommand(0xC4); LCD_WriteData(0x45); */

    LCD_WriteCommand(0xC9);
    LCD_WriteData(0x10);

    LCD_WriteCommand(0xFF);
    LCD_WriteData(0x61);

    LCD_WriteCommand(0x99);
    LCD_WriteData(0x3A);
    LCD_WriteCommand(0x9D);
    LCD_WriteData(0x43);
    LCD_WriteCommand(0x98);
    LCD_WriteData(0x3E);
    LCD_WriteCommand(0x9C);
    LCD_WriteData(0x4B);

    /* 伽马 (F0/F2 正极性, F1/F3 负极性) */
    LCD_WriteCommand(0xF0);
    LCD_WriteData(0x06);
    LCD_WriteData(0x08);
    LCD_WriteData(0x08);
    LCD_WriteData(0x06);
    LCD_WriteData(0x05);
    LCD_WriteData(0x1D);

    LCD_WriteCommand(0xF2);
    LCD_WriteData(0x00);
    LCD_WriteData(0x01);
    LCD_WriteData(0x09);
    LCD_WriteData(0x07);
    LCD_WriteData(0x04);
    LCD_WriteData(0x23);

    LCD_WriteCommand(0xF1);
    LCD_WriteData(0x3B);
    LCD_WriteData(0x68);
    LCD_WriteData(0x66);
    LCD_WriteData(0x36);
    LCD_WriteData(0x35);
    LCD_WriteData(0x2F);

    LCD_WriteCommand(0xF3);
    LCD_WriteData(0x37);
    LCD_WriteData(0x6A);
    LCD_WriteData(0x66);
    LCD_WriteData(0x37);
    LCD_WriteData(0x35);
    LCD_WriteData(0x35);

    LCD_WriteCommand(0xFA);
    LCD_WriteData(0x80);
    LCD_WriteData(0x0F);

    LCD_WriteCommand(0xBE);
    LCD_WriteData(0x11);  /* source bias */

    LCD_WriteCommand(0xCB);
    LCD_WriteData(0x02);

    LCD_WriteCommand(0xCD);
    LCD_WriteData(0x22);

    LCD_WriteCommand(0x9B);
    LCD_WriteData(0xFF);

    /* Tearing Effect line off */
    LCD_WriteCommand(0x35);
    LCD_WriteData(0x00);

    LCD_WriteCommand(0x44);
    LCD_WriteData(0x00);
    LCD_WriteData(0x0A);

    /* Sleep Out */
    LCD_WriteCommand(0x11);
    delay_ms(200);

    /* 显示开启 */
    LCD_WriteCommand(0x29);

    /* RAMWR: 准备写显存 */
    LCD_WriteCommand(0x2C);
}

/* ===== 背光 PWM (TIM2_CH3 on PA2) ===== */

static void LCD_BL_PWMInit(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};

    /* PA2 复用推挽, TIM2_CH3 */
    GPIO_InitStructure.GPIO_Pin   = PIN_LCD_BL;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LCD_CTRL_PORT, &GPIO_InitStructure);

    /* TIM2 时钟 */
    RCC_APB1PeriphClockCmd(LCD_BL_TIMER_CLK, ENABLE);

    /* 1kHz PWM (48MHz / 48 = 1MHz, /1000 = 1kHz) */
    TIM_TimeBaseStructure.TIM_Prescaler         = 47;  /* 48MHz / 48 = 1MHz */
    TIM_TimeBaseStructure.TIM_Period            = 999; /* 1MHz / 1000 = 1kHz */
    TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;
    TIM_TimeBaseInit(LCD_BL_TIMER, &TIM_TimeBaseStructure);

    /* PWM 模式 1, 初始占空比 0 */
    TIM_OCInitStructure.TIM_OCMode       = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState  = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse        = 0;
    TIM_OCInitStructure.TIM_OCPolarity   = TIM_OCPolarity_High;
    TIM_OC3Init(LCD_BL_TIMER, &TIM_OCInitStructure);
    TIM_OC3PreloadConfig(LCD_BL_TIMER, TIM_OCPreload_Enable);

    TIM_Cmd(LCD_BL_TIMER, ENABLE);
}

/* ===== 公共 API ===== */

void LCD_Init(void) {
    LCD_SPI_Init();
    LCD_BL_PWMInit();
    LCD_RunInitSequence();
}

void LCD_Reinit(void) {
    LCD_RunInitSequence();
}

/* 设置地址窗口 (带控制器 RAM 偏移), 之后即可流式写 RAMWR */
static void LCD_SetAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    uint16_t x0 = x + LCD_X_OFFSET;
    uint16_t x1 = x + LCD_X_OFFSET + w - 1;
    uint16_t y0 = y + LCD_Y_OFFSET;
    uint16_t y1 = y + LCD_Y_OFFSET + h - 1;

    LCD_WriteCommand(0x2A);  /* CASET */
    LCD_WriteData(x0 >> 8); LCD_WriteData(x0 & 0xFF);
    LCD_WriteData(x1 >> 8); LCD_WriteData(x1 & 0xFF);
    LCD_WriteCommand(0x2B);  /* RASET */
    LCD_WriteData(y0 >> 8); LCD_WriteData(y0 & 0xFF);
    LCD_WriteData(y1 >> 8); LCD_WriteData(y1 & 0xFF);
    LCD_WriteCommand(0x2C);  /* RAMWR */
}

/* 用单色流式填充一个矩形 (不需要像素缓冲) */
static void LCD_FillColor(uint16_t x, uint16_t y, uint16_t w, uint16_t h, lcd_color_t color) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT || w == 0 || h == 0) return;
    if (x + w > LCD_WIDTH)  w = LCD_WIDTH  - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

    LCD_SetAddrWindow(x, y, w, h);

    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    LCD_CS_LOW();
    LCD_DC_DATA();
    for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
        SPI_SendByte(hi);
        SPI_SendByte(lo);
    }
    LCD_CS_HIGH();
}

void LCD_Clear(lcd_color_t color) {
    /* 设置全屏窗口 (带控制器 RAM 偏移) */
    uint16_t x0 = LCD_X_OFFSET;
    uint16_t x1 = LCD_X_OFFSET + LCD_WIDTH - 1;
    uint16_t y0 = LCD_Y_OFFSET;
    uint16_t y1 = LCD_Y_OFFSET + LCD_HEIGHT - 1;

    LCD_WriteCommand(0x2A);
    LCD_WriteData(x0 >> 8); LCD_WriteData(x0 & 0xFF);
    LCD_WriteData(x1 >> 8); LCD_WriteData(x1 & 0xFF);
    LCD_WriteCommand(0x2B);
    LCD_WriteData(y0 >> 8); LCD_WriteData(y0 & 0xFF);
    LCD_WriteData(y1 >> 8); LCD_WriteData(y1 & 0xFF);
    LCD_WriteCommand(0x2C);  /* RAMWR */

    /* 全屏填充单色 */
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    LCD_CS_LOW();
    LCD_DC_DATA();
    for (uint32_t i = 0; i < LCD_PIXELS; i++) {
        SPI_SendByte(hi);
        SPI_SendByte(lo);
    }
    LCD_CS_HIGH();
}

void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *pixels) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

    /* 加控制器 RAM 偏移 */
    uint16_t x0 = x + LCD_X_OFFSET;
    uint16_t x1 = x + LCD_X_OFFSET + w - 1;
    uint16_t y0 = y + LCD_Y_OFFSET;
    uint16_t y1 = y + LCD_Y_OFFSET + h - 1;

    /* 设置窗口 */
    LCD_WriteCommand(0x2A);  /* CASET */
    LCD_WriteData(x0 >> 8); LCD_WriteData(x0 & 0xFF);
    LCD_WriteData(x1 >> 8); LCD_WriteData(x1 & 0xFF);
    LCD_WriteCommand(0x2B);  /* RASET */
    LCD_WriteData(y0 >> 8); LCD_WriteData(y0 & 0xFF);
    LCD_WriteData(y1 >> 8); LCD_WriteData(y1 & 0xFF);
    LCD_WriteCommand(0x2C);  /* RAMWR */

    LCD_WriteDataMulti(pixels, (uint32_t)w * h * 2);
}

void LCD_BL_SetBrightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    /* CCR 范围 0-999, 0%=0, 100%=999 */
    uint16_t ccr = (uint16_t)((uint32_t)pct * 999 / 100);
    TIM_SetCompare3(LCD_BL_TIMER, ccr);
}

/* ===== 调试辅助 ===== */

/* 纯色循环: 红->绿->蓝->白->黑, 每色停留 ms 毫秒。
 * 用途: 确认全屏点亮、无花边/错位, 颜色正确(红蓝不反)。 */
void LCD_DebugColorCycle(uint16_t ms) {
    static const lcd_color_t seq[] = {
        LCD_COLOR_RED, LCD_COLOR_GREEN, LCD_COLOR_BLUE,
        LCD_COLOR_WHITE, LCD_COLOR_BLACK
    };
    for (uint32_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        LCD_Clear(seq[i]);
        delay_ms(ms);
    }
}

/* 对齐图案: 黑底 + 1px 白色外边框 + 四角色块(内缩避开圆角) + 居中十字。
 * 用途: 核对 CASET/RASET 偏移是否正好贴屏边; 认屏幕方向。
 *   - 边框直边看不到 / 被截 -> 该方向偏移偏大, 减小对应 LCD_*_OFFSET
 *   - 边框直边与屏边有黑缝   -> 偏移偏小, 增大对应偏移
 *   - 圆角屏四角被切, 故色块内缩 inset 像素落在可视区内
 *   - 色块顺序: 左上红/右上绿/左下蓝/右下白, 位置错乱=镜像/翻转 */
void LCD_DebugAlignPattern(void) {
    const uint16_t W = LCD_WIDTH, H = LCD_HEIGHT;
    const uint16_t m = 12;      /* 色块尺寸 */
    const uint16_t inset = 24;  /* 内缩量: 需大于圆角半径, 圆角越大调越大 */

    LCD_Clear(LCD_COLOR_BLACK);

    /* 1px 外边框 (四条边; 圆角处会被切, 直边可见) */
    LCD_FillColor(0,     0,     W, 1, LCD_COLOR_WHITE);  /* 上 */
    LCD_FillColor(0,     H - 1, W, 1, LCD_COLOR_WHITE);  /* 下 */
    LCD_FillColor(0,     0,     1, H, LCD_COLOR_WHITE);  /* 左 */
    LCD_FillColor(W - 1, 0,     1, H, LCD_COLOR_WHITE);  /* 右 */

    /* 四角色块 (内缩 inset, 避开圆角; 不同色便于区分方向) */
    LCD_FillColor(inset,         inset,         m, m, LCD_COLOR_RED);    /* 左上 */
    LCD_FillColor(W - inset - m, inset,         m, m, LCD_COLOR_GREEN);  /* 右上 */
    LCD_FillColor(inset,         H - inset - m, m, m, LCD_COLOR_BLUE);   /* 左下 */
    LCD_FillColor(W - inset - m, H - inset - m, m, m, LCD_COLOR_WHITE);  /* 右下 */

    /* 居中十字 */
    LCD_FillColor(0,     H / 2, W, 1, LCD_COLOR_WHITE);  /* 水平 */
    LCD_FillColor(W / 2, 0,     1, H, LCD_COLOR_WHITE);  /* 垂直 */
}

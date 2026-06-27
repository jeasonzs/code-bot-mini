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

/* ===== GC9307 初始化序列 ===== */
/* 参考: 社区 GC9307 init 序列, 与 ST7789V3 兼容大部分命令 */

static void LCD_RunInitSequence(void) {
    /* 硬件复位 */
    LCD_RST_LOW();
    delay_ms(100);
    LCD_RST_HIGH();
    delay_ms(120);

    /* Sleep Out */
    LCD_WriteCommand(0x11);
    delay_ms(120);

    /* 显示关闭 */
    LCD_WriteCommand(0x28);

    /* 像素格式: RGB565 */
    LCD_WriteCommand(0x3A);
    LCD_WriteData(0x55);  /* 16-bit/pixel */
    delay_ms(10);

    /* Porch 控制 (厂商值) */
    LCD_WriteCommand(0xB2);
    LCD_WriteData(0x0C);
    LCD_WriteData(0x0C);
    LCD_WriteData(0x00);
    LCD_WriteData(0x33);
    LCD_WriteData(0x33);

    /* Gate 控制 (厂商值) */
    LCD_WriteCommand(0xB7);
    LCD_WriteData(0x35);

    /* VCOM 设置 */
    LCD_WriteCommand(0xBB);
    LCD_WriteData(0x32);  /* VCOM=1.35V */

    /* LCM 控制 */
    LCD_WriteCommand(0xC2);
    LCD_WriteData(0x01);

    /* VDV/VRH 写入 */
    LCD_WriteCommand(0xC3);
    LCD_WriteData(0x15);

    /* VRH 写入 */
    LCD_WriteCommand(0xC4);
    LCD_WriteData(0x20);

    /* VDV 写入 */
    LCD_WriteCommand(0xC5);
    LCD_WriteData(0x00);

    /* 帧率控制 (60Hz) */
    LCD_WriteCommand(0xC6);
    LCD_WriteData(0x0F);

    /* 电源控制 1 */
    LCD_WriteCommand(0xD0);
    LCD_WriteData(0xA4);
    LCD_WriteData(0xA1);

    /* 电源控制 2 */
    LCD_WriteCommand(0xD6);
    LCD_WriteData(0xA1);

    /* 伽马正极性 */
    LCD_WriteCommand(0xE0);
    LCD_WriteData(0xF0);
    LCD_WriteData(0x05);
    LCD_WriteData(0x0E);
    LCD_WriteData(0x08);
    LCD_WriteData(0x04);
    LCD_WriteData(0x05);
    LCD_WriteData(0x11);
    LCD_WriteData(0x0B);
    LCD_WriteData(0x0A);
    LCD_WriteData(0x09);
    LCD_WriteData(0x05);
    LCD_WriteData(0x2A);
    LCD_WriteData(0xF6);
    LCD_WriteData(0x1E);
    LCD_WriteData(0x2D);
    LCD_WriteData(0x2D);
    LCD_WriteData(0x05);

    /* 伽马负极性 */
    LCD_WriteCommand(0xE1);
    LCD_WriteData(0xF0);
    LCD_WriteData(0x05);
    LCD_WriteData(0x0E);
    LCD_WriteData(0x09);
    LCD_WriteData(0x04);
    LCD_WriteData(0x06);
    LCD_WriteData(0x10);
    LCD_WriteData(0x0B);
    LCD_WriteData(0x0A);
    LCD_WriteData(0x09);
    LCD_WriteData(0x05);
    LCD_WriteData(0x2A);
    LCD_WriteData(0xF6);
    LCD_WriteData(0x1E);
    LCD_WriteData(0x2D);
    LCD_WriteData(0x2D);
    LCD_WriteData(0x05);

    /* 显示开启 */
    LCD_WriteCommand(0x29);
    delay_ms(50);
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

void LCD_Clear(lcd_color_t color) {
    /* 设置全屏窗口 */
    LCD_WriteCommand(0x2A);
    LCD_WriteData(0); LCD_WriteData(0);
    LCD_WriteData((LCD_WIDTH-1) >> 8); LCD_WriteData((LCD_WIDTH-1) & 0xFF);
    LCD_WriteCommand(0x2B);
    LCD_WriteData(0); LCD_WriteData(0);
    LCD_WriteData((LCD_HEIGHT-1) >> 8); LCD_WriteData((LCD_HEIGHT-1) & 0xFF);
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

    /* 设置窗口 */
    LCD_WriteCommand(0x2A);  /* CASET */
    LCD_WriteData(x >> 8); LCD_WriteData(x & 0xFF);
    LCD_WriteData((x + w - 1) >> 8); LCD_WriteData((x + w - 1) & 0xFF);
    LCD_WriteCommand(0x2B);  /* RASET */
    LCD_WriteData(y >> 8); LCD_WriteData(y & 0xFF);
    LCD_WriteData((y + h - 1) >> 8); LCD_WriteData((y + h - 1) & 0xFF);
    LCD_WriteCommand(0x2C);  /* RAMWR */

    LCD_WriteDataMulti(pixels, (uint32_t)w * h * 2);
}

void LCD_BL_SetBrightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    /* CCR 范围 0-999, 0%=0, 100%=999 */
    uint16_t ccr = (uint16_t)((uint32_t)pct * 999 / 100);
    TIM_SetCompare3(LCD_BL_TIMER, ccr);
}

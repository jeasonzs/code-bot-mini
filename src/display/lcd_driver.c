/**
 * @file    lcd_driver.c
 * @brief   GC9307 / ST7789V3 LCD 驱动 (via SPI1)
 *
 * SPI1 @ 24MHz max, 实际用 ~18MHz 稳妥
 * 屏幕: 1.47" 320x172 IPS RGB565
 * 通信: SPI mode 0 (CPOL=0, CPHA=0), MSB first
 *
 * v0.18: 流式绘制 API (pull model, 跟 EP1 ring buffer 同构)
 *   - LCD_BeginRect: 开窗 + CS_LOW + DC_DATA, 启动 streaming
 *   - LCD_WritePixelsStream: 收一包 (从 EP5 ring buffer 拉) → memcpy → SPI DMA
 *   - LCD_WritePixelsStreamDmaDone: DMA 完成时 CS_HIGH
 *   - LCD_EndRect / LCD_AbortStream: 收尾
 *
 * 数据流: USB ISR 把 EP5 包入队到 ring buffer; main loop 调 LCD_WritePixelsStream 消费.
 * ISR 不调应用层 callback, 应用层不碰 USB 寄存器.
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

    /* DMA1 时钟 (供 SPI1 TX DMA 用) */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

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

    /* SPI1 配置 - 单向发送 (TX only):
     * GC9307 命令/数据协议是单向写, 不需要 RX, 改 1Line_Tx 可省掉每字节 RXNE 等待
     * 和 RX 读, 让 SPI_SendByte 每字节节省 ~16 CPU 周期。
     * 注: 配套改用 TXE 单标志等, 不再读 SPI_I2S_ReceiveData。 */
    SPI_InitStructure.SPI_Direction         = SPI_Direction_1Line_Tx;
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

/* 阻塞发送单字节 (单向 TX only: 只等 TXE, 不读 RX) */
static void SPI_SendByte(uint8_t data) {
    while (SPI_I2S_GetFlagStatus(LCD_SPI, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(LCD_SPI, data);
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

/* 发送 LCD 数据 (DC=1) - 用于命令序列中的少量字节 (CASET/RASET 等) */
static void LCD_WriteData(uint8_t data) {
    LCD_CS_LOW();
    LCD_DC_DATA();
    SPI_SendByte(data);
    LCD_CS_HIGH();
}

/* ===== DMA 加速 ===== */
/* DMA1_Channel3 → SPI1 TX (WCH EVT 示例约定)。8-bit byte 模式。
 * 提供两个发送原语:
 *   LCD_DMASend(src, len)         - 一次性发送, src 指针按 chunk 推进
 *   LCD_DMASendRepeat(src, len)   - 重复发送, src 每次都从头读 (用于单色填充)
 * 调用者必须保证: src 指向的内存 ≥ len 字节, 且在 DMA 期间不被修改。
 *
 * s_dma_buf[1024] 用作重复模式的预填 buffer (单色填充时存 512 像素)。 */
#define LCD_DMA_CHANNEL   DMA1_Channel3
#define LCD_DMA_TC_FLAG   DMA1_FLAG_TC3
#define LCD_DMA_BUF_SIZE  1024

static uint8_t s_dma_buf[LCD_DMA_BUF_SIZE];

/* 填 buffer 为 hi,lo,hi,lo,... 重复 (512 像素) */
static void LCD_FillDmaBuf(uint8_t hi, uint8_t lo) {
    for (uint32_t i = 0; i < LCD_DMA_BUF_SIZE; i += 2) {
        s_dma_buf[i]     = hi;
        s_dma_buf[i + 1] = lo;
    }
}

/* 将 LE uint16_t 数组 (host/caller 自然格式) 翻转为 BE 字节流写入 s_dma_buf,
 * 供 DMA 一次性发给 GC9307。处理 pixel_count 个像素 (≤ LCD_DMA_BUF_SIZE/2)。
 * 16-bit 简单实现: ~5 cycles/pixel, 全屏约 5.7ms 开销 (相对 DMA 36.7ms 是 ~15%)。 */
static void LCD_SwapToDmaBuf(const lcd_color_t *src, uint32_t pixel_count) {
    uint8_t *dst = s_dma_buf;
    for (uint32_t i = 0; i < pixel_count; i++) {
        uint16_t v = src[i];
        *dst++ = (uint8_t)(v >> 8);  /* hi */
        *dst++ = (uint8_t)(v & 0xFF); /* lo */
    }
}

/* 启动 DMA 通道 (peripheral=SPI1->DATAR, 8-bit byte, normal mode) */
static void LCD_DMAStart(void) {
    DMA_InitTypeDef dma = {0};
    dma.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DATAR;
    dma.DMA_DIR                = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize         = LCD_DMA_BUF_SIZE;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode               = DMA_Mode_Normal;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_DeInit(LCD_DMA_CHANNEL);
    DMA_Init(LCD_DMA_CHANNEL, &dma);
    SPI_I2S_DMACmd(LCD_SPI, SPI_I2S_DMAReq_Tx, ENABLE);
}

/* 发一 chunk 并等完成 (阻塞, LCD_DrawRect 用) */
static inline void LCD_DMASendChunk(const uint8_t *src, uint32_t chunk) {
    LCD_DMA_CHANNEL->MADDR = (uint32_t)src;
    LCD_DMA_CHANNEL->CNTR  = chunk;
    DMA_Cmd(LCD_DMA_CHANNEL, ENABLE);
    while (DMA_GetFlagStatus(LCD_DMA_TC_FLAG) == RESET);
    DMA_ClearFlag(LCD_DMA_TC_FLAG);
}

/* 关闭 DMA 通道, 等 SPI 移位寄存器空 */
static inline void LCD_DMAStop(void) {
    while (SPI_I2S_GetFlagStatus(LCD_SPI, SPI_I2S_FLAG_BSY) == SET);
    SPI_I2S_DMACmd(LCD_SPI, SPI_I2S_DMAReq_Tx, DISABLE);
}

/* 发送 len 字节 (src 指针按 chunk 推进; 单次数据用) */
static void LCD_DMASend(const uint8_t *src, uint32_t len) {
    LCD_DMAStart();
    while (len > 0) {
        uint32_t chunk = (len > LCD_DMA_BUF_SIZE) ? LCD_DMA_BUF_SIZE : len;
        LCD_DMASendChunk(src, chunk);
        src += chunk;
        len -= chunk;
    }
    LCD_DMAStop();
}

/* 重复发送 len 字节 (src 每次都从头读; 单色填充用, 需先填好 src) */
static void LCD_DMASendRepeat(const uint8_t *src, uint32_t len) {
    LCD_DMAStart();
    while (len > 0) {
        uint32_t chunk = (len > LCD_DMA_BUF_SIZE) ? LCD_DMA_BUF_SIZE : len;
        LCD_DMASendChunk(src, chunk);  /* src 不变, MADDR 重设为 src */
        len -= chunk;
    }
    LCD_DMAStop();
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

	TIM_CtrlPWMOutputs(LCD_BL_TIMER, ENABLE );
	TIM_ARRPreloadConfig( LCD_BL_TIMER, ENABLE );
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

/* 用单色填充一个矩形 (不需要像素缓冲; 走 DMA) */
static void LCD_FillColor(uint16_t x, uint16_t y, uint16_t w, uint16_t h, lcd_color_t color) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT || w == 0 || h == 0) return;
    if (x + w > LCD_WIDTH)  w = LCD_WIDTH  - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

    LCD_SetAddrWindow(x, y, w, h);

    LCD_FillDmaBuf(color >> 8, color & 0xFF);
    LCD_CS_LOW();
    LCD_DC_DATA();
    LCD_DMASendRepeat(s_dma_buf, (uint32_t)w * h * 2);
    LCD_CS_HIGH();
}

void LCD_Clear(lcd_color_t color) {
    LCD_SetAddrWindow(0, 0, LCD_WIDTH, LCD_HEIGHT);

    LCD_FillDmaBuf(color >> 8, color & 0xFF);
    LCD_CS_LOW();
    LCD_DC_DATA();
    LCD_DMASendRepeat(s_dma_buf, (uint32_t)LCD_PIXELS * 2);
    LCD_CS_HIGH();
}

void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const lcd_color_t *pixels) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

    LCD_SetAddrWindow(x, y, w, h);

    /* 分块 swap + DMA (s_dma_buf 一次最多装 512 像素) */
    const uint32_t total_pixels = (uint32_t)w * h;
    const uint32_t max_chunk    = LCD_DMA_BUF_SIZE / 2;  /* 512 像素 */
    uint32_t sent = 0;

    LCD_CS_LOW();
    LCD_DC_DATA();
    while (sent < total_pixels) {
        uint32_t chunk = total_pixels - sent;
        if (chunk > max_chunk) chunk = max_chunk;
        LCD_SwapToDmaBuf(pixels + sent, chunk);
        LCD_DMASend(s_dma_buf, chunk * 2);
        sent += chunk;
    }
    LCD_CS_HIGH();
}

/* ============================================================== */
/* v0.18: 流式绘制 API (同步阻塞, 0 状态)                          */
/* ============================================================== */

/* 直接从 ring buffer slot DMA 到 SPI, 不需要中转 buffer.
 * 协议规定 host 发的是 RGB565 大端字节流 (匹配 GC9307 SPI 期望),
 * MCU 端不做 byte-swap. ring buffer slot 在 SPI DMA 期间不会被覆盖
 * (ISR 写 LoadPtr slot, 跟 DealPtr slot 不同), 所以直接 DMA from slot 安全. */

void LCD_BeginRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

    LCD_SetAddrWindow(x, y, w, h);
    LCD_CS_LOW();
    LCD_DC_DATA();
}

void LCD_EndRect(void) {
    /* LCD_WritePixelsStream 是同步阻塞, 调用返回时 DMA 已 done + SPI 移位寄存器已空.
     * 直接 CS_HIGH 即可. 无状态. */
    LCD_CS_HIGH();
}

void LCD_AbortStream(void) {
    /* 跟 LCD_EndRect 一样: 无状态, CS_HIGH 即丢剩余数据. */
    LCD_CS_HIGH();
}

void LCD_WritePixelsStream(const uint8_t *buf, uint16_t len) {
    if (len == 0) return;
    /* 复用现成的 blocking multi-chunk DMA: 自动分 chunk, 每 chunk 同步等 TC,
     * 末尾 LCD_DMAStop 等 SPI BSY 清. host 端发的是 BE 字节流, MCU 不 swap. */
    LCD_DMASend(buf, len);
}

void LCD_BL_SetBrightness(uint8_t pct) {
    // if (pct > 100) pct = 100;
    // /* CCR 范围 0-999, 0%=0, 100%=999 */
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
 *   - 色块顺序: 左上红/右上绿/左下蓝/右下白, 位置错乱=镜像/翻转
 * + DrawRect 测试: 在中心画一组矩形, 验证 LCD_DrawRect 走 DMA 路径正常。
 *   - 不同尺寸 (16x16 / 32x32 / 17x30 奇数宽 / 64x8 扁条 / 8x64 长条)
 *   - 不同位置 (避免圆角区域)
 *   - 验证: 颜色正确, 边界对齐, 无撕裂/拉花 */
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

    /* ===== DrawRect 测试 (DMA + byte-swap 路径) =====
     * 8 色横条 (5 像素高 × 8 条 = 40 高), 验证:
     *   1. lcd_color_t 自然表达 (uint16_t)
     *   2. 驱动 byte-swap 对 8 种典型 RGB565 值都正确 (含 0x0000/0xFFFF 边界) */
    static const lcd_color_t palette[8] = {
        LCD_COLOR_RED, LCD_COLOR_GREEN, LCD_COLOR_BLUE,
        LCD_COLOR_YELLOW, LCD_COLOR_CYAN, LCD_COLOR_MAGENTA,
        LCD_COLOR_WHITE, LCD_COLOR_BLACK
    };
    static lcd_color_t stripes[40 * 40];   /* 3200B */
    for (uint32_t row = 0; row < 40; row++) {
        lcd_color_t c = palette[row / 5];
        for (uint32_t col = 0; col < 40; col++) {
            stripes[row * 40 + col] = c;
        }
    }

    /* 在屏幕中心画方块 (避开圆角) */
    LCD_DrawRect(W / 2 - 20, H / 2 - 20, 40, 40, stripes);
}

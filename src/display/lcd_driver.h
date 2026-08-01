/**
 * @file    lcd_driver.h
 * @brief   LCD 驱动接口 (GC9307 / ST7789V3)
 *
 * 通过 SPI1 (PA4/5/6/7) 驱动 GC9307 1.47" 320x172 IPS
 *
 * v0.18 流式 API:
 *   - LCD_BeginRect + LCD_WritePixelsStream + LCD_EndRect: 流式推像素
 *   - LCD_AbortStream: 强制中断当前流
 *   - LCD_IsStreaming: 查询当前是否在流式状态
 *   - EP5_OUT_Callback: 弱符号 (USB 库调用), 触发流式 DMA
 */

#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H

#include <stdint.h>
#include "pinout.h"

#define LCD_WIDTH            320
#define LCD_HEIGHT           172
#define LCD_PIXELS           ((uint32_t)LCD_WIDTH * LCD_HEIGHT)

/* GC9307 是 240x320 控制器; 172 短边在 RAM 中居中, 间隙 (240-172)/2=34。
 * 横屏(MADCTL 0x68, MV=1): 短边=高度, 偏移加在行(RASET/Y), 列(X)不偏移。
 * 若改竖屏(MADCTL 0x48, 172 宽): 改成 X=34, Y=0。 */
#define LCD_X_OFFSET         0
#define LCD_Y_OFFSET         34

/* 颜色格式: RGB565 (16-bit) */
typedef uint16_t lcd_color_t;

#define LCD_COLOR_BLACK      0x0000
#define LCD_COLOR_WHITE      0xFFFF
#define LCD_COLOR_RED        0xF800
#define LCD_COLOR_GREEN      0x07E0
#define LCD_COLOR_BLUE       0x001F
#define LCD_COLOR_YELLOW     0xFFE0
#define LCD_COLOR_CYAN       0x07FF
#define LCD_COLOR_MAGENTA    0xF81F

/* Service 主题色 (code-bot-service/src/codebot/render/theme.py: VSCodeDark).
 * 设备侧 standby 等独立画面复用, 不再自己挑色. */
#define LCD_COLOR_THEME_FG   0x8E9F  /* VSCodeDark.INFO    = #8CD2FA */

/* 初始化 (时钟 + SPI + GC9307 init sequence) */
void LCD_Init(void);

/* 重新初始化 (CMD_RESET_DISPLAY) */
void LCD_Reinit(void);

/* 全屏填充 */
void LCD_Clear(lcd_color_t color);

/* 绘制一个矩形 (x, y, w, h, pixels 紧跟其后, RGB565 按行)
 *
 * pixels 字节序约定: 每像素 uint16_t (lcd_color_t) 按 host 端 LE 布局。
 * 驱动内部负责 byte-swap 成 GC9307 SPI 期望的 BE 字节流。
 * 调用方直接传 uint16_t[] / lcd_color_t[] 即可, 无需手动拆 hi/lo 字节。
 *
 * w*h*2 字节的源 buffer 在 DMA 期间不能被修改。
 */
void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const lcd_color_t *pixels);

/* 设置背光亮度 (0-100, 0=灭, 100=最亮) */
void LCD_BL_SetBrightness(uint8_t pct);

/* ============================================================== */
/* v0.18: 流式绘制 API (EP5 OUT image data → SPI DMA 直通 LCD)   */
/* ============================================================== */

/* 开始一个绘制矩形: SetAddrWindow + CS_LOW + DC_DATA */
void LCD_BeginRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/* 礼貌结束: 调用前保证已把所有像素 LCD_WritePixelsStream 完, CS_HIGH 即可 */
void LCD_EndRect(void);

/* 强制结束: 立即 CS_HIGH, 丢剩余. 用于异常恢复 (CMD_DRAW_RECT_ABORT) */
void LCD_AbortStream(void);

/* EP5 OUT 收到的像素 buffer (RGB565 大端字节流, 匹配 GC9307 SPI 期望) 通过
 * 这个函数进 SPI DMA. MCU 不做 byte-swap, 直接 DMA from buf → SPI.
 *
 * 同步阻塞: 启 DMA 后死等 TC flag, 再等 SPI BSY 清零 (最后一字节已移出).
 * 调用返回时 SPI 已空闲, 可以安全 CS_HIGH. */
void LCD_WritePixelsStream(const uint8_t *buf, uint16_t len);

/* SPI TX DMA 完成回调 (在 DMA1_Channel3 ISR 里调用)
 * - 清 s_dma_busy flag
 * - 若 s_pending_end: CS_HIGH, 清除 pending */
void LCD_WritePixelsStreamDmaDone(void);

/* ===== 调试辅助 ===== */

/* 纯色循环 (红/绿/蓝/白/黑), 每色停留 ms 毫秒 */
void LCD_DebugColorCycle(uint16_t ms);

/* 对齐图案 (外边框 + 四角方块 + 居中十字), 用于核对偏移是否贴屏边 */
void LCD_DebugAlignPattern(void);

#endif /* LCD_DRIVER_H */

/**
 * @file    lcd_driver.h
 * @brief   LCD 驱动接口 (GC9307 / ST7789V3)
 *
 * 通过 SPI1 (PA4/5/6/7) 驱动 GC9307 1.47" 320x172 IPS
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

/* 初始化 (时钟 + SPI + GC9307 init sequence) */
void LCD_Init(void);

/* 重新初始化 (CMD_RESET_DISPLAY) */
void LCD_Reinit(void);

/* 全屏填充 */
void LCD_Clear(lcd_color_t color);

/* 绘制一个矩形 (x, y, w, h, pixels 紧跟其后, RGB565 按行) */
void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *pixels);

/* 设置背光亮度 (0-100, 0=灭, 100=最亮) */
void LCD_BL_SetBrightness(uint8_t pct);

/* ===== 调试辅助 ===== */

/* 纯色循环 (红/绿/蓝/白/黑), 每色停留 ms 毫秒 */
void LCD_DebugColorCycle(uint16_t ms);

/* 对齐图案 (外边框 + 四角方块 + 居中十字), 用于核对偏移是否贴屏边 */
void LCD_DebugAlignPattern(void);

#endif /* LCD_DRIVER_H */

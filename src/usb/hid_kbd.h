/**
 * @file    hid_kbd.h
 * @brief   HID Keyboard 击键队列 (CMD_HID_KEYSTROKES 处理)
 *
 * 当 host 收到屏 5/屏 7 的图标长按事件, 转换为 HID Keyboard report
 * 序列发送到本设备, 设备排队后通过 EP3 IN 周期发送
 */

#ifndef HID_KBD_H
#define HID_KBD_H

#include <stdint.h>

/* HID Keyboard report 长度 (1B modifier + 1B reserved + 6B keycode) */
#define HID_REPORT_SIZE   8

/* 击键队列深度 (每次"git status"约 24 个 report, 队列 32 足够) */
#define HID_QUEUE_DEPTH   32

/* 初始化 HID 击键队列 (main 启动时调用一次) */
void HID_Kbd_Init(void);

/* 入队一组 HID 击键 (count × 8B reports, 紧跟 release 全 0 report) */
void HID_Kbd_EnqueueReports(const uint8_t *reports, uint8_t count, uint8_t delay_ms);

/* 主循环调用: 排空队列, 每次发送 1 个 report (20-50ms 节流) */
void HID_Kbd_SendPending(void);

#endif /* HID_KBD_H */

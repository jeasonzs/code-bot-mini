/**
 * @file    hid_kbd.c
 * @brief   HID Keyboard 击键队列 + 发送
 */

#include "hid_kbd.h"
#include "util/ringbuf.h"
#include "ch32x035_conf.h"

/* WCH USB 驱动期望的 HID SetReport 缓冲 (ch32x035_usbfs_device.c 引用) */
__attribute__((aligned(4))) uint8_t HID_Report_Buffer[64];
volatile uint8_t HID_Set_Report_Flag = 0;

/* 入队的 report 在这里: 单字节环形缓冲, 存放 8B report 单元 */
static uint8_t  s_q_storage[HID_QUEUE_DEPTH * HID_REPORT_SIZE];
static ringbuf_t s_q_rb;
static uint32_t s_last_send_tick = 0;  /* 1ms tick */

/* USB 发送端: 需外部实现 - 在 usb_endp.c 中 */
extern uint8_t EP3_Tx_Buf[];
extern int HID_SendReport(const uint8_t *report);

void HID_Kbd_Init(void) {
    ringbuf_init(&s_q_rb, s_q_storage, sizeof(s_q_storage));
    s_last_send_tick = 0;
}

void HID_Kbd_EnqueueReports(const uint8_t *reports, uint8_t count, uint8_t delay_ms) {
    /* 简化: 暂不实现 delay_ms 排队, 直接 enqueue 全部 */
    (void)delay_ms;
    /* 期望 reports 数组是 [press, release, press, release, ...] */
    for (uint8_t i = 0; i < count; i++) {
        ringbuf_write(&s_q_rb, &reports[i * HID_REPORT_SIZE], HID_REPORT_SIZE);
    }
}

void HID_Kbd_SendPending(void) {
    /* 节流: 20ms 发送一个 report (USB polling 间隔 1ms, 但 HID 击键要慢一点更像真人) */
    extern volatile uint32_t g_ticks_ms;
    if (g_ticks_ms - s_last_send_tick < 20) return;
    s_last_send_tick = g_ticks_ms;

    if (ringbuf_is_empty(&s_q_rb)) return;

    uint8_t report[HID_REPORT_SIZE];
    if (ringbuf_read(&s_q_rb, report, HID_REPORT_SIZE) != HID_REPORT_SIZE) return;

    /* 真的通过 EP3 IN 上报. 失败 (busy/未使能) 时把 report 放回队首, 下一轮重试. */
    if (HID_SendReport(report) != 0) {
        /* 推回去: tail 倒退一格, 下一轮再试 */
        ringbuf_unread(&s_q_rb, HID_REPORT_SIZE);
    }
}

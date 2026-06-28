/**
 * @file    ch32x035_it.c
 * @brief   中断服务程序 - 覆盖 WCH 启动文件中的 weak 默认实现
 *
 * WCH 启动文件 (startup_ch32x035.S) 提供所有 IRQ 的 weak 默认实现 (空函数),
 * 我们需要在这里覆盖实际使用的 IRQ handler.
 *
 * 实际使用的 IRQ (按优先级):
 *   - USBFS_IRQn: USB 收发
 *   - DMA1_Channel3_IRQn: SPI TX DMA 完成 (TODO)
 *   - I2C1_EV_IRQn: I2C 事件 (TODO)
 *   - I2C1_ER_IRQn: I2C 错误 (TODO)
 *   - EXTI15_8_IRQn: Touch INT (PA9, 共享向量含 EXTI8..EXTI15)
 *   - TIM3_IRQn: 1ms 滴答, 累加 g_ticks_ms (SysTick 留给 debug.c Delay_Ms)
 */

#include "ch32x035_conf.h"
#include "pinout.h"
#include "display/touch.h"

/* WCH RISC-V 中断处理函数必须用 interrupt("WCH-Interrupt-fast") 属性,
 * 否则 IRQ 入口不会保存/恢复 mstatus, 导致中断返回后全局中断被意外关掉,
 * 现象: TIM3 只触发一次 (g_ticks_ms 卡在 1). */
void TIM3_IRQHandler(void)         __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI15_8_IRQHandler(void)     __attribute__((interrupt("WCH-Interrupt-fast")));

/* USB 中断 - 由 WCH USBFS 驱动实现, 不需要我们处理
 * (USBFS_IRQHandler 在 ch32x035_usbfs_device.c 中已定义) */

/**
 * @brief TIM3 update 中断 - 1ms 滴答
 * @note  TIM3 由 main.c 的 Tick_TIM3_Init() 配置为 1kHz update.
 */
void TIM3_IRQHandler(void) {
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        g_ticks_ms++;
    }
}

/**
 * @brief Touch INT 中断 (PA9 EXTI9, 共享 EXTI15_8 向量含 EXTI8..EXTI15)
 * @note  下降沿触发, 通知触摸模块读坐标
 */
void EXTI15_8_IRQHandler(void) {
    if (EXTI_GetITStatus(TP_INT_EXTI_LINE) != RESET) {
        EXTI_ClearITPendingBit(TP_INT_EXTI_LINE);
        Touch_EXTI_Handler();
    }
}

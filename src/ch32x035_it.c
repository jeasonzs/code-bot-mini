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
 *   - EXTI0_IRQn: Touch INT (PB0)
 *   - SysTick_IRQn: 1ms 滴答 (WCH core_riscv.c 提供)
 */

#include "ch32x035_conf.h"
#include "pinout.h"
#include "display/touch.h"

/* USB 中断 - 由 WCH USBFS 驱动实现, 不需要我们处理
 * (USBFS_IRQHandler 在 ch32x035_usbfs_device.c 中已定义) */

/**
 * @brief Touch INT 中断 (PB0 EXTI0)
 * @note  下降沿触发, 通知触摸模块读坐标
 */
void EXTI0_IRQHandler(void) {
    if (EXTI_GetITStatus(TP_INT_EXTI_LINE) != RESET) {
        EXTI_ClearITPendingBit(TP_INT_EXTI_LINE);
        Touch_EXTI_Handler();
    }
}

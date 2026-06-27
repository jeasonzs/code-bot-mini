/**
 * @file    debug_usart.c
 * @brief   USART3 初始化 (PC18=TX, PC19=RX), 仅 DEBUG_IFACE=SERIAL 时编译
 *
 * 调用 Debug_USART_Init() 后, libc_stubs.c 的 printf 即可通过 PC18 串口输出.
 */

#include "debug_usart.h"

#if (DEBUG_IFACE == DEBUG_IFACE_SERIAL)

void Debug_USART_Init(void) {
    GPIO_InitTypeDef  gpio_cfg  = {0};
    USART_InitTypeDef usart_cfg = {0};

    /* 1. 时钟: GPIOC + AFIO (remap 需要) + USART3 */
    RCC_APB2PeriphClockCmd(DEBUG_USART_GPIO_CLK | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(DEBUG_USART_CLK, ENABLE);

    /* 2. USART3 重映射到 PC18/PC19 (覆盖默认 AF0 = WCH SDI) */
    DEBUG_USART_REMAP();

    /* 3. PC18 (TX): 复用推挽输出, 50MHz */
    gpio_cfg.GPIO_Pin   = DEBUG_USART_TX_PIN;
    gpio_cfg.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio_cfg.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DEBUG_USART_GPIO_PORT, &gpio_cfg);

    /* 4. PC19 (RX): 浮空输入 (主机侧一般会带 1K 上拉或线浮空) */
    gpio_cfg.GPIO_Pin   = DEBUG_USART_RX_PIN;
    gpio_cfg.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    gpio_cfg.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DEBUG_USART_GPIO_PORT, &gpio_cfg);

    /* 5. USART3 配置: DEBUG_USART_BAUDRATE 8N1, Tx+Rx 双工 (见 pinout.h NOTE_9600) */
    usart_cfg.USART_BaudRate            = DEBUG_USART_BAUDRATE;
    usart_cfg.USART_WordLength          = USART_WordLength_8b;
    usart_cfg.USART_StopBits            = USART_StopBits_1;
    usart_cfg.USART_Parity             = USART_Parity_No;
    usart_cfg.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart_cfg.USART_Mode               = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(DEBUG_USART, &usart_cfg);

    /* 6. 使能 */
    USART_Cmd(DEBUG_USART, ENABLE);
}

#endif /* DEBUG_IFACE == DEBUG_IFACE_SERIAL */

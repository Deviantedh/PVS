#ifndef SIM_USART1_H
#define SIM_USART1_H

#include <stddef.h>
#include <stdint.h>

#define USART1_BASE 0x40013800u

#define USART1_SR_OFFSET  0x00u
#define USART1_DR_OFFSET  0x04u
#define USART1_BRR_OFFSET 0x08u
#define USART1_CR1_OFFSET 0x0Cu

#define USART1_SR_RXNE 0x0020u
#define USART1_SR_TXE  0x0080u

#define USART1_CR1_RE     0x0004u
#define USART1_CR1_TE     0x0008u
#define USART1_CR1_RXNEIE 0x0020u
#define USART1_CR1_UE     0x2000u

#define USART1_TX_BUFFER_SIZE 256u

typedef struct usart1 {
    uint32_t sr;
    uint32_t dr;
    uint32_t brr;
    uint32_t cr1;
    uint8_t tx_buffer[USART1_TX_BUFFER_SIZE];
    size_t tx_head;
    size_t tx_tail;
    size_t tx_count;
} usart1_t;

void usart1_init(usart1_t *usart);
void usart1_reset(usart1_t *usart);
int usart1_read32(const usart1_t *usart, uint32_t offset, uint32_t *value);
int usart1_write32(usart1_t *usart, uint32_t offset, uint32_t value);
size_t usart1_tx_available(const usart1_t *usart);
int usart1_tx_pop(usart1_t *usart, uint8_t *value);

#endif

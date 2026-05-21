#include "sim/usart1.h"

#include <stddef.h>
#include <string.h>

static int usart1_tx_push(usart1_t *usart, uint8_t value) {
    if (usart->tx_count >= USART1_TX_BUFFER_SIZE) {
        return -1;
    }

    usart->tx_buffer[usart->tx_tail] = value;
    usart->tx_tail = (usart->tx_tail + 1u) % USART1_TX_BUFFER_SIZE;
    usart->tx_count++;
    return 0;
}

void usart1_init(usart1_t *usart) {
    usart1_reset(usart);
}

void usart1_reset(usart1_t *usart) {
    if (usart == NULL) {
        return;
    }

    memset(usart, 0, sizeof(*usart));
    usart->sr = USART1_SR_TXE;
}

int usart1_read32(const usart1_t *usart, uint32_t offset, uint32_t *value) {
    if (usart == NULL || value == NULL) {
        return -1;
    }

    switch (offset) {
    case USART1_SR_OFFSET:
        *value = usart->sr;
        return 0;
    case USART1_DR_OFFSET:
        *value = usart->dr;
        return 0;
    case USART1_BRR_OFFSET:
        *value = usart->brr;
        return 0;
    case USART1_CR1_OFFSET:
        *value = usart->cr1;
        return 0;
    default:
        return -1;
    }
}

int usart1_write32(usart1_t *usart, uint32_t offset, uint32_t value) {
    if (usart == NULL) {
        return -1;
    }

    switch (offset) {
    case USART1_SR_OFFSET:
        usart->sr = (value & USART1_SR_RXNE) | USART1_SR_TXE;
        return 0;
    case USART1_DR_OFFSET:
        usart->dr = value & 0x01FFu;
        usart->sr |= USART1_SR_TXE;
        if ((usart->cr1 & (USART1_CR1_UE | USART1_CR1_TE)) == (USART1_CR1_UE | USART1_CR1_TE)) {
            return usart1_tx_push(usart, (uint8_t)(value & 0xFFu));
        }
        return 0;
    case USART1_BRR_OFFSET:
        usart->brr = value;
        return 0;
    case USART1_CR1_OFFSET:
        usart->cr1 = value & (USART1_CR1_UE | USART1_CR1_TE | USART1_CR1_RE | USART1_CR1_RXNEIE);
        return 0;
    default:
        return -1;
    }
}

size_t usart1_tx_available(const usart1_t *usart) {
    if (usart == NULL) {
        return 0;
    }

    return usart->tx_count;
}

int usart1_tx_pop(usart1_t *usart, uint8_t *value) {
    if (usart == NULL || value == NULL || usart->tx_count == 0u) {
        return -1;
    }

    *value = usart->tx_buffer[usart->tx_head];
    usart->tx_head = (usart->tx_head + 1u) % USART1_TX_BUFFER_SIZE;
    usart->tx_count--;
    return 0;
}

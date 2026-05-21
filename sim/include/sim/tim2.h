#ifndef SIM_TIM2_H
#define SIM_TIM2_H

#include <stdint.h>

#include "sim/irq.h"
#include "sim/nvic.h"

#define TIM2_BASE       0x40000000u
#define TIM2_IRQ_NUMBER SIM_IRQ_TIM2

#define TIM2_CR1_OFFSET  0x00u
#define TIM2_DIER_OFFSET 0x0Cu
#define TIM2_SR_OFFSET   0x10u
#define TIM2_CNT_OFFSET  0x24u
#define TIM2_PSC_OFFSET  0x28u
#define TIM2_ARR_OFFSET  0x2Cu

#define TIM2_CR1_CEN 0x0001u
#define TIM2_DIER_UIE 0x0001u
#define TIM2_SR_UIF 0x0001u

typedef struct tim2 {
    uint32_t cr1;
    uint32_t psc;
    uint32_t arr;
    uint32_t cnt;
    uint32_t dier;
    uint32_t sr;
    uint32_t psc_counter;
} tim2_t;

void tim2_init(tim2_t *tim2);
void tim2_reset(tim2_t *tim2);
int tim2_read32(const tim2_t *tim2, uint32_t offset, uint32_t *value);
int tim2_write32(tim2_t *tim2, uint32_t offset, uint32_t value);
void tim2_tick(tim2_t *tim2, nvic_t *nvic, uint32_t ticks);

#endif

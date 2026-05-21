#include "sim/tim2.h"

#include <stddef.h>
#include <string.h>

static void tim2_raise_update(tim2_t *tim2, nvic_t *nvic) {
    tim2->sr |= TIM2_SR_UIF;
    if ((tim2->dier & TIM2_DIER_UIE) != 0u) {
        (void)nvic_set_pending(nvic, TIM2_IRQ_NUMBER);
    }
}

void tim2_init(tim2_t *tim2) {
    tim2_reset(tim2);
}

void tim2_reset(tim2_t *tim2) {
    if (tim2 == NULL) {
        return;
    }

    memset(tim2, 0, sizeof(*tim2));
}

int tim2_read32(const tim2_t *tim2, uint32_t offset, uint32_t *value) {
    if (tim2 == NULL || value == NULL) {
        return -1;
    }

    switch (offset) {
    case TIM2_CR1_OFFSET:
        *value = tim2->cr1;
        return 0;
    case TIM2_DIER_OFFSET:
        *value = tim2->dier;
        return 0;
    case TIM2_SR_OFFSET:
        *value = tim2->sr;
        return 0;
    case TIM2_CNT_OFFSET:
        *value = tim2->cnt;
        return 0;
    case TIM2_PSC_OFFSET:
        *value = tim2->psc;
        return 0;
    case TIM2_ARR_OFFSET:
        *value = tim2->arr;
        return 0;
    default:
        return -1;
    }
}

int tim2_write32(tim2_t *tim2, uint32_t offset, uint32_t value) {
    if (tim2 == NULL) {
        return -1;
    }

    switch (offset) {
    case TIM2_CR1_OFFSET:
        tim2->cr1 = value & TIM2_CR1_CEN;
        return 0;
    case TIM2_DIER_OFFSET:
        tim2->dier = value & TIM2_DIER_UIE;
        return 0;
    case TIM2_SR_OFFSET:
        tim2->sr = value & TIM2_SR_UIF;
        return 0;
    case TIM2_CNT_OFFSET:
        tim2->cnt = value;
        return 0;
    case TIM2_PSC_OFFSET:
        tim2->psc = value;
        tim2->psc_counter = 0;
        return 0;
    case TIM2_ARR_OFFSET:
        tim2->arr = value;
        return 0;
    default:
        return -1;
    }
}

void tim2_tick(tim2_t *tim2, nvic_t *nvic, uint32_t ticks) {
    if (tim2 == NULL || ticks == 0u) {
        return;
    }

    for (uint32_t i = 0; i < ticks; ++i) {
        if ((tim2->cr1 & TIM2_CR1_CEN) == 0u) {
            continue;
        }

        if (tim2->psc_counter < tim2->psc) {
            tim2->psc_counter++;
            continue;
        }

        tim2->psc_counter = 0;
        if (tim2->cnt >= tim2->arr) {
            tim2->cnt = 0;
            tim2_raise_update(tim2, nvic);
        } else {
            tim2->cnt++;
        }
    }
}

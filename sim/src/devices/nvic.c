#include "sim/nvic.h"

#include <string.h>

static int nvic_irq_is_valid(int irq) {
    return irq >= 0 && irq < (int)NVIC_MAX_IRQS;
}

void nvic_init(nvic_t *nvic) {
    nvic_reset(nvic);
}

void nvic_reset(nvic_t *nvic) {
    if (nvic == NULL) {
        return;
    }

    memset(nvic->enabled, 0, sizeof(nvic->enabled));
    memset(nvic->pending, 0, sizeof(nvic->pending));
    memset(nvic->priority, 0, sizeof(nvic->priority));
}

int nvic_enable_irq(nvic_t *nvic, int irq) {
    if (nvic == NULL || !nvic_irq_is_valid(irq)) {
        return -1;
    }

    nvic->enabled[irq] = 1u;
    return 0;
}

int nvic_disable_irq(nvic_t *nvic, int irq) {
    if (nvic == NULL || !nvic_irq_is_valid(irq)) {
        return -1;
    }

    nvic->enabled[irq] = 0u;
    return 0;
}

int nvic_is_enabled(const nvic_t *nvic, int irq) {
    if (nvic == NULL || !nvic_irq_is_valid(irq)) {
        return 0;
    }

    return nvic->enabled[irq] != 0u;
}

int nvic_set_pending(nvic_t *nvic, int irq) {
    if (nvic == NULL || !nvic_irq_is_valid(irq)) {
        return -1;
    }

    nvic->pending[irq] = 1u;
    return 0;
}

int nvic_clear_pending(nvic_t *nvic, int irq) {
    if (nvic == NULL || !nvic_irq_is_valid(irq)) {
        return -1;
    }

    nvic->pending[irq] = 0u;
    return 0;
}

int nvic_is_pending(const nvic_t *nvic, int irq) {
    if (nvic == NULL || !nvic_irq_is_valid(irq)) {
        return 0;
    }

    return nvic->pending[irq] != 0u;
}

int nvic_set_priority(nvic_t *nvic, int irq, uint8_t priority) {
    if (nvic == NULL || !nvic_irq_is_valid(irq)) {
        return -1;
    }

    nvic->priority[irq] = priority;
    return 0;
}

int nvic_get_priority(const nvic_t *nvic, int irq, uint8_t *priority) {
    if (nvic == NULL || priority == NULL || !nvic_irq_is_valid(irq)) {
        return -1;
    }

    *priority = nvic->priority[irq];
    return 0;
}

int nvic_select_next(const nvic_t *nvic) {
    int selected = NVIC_NO_IRQ;
    uint8_t best_priority = 0xFFu;

    if (nvic == NULL) {
        return NVIC_NO_IRQ;
    }

    for (int irq = 0; irq < (int)NVIC_MAX_IRQS; ++irq) {
        if (nvic->enabled[irq] == 0u || nvic->pending[irq] == 0u) {
            continue;
        }

        if (selected == NVIC_NO_IRQ || nvic->priority[irq] < best_priority) {
            selected = irq;
            best_priority = nvic->priority[irq];
        }
    }

    return selected;
}

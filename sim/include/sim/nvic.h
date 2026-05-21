#ifndef SIM_NVIC_H
#define SIM_NVIC_H

#include <stdint.h>

#define NVIC_MAX_IRQS 64u
#define NVIC_NO_IRQ   (-1)

typedef struct nvic {
    uint8_t enabled[NVIC_MAX_IRQS];
    uint8_t pending[NVIC_MAX_IRQS];
    uint8_t priority[NVIC_MAX_IRQS];
} nvic_t;

void nvic_init(nvic_t *nvic);
void nvic_reset(nvic_t *nvic);
int nvic_enable_irq(nvic_t *nvic, int irq);
int nvic_disable_irq(nvic_t *nvic, int irq);
int nvic_is_enabled(const nvic_t *nvic, int irq);
int nvic_set_pending(nvic_t *nvic, int irq);
int nvic_clear_pending(nvic_t *nvic, int irq);
int nvic_is_pending(const nvic_t *nvic, int irq);
int nvic_set_priority(nvic_t *nvic, int irq, uint8_t priority);
int nvic_get_priority(const nvic_t *nvic, int irq, uint8_t *priority);
int nvic_select_next(const nvic_t *nvic);

#endif

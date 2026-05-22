#ifndef SIM_GPIO_H
#define SIM_GPIO_H

#include <stdint.h>

#define GPIOA_BASE 0x40010800u

#define GPIO_CRL_OFFSET  0x00u
#define GPIO_CRH_OFFSET  0x04u
#define GPIO_IDR_OFFSET  0x08u
#define GPIO_ODR_OFFSET  0x0Cu
#define GPIO_BSRR_OFFSET 0x10u
#define GPIO_BRR_OFFSET  0x14u

typedef struct gpio {
    uint32_t crl;
    uint32_t crh;
    uint32_t idr;
    uint32_t odr;
} gpio_t;

void gpio_init(gpio_t *gpio);
void gpio_reset(gpio_t *gpio);
int gpio_read32(const gpio_t *gpio, uint32_t offset, uint32_t *value);
int gpio_write32(gpio_t *gpio, uint32_t offset, uint32_t value);
int gpio_set_input(gpio_t *gpio, uint8_t pin, int level);
int gpio_get_level(const gpio_t *gpio, uint8_t pin, int *level);

#endif

#include "sim/gpio.h"

#include <stddef.h>
#include <string.h>

void gpio_init(gpio_t *gpio) {
    gpio_reset(gpio);
}

void gpio_reset(gpio_t *gpio) {
    if (gpio == NULL) {
        return;
    }

    memset(gpio, 0, sizeof(*gpio));
}

int gpio_read32(const gpio_t *gpio, uint32_t offset, uint32_t *value) {
    if (gpio == NULL || value == NULL) {
        return -1;
    }

    switch (offset) {
    case GPIO_CRL_OFFSET:
        *value = gpio->crl;
        return 0;
    case GPIO_CRH_OFFSET:
        *value = gpio->crh;
        return 0;
    case GPIO_IDR_OFFSET:
        *value = gpio->idr & 0xFFFFu;
        return 0;
    case GPIO_ODR_OFFSET:
        *value = gpio->odr & 0xFFFFu;
        return 0;
    default:
        return -1;
    }
}

int gpio_write32(gpio_t *gpio, uint32_t offset, uint32_t value) {
    if (gpio == NULL) {
        return -1;
    }

    switch (offset) {
    case GPIO_CRL_OFFSET:
        gpio->crl = value;
        return 0;
    case GPIO_CRH_OFFSET:
        gpio->crh = value;
        return 0;
    case GPIO_ODR_OFFSET:
        gpio->odr = value & 0xFFFFu;
        return 0;
    case GPIO_BSRR_OFFSET:
        gpio->odr |= value & 0xFFFFu;
        gpio->odr &= ~((value >> 16) & 0xFFFFu);
        return 0;
    case GPIO_BRR_OFFSET:
        gpio->odr &= ~(value & 0xFFFFu);
        return 0;
    default:
        return -1;
    }
}

int gpio_set_input(gpio_t *gpio, uint8_t pin, int level) {
    uint32_t mask;

    if (gpio == NULL || pin >= 16u) {
        return -1;
    }

    mask = 1u << pin;
    if (level != 0) {
        gpio->idr |= mask;
    } else {
        gpio->idr &= ~mask;
    }
    return 0;
}

int gpio_get_level(const gpio_t *gpio, uint8_t pin, int *level) {
    if (gpio == NULL || level == NULL || pin >= 16u) {
        return -1;
    }

    *level = (gpio->idr & (1u << pin)) != 0u ? 1 : 0;
    return 0;
}

#include "sim/bus.h"

#include <stddef.h>

#include "sim/tim2.h"

static bus_result_t bus_result_make(
    bus_status_t status,
    bus_access_type_t access,
    uint32_t addr,
    uint8_t width
) {
    bus_result_t result;

    result.status = status;
    result.access = access;
    result.addr = addr;
    result.width = width;
    return result;
}

static bus_result_t bus_translate(
    const bus_t *bus,
    uint32_t addr,
    uint8_t **ptr,
    size_t width,
    bus_access_type_t access
) {
    if (bus == NULL || bus->memory == NULL || ptr == NULL) {
        return bus_result_make(BUS_STATUS_BAD_ARGUMENT, access, addr, (uint8_t)width);
    }

    if ((width == 2u && (addr & 0x1u) != 0u) || (width == 4u && (addr & 0x3u) != 0u)) {
        return bus_result_make(BUS_STATUS_UNALIGNED, access, addr, (uint8_t)width);
    }

    if (addr >= SIM_FLASH_BASE && (size_t)(addr - SIM_FLASH_BASE) + width <= bus->memory->flash_size) {
        *ptr = &bus->memory->flash[addr - SIM_FLASH_BASE];
        return bus_result_make(BUS_STATUS_OK, access, addr, (uint8_t)width);
    }

    if (addr >= SIM_SRAM_BASE && (size_t)(addr - SIM_SRAM_BASE) + width <= bus->memory->sram_size) {
        *ptr = &bus->memory->sram[addr - SIM_SRAM_BASE];
        return bus_result_make(BUS_STATUS_OK, access, addr, (uint8_t)width);
    }

    return bus_result_make(BUS_STATUS_UNMAPPED, access, addr, (uint8_t)width);
}

static int bus_is_unaligned(uint32_t addr, size_t width) {
    return (width == 2u && (addr & 0x1u) != 0u) || (width == 4u && (addr & 0x3u) != 0u);
}

static int bus_tim2_offset(uint32_t addr, uint32_t *offset) {
    if (addr < TIM2_BASE || offset == NULL) {
        return 0;
    }

    *offset = addr - TIM2_BASE;
    return *offset <= TIM2_ARR_OFFSET;
}

void bus_init(bus_t *bus, memory_t *memory, tim2_t *tim2) {
    if (bus == NULL) {
        return;
    }

    bus->memory = memory;
    bus->tim2 = tim2;
}

bus_result_t bus_read8(bus_t *bus, uint32_t addr, uint8_t *value) {
    uint8_t *ptr = NULL;
    bus_result_t result = bus_translate(bus, addr, &ptr, sizeof(uint8_t), BUS_ACCESS_READ);

    if (value == NULL) {
        return bus_result_make(BUS_STATUS_BAD_ARGUMENT, BUS_ACCESS_READ, addr, sizeof(uint8_t));
    }

    if (result.status != BUS_STATUS_OK) {
        return result;
    }

    *value = ptr[0];
    return result;
}

bus_result_t bus_read16(bus_t *bus, uint32_t addr, uint16_t *value) {
    uint8_t *ptr = NULL;
    bus_result_t result = bus_translate(bus, addr, &ptr, sizeof(uint16_t), BUS_ACCESS_READ);

    if (value == NULL) {
        return bus_result_make(BUS_STATUS_BAD_ARGUMENT, BUS_ACCESS_READ, addr, sizeof(uint16_t));
    }

    if (result.status != BUS_STATUS_OK) {
        return result;
    }

    *value = (uint16_t)ptr[0]
        | ((uint16_t)ptr[1] << 8);
    return result;
}

bus_result_t bus_read32(bus_t *bus, uint32_t addr, uint32_t *value) {
    uint8_t *ptr = NULL;
    bus_result_t result;
    uint32_t offset = 0;

    if (value == NULL) {
        return bus_result_make(BUS_STATUS_BAD_ARGUMENT, BUS_ACCESS_READ, addr, sizeof(uint32_t));
    }

    if (bus_is_unaligned(addr, sizeof(uint32_t))) {
        return bus_result_make(BUS_STATUS_UNALIGNED, BUS_ACCESS_READ, addr, sizeof(uint32_t));
    }

    if (bus != NULL && bus->tim2 != NULL && bus_tim2_offset(addr, &offset)) {
        if (tim2_read32(bus->tim2, offset, value) == 0) {
            return bus_result_make(BUS_STATUS_OK, BUS_ACCESS_READ, addr, sizeof(uint32_t));
        }

        return bus_result_make(BUS_STATUS_UNMAPPED, BUS_ACCESS_READ, addr, sizeof(uint32_t));
    }

    result = bus_translate(bus, addr, &ptr, sizeof(uint32_t), BUS_ACCESS_READ);
    if (result.status != BUS_STATUS_OK) {
        return result;
    }

    *value = (uint32_t)ptr[0]
        | ((uint32_t)ptr[1] << 8)
        | ((uint32_t)ptr[2] << 16)
        | ((uint32_t)ptr[3] << 24);
    return result;
}

bus_result_t bus_write8(bus_t *bus, uint32_t addr, uint8_t value) {
    uint8_t *ptr = NULL;
    bus_result_t result = bus_translate(bus, addr, &ptr, sizeof(uint8_t), BUS_ACCESS_WRITE);

    if (result.status != BUS_STATUS_OK) {
        return result;
    }

    ptr[0] = value;
    return result;
}

bus_result_t bus_write16(bus_t *bus, uint32_t addr, uint16_t value) {
    uint8_t *ptr = NULL;
    bus_result_t result = bus_translate(bus, addr, &ptr, sizeof(uint16_t), BUS_ACCESS_WRITE);

    if (result.status != BUS_STATUS_OK) {
        return result;
    }

    ptr[0] = (uint8_t)(value & 0xFFu);
    ptr[1] = (uint8_t)((value >> 8) & 0xFFu);
    return result;
}

bus_result_t bus_write32(bus_t *bus, uint32_t addr, uint32_t value) {
    uint8_t *ptr = NULL;
    bus_result_t result;
    uint32_t offset = 0;

    if (bus_is_unaligned(addr, sizeof(uint32_t))) {
        return bus_result_make(BUS_STATUS_UNALIGNED, BUS_ACCESS_WRITE, addr, sizeof(uint32_t));
    }

    if (bus != NULL && bus->tim2 != NULL && bus_tim2_offset(addr, &offset)) {
        if (tim2_write32(bus->tim2, offset, value) == 0) {
            return bus_result_make(BUS_STATUS_OK, BUS_ACCESS_WRITE, addr, sizeof(uint32_t));
        }

        return bus_result_make(BUS_STATUS_UNMAPPED, BUS_ACCESS_WRITE, addr, sizeof(uint32_t));
    }

    result = bus_translate(bus, addr, &ptr, sizeof(uint32_t), BUS_ACCESS_WRITE);

    if (result.status != BUS_STATUS_OK) {
        return result;
    }

    ptr[0] = (uint8_t)(value & 0xFFu);
    ptr[1] = (uint8_t)((value >> 8) & 0xFFu);
    ptr[2] = (uint8_t)((value >> 16) & 0xFFu);
    ptr[3] = (uint8_t)((value >> 24) & 0xFFu);
    return result;
}

int bus_result_is_ok(bus_result_t result) {
    return result.status == BUS_STATUS_OK;
}

int bus_tick(bus_t *bus, uint32_t ticks) {
    (void)bus;
    (void)ticks;
    return 0;
}

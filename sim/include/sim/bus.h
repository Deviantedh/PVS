#ifndef SIM_BUS_H
#define SIM_BUS_H

#include <stddef.h>
#include <stdint.h>

#include "sim/memory.h"

typedef enum bus_status {
    BUS_STATUS_OK = 0,
    BUS_STATUS_UNMAPPED,
    BUS_STATUS_UNALIGNED,
    BUS_STATUS_BAD_ARGUMENT
} bus_status_t;

typedef enum bus_access_type {
    BUS_ACCESS_READ = 0,
    BUS_ACCESS_WRITE
} bus_access_type_t;

typedef struct bus_result {
    bus_status_t status;
    bus_access_type_t access;
    uint32_t addr;
    uint8_t width;
} bus_result_t;

typedef struct bus {
    memory_t *memory;
} bus_t;

void bus_init(bus_t *bus, memory_t *memory);
bus_result_t bus_read16(bus_t *bus, uint32_t addr, uint16_t *value);
bus_result_t bus_read32(bus_t *bus, uint32_t addr, uint32_t *value);
bus_result_t bus_write32(bus_t *bus, uint32_t addr, uint32_t value);
int bus_result_is_ok(bus_result_t result);
int bus_tick(bus_t *bus, uint32_t ticks);

#endif

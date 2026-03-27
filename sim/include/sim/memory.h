#ifndef SIM_MEMORY_H
#define SIM_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#define SIM_FLASH_BASE 0x08000000u
#define SIM_SRAM_BASE  0x20000000u

typedef struct memory {
    uint8_t *flash;
    size_t flash_size;
    uint8_t *sram;
    size_t sram_size;
} memory_t;

int memory_init(memory_t *memory, size_t flash_size, size_t sram_size);
void memory_reset(memory_t *memory);
int memory_load_flash(memory_t *memory, uint32_t addr, const void *data, size_t size);
void memory_destroy(memory_t *memory);

#endif

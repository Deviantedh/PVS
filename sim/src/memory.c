#include "sim/memory.h"

#include <stdlib.h>
#include <string.h>

int memory_init(memory_t *memory, size_t flash_size, size_t sram_size) {
    if (memory == NULL) {
        return -1;
    }

    memory->flash = calloc(flash_size, sizeof(uint8_t));
    memory->sram = calloc(sram_size, sizeof(uint8_t));
    if (memory->flash == NULL || memory->sram == NULL) {
        free(memory->flash);
        free(memory->sram);
        memory->flash = NULL;
        memory->sram = NULL;
        memory->flash_size = 0;
        memory->sram_size = 0;
        return -1;
    }

    memory->flash_size = flash_size;
    memory->sram_size = sram_size;
    return 0;
}

void memory_reset(memory_t *memory) {
    if (memory == NULL) {
        return;
    }

    if (memory->sram != NULL) {
        memset(memory->sram, 0, memory->sram_size);
    }
}

int memory_load_flash(memory_t *memory, uint32_t addr, const void *data, size_t size) {
    size_t offset;

    if (memory == NULL || data == NULL) {
        return -1;
    }

    if (addr < SIM_FLASH_BASE) {
        return -1;
    }

    offset = (size_t)(addr - SIM_FLASH_BASE);
    if (offset + size > memory->flash_size) {
        return -1;
    }

    memcpy(&memory->flash[offset], data, size);
    return 0;
}

void memory_destroy(memory_t *memory) {
    if (memory == NULL) {
        return;
    }

    free(memory->flash);
    free(memory->sram);
    memory->flash = NULL;
    memory->sram = NULL;
    memory->flash_size = 0;
    memory->sram_size = 0;
}

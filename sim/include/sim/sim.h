#ifndef SIM_SIM_H
#define SIM_SIM_H

#include <stddef.h>
#include <stdint.h>

#include "sim/bus.h"
#include "sim/cpu.h"
#include "sim/memory.h"
#include "sim/nvic.h"

typedef enum sim_stop_reason {
    SIM_STOP_NONE = 0,
    SIM_STOP_BREAK,
    SIM_STOP_MAX_INSTRUCTIONS,
    SIM_STOP_UNSUPPORTED_INSTR,
    SIM_STOP_FAULT
} sim_stop_reason_t;

typedef struct sim_config {
    size_t flash_size;
    size_t sram_size;
    uint64_t max_instructions;
} sim_config_t;

typedef struct sim {
    cpu_state_t cpu;
    memory_t memory;
    bus_t bus;
    nvic_t nvic;
    sim_config_t config;
    sim_stop_reason_t stop_reason;
    bus_result_t last_bus_result;
    int initialized;
} sim_t;

int sim_init(sim_t *sim, const sim_config_t *config);
int sim_load_firmware(sim_t *sim, const void *data, size_t size);
int sim_reset(sim_t *sim);
sim_stop_reason_t sim_step(sim_t *sim);
sim_stop_reason_t sim_run(sim_t *sim, uint64_t max_steps);
void sim_destroy(sim_t *sim);

#endif

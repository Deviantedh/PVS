#include "sim/sim.h"

#include <stddef.h>

enum {
    SIM_DEFAULT_FLASH_SIZE = 64u * 1024u,
    SIM_DEFAULT_SRAM_SIZE = 20u * 1024u
};

static int sim_fail_with_bus(sim_t *sim, bus_result_t result) {
    sim->last_bus_result = result;
    sim->stop_reason = SIM_STOP_FAULT;
    return -1;
}

static sim_stop_reason_t sim_stop_with_bus(sim_t *sim, bus_result_t result) {
    sim->last_bus_result = result;
    sim->stop_reason = SIM_STOP_FAULT;
    return sim->stop_reason;
}

static bus_result_t sim_read_vector_word(sim_t *sim, uint32_t addr, uint32_t *value) {
    bus_result_t result = bus_read32(&sim->bus, addr, value);

    sim->last_bus_result = result;
    return result;
}

static sim_config_t sim_config_resolve(const sim_config_t *config) {
    sim_config_t resolved = {
        .flash_size = SIM_DEFAULT_FLASH_SIZE,
        .sram_size = SIM_DEFAULT_SRAM_SIZE,
        .max_instructions = 0
    };

    if (config == NULL) {
        return resolved;
    }

    if (config->flash_size != 0) {
        resolved.flash_size = config->flash_size;
    }
    if (config->sram_size != 0) {
        resolved.sram_size = config->sram_size;
    }
    if (config->max_instructions != 0) {
        resolved.max_instructions = config->max_instructions;
    }

    return resolved;
}

int sim_init(sim_t *sim, const sim_config_t *config) {
    if (sim == NULL) {
        return -1;
    }

    sim->config = sim_config_resolve(config);
    sim->stop_reason = SIM_STOP_NONE;
    sim->last_bus_result = (bus_result_t){0};
    sim->initialized = 0;

    if (memory_init(&sim->memory, sim->config.flash_size, sim->config.sram_size) != 0) {
        return -1;
    }

    nvic_init(&sim->nvic);
    tim2_init(&sim->tim2);
    bus_init(&sim->bus, &sim->memory, &sim->tim2);
    cpu_state_reset(&sim->cpu);
    sim->initialized = 1;
    return 0;
}

int sim_load_firmware(sim_t *sim, const void *data, size_t size) {
    if (sim == NULL || !sim->initialized) {
        return -1;
    }

    return memory_load_flash(&sim->memory, SIM_FLASH_BASE, data, size);
}

int sim_reset(sim_t *sim) {
    uint32_t initial_msp = 0;
    uint32_t reset_pc = 0;
    bus_result_t result;

    if (sim == NULL || !sim->initialized) {
        return -1;
    }

    cpu_state_reset(&sim->cpu);
    nvic_reset(&sim->nvic);
    tim2_reset(&sim->tim2);
    memory_reset(&sim->memory);
    sim->stop_reason = SIM_STOP_NONE;
    sim->last_bus_result = (bus_result_t){0};

    result = sim_read_vector_word(sim, SIM_FLASH_BASE, &initial_msp);
    if (!bus_result_is_ok(result)) {
        return sim_fail_with_bus(sim, result);
    }

    result = sim_read_vector_word(sim, SIM_FLASH_BASE + 4u, &reset_pc);
    if (!bus_result_is_ok(result)) {
        return sim_fail_with_bus(sim, result);
    }

    if ((reset_pc & 0x1u) == 0u) {
        sim->stop_reason = SIM_STOP_FAULT;
        return -1;
    }

    sim->cpu.msp = initial_msp;
    sim->cpu.pc = reset_pc;
    sim->cpu.xpsr = 0x01000000u;
    return 0;
}

sim_stop_reason_t sim_step(sim_t *sim) {
    cpu_step_result_t cpu_result;

    if (sim == NULL || !sim->initialized) {
        return SIM_STOP_FAULT;
    }

    if (bus_tick(&sim->bus, 1u) != 0) {
        sim->stop_reason = SIM_STOP_FAULT;
        return sim->stop_reason;
    }
    tim2_tick(&sim->tim2, &sim->nvic, 1u);

    cpu_result = cpu_step(sim);
    sim->last_bus_result = cpu_result.bus_result;
    if (cpu_result.status == CPU_STEP_FAULT) {
        return sim_stop_with_bus(sim, cpu_result.bus_result);
    }
    if (cpu_result.status == CPU_STEP_UNSUPPORTED) {
        sim->stop_reason = SIM_STOP_UNSUPPORTED_INSTR;
        return sim->stop_reason;
    }

    if (!sim->cpu.handler_mode && sim->cpu.primask == 0u) {
        int irq = nvic_select_next(&sim->nvic);

        if (irq != NVIC_NO_IRQ) {
            cpu_result = cpu_deliver_irq(sim, irq);
            sim->last_bus_result = cpu_result.bus_result;
            if (cpu_result.status == CPU_STEP_FAULT) {
                return sim_stop_with_bus(sim, cpu_result.bus_result);
            }
            if (cpu_result.status == CPU_STEP_UNSUPPORTED) {
                sim->stop_reason = SIM_STOP_UNSUPPORTED_INSTR;
                return sim->stop_reason;
            }
        }
    }

    if (sim->config.max_instructions != 0 && sim->cpu.instr_count >= sim->config.max_instructions) {
        sim->stop_reason = SIM_STOP_MAX_INSTRUCTIONS;
        return sim->stop_reason;
    }

    sim->stop_reason = SIM_STOP_NONE;
    return sim->stop_reason;
}

sim_stop_reason_t sim_run(sim_t *sim, uint64_t max_steps) {
    uint64_t steps = 0;

    if (sim == NULL || !sim->initialized) {
        return SIM_STOP_FAULT;
    }

    sim->stop_reason = SIM_STOP_NONE;
    while (max_steps == 0 || steps < max_steps) {
        sim_stop_reason_t reason = sim_step(sim);
        steps++;

        if (reason != SIM_STOP_NONE) {
            return reason;
        }
    }

    sim->stop_reason = SIM_STOP_MAX_INSTRUCTIONS;
    return sim->stop_reason;
}

void sim_destroy(sim_t *sim) {
    if (sim == NULL) {
        return;
    }

    memory_destroy(&sim->memory);
    sim->initialized = 0;
    sim->stop_reason = SIM_STOP_NONE;
}

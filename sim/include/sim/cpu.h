#ifndef SIM_CPU_H
#define SIM_CPU_H

#include "sim/bus.h"

#include <stdint.h>

#define CPU_XPSR_N_MASK 0x80000000u
#define CPU_XPSR_Z_MASK 0x40000000u
#define CPU_XPSR_C_MASK 0x20000000u
#define CPU_XPSR_V_MASK 0x10000000u

typedef struct cpu_state {
    uint32_t r[13];
    uint32_t msp;
    uint32_t psp;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
    uint32_t primask;
    uint32_t basepri;
    uint64_t instr_count;
    uint32_t last_pc;
} cpu_state_t;

typedef enum cpu_step_status {
    CPU_STEP_OK = 0,
    CPU_STEP_UNSUPPORTED,
    CPU_STEP_FAULT
} cpu_step_status_t;

typedef struct cpu_step_result {
    cpu_step_status_t status;
    bus_result_t bus_result;
} cpu_step_result_t;

struct sim;

void cpu_state_reset(cpu_state_t *cpu);
cpu_step_result_t cpu_step(struct sim *sim);

#endif

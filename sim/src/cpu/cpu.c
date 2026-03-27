#include "sim/sim.h"

#include <string.h>

static cpu_step_result_t cpu_step_result_make(cpu_step_status_t status, bus_result_t bus_result) {
    cpu_step_result_t result;

    result.status = status;
    result.bus_result = bus_result;
    return result;
}

static void cpu_set_flag(cpu_state_t *cpu, uint32_t mask, int enabled) {
    if (enabled) {
        cpu->xpsr |= mask;
    } else {
        cpu->xpsr &= ~mask;
    }
}

static void cpu_update_nz(cpu_state_t *cpu, uint32_t value) {
    cpu_set_flag(cpu, CPU_XPSR_N_MASK, (value & 0x80000000u) != 0u);
    cpu_set_flag(cpu, CPU_XPSR_Z_MASK, value == 0u);
}

static int cpu_flag_is_set(const cpu_state_t *cpu, uint32_t mask) {
    return (cpu->xpsr & mask) != 0u;
}

static void cpu_update_add_flags(cpu_state_t *cpu, uint32_t left, uint32_t right, uint32_t result) {
    uint64_t wide = (uint64_t)left + (uint64_t)right;

    cpu_update_nz(cpu, result);
    cpu_set_flag(cpu, CPU_XPSR_C_MASK, wide > 0xFFFFFFFFu);
    cpu_set_flag(cpu, CPU_XPSR_V_MASK, ((~(left ^ right) & (left ^ result)) & 0x80000000u) != 0u);
}

static void cpu_update_sub_flags(cpu_state_t *cpu, uint32_t left, uint32_t right, uint32_t result) {
    cpu_update_nz(cpu, result);
    cpu_set_flag(cpu, CPU_XPSR_C_MASK, left >= right);
    cpu_set_flag(cpu, CPU_XPSR_V_MASK, (((left ^ right) & (left ^ result)) & 0x80000000u) != 0u);
}

static int cpu_condition_passed(const cpu_state_t *cpu, uint32_t condition) {
    int n = cpu_flag_is_set(cpu, CPU_XPSR_N_MASK);
    int z = cpu_flag_is_set(cpu, CPU_XPSR_Z_MASK);
    int c = cpu_flag_is_set(cpu, CPU_XPSR_C_MASK);
    int v = cpu_flag_is_set(cpu, CPU_XPSR_V_MASK);

    switch (condition) {
    case 0x0u:
        return z;
    case 0x1u:
        return !z;
    case 0x2u:
        return c;
    case 0x3u:
        return !c;
    case 0x4u:
        return n;
    case 0x5u:
        return !n;
    case 0x6u:
        return v;
    case 0x7u:
        return !v;
    case 0x8u:
        return c && !z;
    case 0x9u:
        return !c || z;
    case 0xAu:
        return n == v;
    case 0xBu:
        return n != v;
    case 0xCu:
        return !z && (n == v);
    case 0xDu:
        return z || (n != v);
    default:
        return 0;
    }
}

static cpu_step_result_t cpu_execute_thumb16(sim_t *sim, uint16_t instr) {
    bus_result_t bus_result = {0};

    if (instr == 0xBF00u) {
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xF800u) == 0x2000u) {
        uint32_t rd = (instr >> 8) & 0x7u;
        uint32_t imm8 = instr & 0x00FFu;

        sim->cpu.r[rd] = imm8;
        cpu_update_nz(&sim->cpu, imm8);
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xF800u) == 0x2800u) {
        uint32_t rn = (instr >> 8) & 0x7u;
        uint32_t imm8 = instr & 0x00FFu;
        uint32_t result = sim->cpu.r[rn] - imm8;

        cpu_update_sub_flags(&sim->cpu, sim->cpu.r[rn], imm8, result);
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xFFC0u) == 0x4280u) {
        uint32_t rm = (instr >> 3) & 0x7u;
        uint32_t rn = instr & 0x7u;
        uint32_t result = sim->cpu.r[rn] - sim->cpu.r[rm];

        cpu_update_sub_flags(&sim->cpu, sim->cpu.r[rn], sim->cpu.r[rm], result);
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xF800u) == 0x3000u) {
        uint32_t rdn = (instr >> 8) & 0x7u;
        uint32_t imm8 = instr & 0x00FFu;
        uint32_t left = sim->cpu.r[rdn];
        uint32_t result = left + imm8;

        sim->cpu.r[rdn] = result;
        cpu_update_add_flags(&sim->cpu, left, imm8, result);
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xF800u) == 0x3800u) {
        uint32_t rdn = (instr >> 8) & 0x7u;
        uint32_t imm8 = instr & 0x00FFu;
        uint32_t left = sim->cpu.r[rdn];
        uint32_t result = left - imm8;

        sim->cpu.r[rdn] = result;
        cpu_update_sub_flags(&sim->cpu, left, imm8, result);
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xFF00u) == 0x4600u) {
        uint32_t rd = ((instr >> 4) & 0x8u) | (instr & 0x7u);
        uint32_t rm = (instr >> 3) & 0xFu;

        if (rd >= 8u || rm >= 8u) {
            return cpu_step_result_make(CPU_STEP_UNSUPPORTED, bus_result);
        }

        sim->cpu.r[rd] = sim->cpu.r[rm];
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xF800u) == 0x4800u) {
        uint32_t rt = (instr >> 8) & 0x7u;
        uint32_t imm8 = instr & 0x00FFu;
        uint32_t base = (sim->cpu.pc + 4u) & ~0x3u;
        uint32_t addr = base + (imm8 << 2);
        uint32_t value = 0;

        bus_result = bus_read32(&sim->bus, addr, &value);
        if (!bus_result_is_ok(bus_result)) {
            return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
        }

        sim->cpu.r[rt] = value;
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xF800u) == 0x6000u) {
        uint32_t imm5 = (instr >> 6) & 0x1Fu;
        uint32_t rn = (instr >> 3) & 0x7u;
        uint32_t rt = instr & 0x7u;
        uint32_t addr = sim->cpu.r[rn] + (imm5 << 2);

        bus_result = bus_write32(&sim->bus, addr, sim->cpu.r[rt]);
        if (!bus_result_is_ok(bus_result)) {
            return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
        }

        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xF800u) == 0x6800u) {
        uint32_t imm5 = (instr >> 6) & 0x1Fu;
        uint32_t rn = (instr >> 3) & 0x7u;
        uint32_t rt = instr & 0x7u;
        uint32_t addr = sim->cpu.r[rn] + (imm5 << 2);
        uint32_t value = 0;

        bus_result = bus_read32(&sim->bus, addr, &value);
        if (!bus_result_is_ok(bus_result)) {
            return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
        }

        sim->cpu.r[rt] = value;
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xF800u) == 0xE000u) {
        int32_t offset = ((int32_t)(instr & 0x07FFu) << 21) >> 20;

        sim->cpu.pc += 4u + (uint32_t)offset;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xF000u) == 0xD000u) {
        uint32_t condition = (instr >> 8) & 0xFu;
        int32_t offset = ((int32_t)(instr & 0x00FFu) << 24) >> 23;

        if (condition == 0xEu || condition == 0xFu) {
            return cpu_step_result_make(CPU_STEP_UNSUPPORTED, bus_result);
        }

        if (cpu_condition_passed(&sim->cpu, condition)) {
            sim->cpu.pc += 4u + (uint32_t)offset;
        } else {
            sim->cpu.pc += 2u;
        }
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    return cpu_step_result_make(CPU_STEP_UNSUPPORTED, bus_result);
}

void cpu_state_reset(cpu_state_t *cpu) {
    if (cpu == NULL) {
        return;
    }

    memset(cpu, 0, sizeof(*cpu));
}

cpu_step_result_t cpu_step(sim_t *sim) {
    uint16_t instr = 0;
    bus_result_t bus_result;

    if (sim == NULL || !sim->initialized) {
        return cpu_step_result_make(
            CPU_STEP_FAULT,
            (bus_result_t){ .status = BUS_STATUS_BAD_ARGUMENT, .access = BUS_ACCESS_READ, .addr = 0u, .width = 0u }
        );
    }

    if ((sim->cpu.pc & 0x1u) == 0u) {
        return cpu_step_result_make(
            CPU_STEP_FAULT,
            (bus_result_t){ .status = BUS_STATUS_UNALIGNED, .access = BUS_ACCESS_READ, .addr = sim->cpu.pc, .width = 2u }
        );
    }

    sim->cpu.last_pc = sim->cpu.pc;
    bus_result = bus_read16(&sim->bus, sim->cpu.pc & ~0x1u, &instr);
    if (!bus_result_is_ok(bus_result)) {
        return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
    }

    sim->cpu.instr_count++;
    return cpu_execute_thumb16(sim, instr);
}

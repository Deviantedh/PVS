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

static uint32_t cpu_lsl(cpu_state_t *cpu, uint32_t value, uint32_t shift) {
    if (shift == 0u) {
        return value;
    }
    if (shift < 32u) {
        cpu_set_flag(cpu, CPU_XPSR_C_MASK, ((value >> (32u - shift)) & 0x1u) != 0u);
        return value << shift;
    }
    if (shift == 32u) {
        cpu_set_flag(cpu, CPU_XPSR_C_MASK, (value & 0x1u) != 0u);
        return 0;
    }

    cpu_set_flag(cpu, CPU_XPSR_C_MASK, 0);
    return 0;
}

static uint32_t cpu_lsr(cpu_state_t *cpu, uint32_t value, uint32_t shift) {
    if (shift == 0u) {
        return value;
    }
    if (shift < 32u) {
        cpu_set_flag(cpu, CPU_XPSR_C_MASK, ((value >> (shift - 1u)) & 0x1u) != 0u);
        return value >> shift;
    }
    if (shift == 32u) {
        cpu_set_flag(cpu, CPU_XPSR_C_MASK, (value & 0x80000000u) != 0u);
        return 0;
    }

    cpu_set_flag(cpu, CPU_XPSR_C_MASK, 0);
    return 0;
}

static uint32_t cpu_asr(cpu_state_t *cpu, uint32_t value, uint32_t shift) {
    uint32_t sign = value & 0x80000000u;

    if (shift == 0u) {
        return value;
    }
    if (shift < 32u) {
        uint32_t result = value >> shift;

        cpu_set_flag(cpu, CPU_XPSR_C_MASK, ((value >> (shift - 1u)) & 0x1u) != 0u);
        if (sign != 0u) {
            result |= 0xFFFFFFFFu << (32u - shift);
        }
        return result;
    }

    cpu_set_flag(cpu, CPU_XPSR_C_MASK, sign != 0u);
    return sign != 0u ? 0xFFFFFFFFu : 0u;
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

static int cpu_is_exc_return(uint32_t value) {
    return value == CPU_EXC_RETURN_THREAD_MSP;
}

static uint32_t cpu_count_bits(uint32_t value) {
    uint32_t count = 0;

    while (value != 0u) {
        count += value & 0x1u;
        value >>= 1;
    }

    return count;
}

static cpu_step_result_t cpu_exception_return(sim_t *sim) {
    bus_result_t bus_result = {0};
    uint32_t sp;
    uint32_t stacked[8];

    if (sim == NULL || !sim->cpu.handler_mode || !cpu_is_exc_return(sim->cpu.lr)) {
        return cpu_step_result_make(CPU_STEP_UNSUPPORTED, bus_result);
    }

    sp = sim->cpu.msp;
    for (uint32_t i = 0; i < 8u; ++i) {
        bus_result = bus_read32(&sim->bus, sp + (i * 4u), &stacked[i]);
        if (!bus_result_is_ok(bus_result)) {
            return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
        }
    }

    sim->cpu.r[0] = stacked[0];
    sim->cpu.r[1] = stacked[1];
    sim->cpu.r[2] = stacked[2];
    sim->cpu.r[3] = stacked[3];
    sim->cpu.r[12] = stacked[4];
    sim->cpu.lr = stacked[5];
    sim->cpu.pc = stacked[6];
    sim->cpu.xpsr = stacked[7] & ~CPU_XPSR_EXCEPTION_MASK;
    sim->cpu.msp = sp + 32u;
    sim->cpu.active_exception = 0;
    sim->cpu.handler_mode = 0;
    return cpu_step_result_make(CPU_STEP_OK, bus_result);
}

static cpu_step_result_t cpu_execute_thumb16(sim_t *sim, uint16_t instr) {
    bus_result_t bus_result = {0};

    if (instr == 0xBF00u) {
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xFF00u) == 0xDF00u) {
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_BREAK, bus_result);
    }

    if ((instr & 0xF800u) == 0x0000u) {
        uint32_t imm5 = (instr >> 6) & 0x1Fu;
        uint32_t rm = (instr >> 3) & 0x7u;
        uint32_t rd = instr & 0x7u;
        uint32_t result = cpu_lsl(&sim->cpu, sim->cpu.r[rm], imm5);

        sim->cpu.r[rd] = result;
        cpu_update_nz(&sim->cpu, result);
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xF800u) == 0x0800u) {
        uint32_t imm5 = (instr >> 6) & 0x1Fu;
        uint32_t rm = (instr >> 3) & 0x7u;
        uint32_t rd = instr & 0x7u;
        uint32_t shift = imm5 == 0u ? 32u : imm5;
        uint32_t result = cpu_lsr(&sim->cpu, sim->cpu.r[rm], shift);

        sim->cpu.r[rd] = result;
        cpu_update_nz(&sim->cpu, result);
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xF800u) == 0x1000u) {
        uint32_t imm5 = (instr >> 6) & 0x1Fu;
        uint32_t rm = (instr >> 3) & 0x7u;
        uint32_t rd = instr & 0x7u;
        uint32_t shift = imm5 == 0u ? 32u : imm5;
        uint32_t result = cpu_asr(&sim->cpu, sim->cpu.r[rm], shift);

        sim->cpu.r[rd] = result;
        cpu_update_nz(&sim->cpu, result);
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if (instr == 0xB662u) {
        sim->cpu.primask = 0u;
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if (instr == 0xB672u) {
        sim->cpu.primask = 1u;
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xFF87u) == 0x4700u) {
        uint32_t rm = (instr >> 3) & 0xFu;
        uint32_t target;

        if (rm == 14u && cpu_is_exc_return(sim->cpu.lr)) {
            return cpu_exception_return(sim);
        }

        if (rm < 13u) {
            target = sim->cpu.r[rm];
        } else if (rm == 14u) {
            target = sim->cpu.lr;
        } else {
            return cpu_step_result_make(CPU_STEP_UNSUPPORTED, bus_result);
        }

        if ((target & 0x1u) == 0u) {
            return cpu_step_result_make(
                CPU_STEP_FAULT,
                (bus_result_t){ .status = BUS_STATUS_UNALIGNED, .access = BUS_ACCESS_READ, .addr = target, .width = 2u }
            );
        }

        sim->cpu.pc = target;
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

    if ((instr & 0xFFC0u) == 0x4000u) {
        uint32_t rm = (instr >> 3) & 0x7u;
        uint32_t rdn = instr & 0x7u;

        sim->cpu.r[rdn] &= sim->cpu.r[rm];
        cpu_update_nz(&sim->cpu, sim->cpu.r[rdn]);
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xFFC0u) == 0x4040u) {
        uint32_t rm = (instr >> 3) & 0x7u;
        uint32_t rdn = instr & 0x7u;

        sim->cpu.r[rdn] ^= sim->cpu.r[rm];
        cpu_update_nz(&sim->cpu, sim->cpu.r[rdn]);
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xFFC0u) == 0x4080u) {
        uint32_t rm = (instr >> 3) & 0x7u;
        uint32_t rdn = instr & 0x7u;
        uint32_t result = cpu_lsl(&sim->cpu, sim->cpu.r[rdn], sim->cpu.r[rm] & 0xFFu);

        sim->cpu.r[rdn] = result;
        cpu_update_nz(&sim->cpu, result);
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xFFC0u) == 0x40C0u) {
        uint32_t rm = (instr >> 3) & 0x7u;
        uint32_t rdn = instr & 0x7u;
        uint32_t result = cpu_lsr(&sim->cpu, sim->cpu.r[rdn], sim->cpu.r[rm] & 0xFFu);

        sim->cpu.r[rdn] = result;
        cpu_update_nz(&sim->cpu, result);
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xFFC0u) == 0x4100u) {
        uint32_t rm = (instr >> 3) & 0x7u;
        uint32_t rdn = instr & 0x7u;
        uint32_t result = cpu_asr(&sim->cpu, sim->cpu.r[rdn], sim->cpu.r[rm] & 0xFFu);

        sim->cpu.r[rdn] = result;
        cpu_update_nz(&sim->cpu, result);
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

    if ((instr & 0xFFC0u) == 0x4300u) {
        uint32_t rm = (instr >> 3) & 0x7u;
        uint32_t rdn = instr & 0x7u;

        sim->cpu.r[rdn] |= sim->cpu.r[rm];
        cpu_update_nz(&sim->cpu, sim->cpu.r[rdn]);
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xFE00u) == 0x1800u) {
        uint32_t rm = (instr >> 6) & 0x7u;
        uint32_t rn = (instr >> 3) & 0x7u;
        uint32_t rd = instr & 0x7u;
        uint32_t left = sim->cpu.r[rn];
        uint32_t right = sim->cpu.r[rm];
        uint32_t result = left + right;

        sim->cpu.r[rd] = result;
        cpu_update_add_flags(&sim->cpu, left, right, result);
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xFE00u) == 0x1A00u) {
        uint32_t rm = (instr >> 6) & 0x7u;
        uint32_t rn = (instr >> 3) & 0x7u;
        uint32_t rd = instr & 0x7u;
        uint32_t left = sim->cpu.r[rn];
        uint32_t right = sim->cpu.r[rm];
        uint32_t result = left - right;

        sim->cpu.r[rd] = result;
        cpu_update_sub_flags(&sim->cpu, left, right, result);
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

    if ((instr & 0xFE00u) == 0xB400u) {
        uint32_t register_list = instr & 0x00FFu;
        uint32_t include_lr = (instr >> 8) & 0x1u;
        uint32_t register_count = cpu_count_bits(register_list) + include_lr;
        uint32_t addr;

        if (register_count == 0u) {
            return cpu_step_result_make(CPU_STEP_UNSUPPORTED, bus_result);
        }

        addr = sim->cpu.msp - (register_count * 4u);
        for (uint32_t reg = 0; reg < 8u; ++reg) {
            if ((register_list & (1u << reg)) == 0u) {
                continue;
            }

            bus_result = bus_write32(&sim->bus, addr, sim->cpu.r[reg]);
            if (!bus_result_is_ok(bus_result)) {
                return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
            }
            addr += 4u;
        }

        if (include_lr != 0u) {
            bus_result = bus_write32(&sim->bus, addr, sim->cpu.lr);
            if (!bus_result_is_ok(bus_result)) {
                return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
            }
        }

        sim->cpu.msp -= register_count * 4u;
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xFE00u) == 0xBC00u) {
        uint32_t register_list = instr & 0x00FFu;
        uint32_t include_pc = (instr >> 8) & 0x1u;
        uint32_t register_count = cpu_count_bits(register_list) + include_pc;
        uint32_t addr = sim->cpu.msp;

        if (register_count == 0u) {
            return cpu_step_result_make(CPU_STEP_UNSUPPORTED, bus_result);
        }

        for (uint32_t reg = 0; reg < 8u; ++reg) {
            if ((register_list & (1u << reg)) == 0u) {
                continue;
            }

            bus_result = bus_read32(&sim->bus, addr, &sim->cpu.r[reg]);
            if (!bus_result_is_ok(bus_result)) {
                return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
            }
            addr += 4u;
        }

        if (include_pc != 0u) {
            uint32_t target = 0;

            bus_result = bus_read32(&sim->bus, addr, &target);
            if (!bus_result_is_ok(bus_result)) {
                return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
            }

            if ((target & 0x1u) == 0u) {
                return cpu_step_result_make(
                    CPU_STEP_FAULT,
                    (bus_result_t){ .status = BUS_STATUS_UNALIGNED, .access = BUS_ACCESS_READ, .addr = target, .width = 2u }
                );
            }

            sim->cpu.pc = target;
        } else {
            sim->cpu.pc += 2u;
        }

        sim->cpu.msp += register_count * 4u;
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

    if ((instr & 0xF800u) == 0x7000u) {
        uint32_t imm5 = (instr >> 6) & 0x1Fu;
        uint32_t rn = (instr >> 3) & 0x7u;
        uint32_t rt = instr & 0x7u;
        uint32_t addr = sim->cpu.r[rn] + imm5;

        bus_result = bus_write8(&sim->bus, addr, (uint8_t)(sim->cpu.r[rt] & 0xFFu));
        if (!bus_result_is_ok(bus_result)) {
            return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
        }

        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xF800u) == 0x7800u) {
        uint32_t imm5 = (instr >> 6) & 0x1Fu;
        uint32_t rn = (instr >> 3) & 0x7u;
        uint32_t rt = instr & 0x7u;
        uint32_t addr = sim->cpu.r[rn] + imm5;
        uint8_t value = 0;

        bus_result = bus_read8(&sim->bus, addr, &value);
        if (!bus_result_is_ok(bus_result)) {
            return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
        }

        sim->cpu.r[rt] = value;
        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xF800u) == 0x8000u) {
        uint32_t imm5 = (instr >> 6) & 0x1Fu;
        uint32_t rn = (instr >> 3) & 0x7u;
        uint32_t rt = instr & 0x7u;
        uint32_t addr = sim->cpu.r[rn] + (imm5 << 1);

        bus_result = bus_write16(&sim->bus, addr, (uint16_t)(sim->cpu.r[rt] & 0xFFFFu));
        if (!bus_result_is_ok(bus_result)) {
            return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
        }

        sim->cpu.pc += 2u;
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    if ((instr & 0xF800u) == 0x8800u) {
        uint32_t imm5 = (instr >> 6) & 0x1Fu;
        uint32_t rn = (instr >> 3) & 0x7u;
        uint32_t rt = instr & 0x7u;
        uint32_t addr = sim->cpu.r[rn] + (imm5 << 1);
        uint16_t value = 0;

        bus_result = bus_read16(&sim->bus, addr, &value);
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

static cpu_step_result_t cpu_execute_thumb32_bl(sim_t *sim, uint16_t first, uint16_t second) {
    bus_result_t bus_result = {0};
    uint32_t s = (first >> 10) & 0x1u;
    uint32_t imm10 = first & 0x03FFu;
    uint32_t j1 = (second >> 13) & 0x1u;
    uint32_t j2 = (second >> 11) & 0x1u;
    uint32_t imm11 = second & 0x07FFu;
    uint32_t i1 = (~(j1 ^ s)) & 0x1u;
    uint32_t i2 = (~(j2 ^ s)) & 0x1u;
    uint32_t imm25 = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);
    int32_t offset = ((int32_t)(imm25 << 7)) >> 7;

    sim->cpu.lr = sim->cpu.pc + 4u;
    sim->cpu.pc = (uint32_t)((int32_t)sim->cpu.pc + 4 + offset);
    return cpu_step_result_make(CPU_STEP_OK, bus_result);
}

static int cpu_is_thumb32_bl(uint16_t first, uint16_t second) {
    return (first & 0xF800u) == 0xF000u && (second & 0xD000u) == 0xD000u;
}

void cpu_state_reset(cpu_state_t *cpu) {
    if (cpu == NULL) {
        return;
    }

    memset(cpu, 0, sizeof(*cpu));
}

cpu_step_result_t cpu_step(sim_t *sim) {
    uint16_t instr = 0;
    uint16_t second = 0;
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

    if ((instr & 0xF800u) == 0xF000u) {
        bus_result = bus_read16(&sim->bus, (sim->cpu.pc & ~0x1u) + 2u, &second);
        if (!bus_result_is_ok(bus_result)) {
            return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
        }

        sim->cpu.instr_count++;
        if (cpu_is_thumb32_bl(instr, second)) {
            return cpu_execute_thumb32_bl(sim, instr, second);
        }

        return cpu_step_result_make(CPU_STEP_UNSUPPORTED, bus_result);
    }

    sim->cpu.instr_count++;
    return cpu_execute_thumb16(sim, instr);
}

cpu_step_result_t cpu_deliver_irq(sim_t *sim, int irq) {
    bus_result_t bus_result = {0};
    uint32_t exception_number;
    uint32_t handler_pc = 0;
    uint32_t sp;
    uint32_t stacked[8];

    if (sim == NULL || !sim->initialized || irq < 0) {
        return cpu_step_result_make(
            CPU_STEP_FAULT,
            (bus_result_t){ .status = BUS_STATUS_BAD_ARGUMENT, .access = BUS_ACCESS_READ, .addr = 0u, .width = 0u }
        );
    }

    if (sim->cpu.handler_mode || sim->cpu.primask != 0u) {
        return cpu_step_result_make(CPU_STEP_OK, bus_result);
    }

    exception_number = CPU_EXTERNAL_EXCEPTION_BASE + (uint32_t)irq;
    bus_result = bus_read32(&sim->bus, SIM_FLASH_BASE + (exception_number * 4u), &handler_pc);
    if (!bus_result_is_ok(bus_result)) {
        return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
    }

    if ((handler_pc & 0x1u) == 0u) {
        return cpu_step_result_make(
            CPU_STEP_FAULT,
            (bus_result_t){ .status = BUS_STATUS_UNALIGNED, .access = BUS_ACCESS_READ, .addr = handler_pc, .width = 2u }
        );
    }

    sp = sim->cpu.msp - 32u;
    stacked[0] = sim->cpu.r[0];
    stacked[1] = sim->cpu.r[1];
    stacked[2] = sim->cpu.r[2];
    stacked[3] = sim->cpu.r[3];
    stacked[4] = sim->cpu.r[12];
    stacked[5] = sim->cpu.lr;
    stacked[6] = sim->cpu.pc;
    stacked[7] = sim->cpu.xpsr;

    for (uint32_t i = 0; i < 8u; ++i) {
        bus_result = bus_write32(&sim->bus, sp + (i * 4u), stacked[i]);
        if (!bus_result_is_ok(bus_result)) {
            return cpu_step_result_make(CPU_STEP_FAULT, bus_result);
        }
    }

    sim->cpu.msp = sp;
    sim->cpu.lr = CPU_EXC_RETURN_THREAD_MSP;
    sim->cpu.pc = handler_pc;
    sim->cpu.xpsr = (sim->cpu.xpsr & ~CPU_XPSR_EXCEPTION_MASK) | exception_number;
    sim->cpu.active_exception = exception_number;
    sim->cpu.handler_mode = 1;
    (void)nvic_clear_pending(&sim->nvic, irq);
    return cpu_step_result_make(CPU_STEP_OK, bus_result);
}

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "sim/sim.h"

static void encode_u32le(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void encode_u16le(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static int expect_int(int actual, int expected) {
    return actual == expected ? 0 : 1;
}

static int expect_u32(uint32_t actual, uint32_t expected) {
    return actual == expected ? 0 : 1;
}

static int expect_flag_set(uint32_t xpsr, uint32_t mask, int expected) {
    int actual = (xpsr & mask) != 0u;

    return actual == expected ? 0 : 1;
}

static uint16_t encode_b_cond(uint8_t cond, int8_t imm8) {
    return (uint16_t)(0xD000u | ((uint16_t)cond << 8) | (uint8_t)imm8);
}

static uint16_t encode_b(int16_t imm11) {
    return (uint16_t)(0xE000u | ((uint16_t)imm11 & 0x07FFu));
}

static uint16_t encode_strb_imm(uint8_t rt, uint8_t rn, uint8_t imm5) {
    return (uint16_t)(0x7000u | ((uint16_t)(imm5 & 0x1Fu) << 6) | ((uint16_t)(rn & 0x7u) << 3) | (rt & 0x7u));
}

static uint16_t encode_ldrb_imm(uint8_t rt, uint8_t rn, uint8_t imm5) {
    return (uint16_t)(0x7800u | ((uint16_t)(imm5 & 0x1Fu) << 6) | ((uint16_t)(rn & 0x7u) << 3) | (rt & 0x7u));
}

static uint16_t encode_strh_imm(uint8_t rt, uint8_t rn, uint8_t imm5) {
    return (uint16_t)(0x8000u | ((uint16_t)(imm5 & 0x1Fu) << 6) | ((uint16_t)(rn & 0x7u) << 3) | (rt & 0x7u));
}

static uint16_t encode_ldrh_imm(uint8_t rt, uint8_t rn, uint8_t imm5) {
    return (uint16_t)(0x8800u | ((uint16_t)(imm5 & 0x1Fu) << 6) | ((uint16_t)(rn & 0x7u) << 3) | (rt & 0x7u));
}

static int test_reset_sequence(void) {
    sim_t sim;
    uint8_t firmware[16] = {0};

    encode_u32le(&firmware[0], 0x20001000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0xBF00u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.msp, 0x20001000u) != 0
        || expect_u32(sim.cpu.pc, SIM_FLASH_BASE + 8u + 1u) != 0
        || expect_u32(sim.cpu.xpsr, 0x01000000u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_step_nop(void) {
    sim_t sim;
    uint8_t firmware[16] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0xBF00u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.pc, SIM_FLASH_BASE + 10u + 1u) != 0
        || expect_u32((uint32_t)sim.cpu.instr_count, 1u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_branch(void) {
    sim_t sim;
    uint8_t firmware[20] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0xE001u);
    encode_u16le(&firmware[10], 0xBF00u);
    encode_u16le(&firmware[12], 0xBF00u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.pc, SIM_FLASH_BASE + 14u + 1u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_ldr_literal(void) {
    sim_t sim;
    uint8_t firmware[32] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x4800u);
    encode_u16le(&firmware[10], 0xBF00u);
    encode_u32le(&firmware[12], 0x12345678u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[0], 0x12345678u) != 0
        || expect_u32(sim.cpu.pc, SIM_FLASH_BASE + 10u + 1u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_mov_register(void) {
    sim_t sim;
    uint8_t firmware[16] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2007u);
    encode_u16le(&firmware[10], 0x4601u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[1], 7u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_add_immediate(void) {
    sim_t sim;
    uint8_t firmware[16] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2001u);
    encode_u16le(&firmware[10], 0x3002u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[0], 3u) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_Z_MASK, 0) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_N_MASK, 0) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_add_register(void) {
    sim_t sim;
    uint8_t firmware[20] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2002u);
    encode_u16le(&firmware[10], 0x2103u);
    encode_u16le(&firmware[12], 0x1842u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[2], 5u) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_Z_MASK, 0) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_N_MASK, 0) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_sub_register(void) {
    sim_t sim;
    uint8_t firmware[20] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2005u);
    encode_u16le(&firmware[10], 0x2103u);
    encode_u16le(&firmware[12], 0x1A42u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[2], 2u) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_C_MASK, 1) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_N_MASK, 0) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_cmp_flags(void) {
    sim_t sim;
    uint8_t firmware[20] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2001u);
    encode_u16le(&firmware[10], 0x2801u);
    encode_u16le(&firmware[12], 0x2802u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_flag_set(sim.cpu.xpsr, CPU_XPSR_Z_MASK, 1) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_N_MASK, 0) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_C_MASK, 1) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_V_MASK, 0) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_flag_set(sim.cpu.xpsr, CPU_XPSR_Z_MASK, 0) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_N_MASK, 1) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_C_MASK, 0) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_V_MASK, 0) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_cmp_register(void) {
    sim_t sim;
    uint8_t firmware[24] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2005u);
    encode_u16le(&firmware[10], 0x2105u);
    encode_u16le(&firmware[12], 0x4288u);
    encode_u16le(&firmware[14], 0x2106u);
    encode_u16le(&firmware[16], 0x4288u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_flag_set(sim.cpu.xpsr, CPU_XPSR_Z_MASK, 1) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_C_MASK, 1) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_flag_set(sim.cpu.xpsr, CPU_XPSR_N_MASK, 1) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_Z_MASK, 0) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_C_MASK, 0) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_beq_taken(void) {
    sim_t sim;
    uint8_t firmware[24] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2001u);
    encode_u16le(&firmware[10], 0x2801u);
    encode_u16le(&firmware[12], encode_b_cond(0x0u, 0));
    encode_u16le(&firmware[14], 0x2101u);
    encode_u16le(&firmware[16], 0x2102u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[1], 2u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_bne_not_taken(void) {
    sim_t sim;
    uint8_t firmware[24] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2001u);
    encode_u16le(&firmware[10], 0x2801u);
    encode_u16le(&firmware[12], encode_b_cond(0x1u, 1));
    encode_u16le(&firmware[14], 0x2101u);
    encode_u16le(&firmware[16], 0x2102u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[1], 2u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_blt_taken_signed(void) {
    sim_t sim;
    uint8_t firmware[24] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2000u);
    encode_u16le(&firmware[10], 0x2101u);
    encode_u16le(&firmware[12], 0x4288u);
    encode_u16le(&firmware[14], encode_b_cond(0xBu, 0));
    encode_u16le(&firmware[16], 0x2201u);
    encode_u16le(&firmware[18], 0x2202u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[2], 2u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_bcs_taken_unsigned(void) {
    sim_t sim;
    uint8_t firmware[24] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2002u);
    encode_u16le(&firmware[10], 0x2801u);
    encode_u16le(&firmware[12], encode_b_cond(0x2u, 0));
    encode_u16le(&firmware[14], 0x2101u);
    encode_u16le(&firmware[16], 0x2102u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[1], 2u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_backward_unconditional_branch(void) {
    sim_t sim;
    uint8_t firmware[16] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0xBF00u);
    encode_u16le(&firmware[10], encode_b(-3));

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.pc, SIM_FLASH_BASE + 8u + 1u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_backward_conditional_branch(void) {
    sim_t sim;
    uint8_t firmware[24] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2001u);
    encode_u16le(&firmware[10], 0x2800u);
    encode_u16le(&firmware[12], encode_b_cond(0x1u, (int8_t)-3));

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.pc, SIM_FLASH_BASE + 10u + 1u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_flag_corner_cases(void) {
    sim_t sim;
    uint8_t firmware[40] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x4803u);
    encode_u16le(&firmware[10], 0x3001u);
    encode_u16le(&firmware[12], 0x4803u);
    encode_u16le(&firmware[14], 0x3001u);
    encode_u32le(&firmware[24], 0xFFFFFFFFu);
    encode_u32le(&firmware[28], 0x7FFFFFFFu);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[0], 0u) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_Z_MASK, 1) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_C_MASK, 1) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_V_MASK, 0) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[0], 0x80000000u) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_N_MASK, 1) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_V_MASK, 1) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_C_MASK, 0) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_sub_carry_borrow_behavior(void) {
    sim_t sim;
    uint8_t firmware[24] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2001u);
    encode_u16le(&firmware[10], 0x3802u);
    encode_u16le(&firmware[12], 0x2002u);
    encode_u16le(&firmware[14], 0x3801u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_flag_set(sim.cpu.xpsr, CPU_XPSR_C_MASK, 0) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_N_MASK, 1) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_flag_set(sim.cpu.xpsr, CPU_XPSR_C_MASK, 1) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_N_MASK, 0) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_sub_negative_result(void) {
    sim_t sim;
    uint8_t firmware[20] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2001u);
    encode_u16le(&firmware[10], 0x2102u);
    encode_u16le(&firmware[12], 0x1A42u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[2], 0xFFFFFFFFu) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_N_MASK, 1) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_C_MASK, 0) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_branch_depends_on_prior_flags(void) {
    sim_t sim;
    uint8_t firmware[24] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2001u);
    encode_u16le(&firmware[10], 0x3001u);
    encode_u16le(&firmware[12], 0x3802u);
    encode_u16le(&firmware[14], encode_b_cond(0x0u, 0));
    encode_u16le(&firmware[16], 0x2101u);
    encode_u16le(&firmware[18], 0x2102u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[1], 2u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_simple_loop_iterations(void) {
    sim_t sim;
    uint8_t firmware[24] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x2000u);
    encode_u16le(&firmware[10], 0x2103u);
    encode_u16le(&firmware[12], 0x1840u);
    encode_u16le(&firmware[14], 0x3901u);
    encode_u16le(&firmware[16], 0x2900u);
    encode_u16le(&firmware[18], encode_b_cond(0x1u, (int8_t)-5));

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    for (int i = 0; i < 14; ++i) {
        if (sim_step(&sim) != SIM_STOP_NONE) {
            sim_destroy(&sim);
            return 1;
        }
    }

    if (expect_u32(sim.cpu.r[0], 6u) != 0
        || expect_u32(sim.cpu.r[1], 0u) != 0
        || expect_flag_set(sim.cpu.xpsr, CPU_XPSR_Z_MASK, 1) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_ldr_str_sram(void) {
    sim_t sim;
    uint8_t firmware[32] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x4903u);
    encode_u16le(&firmware[10], 0x202Au);
    encode_u16le(&firmware[12], 0x6008u);
    encode_u16le(&firmware[14], 0x680Au);
    encode_u16le(&firmware[16], 0xBF00u);
    encode_u32le(&firmware[24], SIM_SRAM_BASE);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[2], 42u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    {
        uint32_t stored = 0;
        bus_result_t result = bus_read32(&sim.bus, SIM_SRAM_BASE, &stored);

        if (!bus_result_is_ok(result) || expect_u32(stored, 42u) != 0) {
            sim_destroy(&sim);
            return 1;
        }
    }

    sim_destroy(&sim);
    return 0;
}

static int test_strb_ldrb(void) {
    sim_t sim;
    uint8_t firmware[32] = {0};
    uint8_t byte_value = 0;

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x4903u);
    encode_u16le(&firmware[10], 0x2034u);
    encode_u16le(&firmware[12], encode_strb_imm(0u, 1u, 0u));
    encode_u16le(&firmware[14], encode_ldrb_imm(2u, 1u, 0u));
    encode_u32le(&firmware[24], SIM_SRAM_BASE);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(sim.cpu.r[2], 0x34u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (!bus_result_is_ok(bus_read8(&sim.bus, SIM_SRAM_BASE, &byte_value)) || expect_u32(byte_value, 0x34u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_strh_ldrh_and_endian(void) {
    sim_t sim;
    uint8_t firmware[40] = {0};
    uint8_t low = 0;
    uint8_t high = 0;
    uint32_t word_value = 0;

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x4904u);
    encode_u16le(&firmware[10], 0x4805u);
    encode_u16le(&firmware[12], encode_strh_imm(0u, 1u, 0u));
    encode_u16le(&firmware[14], encode_ldrh_imm(2u, 1u, 0u));
    encode_u16le(&firmware[16], encode_ldrb_imm(3u, 1u, 0u));
    encode_u16le(&firmware[18], encode_ldrb_imm(4u, 1u, 1u));
    encode_u32le(&firmware[28], SIM_SRAM_BASE);
    encode_u32le(&firmware[32], 0x00001234u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    for (int i = 0; i < 6; ++i) {
        if (sim_step(&sim) != SIM_STOP_NONE) {
            sim_destroy(&sim);
            return 1;
        }
    }

    if (expect_u32(sim.cpu.r[2], 0x1234u) != 0
        || expect_u32(sim.cpu.r[3], 0x34u) != 0
        || expect_u32(sim.cpu.r[4], 0x12u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (!bus_result_is_ok(bus_read8(&sim.bus, SIM_SRAM_BASE, &low))
        || !bus_result_is_ok(bus_read8(&sim.bus, SIM_SRAM_BASE + 1u, &high))
        || !bus_result_is_ok(bus_read32(&sim.bus, SIM_SRAM_BASE, &word_value))) {
        sim_destroy(&sim);
        return 1;
    }

    if (expect_u32(low, 0x34u) != 0
        || expect_u32(high, 0x12u) != 0
        || expect_u32(word_value, 0x00001234u) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_unaligned_halfword_access(void) {
    sim_t sim;
    uint8_t firmware[32] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x4903u);
    encode_u16le(&firmware[10], 0x2012u);
    encode_u16le(&firmware[12], encode_strh_imm(0u, 1u, 0u));
    encode_u32le(&firmware[24], SIM_SRAM_BASE + 1u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_FAULT || sim.last_bus_result.status != BUS_STATUS_UNALIGNED) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_unmapped_byte_and_halfword_access(void) {
    sim_t sim;
    uint8_t firmware[48] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x4905u);
    encode_u16le(&firmware[10], 0x2055u);
    encode_u16le(&firmware[12], encode_strb_imm(0u, 1u, 0u));
    encode_u32le(&firmware[28], SIM_SRAM_BASE + 0x5000u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_FAULT || sim.last_bus_result.status != BUS_STATUS_UNMAPPED) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x4905u);
    encode_u16le(&firmware[10], encode_ldrh_imm(0u, 1u, 0u));
    encode_u32le(&firmware[28], SIM_SRAM_BASE + 0x5000u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_FAULT || sim.last_bus_result.status != BUS_STATUS_UNMAPPED) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_unsupported_instruction(void) {
    sim_t sim;
    uint8_t firmware[16] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0xFFFFu);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_UNSUPPORTED_INSTR) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_faults(void) {
    sim_t sim;
    uint8_t bad_vector_firmware[8] = {0};
    uint8_t bad_store_firmware[24] = {0};

    encode_u32le(&bad_vector_firmware[0], 0x20002000u);
    encode_u32le(&bad_vector_firmware[4], 0x20000000u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, bad_vector_firmware, sizeof(bad_vector_firmware)) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_reset(&sim) == 0 || sim.stop_reason != SIM_STOP_FAULT) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);

    encode_u32le(&bad_store_firmware[0], 0x20002000u);
    encode_u32le(&bad_store_firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&bad_store_firmware[8], 0x4901u);
    encode_u16le(&bad_store_firmware[10], 0x2001u);
    encode_u16le(&bad_store_firmware[12], 0x6008u);
    encode_u32le(&bad_store_firmware[16], SIM_SRAM_BASE + 2u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, bad_store_firmware, sizeof(bad_store_firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_NONE
        || sim_step(&sim) != SIM_STOP_FAULT) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim.last_bus_result.status != BUS_STATUS_UNALIGNED) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_nvic_enable_disable(void) {
    nvic_t nvic;

    nvic_init(&nvic);

    if (nvic_is_enabled(&nvic, 5) != 0) {
        return 1;
    }

    if (nvic_enable_irq(&nvic, 5) != 0 || nvic_is_enabled(&nvic, 5) == 0) {
        return 1;
    }

    if (nvic_disable_irq(&nvic, 5) != 0 || nvic_is_enabled(&nvic, 5) != 0) {
        return 1;
    }

    if (nvic_enable_irq(&nvic, -1) == 0 || nvic_enable_irq(&nvic, (int)NVIC_MAX_IRQS) == 0) {
        return 1;
    }

    return 0;
}

static int test_nvic_pending_set_clear(void) {
    nvic_t nvic;

    nvic_init(&nvic);

    if (nvic_is_pending(&nvic, 7) != 0) {
        return 1;
    }

    if (nvic_set_pending(&nvic, 7) != 0 || nvic_is_pending(&nvic, 7) == 0) {
        return 1;
    }

    if (nvic_clear_pending(&nvic, 7) != 0 || nvic_is_pending(&nvic, 7) != 0) {
        return 1;
    }

    return 0;
}

static int test_nvic_select_by_priority(void) {
    nvic_t nvic;

    nvic_init(&nvic);

    if (nvic_enable_irq(&nvic, 10) != 0
        || nvic_enable_irq(&nvic, 20) != 0
        || nvic_set_pending(&nvic, 10) != 0
        || nvic_set_pending(&nvic, 20) != 0
        || nvic_set_priority(&nvic, 10, 3u) != 0
        || nvic_set_priority(&nvic, 20, 1u) != 0) {
        return 1;
    }

    return nvic_select_next(&nvic) == 20 ? 0 : 1;
}

static int test_nvic_tie_break_by_irq_number(void) {
    nvic_t nvic;

    nvic_init(&nvic);

    if (nvic_enable_irq(&nvic, 12) != 0
        || nvic_enable_irq(&nvic, 4) != 0
        || nvic_set_pending(&nvic, 12) != 0
        || nvic_set_pending(&nvic, 4) != 0
        || nvic_set_priority(&nvic, 12, 2u) != 0
        || nvic_set_priority(&nvic, 4, 2u) != 0) {
        return 1;
    }

    return nvic_select_next(&nvic) == 4 ? 0 : 1;
}

static int test_nvic_select_none(void) {
    nvic_t nvic;

    nvic_init(&nvic);

    if (nvic_select_next(&nvic) != NVIC_NO_IRQ) {
        return 1;
    }

    if (nvic_set_pending(&nvic, 3) != 0 || nvic_select_next(&nvic) != NVIC_NO_IRQ) {
        return 1;
    }

    if (nvic_clear_pending(&nvic, 3) != 0 || nvic_enable_irq(&nvic, 3) != 0) {
        return 1;
    }

    return nvic_select_next(&nvic) == NVIC_NO_IRQ ? 0 : 1;
}

int main(void) {
#define RUN_TEST(fn) \
    do { \
        if ((fn)() != 0) { \
            fprintf(stderr, "%s failed\n", #fn); \
            return 1; \
        } \
    } while (0)

    RUN_TEST(test_reset_sequence);
    RUN_TEST(test_step_nop);
    RUN_TEST(test_branch);
    RUN_TEST(test_ldr_literal);
    RUN_TEST(test_mov_register);
    RUN_TEST(test_add_immediate);
    RUN_TEST(test_add_register);
    RUN_TEST(test_sub_register);
    RUN_TEST(test_cmp_flags);
    RUN_TEST(test_cmp_register);
    RUN_TEST(test_beq_taken);
    RUN_TEST(test_bne_not_taken);
    RUN_TEST(test_blt_taken_signed);
    RUN_TEST(test_bcs_taken_unsigned);
    RUN_TEST(test_backward_unconditional_branch);
    RUN_TEST(test_backward_conditional_branch);
    RUN_TEST(test_flag_corner_cases);
    RUN_TEST(test_sub_carry_borrow_behavior);
    RUN_TEST(test_sub_negative_result);
    RUN_TEST(test_branch_depends_on_prior_flags);
    RUN_TEST(test_simple_loop_iterations);
    RUN_TEST(test_ldr_str_sram);
    RUN_TEST(test_strb_ldrb);
    RUN_TEST(test_strh_ldrh_and_endian);
    RUN_TEST(test_unaligned_halfword_access);
    RUN_TEST(test_unmapped_byte_and_halfword_access);
    RUN_TEST(test_unsupported_instruction);
    RUN_TEST(test_faults);
    RUN_TEST(test_nvic_enable_disable);
    RUN_TEST(test_nvic_pending_set_clear);
    RUN_TEST(test_nvic_select_by_priority);
    RUN_TEST(test_nvic_tie_break_by_irq_number);
    RUN_TEST(test_nvic_select_none);

#undef RUN_TEST

    return 0;
}

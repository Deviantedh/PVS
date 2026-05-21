#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "sim/sim.h"
#include "sim/irq.h"
#include "sim/usart1.h"

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

static uint16_t encode_push(uint8_t register_list, int include_lr) {
    return (uint16_t)(0xB400u | ((include_lr ? 1u : 0u) << 8) | register_list);
}

static uint16_t encode_pop(uint8_t register_list, int include_pc) {
    return (uint16_t)(0xBC00u | ((include_pc ? 1u : 0u) << 8) | register_list);
}

static void encode_bl(uint8_t *dst, uint32_t instr_addr, uint32_t target_addr) {
    int32_t offset = (int32_t)(target_addr & ~0x1u) - (int32_t)instr_addr - 4;
    uint32_t imm25 = (uint32_t)offset & 0x01FFFFFFu;
    uint32_t s = (imm25 >> 24) & 0x1u;
    uint32_t i1 = (imm25 >> 23) & 0x1u;
    uint32_t i2 = (imm25 >> 22) & 0x1u;
    uint32_t imm10 = (imm25 >> 12) & 0x03FFu;
    uint32_t imm11 = (imm25 >> 1) & 0x07FFu;
    uint32_t j1 = (~(i1 ^ s)) & 0x1u;
    uint32_t j2 = (~(i2 ^ s)) & 0x1u;

    encode_u16le(&dst[0], (uint16_t)(0xF000u | (s << 10) | imm10));
    encode_u16le(&dst[2], (uint16_t)(0xD000u | (j1 << 13) | (j2 << 11) | imm11));
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

static uint16_t encode_str_imm(uint8_t rt, uint8_t rn, uint8_t imm5) {
    return (uint16_t)(0x6000u | ((uint16_t)(imm5 & 0x1Fu) << 6) | ((uint16_t)(rn & 0x7u) << 3) | (rt & 0x7u));
}

static uint16_t encode_bx_lr(void) {
    return 0x4770u;
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

static int test_irq_numbers_are_centralized(void) {
    if (TIM2_IRQ_NUMBER != SIM_IRQ_TIM2 || USART1_IRQ_NUMBER != SIM_IRQ_USART1) {
        return 1;
    }

    return SIM_IRQ_TIM2 == 28 && SIM_IRQ_USART1 == 37 ? 0 : 1;
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

static int test_push_registers_and_lr(void) {
    sim_t sim;
    uint8_t firmware[16] = {0};
    uint32_t frame_base = SIM_SRAM_BASE + 0x100u - 12u;
    uint32_t value = 0;

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], encode_push(0x03u, 1));

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim.cpu.r[0] = 0x11111111u;
    sim.cpu.r[1] = 0x22222222u;
    sim.cpu.lr = 0x08001235u;

    if (sim_step(&sim) != SIM_STOP_NONE || sim.cpu.msp != frame_base) {
        sim_destroy(&sim);
        return 1;
    }

    if (!bus_result_is_ok(bus_read32(&sim.bus, frame_base, &value)) || value != 0x11111111u
        || !bus_result_is_ok(bus_read32(&sim.bus, frame_base + 4u, &value)) || value != 0x22222222u
        || !bus_result_is_ok(bus_read32(&sim.bus, frame_base + 8u, &value)) || value != 0x08001235u) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_pop_registers(void) {
    sim_t sim;
    uint8_t firmware[16] = {0};

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], encode_pop(0x05u, 0));

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim.cpu.msp = SIM_SRAM_BASE + 0x80u;
    if (!bus_result_is_ok(bus_write32(&sim.bus, sim.cpu.msp, 0xAAAAAAAAu))
        || !bus_result_is_ok(bus_write32(&sim.bus, sim.cpu.msp + 4u, 0xBBBBBBBBu))) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim.cpu.r[0] != 0xAAAAAAAAu
        || sim.cpu.r[2] != 0xBBBBBBBBu
        || sim.cpu.msp != SIM_SRAM_BASE + 0x88u
        || sim.cpu.pc != SIM_FLASH_BASE + 10u + 1u) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_pop_pc_branches(void) {
    sim_t sim;
    uint8_t firmware[16] = {0};
    uint32_t target = SIM_FLASH_BASE + 10u + 1u;

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], encode_pop(0x00u, 1));
    encode_u16le(&firmware[10], 0xBF00u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim.cpu.msp = SIM_SRAM_BASE + 0x80u;
    if (!bus_result_is_ok(bus_write32(&sim.bus, sim.cpu.msp, target))) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE
        || sim.cpu.pc != target
        || sim.cpu.msp != SIM_SRAM_BASE + 0x84u) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_bl_branches_and_sets_lr(void) {
    sim_t sim;
    uint8_t firmware[32] = {0};
    uint32_t call_addr = SIM_FLASH_BASE + 8u;
    uint32_t return_addr = SIM_FLASH_BASE + 12u + 1u;
    uint32_t function_addr = SIM_FLASH_BASE + 16u + 1u;

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], call_addr + 1u);
    encode_bl(&firmware[8], call_addr, function_addr);
    encode_u16le(&firmware[12], 0x2109u);
    encode_u16le(&firmware[14], encode_b(-2));
    encode_u16le(&firmware[16], 0x2007u);
    encode_u16le(&firmware[18], encode_bx_lr());

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim.cpu.pc != function_addr || sim.cpu.lr != return_addr) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim.cpu.r[0] != 7u) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim.cpu.pc != return_addr) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim.cpu.r[1] != 9u) {
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

static int test_tim2_does_not_tick_when_disabled(void) {
    tim2_t tim2;
    nvic_t nvic;

    tim2_init(&tim2);
    nvic_init(&nvic);

    if (tim2_write32(&tim2, TIM2_ARR_OFFSET, 10u) != 0
        || tim2_write32(&tim2, TIM2_CNT_OFFSET, 3u) != 0) {
        return 1;
    }

    tim2_tick(&tim2, &nvic, 5u);
    return tim2.cnt == 3u && tim2.sr == 0u ? 0 : 1;
}

static int test_tim2_counts_when_enabled(void) {
    tim2_t tim2;
    nvic_t nvic;

    tim2_init(&tim2);
    nvic_init(&nvic);

    if (tim2_write32(&tim2, TIM2_ARR_OFFSET, 10u) != 0
        || tim2_write32(&tim2, TIM2_CR1_OFFSET, TIM2_CR1_CEN) != 0) {
        return 1;
    }

    tim2_tick(&tim2, &nvic, 3u);
    return tim2.cnt == 3u ? 0 : 1;
}

static int test_tim2_prescaler_counts_consistently(void) {
    tim2_t tim2;
    nvic_t nvic;

    tim2_init(&tim2);
    nvic_init(&nvic);

    if (tim2_write32(&tim2, TIM2_ARR_OFFSET, 10u) != 0
        || tim2_write32(&tim2, TIM2_PSC_OFFSET, 1u) != 0
        || tim2_write32(&tim2, TIM2_CR1_OFFSET, TIM2_CR1_CEN) != 0) {
        return 1;
    }

    tim2_tick(&tim2, &nvic, 1u);
    if (tim2.cnt != 0u) {
        return 1;
    }

    tim2_tick(&tim2, &nvic, 1u);
    return tim2.cnt == 1u ? 0 : 1;
}

static int test_tim2_overflow_sets_uif(void) {
    tim2_t tim2;
    nvic_t nvic;

    tim2_init(&tim2);
    nvic_init(&nvic);

    if (tim2_write32(&tim2, TIM2_ARR_OFFSET, 2u) != 0
        || tim2_write32(&tim2, TIM2_CR1_OFFSET, TIM2_CR1_CEN) != 0) {
        return 1;
    }

    tim2_tick(&tim2, &nvic, 3u);
    return tim2.cnt == 0u && (tim2.sr & TIM2_SR_UIF) != 0u ? 0 : 1;
}

static int test_tim2_overflow_sets_pending_when_uie_enabled(void) {
    tim2_t tim2;
    nvic_t nvic;

    tim2_init(&tim2);
    nvic_init(&nvic);

    if (tim2_write32(&tim2, TIM2_ARR_OFFSET, 1u) != 0
        || tim2_write32(&tim2, TIM2_DIER_OFFSET, TIM2_DIER_UIE) != 0
        || tim2_write32(&tim2, TIM2_CR1_OFFSET, TIM2_CR1_CEN) != 0) {
        return 1;
    }

    tim2_tick(&tim2, &nvic, 2u);
    return nvic_is_pending(&nvic, TIM2_IRQ_NUMBER) != 0 ? 0 : 1;
}

static int test_tim2_overflow_does_not_set_pending_when_uie_disabled(void) {
    tim2_t tim2;
    nvic_t nvic;

    tim2_init(&tim2);
    nvic_init(&nvic);

    if (tim2_write32(&tim2, TIM2_ARR_OFFSET, 1u) != 0
        || tim2_write32(&tim2, TIM2_CR1_OFFSET, TIM2_CR1_CEN) != 0) {
        return 1;
    }

    tim2_tick(&tim2, &nvic, 2u);
    return nvic_is_pending(&nvic, TIM2_IRQ_NUMBER) == 0 ? 0 : 1;
}

static int test_tim2_ticks_during_sim_step(void) {
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

    if (tim2_write32(&sim.tim2, TIM2_ARR_OFFSET, 10u) != 0
        || tim2_write32(&sim.tim2, TIM2_CR1_OFFSET, TIM2_CR1_CEN) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim.tim2.cnt != 1u) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_tim2_mmio_read_write_registers(void) {
    sim_t sim;
    uint32_t value = 0;

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (!bus_result_is_ok(bus_write32(&sim.bus, TIM2_BASE + TIM2_CR1_OFFSET, TIM2_CR1_CEN))
        || !bus_result_is_ok(bus_write32(&sim.bus, TIM2_BASE + TIM2_PSC_OFFSET, 7u))
        || !bus_result_is_ok(bus_write32(&sim.bus, TIM2_BASE + TIM2_ARR_OFFSET, 42u))
        || !bus_result_is_ok(bus_write32(&sim.bus, TIM2_BASE + TIM2_DIER_OFFSET, TIM2_DIER_UIE))) {
        sim_destroy(&sim);
        return 1;
    }

    if (!bus_result_is_ok(bus_read32(&sim.bus, TIM2_BASE + TIM2_CR1_OFFSET, &value)) || value != TIM2_CR1_CEN) {
        sim_destroy(&sim);
        return 1;
    }

    if (!bus_result_is_ok(bus_read32(&sim.bus, TIM2_BASE + TIM2_PSC_OFFSET, &value)) || value != 7u) {
        sim_destroy(&sim);
        return 1;
    }

    if (!bus_result_is_ok(bus_read32(&sim.bus, TIM2_BASE + TIM2_ARR_OFFSET, &value)) || value != 42u) {
        sim_destroy(&sim);
        return 1;
    }

    if (!bus_result_is_ok(bus_read32(&sim.bus, TIM2_BASE + TIM2_DIER_OFFSET, &value)) || value != TIM2_DIER_UIE) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_tim2_ticks_after_mmio_configuration(void) {
    sim_t sim;

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (!bus_result_is_ok(bus_write32(&sim.bus, TIM2_BASE + TIM2_ARR_OFFSET, 10u))
        || !bus_result_is_ok(bus_write32(&sim.bus, TIM2_BASE + TIM2_CR1_OFFSET, TIM2_CR1_CEN))) {
        sim_destroy(&sim);
        return 1;
    }

    tim2_tick(&sim.tim2, &sim.nvic, 4u);
    if (sim.tim2.cnt != 4u) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_tim2_mmio_overflow_sets_pending_irq(void) {
    sim_t sim;
    uint8_t firmware[16] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0xBF00u);
    encode_u16le(&firmware[10], 0xBF00u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (!bus_result_is_ok(bus_write32(&sim.bus, TIM2_BASE + TIM2_ARR_OFFSET, 1u))
        || !bus_result_is_ok(bus_write32(&sim.bus, TIM2_BASE + TIM2_DIER_OFFSET, TIM2_DIER_UIE))
        || !bus_result_is_ok(bus_write32(&sim.bus, TIM2_BASE + TIM2_CR1_OFFSET, TIM2_CR1_CEN))) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || nvic_is_pending(&sim.nvic, TIM2_IRQ_NUMBER) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || nvic_is_pending(&sim.nvic, TIM2_IRQ_NUMBER) == 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_tim2_unmapped_mmio_address(void) {
    sim_t sim;
    uint32_t value = 0;
    bus_result_t result;

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    result = bus_write32(&sim.bus, TIM2_BASE + 0x400u, 1u);
    if (result.status != BUS_STATUS_UNMAPPED) {
        sim_destroy(&sim);
        return 1;
    }

    result = bus_read32(&sim.bus, TIM2_BASE + 0x400u, &value);
    if (result.status != BUS_STATUS_UNMAPPED) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_tim2_configured_by_firmware_str_mmio(void) {
    sim_t sim;
    uint8_t firmware[32] = {0};

    encode_u32le(&firmware[0], 0x20002000u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x4903u);
    encode_u16le(&firmware[10], 0x200Au);
    encode_u16le(&firmware[12], encode_str_imm(0u, 1u, TIM2_ARR_OFFSET / 4u));
    encode_u16le(&firmware[14], 0x2001u);
    encode_u16le(&firmware[16], encode_str_imm(0u, 1u, TIM2_CR1_OFFSET / 4u));
    encode_u16le(&firmware[18], 0xBF00u);
    encode_u32le(&firmware[24], TIM2_BASE);

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

    if (sim.tim2.arr != 10u || (sim.tim2.cr1 & TIM2_CR1_CEN) == 0u || sim.tim2.cnt != 1u) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_irq_delivery_jumps_to_vector_handler(void) {
    sim_t sim;
    uint8_t firmware[256] = {0};
    uint32_t handler_addr = SIM_FLASH_BASE + 192u + 1u;

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u32le(&firmware[(CPU_EXTERNAL_EXCEPTION_BASE + TIM2_IRQ_NUMBER) * 4u], handler_addr);
    encode_u16le(&firmware[8], 0xBF00u);
    encode_u16le(&firmware[192], encode_bx_lr());

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (nvic_enable_irq(&sim.nvic, TIM2_IRQ_NUMBER) != 0 || nvic_set_pending(&sim.nvic, TIM2_IRQ_NUMBER) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim.cpu.pc != handler_addr || !sim.cpu.handler_mode || sim.cpu.active_exception != CPU_EXTERNAL_EXCEPTION_BASE + TIM2_IRQ_NUMBER) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_irq_entry_writes_stack_frame(void) {
    sim_t sim;
    uint8_t firmware[256] = {0};
    uint32_t handler_addr = SIM_FLASH_BASE + 192u + 1u;
    uint32_t frame_base = SIM_SRAM_BASE + 0x100u - 32u;
    uint32_t value = 0;

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u32le(&firmware[(CPU_EXTERNAL_EXCEPTION_BASE + TIM2_IRQ_NUMBER) * 4u], handler_addr);
    encode_u16le(&firmware[8], 0xBF00u);
    encode_u16le(&firmware[192], encode_bx_lr());

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    sim.cpu.r[0] = 0x11111111u;
    sim.cpu.r[1] = 0x22222222u;
    sim.cpu.r[2] = 0x33333333u;
    sim.cpu.r[3] = 0x44444444u;
    sim.cpu.r[12] = 0xCCCCCCCCu;
    sim.cpu.lr = 0xAAAAAAAAu;

    if (nvic_enable_irq(&sim.nvic, TIM2_IRQ_NUMBER) != 0 || nvic_set_pending(&sim.nvic, TIM2_IRQ_NUMBER) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim.cpu.msp != frame_base) {
        sim_destroy(&sim);
        return 1;
    }

    if (!bus_result_is_ok(bus_read32(&sim.bus, frame_base + 0u, &value)) || value != 0x11111111u
        || !bus_result_is_ok(bus_read32(&sim.bus, frame_base + 4u, &value)) || value != 0x22222222u
        || !bus_result_is_ok(bus_read32(&sim.bus, frame_base + 8u, &value)) || value != 0x33333333u
        || !bus_result_is_ok(bus_read32(&sim.bus, frame_base + 12u, &value)) || value != 0x44444444u
        || !bus_result_is_ok(bus_read32(&sim.bus, frame_base + 16u, &value)) || value != 0xCCCCCCCCu
        || !bus_result_is_ok(bus_read32(&sim.bus, frame_base + 20u, &value)) || value != 0xAAAAAAAAu
        || !bus_result_is_ok(bus_read32(&sim.bus, frame_base + 24u, &value)) || value != SIM_FLASH_BASE + 10u + 1u
        || !bus_result_is_ok(bus_read32(&sim.bus, frame_base + 28u, &value)) || value != 0x01000000u) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_irq_return_resumes_thread_execution(void) {
    sim_t sim;
    uint8_t firmware[256] = {0};
    uint32_t handler_addr = SIM_FLASH_BASE + 192u + 1u;

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u32le(&firmware[(CPU_EXTERNAL_EXCEPTION_BASE + TIM2_IRQ_NUMBER) * 4u], handler_addr);
    encode_u16le(&firmware[8], 0xBF00u);
    encode_u16le(&firmware[10], 0x2007u);
    encode_u16le(&firmware[192], encode_bx_lr());

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (nvic_enable_irq(&sim.nvic, TIM2_IRQ_NUMBER) != 0 || nvic_set_pending(&sim.nvic, TIM2_IRQ_NUMBER) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim.cpu.pc != handler_addr) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim.cpu.handler_mode || sim.cpu.pc != SIM_FLASH_BASE + 10u + 1u) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim.cpu.r[0] != 7u) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_irq_disabled_does_not_enter_handler(void) {
    sim_t sim;
    uint8_t firmware[256] = {0};
    uint32_t handler_addr = SIM_FLASH_BASE + 192u + 1u;

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u32le(&firmware[(CPU_EXTERNAL_EXCEPTION_BASE + TIM2_IRQ_NUMBER) * 4u], handler_addr);
    encode_u16le(&firmware[8], 0xBF00u);
    encode_u16le(&firmware[192], encode_bx_lr());

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (nvic_set_pending(&sim.nvic, TIM2_IRQ_NUMBER) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE || sim.cpu.pc != SIM_FLASH_BASE + 10u + 1u || sim.cpu.handler_mode) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_irq_bad_vector_faults_controlled(void) {
    sim_t sim;
    uint8_t firmware[256] = {0};

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u32le(&firmware[(CPU_EXTERNAL_EXCEPTION_BASE + TIM2_IRQ_NUMBER) * 4u], SIM_FLASH_BASE + 192u);
    encode_u16le(&firmware[8], 0xBF00u);

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0 || sim_reset(&sim) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    if (nvic_enable_irq(&sim.nvic, TIM2_IRQ_NUMBER) != 0 || nvic_set_pending(&sim.nvic, TIM2_IRQ_NUMBER) != 0) {
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

static int test_usart1_reset_state(void) {
    usart1_t usart;
    uint32_t value = 0;

    usart1_init(&usart);

    if (usart.sr != USART1_SR_TXE || usart.dr != 0u || usart.brr != 0u || usart.cr1 != 0u) {
        return 1;
    }

    if (usart1_tx_available(&usart) != 0u) {
        return 1;
    }

    if (usart1_read32(&usart, USART1_SR_OFFSET, &value) != 0 || value != USART1_SR_TXE) {
        return 1;
    }

    return 0;
}

static int test_usart1_dr_write_without_ue_or_te_does_not_transmit(void) {
    usart1_t usart;

    usart1_init(&usart);

    if (usart1_write32(&usart, USART1_DR_OFFSET, 'A') != 0 || usart1_tx_available(&usart) != 0u) {
        return 1;
    }

    if (usart1_write32(&usart, USART1_CR1_OFFSET, USART1_CR1_UE) != 0
        || usart1_write32(&usart, USART1_DR_OFFSET, 'B') != 0
        || usart1_tx_available(&usart) != 0u) {
        return 1;
    }

    if (usart1_write32(&usart, USART1_CR1_OFFSET, USART1_CR1_TE) != 0
        || usart1_write32(&usart, USART1_DR_OFFSET, 'C') != 0
        || usart1_tx_available(&usart) != 0u) {
        return 1;
    }

    return 0;
}

static int test_usart1_dr_write_with_ue_and_te_transmits_byte(void) {
    usart1_t usart;
    uint8_t byte = 0;

    usart1_init(&usart);

    if (usart1_write32(&usart, USART1_CR1_OFFSET, USART1_CR1_UE | USART1_CR1_TE) != 0
        || usart1_write32(&usart, USART1_DR_OFFSET, 'Z') != 0) {
        return 1;
    }

    if (usart1_tx_available(&usart) != 1u || usart1_tx_pop(&usart, &byte) != 0 || byte != 'Z') {
        return 1;
    }

    return usart1_tx_available(&usart) == 0u ? 0 : 1;
}

static int test_usart1_txe_stays_set_in_minimal_model(void) {
    usart1_t usart;
    uint32_t value = 0;

    usart1_init(&usart);

    if (usart1_write32(&usart, USART1_CR1_OFFSET, USART1_CR1_UE | USART1_CR1_TE) != 0
        || usart1_write32(&usart, USART1_DR_OFFSET, 'X') != 0
        || usart1_read32(&usart, USART1_SR_OFFSET, &value) != 0) {
        return 1;
    }

    return (value & USART1_SR_TXE) != 0u ? 0 : 1;
}

static int test_usart1_tx_output_fifo_order(void) {
    usart1_t usart;
    uint8_t byte = 0;

    usart1_init(&usart);

    if (usart1_write32(&usart, USART1_CR1_OFFSET, USART1_CR1_UE | USART1_CR1_TE) != 0
        || usart1_write32(&usart, USART1_DR_OFFSET, 'O') != 0
        || usart1_write32(&usart, USART1_DR_OFFSET, 'K') != 0) {
        return 1;
    }

    if (usart1_tx_available(&usart) != 2u) {
        return 1;
    }

    if (usart1_tx_pop(&usart, &byte) != 0 || byte != 'O') {
        return 1;
    }

    if (usart1_tx_pop(&usart, &byte) != 0 || byte != 'K') {
        return 1;
    }

    return usart1_tx_pop(&usart, &byte) != 0 ? 0 : 1;
}

static int test_usart1_mmio_read_write_registers(void) {
    sim_t sim;
    uint32_t value = 0;

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (!bus_result_is_ok(bus_write32(&sim.bus, USART1_BASE + USART1_BRR_OFFSET, 0x1234u))
        || !bus_result_is_ok(bus_write32(&sim.bus, USART1_BASE + USART1_CR1_OFFSET, USART1_CR1_UE | USART1_CR1_TE | USART1_CR1_RE))
        || !bus_result_is_ok(bus_write32(&sim.bus, USART1_BASE + USART1_SR_OFFSET, USART1_SR_RXNE))) {
        sim_destroy(&sim);
        return 1;
    }

    if (!bus_result_is_ok(bus_read32(&sim.bus, USART1_BASE + USART1_BRR_OFFSET, &value)) || value != 0x1234u) {
        sim_destroy(&sim);
        return 1;
    }

    if (!bus_result_is_ok(bus_read32(&sim.bus, USART1_BASE + USART1_CR1_OFFSET, &value))
        || value != (USART1_CR1_UE | USART1_CR1_TE | USART1_CR1_RE)) {
        sim_destroy(&sim);
        return 1;
    }

    if (!bus_result_is_ok(bus_read32(&sim.bus, USART1_BASE + USART1_SR_OFFSET, &value))
        || value != (USART1_SR_RXNE | USART1_SR_TXE)) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_usart1_mmio_dr_write_produces_sim_uart_output(void) {
    sim_t sim;

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (!bus_result_is_ok(bus_write32(&sim.bus, USART1_BASE + USART1_CR1_OFFSET, USART1_CR1_UE | USART1_CR1_TE))
        || !bus_result_is_ok(bus_write32(&sim.bus, USART1_BASE + USART1_DR_OFFSET, 'A'))) {
        sim_destroy(&sim);
        return 1;
    }

    if (sim_drain_uart_output(&sim) != 1u
        || sim_uart_output_size(&sim) != 1u
        || sim_uart_output_data(&sim)[0] != 'A') {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_usart1_mmio_dr_write_without_enable_has_no_output(void) {
    sim_t sim;

    if (sim_init(&sim, NULL) != 0) {
        return 1;
    }

    if (!bus_result_is_ok(bus_write32(&sim.bus, USART1_BASE + USART1_DR_OFFSET, 'A'))
        || sim_drain_uart_output(&sim) != 0u
        || sim_uart_output_size(&sim) != 0u) {
        sim_destroy(&sim);
        return 1;
    }

    if (!bus_result_is_ok(bus_write32(&sim.bus, USART1_BASE + USART1_CR1_OFFSET, USART1_CR1_UE))
        || !bus_result_is_ok(bus_write32(&sim.bus, USART1_BASE + USART1_DR_OFFSET, 'B'))
        || sim_drain_uart_output(&sim) != 0u
        || sim_uart_output_size(&sim) != 0u) {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
}

static int test_usart1_firmware_str_mmio_produces_output(void) {
    sim_t sim;
    uint8_t firmware[40] = {0};

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u16le(&firmware[8], 0x4904u);
    encode_u16le(&firmware[10], 0x4805u);
    encode_u16le(&firmware[12], encode_str_imm(0u, 1u, USART1_CR1_OFFSET / 4u));
    encode_u16le(&firmware[14], 0x2055u);
    encode_u16le(&firmware[16], encode_str_imm(0u, 1u, USART1_DR_OFFSET / 4u));
    encode_u16le(&firmware[18], 0xBF00u);
    encode_u32le(&firmware[28], USART1_BASE);
    encode_u32le(&firmware[32], USART1_CR1_UE | USART1_CR1_TE);

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

    if (sim_uart_output_size(&sim) != 1u || sim_uart_output_data(&sim)[0] != 'U') {
        sim_destroy(&sim);
        return 1;
    }

    sim_destroy(&sim);
    return 0;
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
    RUN_TEST(test_irq_numbers_are_centralized);
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
    RUN_TEST(test_push_registers_and_lr);
    RUN_TEST(test_pop_registers);
    RUN_TEST(test_pop_pc_branches);
    RUN_TEST(test_bl_branches_and_sets_lr);
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
    RUN_TEST(test_tim2_does_not_tick_when_disabled);
    RUN_TEST(test_tim2_counts_when_enabled);
    RUN_TEST(test_tim2_prescaler_counts_consistently);
    RUN_TEST(test_tim2_overflow_sets_uif);
    RUN_TEST(test_tim2_overflow_sets_pending_when_uie_enabled);
    RUN_TEST(test_tim2_overflow_does_not_set_pending_when_uie_disabled);
    RUN_TEST(test_tim2_ticks_during_sim_step);
    RUN_TEST(test_tim2_mmio_read_write_registers);
    RUN_TEST(test_tim2_ticks_after_mmio_configuration);
    RUN_TEST(test_tim2_mmio_overflow_sets_pending_irq);
    RUN_TEST(test_tim2_unmapped_mmio_address);
    RUN_TEST(test_tim2_configured_by_firmware_str_mmio);
    RUN_TEST(test_irq_delivery_jumps_to_vector_handler);
    RUN_TEST(test_irq_entry_writes_stack_frame);
    RUN_TEST(test_irq_return_resumes_thread_execution);
    RUN_TEST(test_irq_disabled_does_not_enter_handler);
    RUN_TEST(test_irq_bad_vector_faults_controlled);
    RUN_TEST(test_usart1_reset_state);
    RUN_TEST(test_usart1_dr_write_without_ue_or_te_does_not_transmit);
    RUN_TEST(test_usart1_dr_write_with_ue_and_te_transmits_byte);
    RUN_TEST(test_usart1_txe_stays_set_in_minimal_model);
    RUN_TEST(test_usart1_tx_output_fifo_order);
    RUN_TEST(test_usart1_mmio_read_write_registers);
    RUN_TEST(test_usart1_mmio_dr_write_produces_sim_uart_output);
    RUN_TEST(test_usart1_mmio_dr_write_without_enable_has_no_output);
    RUN_TEST(test_usart1_firmware_str_mmio_produces_output);

#undef RUN_TEST

    return 0;
}

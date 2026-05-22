#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sim/sim.h"

static void encode_u16le(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void encode_u32le(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint16_t encode_b(int16_t imm11) {
    return (uint16_t)(0xE000u | ((uint16_t)imm11 & 0x07FFu));
}

static uint16_t encode_b_cond(uint8_t cond, int8_t imm8) {
    return (uint16_t)(0xD000u | ((uint16_t)cond << 8) | (uint8_t)imm8);
}

static uint16_t encode_str_imm(uint8_t rt, uint8_t rn, uint8_t imm5) {
    return (uint16_t)(0x6000u
        | ((uint16_t)(imm5 & 0x1Fu) << 6)
        | ((uint16_t)(rn & 0x7u) << 3)
        | (rt & 0x7u));
}

static uint16_t encode_ldr_imm(uint8_t rt, uint8_t rn, uint8_t imm5) {
    return (uint16_t)(0x6800u
        | ((uint16_t)(imm5 & 0x1Fu) << 6)
        | ((uint16_t)(rn & 0x7u) << 3)
        | (rt & 0x7u));
}

static uint16_t encode_and_reg(uint8_t rdn, uint8_t rm) {
    return (uint16_t)(0x4000u | ((uint16_t)(rm & 0x7u) << 3) | (rdn & 0x7u));
}

static uint16_t encode_cpsie_i(void) {
    return 0xB662u;
}

static uint16_t encode_cpsid_i(void) {
    return 0xB672u;
}

static int load_and_reset(sim_t *sim, const uint8_t *firmware, size_t firmware_size) {
    if (sim_init(sim, NULL) != 0) {
        return -1;
    }

    if (sim_load_firmware(sim, firmware, firmware_size) != 0) {
        sim_destroy(sim);
        return -1;
    }

    if (sim_reset(sim) != 0) {
        sim_destroy(sim);
        return -1;
    }

    return 0;
}

static int run_steps(sim_t *sim, uint32_t steps) {
    for (uint32_t i = 0; i < steps; ++i) {
        if (sim_step(sim) != SIM_STOP_NONE) {
            return -1;
        }
    }

    return 0;
}

static int run_until_break(sim_t *sim, uint32_t max_steps) {
    for (uint32_t i = 0; i < max_steps; ++i) {
        sim_stop_reason_t reason = sim_step(sim);
        if (reason == SIM_STOP_BREAK) {
            return 0;
        }
        if (reason != SIM_STOP_NONE) {
            return -1;
        }
    }

    return -1;
}

static void build_hello_uart(uint8_t *firmware, size_t size) {
    memset(firmware, 0, size);

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);

    encode_u16le(&firmware[8], 0x4903u);
    encode_u16le(&firmware[10], 0x4A04u);
    encode_u16le(&firmware[12], encode_str_imm(2u, 1u, USART1_CR1_OFFSET / 4u));
    encode_u16le(&firmware[14], 0x204Fu);
    encode_u16le(&firmware[16], encode_str_imm(0u, 1u, USART1_DR_OFFSET / 4u));
    encode_u16le(&firmware[18], 0x204Bu);
    encode_u16le(&firmware[20], encode_str_imm(0u, 1u, USART1_DR_OFFSET / 4u));
    encode_u16le(&firmware[22], encode_b(-2));

    encode_u32le(&firmware[24], USART1_BASE);
    encode_u32le(&firmware[28], USART1_CR1_UE | USART1_CR1_TE);
}

static int test_hello_uart_firmware(void) {
    sim_t sim;
    uint8_t firmware[64];
    int failed;

    build_hello_uart(firmware, sizeof(firmware));
    if (load_and_reset(&sim, firmware, sizeof(firmware)) != 0) {
        return 1;
    }

    failed = run_steps(&sim, 7u) != 0
        || sim_uart_output_size(&sim) != 2u
        || memcmp(sim_uart_output_data(&sim), "OK", 2u) != 0;

    sim_destroy(&sim);
    return failed ? 1 : 0;
}

static void build_loop_counter(uint8_t *firmware, size_t size) {
    memset(firmware, 0, size);

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);

    encode_u16le(&firmware[8], 0x4903u);
    encode_u16le(&firmware[10], 0x2000u);
    encode_u16le(&firmware[12], 0x3001u);
    encode_u16le(&firmware[14], 0x2803u);
    encode_u16le(&firmware[16], encode_b_cond(0x1u, -4));
    encode_u16le(&firmware[18], encode_str_imm(0u, 1u, 0u));
    encode_u16le(&firmware[20], encode_b(-2));

    encode_u32le(&firmware[24], SIM_SRAM_BASE);
}

static int test_loop_counter_firmware(void) {
    sim_t sim;
    uint8_t firmware[64];
    uint32_t value = 0;
    int failed;

    build_loop_counter(firmware, sizeof(firmware));
    if (load_and_reset(&sim, firmware, sizeof(firmware)) != 0) {
        return 1;
    }

    failed = run_steps(&sim, 12u) != 0
        || !bus_result_is_ok(bus_read32(&sim.bus, SIM_SRAM_BASE, &value))
        || value != 3u;

    sim_destroy(&sim);
    return failed ? 1 : 0;
}

static void build_timer_irq(uint8_t *firmware, size_t size) {
    uint32_t handler_addr = SIM_FLASH_BASE + 192u + 1u;

    memset(firmware, 0, size);

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u32le(&firmware[(CPU_EXTERNAL_EXCEPTION_BASE + TIM2_IRQ_NUMBER) * 4u], handler_addr);

    encode_u16le(&firmware[8], 0x4907u);
    encode_u16le(&firmware[10], 0x2004u);
    encode_u16le(&firmware[12], encode_str_imm(0u, 1u, TIM2_ARR_OFFSET / 4u));
    encode_u16le(&firmware[14], 0x2001u);
    encode_u16le(&firmware[16], encode_str_imm(0u, 1u, TIM2_DIER_OFFSET / 4u));
    encode_u16le(&firmware[18], encode_str_imm(0u, 1u, TIM2_CR1_OFFSET / 4u));
    encode_u16le(&firmware[20], encode_b(-2));
    encode_u32le(&firmware[40], TIM2_BASE);

    encode_u16le(&firmware[192], 0x4903u);
    encode_u16le(&firmware[194], 0x2000u);
    encode_u16le(&firmware[196], encode_str_imm(0u, 1u, TIM2_CR1_OFFSET / 4u));
    encode_u16le(&firmware[198], 0x4903u);
    encode_u16le(&firmware[200], 0x2001u);
    encode_u16le(&firmware[202], encode_str_imm(0u, 1u, 0u));
    encode_u16le(&firmware[204], 0x4770u);
    encode_u32le(&firmware[208], TIM2_BASE);
    encode_u32le(&firmware[212], SIM_SRAM_BASE + 4u);
}

static int test_timer_irq_firmware(void) {
    sim_t sim;
    uint8_t firmware[256];
    uint32_t value = 0;
    int failed;

    build_timer_irq(firmware, sizeof(firmware));
    if (load_and_reset(&sim, firmware, sizeof(firmware)) != 0) {
        return 1;
    }

    if (nvic_enable_irq(&sim.nvic, TIM2_IRQ_NUMBER) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    failed = run_steps(&sim, 18u) != 0
        || !bus_result_is_ok(bus_read32(&sim.bus, SIM_SRAM_BASE + 4u, &value))
        || value != 1u
        || sim.cpu.handler_mode;

    sim_destroy(&sim);
    return failed ? 1 : 0;
}

static void build_timer_irq_to_uart(uint8_t *firmware, size_t size) {
    uint32_t handler_addr = SIM_FLASH_BASE + 256u + 1u;

    memset(firmware, 0, size);

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);
    encode_u32le(&firmware[(CPU_EXTERNAL_EXCEPTION_BASE + TIM2_IRQ_NUMBER) * 4u], handler_addr);

    encode_u16le(&firmware[8], encode_cpsid_i());
    encode_u16le(&firmware[10], 0x4908u);
    encode_u16le(&firmware[12], 0x4A08u);
    encode_u16le(&firmware[14], encode_str_imm(2u, 1u, USART1_CR1_OFFSET / 4u));
    encode_u16le(&firmware[16], 0x4908u);
    encode_u16le(&firmware[18], 0x2001u);
    encode_u16le(&firmware[20], encode_str_imm(0u, 1u, TIM2_ARR_OFFSET / 4u));
    encode_u16le(&firmware[22], encode_str_imm(0u, 1u, TIM2_DIER_OFFSET / 4u));
    encode_u16le(&firmware[24], encode_str_imm(0u, 1u, TIM2_CR1_OFFSET / 4u));
    encode_u16le(&firmware[26], 0x4907u);
    encode_u16le(&firmware[28], 0x4807u);
    encode_u16le(&firmware[30], encode_str_imm(0u, 1u, 0u));
    encode_u16le(&firmware[32], encode_cpsie_i());
    encode_u16le(&firmware[34], encode_b(-2));
    encode_u32le(&firmware[44], USART1_BASE);
    encode_u32le(&firmware[48], USART1_CR1_UE | USART1_CR1_TE);
    encode_u32le(&firmware[52], TIM2_BASE);
    encode_u32le(&firmware[56], NVIC_ISER_BASE);
    encode_u32le(&firmware[60], 1u << TIM2_IRQ_NUMBER);

    encode_u16le(&firmware[256], 0x4906u);
    encode_u16le(&firmware[258], 0x2000u);
    encode_u16le(&firmware[260], encode_str_imm(0u, 1u, TIM2_CR1_OFFSET / 4u));
    encode_u16le(&firmware[262], 0x4906u);
    encode_u16le(&firmware[264], 0x4806u);
    encode_u16le(&firmware[266], encode_str_imm(0u, 1u, 0u));
    encode_u16le(&firmware[268], 0x4906u);
    encode_u16le(&firmware[270], 0x2054u);
    encode_u16le(&firmware[272], encode_str_imm(0u, 1u, USART1_DR_OFFSET / 4u));
    encode_u16le(&firmware[274], 0x4770u);
    encode_u32le(&firmware[284], TIM2_BASE);
    encode_u32le(&firmware[288], NVIC_ICPR_BASE);
    encode_u32le(&firmware[292], 1u << TIM2_IRQ_NUMBER);
    encode_u32le(&firmware[296], USART1_BASE);
}

static int test_timer_irq_to_uart_firmware(void) {
    sim_t sim;
    uint8_t firmware[320];
    int failed;

    build_timer_irq_to_uart(firmware, sizeof(firmware));
    if (load_and_reset(&sim, firmware, sizeof(firmware)) != 0) {
        return 1;
    }

    failed = run_steps(&sim, 23u) != 0
        || sim_uart_output_size(&sim) != 1u
        || memcmp(sim_uart_output_data(&sim), "T", 1u) != 0
        || sim.cpu.handler_mode
        || sim.cpu.active_exception != 0u
        || sim.cpu.pc != SIM_FLASH_BASE + 34u + 1u
        || nvic_is_pending(&sim.nvic, TIM2_IRQ_NUMBER) != 0
        || (sim.tim2.cr1 & TIM2_CR1_CEN) != 0u;

    sim_destroy(&sim);
    return failed ? 1 : 0;
}

static void build_gpio_pin_to_uart(uint8_t *firmware, size_t size) {
    memset(firmware, 0, size);

    encode_u32le(&firmware[0], SIM_SRAM_BASE + 0x100u);
    encode_u32le(&firmware[4], SIM_FLASH_BASE + 8u + 1u);

    encode_u16le(&firmware[8], 0x4909u);
    encode_u16le(&firmware[10], 0x4A0Au);
    encode_u16le(&firmware[12], 0x4B0Au);
    encode_u16le(&firmware[14], encode_str_imm(3u, 2u, USART1_CR1_OFFSET / 4u));
    encode_u16le(&firmware[16], encode_ldr_imm(0u, 1u, GPIO_IDR_OFFSET / 4u));
    encode_u16le(&firmware[18], 0x4B0Au);
    encode_u16le(&firmware[20], encode_and_reg(0u, 3u));
    encode_u16le(&firmware[22], 0x2800u);
    encode_u16le(&firmware[24], encode_b_cond(0x1u, 3));
    encode_u16le(&firmware[26], 0x204Cu);
    encode_u16le(&firmware[28], encode_str_imm(0u, 2u, USART1_DR_OFFSET / 4u));
    encode_u16le(&firmware[30], 0xDF00u);
    encode_u16le(&firmware[32], 0xBF00u);
    encode_u16le(&firmware[34], 0x2048u);
    encode_u16le(&firmware[36], encode_str_imm(0u, 2u, USART1_DR_OFFSET / 4u));
    encode_u16le(&firmware[38], 0xDF00u);

    encode_u32le(&firmware[48], GPIOA_BASE);
    encode_u32le(&firmware[52], USART1_BASE);
    encode_u32le(&firmware[56], USART1_CR1_UE | USART1_CR1_TE);
    encode_u32le(&firmware[60], 1u);
}

static int test_gpio_pin_to_uart_firmware_low(void) {
    sim_t sim;
    uint8_t firmware[64];
    int failed;

    build_gpio_pin_to_uart(firmware, sizeof(firmware));
    if (load_and_reset(&sim, firmware, sizeof(firmware)) != 0) {
        return 1;
    }

    if (gpio_set_input(&sim.gpioa, 0u, 0) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    failed = run_until_break(&sim, 16u) != 0
        || sim_uart_output_size(&sim) != 1u
        || memcmp(sim_uart_output_data(&sim), "L", 1u) != 0;

    sim_destroy(&sim);
    return failed ? 1 : 0;
}

static int test_gpio_pin_to_uart_firmware_high(void) {
    sim_t sim;
    uint8_t firmware[64];
    int failed;

    build_gpio_pin_to_uart(firmware, sizeof(firmware));
    if (load_and_reset(&sim, firmware, sizeof(firmware)) != 0) {
        return 1;
    }

    if (gpio_set_input(&sim.gpioa, 0u, 1) != 0) {
        sim_destroy(&sim);
        return 1;
    }

    failed = run_until_break(&sim, 16u) != 0
        || sim_uart_output_size(&sim) != 1u
        || memcmp(sim_uart_output_data(&sim), "H", 1u) != 0;

    sim_destroy(&sim);
    return failed ? 1 : 0;
}

int main(void) {
#define RUN_TEST(fn) \
    do { \
        if ((fn)() != 0) { \
            fprintf(stderr, "%s failed\n", #fn); \
            return 1; \
        } \
    } while (0)

    RUN_TEST(test_hello_uart_firmware);
    RUN_TEST(test_loop_counter_firmware);
    RUN_TEST(test_timer_irq_firmware);
    RUN_TEST(test_timer_irq_to_uart_firmware);
    RUN_TEST(test_gpio_pin_to_uart_firmware_low);
    RUN_TEST(test_gpio_pin_to_uart_firmware_high);

#undef RUN_TEST

    return 0;
}

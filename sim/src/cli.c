#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim/sim.h"

typedef struct cli_options {
    const char *firmware_path;
    const char *uart_in;
    const char *uart_out_path;
    const char *json_result_path;
    uint64_t max_instructions;
    uint32_t timeout_ms;
} cli_options_t;

static void print_usage(FILE *stream) {
    fprintf(stream,
        "usage: pvs_sim_cli --firmware <path> [--max-instr <n>] [--timeout-ms <n>] "
        "[--uart-in <string>] [--uart-out <path>] [--json-result <path>]\n");
}

static int parse_u64(const char *text, uint64_t *value) {
    char *end = NULL;
    unsigned long long parsed;

    if (text == NULL || value == NULL) {
        return -1;
    }

    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    *value = (uint64_t)parsed;
    return 0;
}

static int parse_u32(const char *text, uint32_t *value) {
    uint64_t parsed = 0;

    if (parse_u64(text, &parsed) != 0 || parsed > UINT32_MAX) {
        return -1;
    }

    *value = (uint32_t)parsed;
    return 0;
}

static int parse_args(int argc, char **argv, cli_options_t *options) {
    memset(options, 0, sizeof(*options));
    options->max_instructions = 1000000u;
    options->timeout_ms = 5000u;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(stdout);
            exit(0);
        }

        if (i + 1 >= argc) {
            return -1;
        }

        if (strcmp(arg, "--firmware") == 0) {
            options->firmware_path = argv[++i];
        } else if (strcmp(arg, "--max-instr") == 0) {
            if (parse_u64(argv[++i], &options->max_instructions) != 0) {
                return -1;
            }
        } else if (strcmp(arg, "--timeout-ms") == 0) {
            if (parse_u32(argv[++i], &options->timeout_ms) != 0) {
                return -1;
            }
        } else if (strcmp(arg, "--uart-in") == 0) {
            options->uart_in = argv[++i];
        } else if (strcmp(arg, "--uart-out") == 0) {
            options->uart_out_path = argv[++i];
        } else if (strcmp(arg, "--json-result") == 0) {
            options->json_result_path = argv[++i];
        } else {
            return -1;
        }
    }

    return options->firmware_path != NULL ? 0 : -1;
}

static int read_file(const char *path, uint8_t **data, size_t *size) {
    FILE *file = NULL;
    long length = 0;
    uint8_t *buffer = NULL;

    if (path == NULL || data == NULL || size == NULL) {
        return -1;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }

    length = ftell(file);
    if (length < 0) {
        fclose(file);
        return -1;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }

    buffer = malloc((size_t)length);
    if (buffer == NULL && length != 0) {
        fclose(file);
        return -1;
    }

    if (length != 0 && fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        free(buffer);
        fclose(file);
        return -1;
    }

    fclose(file);
    *data = buffer;
    *size = (size_t)length;
    return 0;
}

static const char *status_from_stop(sim_stop_reason_t reason) {
    switch (reason) {
    case SIM_STOP_NONE:
    case SIM_STOP_BREAK:
    case SIM_STOP_MAX_INSTRUCTIONS:
        return "OK";
    case SIM_STOP_UNSUPPORTED_INSTR:
        return "UNSUPPORTED_INSTR";
    case SIM_STOP_FAULT:
        return "FAULT";
    default:
        return "CRASH";
    }
}

static int exit_code_from_stop(sim_stop_reason_t reason) {
    switch (reason) {
    case SIM_STOP_NONE:
    case SIM_STOP_BREAK:
    case SIM_STOP_MAX_INSTRUCTIONS:
        return 0;
    case SIM_STOP_UNSUPPORTED_INSTR:
        return 11;
    case SIM_STOP_FAULT:
        return 12;
    default:
        return 12;
    }
}

static void write_json_string(FILE *file, const char *value) {
    fputc('"', file);
    for (const unsigned char *p = (const unsigned char *)value; p != NULL && *p != '\0'; ++p) {
        switch (*p) {
        case '\\':
            fputs("\\\\", file);
            break;
        case '"':
            fputs("\\\"", file);
            break;
        case '\n':
            fputs("\\n", file);
            break;
        case '\r':
            fputs("\\r", file);
            break;
        case '\t':
            fputs("\\t", file);
            break;
        default:
            if (*p < 0x20u) {
                fprintf(file, "\\u%04x", *p);
            } else {
                fputc(*p, file);
            }
            break;
        }
    }
    fputc('"', file);
}

static int write_uart_output(const cli_options_t *options, const sim_t *sim) {
    FILE *file = stdout;

    if (options->uart_out_path != NULL) {
        file = fopen(options->uart_out_path, "wb");
        if (file == NULL) {
            return -1;
        }
    }

    if (sim_uart_output_size(sim) > 0u) {
        fwrite(sim_uart_output_data(sim), 1, sim_uart_output_size(sim), file);
    }

    if (file != stdout) {
        fclose(file);
    }
    return 0;
}

static void write_irq_list(FILE *file, const uint8_t *values) {
    int first = 1;

    fputc('[', file);
    for (int irq = 0; irq < (int)NVIC_MAX_IRQS; ++irq) {
        if (values[irq] == 0u) {
            continue;
        }

        if (!first) {
            fputc(',', file);
        }
        fprintf(file, "%d", irq);
        first = 0;
    }
    fputc(']', file);
}

typedef struct pin_snapshot {
    const char *name;
    const char *port;
    uint8_t index;
    const char *mode;
    int level;
    const char *label;
} pin_snapshot_t;

static const pin_snapshot_t blue_pill_pins[] = {
    {"PA0", "A", 0u, "unknown", -1, ""},
    {"PA1", "A", 1u, "unknown", -1, ""},
    {"PA2", "A", 2u, "unknown", -1, "USART1_TX"},
    {"PA3", "A", 3u, "unknown", -1, "USART1_RX"},
    {"PA4", "A", 4u, "unknown", -1, ""},
    {"PA5", "A", 5u, "unknown", -1, ""},
    {"PA6", "A", 6u, "unknown", -1, ""},
    {"PA7", "A", 7u, "unknown", -1, ""},
    {"PB0", "B", 0u, "unknown", -1, ""},
    {"PB1", "B", 1u, "unknown", -1, ""},
    {"PB10", "B", 10u, "unknown", -1, "TIM2"},
    {"PB11", "B", 11u, "unknown", -1, ""},
    {"PC13", "C", 13u, "unknown", -1, "LED"},
    {"PC14", "C", 14u, "unknown", -1, "OSC32_IN"},
    {"PC15", "C", 15u, "unknown", -1, "OSC32_OUT"}
};

static void write_pin_snapshot(FILE *file) {
    fputc('[', file);
    for (size_t i = 0; i < sizeof(blue_pill_pins) / sizeof(blue_pill_pins[0]); ++i) {
        const pin_snapshot_t *pin = &blue_pill_pins[i];

        if (i != 0u) {
            fputc(',', file);
        }

        fprintf(file, "{\"name\":");
        write_json_string(file, pin->name);
        fprintf(file, ",\"port\":");
        write_json_string(file, pin->port);
        fprintf(file, ",\"index\":%u", pin->index);
        fprintf(file, ",\"mode\":");
        write_json_string(file, pin->mode);
        fprintf(file, ",\"level\":");
        if (pin->level < 0) {
            fprintf(file, "null");
        } else {
            fprintf(file, "%d", pin->level);
        }
        fprintf(file, ",\"label\":");
        write_json_string(file, pin->label);
        fprintf(file, "}");
    }
    fputc(']', file);
}

static void write_snapshot(FILE *file, const sim_t *sim) {
    fprintf(file,
        ",\"cpu\":{"
        "\"pc\":%u,"
        "\"msp\":%u,"
        "\"lr\":%u,"
        "\"xpsr\":%u,"
        "\"primask\":%u,"
        "\"instr_count\":%llu"
        "}",
        sim->cpu.pc,
        sim->cpu.msp,
        sim->cpu.lr,
        sim->cpu.xpsr,
        sim->cpu.primask,
        (unsigned long long)sim->cpu.instr_count);

    fprintf(file,
        ",\"peripherals\":{"
        "\"tim2\":{"
        "\"cr1\":%u,"
        "\"psc\":%u,"
        "\"arr\":%u,"
        "\"cnt\":%u,"
        "\"dier\":%u,"
        "\"sr\":%u"
        "},"
        "\"usart1\":{"
        "\"sr\":%u,"
        "\"dr\":%u,"
        "\"brr\":%u,"
        "\"cr1\":%u"
        "},"
        "\"nvic\":{"
        "\"selected\":%d,"
        "\"enabled\":",
        sim->tim2.cr1,
        sim->tim2.psc,
        sim->tim2.arr,
        sim->tim2.cnt,
        sim->tim2.dier,
        sim->tim2.sr,
        sim->usart1.sr,
        sim->usart1.dr,
        sim->usart1.brr,
        sim->usart1.cr1,
        nvic_select_next(&sim->nvic));
    write_irq_list(file, sim->nvic.enabled);
    fprintf(file, ",\"pending\":");
    write_irq_list(file, sim->nvic.pending);
    fprintf(file, "}},\"pins\":");
    write_pin_snapshot(file);
}

static int write_json_result(const cli_options_t *options, const sim_t *sim, sim_stop_reason_t reason, int exit_code) {
    FILE *file;

    if (options->json_result_path == NULL) {
        return 0;
    }

    file = fopen(options->json_result_path, "wb");
    if (file == NULL) {
        return -1;
    }

    fprintf(file, "{");
    fprintf(file, "\"job_id\":\"\",");
    fprintf(file, "\"status\":");
    write_json_string(file, status_from_stop(reason));
    fprintf(file, ",\"exit_code\":%d,", exit_code);
    fprintf(file, "\"uart_output\":");
    write_json_string(file, sim_uart_output_data(sim));
    fprintf(file, ",\"instructions_executed\":%llu", (unsigned long long)sim->cpu.instr_count);
    write_snapshot(file, sim);
    fprintf(file, "}\n");
    fclose(file);
    return 0;
}

int main(int argc, char **argv) {
    cli_options_t options;
    sim_config_t config = {0};
    sim_t sim;
    uint8_t *firmware = NULL;
    size_t firmware_size = 0;
    sim_stop_reason_t reason;
    int exit_code;

    if (parse_args(argc, argv, &options) != 0) {
        print_usage(stderr);
        return 13;
    }

    (void)options.timeout_ms;
    (void)options.uart_in;

    if (read_file(options.firmware_path, &firmware, &firmware_size) != 0) {
        fprintf(stderr, "failed to read firmware: %s\n", options.firmware_path);
        return 13;
    }

    config.max_instructions = options.max_instructions;
    if (sim_init(&sim, &config) != 0) {
        free(firmware);
        fprintf(stderr, "failed to initialize simulator\n");
        return 12;
    }

    if (sim_load_firmware(&sim, firmware, firmware_size) != 0 || sim_reset(&sim) != 0) {
        free(firmware);
        sim_destroy(&sim);
        fprintf(stderr, "failed to load/reset firmware\n");
        return 13;
    }
    free(firmware);

    reason = sim_run(&sim, options.max_instructions);
    (void)sim_drain_uart_output(&sim);
    exit_code = exit_code_from_stop(reason);

    if (write_uart_output(&options, &sim) != 0) {
        sim_destroy(&sim);
        return 12;
    }

    if (write_json_result(&options, &sim, reason, exit_code) != 0) {
        sim_destroy(&sim);
        return 12;
    }

    sim_destroy(&sim);
    return exit_code;
}

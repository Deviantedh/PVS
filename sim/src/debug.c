#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim/sim.h"

typedef enum debug_status {
    DEBUG_STATUS_IDLE = 0,
    DEBUG_STATUS_STOPPED,
    DEBUG_STATUS_FAILED
} debug_status_t;

typedef struct debug_options {
    const char *firmware_path;
} debug_options_t;

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

static void usage(FILE *stream) {
    fprintf(stream, "usage: pvs_sim_debug --firmware <path>\n");
}

static int parse_args(int argc, char **argv, debug_options_t *options) {
    memset(options, 0, sizeof(*options));
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--firmware") == 0 && i + 1 < argc) {
            options->firmware_path = argv[++i];
        } else {
            return -1;
        }
    }
    return options->firmware_path != NULL ? 0 : -1;
}

static int read_file(const char *path, uint8_t **data, size_t *size) {
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *buffer;

    if (file == NULL) {
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
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

static const char *stop_reason_name(sim_stop_reason_t reason) {
    switch (reason) {
    case SIM_STOP_NONE:
        return "OK";
    case SIM_STOP_BREAK:
        return "BREAK";
    case SIM_STOP_MAX_INSTRUCTIONS:
        return "MAX_INSTRUCTIONS";
    case SIM_STOP_UNSUPPORTED_INSTR:
        return "UNSUPPORTED_INSTR";
    case SIM_STOP_FAULT:
        return "FAULT";
    default:
        return "CRASH";
    }
}

static const char *debug_status_name(debug_status_t status) {
    switch (status) {
    case DEBUG_STATUS_IDLE:
        return "idle";
    case DEBUG_STATUS_STOPPED:
        return "stopped";
    case DEBUG_STATUS_FAILED:
        return "failed";
    default:
        return "failed";
    }
}

static void write_json_string(FILE *file, const char *value) {
    fputc('"', file);
    for (const unsigned char *p = (const unsigned char *)value; p != NULL && *p != '\0'; ++p) {
        if (*p == '\\') {
            fputs("\\\\", file);
        } else if (*p == '"') {
            fputs("\\\"", file);
        } else if (*p == '\n') {
            fputs("\\n", file);
        } else if (*p < 0x20u) {
            fprintf(file, "\\u%04x", *p);
        } else {
            fputc(*p, file);
        }
    }
    fputc('"', file);
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
        fprintf(file, ",\"index\":%u,\"mode\":", pin->index);
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

static void write_state(FILE *file, const sim_t *sim, debug_status_t status, sim_stop_reason_t reason) {
    fprintf(file, "{\"session_id\":\"\",\"status\":");
    write_json_string(file, debug_status_name(status));
    fprintf(file, ",\"stop_reason\":");
    write_json_string(file, stop_reason_name(reason));
    fprintf(file, ",\"uart_output\":");
    write_json_string(file, sim_uart_output_data(sim));
    fprintf(file, ",\"instructions_executed\":%llu", (unsigned long long)sim->cpu.instr_count);
    fprintf(file,
        ",\"cpu\":{\"pc\":%u,\"msp\":%u,\"lr\":%u,\"xpsr\":%u,\"primask\":%u,\"instr_count\":%llu}",
        sim->cpu.pc,
        sim->cpu.msp,
        sim->cpu.lr,
        sim->cpu.xpsr,
        sim->cpu.primask,
        (unsigned long long)sim->cpu.instr_count);
    fprintf(file,
        ",\"peripherals\":{\"tim2\":{\"cr1\":%u,\"psc\":%u,\"arr\":%u,\"cnt\":%u,\"dier\":%u,\"sr\":%u},"
        "\"usart1\":{\"sr\":%u,\"dr\":%u,\"brr\":%u,\"cr1\":%u},"
        "\"nvic\":{\"selected\":%d,\"enabled\":",
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
    fprintf(file, "}\n");
    fflush(file);
}

static sim_stop_reason_t step_many(sim_t *sim, uint64_t steps) {
    sim_stop_reason_t reason = SIM_STOP_NONE;

    for (uint64_t i = 0; i < steps; ++i) {
        reason = sim_step(sim);
        if (reason != SIM_STOP_NONE) {
            return reason;
        }
    }
    return SIM_STOP_NONE;
}

int main(int argc, char **argv) {
    debug_options_t options;
    sim_t sim;
    uint8_t *firmware = NULL;
    size_t firmware_size = 0;
    char line[128];
    sim_stop_reason_t reason = SIM_STOP_NONE;
    debug_status_t status = DEBUG_STATUS_IDLE;

    if (parse_args(argc, argv, &options) != 0) {
        usage(stderr);
        return 13;
    }
    if (read_file(options.firmware_path, &firmware, &firmware_size) != 0) {
        fprintf(stderr, "failed to read firmware\n");
        return 13;
    }
    if (sim_init(&sim, NULL) != 0 || sim_load_firmware(&sim, firmware, firmware_size) != 0 || sim_reset(&sim) != 0) {
        free(firmware);
        fprintf(stderr, "failed to initialize debug simulator\n");
        return 13;
    }
    free(firmware);

    write_state(stdout, &sim, status, reason);

    while (fgets(line, sizeof(line), stdin) != NULL) {
        char command[16] = {0};
        unsigned long long count = 0;

        if (sscanf(line, "%15s %llu", command, &count) < 1) {
            continue;
        }
        if (strcmp(command, "quit") == 0) {
            break;
        }
        if (strcmp(command, "state") == 0) {
            write_state(stdout, &sim, status, reason);
            continue;
        }
        if (strcmp(command, "stop") == 0) {
            status = DEBUG_STATUS_STOPPED;
            write_state(stdout, &sim, status, reason);
            continue;
        }
        if (strcmp(command, "step") == 0) {
            if (count == 0) {
                count = 1;
            }
            reason = step_many(&sim, (uint64_t)count);
            status = reason == SIM_STOP_FAULT || reason == SIM_STOP_UNSUPPORTED_INSTR ? DEBUG_STATUS_FAILED : DEBUG_STATUS_STOPPED;
            write_state(stdout, &sim, status, reason);
            continue;
        }
        if (strcmp(command, "run") == 0) {
            if (count == 0) {
                count = 1000;
            }
            reason = step_many(&sim, (uint64_t)count);
            status = reason == SIM_STOP_FAULT || reason == SIM_STOP_UNSUPPORTED_INSTR ? DEBUG_STATUS_FAILED : DEBUG_STATUS_STOPPED;
            write_state(stdout, &sim, status, reason);
            continue;
        }
        status = DEBUG_STATUS_FAILED;
        write_state(stdout, &sim, status, SIM_STOP_FAULT);
    }

    sim_destroy(&sim);
    return 0;
}

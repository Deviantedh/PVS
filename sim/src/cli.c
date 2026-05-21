#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim/sim.h"

enum {
    CLI_DEFAULT_MAX_STEPS = 100000u
};

static const char *stop_reason_name(sim_stop_reason_t reason) {
    switch (reason) {
    case SIM_STOP_NONE:
        return "none";
    case SIM_STOP_BREAK:
        return "break";
    case SIM_STOP_MAX_INSTRUCTIONS:
        return "max_steps";
    case SIM_STOP_UNSUPPORTED_INSTR:
        return "unsupported_instruction";
    case SIM_STOP_FAULT:
        return "fault";
    default:
        return "unknown";
    }
}

static const char *bus_status_name(bus_status_t status) {
    switch (status) {
    case BUS_STATUS_OK:
        return "ok";
    case BUS_STATUS_UNMAPPED:
        return "unmapped";
    case BUS_STATUS_UNALIGNED:
        return "unaligned";
    case BUS_STATUS_BAD_ARGUMENT:
        return "bad_argument";
    default:
        return "unknown";
    }
}

static const char *bus_access_name(bus_access_type_t access) {
    switch (access) {
    case BUS_ACCESS_READ:
        return "read";
    case BUS_ACCESS_WRITE:
        return "write";
    default:
        return "unknown";
    }
}

static void print_usage(const char *argv0) {
    fprintf(stderr, "usage: %s <firmware.bin> [--max-steps N]\n", argv0);
}

static int parse_u64(const char *text, uint64_t *value) {
    char *end = NULL;
    unsigned long long parsed;

    if (text == NULL || value == NULL || text[0] == '\0') {
        return -1;
    }

    errno = 0;
    parsed = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    *value = (uint64_t)parsed;
    return 0;
}

static int parse_args(int argc, char **argv, const char **firmware_path, uint64_t *max_steps) {
    *firmware_path = NULL;
    *max_steps = CLI_DEFAULT_MAX_STEPS;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            return 1;
        }

        if (strcmp(argv[i], "--max-steps") == 0) {
            if (i + 1 >= argc || parse_u64(argv[i + 1], max_steps) != 0) {
                return -1;
            }
            i++;
            continue;
        }

        if (*firmware_path != NULL) {
            return -1;
        }

        *firmware_path = argv[i];
    }

    return *firmware_path == NULL ? -1 : 0;
}

static int load_file(const char *path, uint8_t **data, size_t *size) {
    FILE *file;
    long file_size;
    uint8_t *buffer;

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

    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return -1;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }

    buffer = malloc((size_t)file_size);
    if (buffer == NULL && file_size != 0) {
        fclose(file);
        return -1;
    }

    if (file_size != 0 && fread(buffer, 1u, (size_t)file_size, file) != (size_t)file_size) {
        free(buffer);
        fclose(file);
        return -1;
    }

    fclose(file);
    *data = buffer;
    *size = (size_t)file_size;
    return 0;
}

static void print_summary(const sim_t *sim, sim_stop_reason_t reason, uint64_t max_steps) {
    printf("stop_reason: %s\n", stop_reason_name(reason));
    printf("instructions: %llu\n", (unsigned long long)sim->cpu.instr_count);
    printf("max_steps: %llu\n", (unsigned long long)max_steps);
    printf("pc: 0x%08X\n", sim->cpu.pc);
    printf("msp: 0x%08X\n", sim->cpu.msp);

    if (reason == SIM_STOP_FAULT) {
        printf(
            "fault: status=%s access=%s addr=0x%08X width=%u\n",
            bus_status_name(sim->last_bus_result.status),
            bus_access_name(sim->last_bus_result.access),
            sim->last_bus_result.addr,
            sim->last_bus_result.width
        );
    }

    if (sim_uart_output_size(sim) != 0u) {
        printf("uart_output:\n");
        fwrite(sim_uart_output_data(sim), 1u, sim_uart_output_size(sim), stdout);
        printf("\n");
    } else {
        printf("uart_output: <empty>\n");
    }
}

int main(int argc, char **argv) {
    const char *firmware_path = NULL;
    uint64_t max_steps = 0;
    uint8_t *firmware = NULL;
    size_t firmware_size = 0;
    sim_stop_reason_t reason;
    sim_t sim;
    int parse_result = parse_args(argc, argv, &firmware_path, &max_steps);

    if (parse_result != 0) {
        print_usage(argv[0]);
        return parse_result > 0 ? 0 : 2;
    }

    if (load_file(firmware_path, &firmware, &firmware_size) != 0) {
        fprintf(stderr, "failed to read firmware: %s\n", firmware_path);
        return 1;
    }

    if (sim_init(&sim, NULL) != 0) {
        fprintf(stderr, "failed to initialize simulator\n");
        free(firmware);
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, firmware_size) != 0) {
        fprintf(stderr, "failed to load firmware into flash\n");
        sim_destroy(&sim);
        free(firmware);
        return 1;
    }

    free(firmware);

    if (sim_reset(&sim) != 0) {
        fprintf(stderr, "failed to reset simulator\n");
        print_summary(&sim, sim.stop_reason, max_steps);
        sim_destroy(&sim);
        return 1;
    }

    reason = sim_run(&sim, max_steps);
    (void)sim_drain_uart_output(&sim);
    print_summary(&sim, reason, max_steps);

    sim_destroy(&sim);
    return reason == SIM_STOP_FAULT || reason == SIM_STOP_UNSUPPORTED_INSTR ? 1 : 0;
}

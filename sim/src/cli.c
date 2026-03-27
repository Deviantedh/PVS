#include <stdio.h>

#include "sim/sim.h"

int main(void) {
    sim_t sim;
    uint8_t firmware[] = {
        0x00, 0x50, 0x00, 0x20,
        0x09, 0x00, 0x00, 0x08,
        0x00, 0xBF
    };

    if (sim_init(&sim, NULL) != 0) {
        fprintf(stderr, "failed to initialize simulator\n");
        return 1;
    }

    if (sim_load_firmware(&sim, firmware, sizeof(firmware)) != 0) {
        fprintf(stderr, "failed to load firmware\n");
        sim_destroy(&sim);
        return 1;
    }

    if (sim_reset(&sim) != 0) {
        fprintf(stderr, "failed to reset simulator\n");
        sim_destroy(&sim);
        return 1;
    }

    if (sim_step(&sim) != SIM_STOP_NONE) {
        fprintf(stderr, "failed to execute first instruction\n");
        sim_destroy(&sim);
        return 1;
    }

    printf("pvs_sim_cli: simulator scaffold is initialized\n");
    sim_destroy(&sim);
    return 0;
}

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#error "cli_json_snapshot_test currently expects a POSIX test environment"
#endif

#include <unistd.h>

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

static int write_firmware(const char *path) {
    uint8_t firmware[12] = {0};
    FILE *file;

    encode_u32le(&firmware[0], 0x20000100u);
    encode_u32le(&firmware[4], 0x08000009u);
    encode_u16le(&firmware[8], 0xBF00u);
    encode_u16le(&firmware[10], 0xE7FEu);

    file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    if (fwrite(firmware, 1, sizeof(firmware), file) != sizeof(firmware)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    return 0;
}

static int file_contains(const char *path, const char *needle) {
    FILE *file = fopen(path, "rb");
    char buffer[8192];
    size_t nread;
    int found = 0;

    if (file == NULL) {
        return 0;
    }

    nread = fread(buffer, 1, sizeof(buffer) - 1u, file);
    buffer[nread] = '\0';
    found = strstr(buffer, needle) != NULL;
    fclose(file);
    return found;
}

int main(int argc, char **argv) {
    char tmp_template[] = "/tmp/pvs-cli-json-XXXXXX";
    char firmware_path[256];
    char result_path[256];
    char command[1024];
    char *tmp_dir;
    int status;
    int failed = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <pvs_sim_cli>\n", argv[0]);
        return 1;
    }

    tmp_dir = mkdtemp(tmp_template);
    if (tmp_dir == NULL) {
        return 1;
    }

    snprintf(firmware_path, sizeof(firmware_path), "%s/firmware.bin", tmp_dir);
    snprintf(result_path, sizeof(result_path), "%s/result.json", tmp_dir);
    if (write_firmware(firmware_path) != 0) {
        return 1;
    }

    snprintf(command,
        sizeof(command),
        "\"%s\" --firmware \"%s\" --max-instr 4 --json-result \"%s\"",
        argv[1],
        firmware_path,
        result_path);
    status = system(command);
    if (status != 0) {
        fprintf(stderr, "CLI command failed: %s\n", command);
        return 1;
    }

    failed |= !file_contains(result_path, "\"cpu\"");
    failed |= !file_contains(result_path, "\"pc\"");
    failed |= !file_contains(result_path, "\"msp\"");
    failed |= !file_contains(result_path, "\"peripherals\"");
    failed |= !file_contains(result_path, "\"tim2\"");
    failed |= !file_contains(result_path, "\"usart1\"");
    failed |= !file_contains(result_path, "\"nvic\"");
    failed |= !file_contains(result_path, "\"pins\":[");
    failed |= !file_contains(result_path, "\"name\":\"PA2\"");
    failed |= !file_contains(result_path, "\"port\":\"A\"");
    failed |= !file_contains(result_path, "\"mode\":\"unknown\"");
    failed |= !file_contains(result_path, "\"level\":null");
    failed |= !file_contains(result_path, "\"label\":\"USART1_TX\"");

    remove(firmware_path);
    remove(result_path);
    rmdir(tmp_dir);
    return failed ? 1 : 0;
}

/* C is such a lovely language */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    const uint8_t poly = 0x39;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ poly;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

void debug_dump_bytes(const char *name, const uint8_t *buffer, size_t length)
{
#if DEBUG
    printf("%s\n", name);

    for (size_t i = 0; i < length; i += 16) {
        printf("%08zx  ", i);

        for (size_t j = 0; j < 16; j++) {
            if (i + j < length) {
                printf("%02x ", buffer[i + j]);
            } else {
                printf("   "); // padding for incomplete lines
            }
            if (j == 7) printf(" ");
        }

        printf("\n");
    }
#endif
}

static bool read_file_handle(FILE *file, void **out, size_t *size)
{
    const size_t chunk_size = 1024;

    void *buffer = NULL;
    size_t buffer_size = 0;
    size_t written = 0;

    for (;;) {
        size_t space = buffer_size - written;
        if (space < chunk_size) {
            void *new_buf = realloc(buffer, buffer_size + chunk_size);
            if (!new_buf) {
                free(buffer);
                return false;
            }
            buffer = new_buf;
            buffer_size += chunk_size;
            continue;
        }

        ssize_t nread = fread(buffer + written, 1, chunk_size, file);
        if (nread == 0) {
            break;
        }

        written += nread;
    }

    if (ferror(file) || !written) {
        free(buffer);
        return false;
    }

    *out = buffer;
    *size = written;

    return true;
}

bool read_file(const char *filename, void **out, size_t *size)
{
    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("unable to open file '%s'!\n", filename);
        return false;
    }

    bool ok = read_file_handle(file, out, size);
    fclose(file);

    return ok;
}


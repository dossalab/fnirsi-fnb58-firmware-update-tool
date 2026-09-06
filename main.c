/*
 * Quick tool to perform DFU update on FNB58
 * Just feed it a .unf firmware file, and enjoy!
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <hidapi/hidapi.h>
#include <unistd.h>
#include <stdbool.h>

#include "dfu.h"

static void make_packet(hid_buffer_t buffer, uint8_t ep, uint32_t param,
                        const void *payload, size_t len)
{
    if (len > MAX_PACKET_PAYLOAD_SIZE) {
        len = MAX_PACKET_PAYLOAD_SIZE;
    }

    memset(buffer, 0, HID_BUFFER_SIZE);

    buffer[0] = ep;
    buffer[1] = param;
    buffer[2] = param >> 8;
    buffer[3] = param >> 16;
    buffer[4] = param >> 24;
    memcpy(buffer + 5, payload, len);

    uint8_t crc = crc8(buffer, HID_BUFFER_SIZE - 1);
    buffer[HID_BUFFER_SIZE - 1] = crc;
}

static bool listen_for_response(hid_device *hid, int timeout_ms)
{
    hid_buffer_t buffer;

    memset(buffer, 0, MAX_PACKET_PAYLOAD_SIZE);

    int n = hid_read_timeout(hid, buffer, MAX_PACKET_PAYLOAD_SIZE, timeout_ms);
    if (n <= 0) {
        printf("read error (%d)!\n", n);
        return false;
    }

    debug_dump_bytes("> response", buffer, MAX_PACKET_PAYLOAD_SIZE);
    return true;
}

static bool fw_start_update(hid_device *hid, uint16_t fw_version_code, uint32_t fw_size)
{
    // Erase takes a while, so give it a longer timeout
    const int erase_timeout_ms = 1000 * 10;

    hid_buffer_t buffer;
    uint8_t payload[6];

    payload[0] = fw_version_code;
    payload[1] = fw_version_code >> 8;
    payload[2] = fw_size;
    payload[3] = fw_size >> 8;
    payload[4] = fw_size >> 16;
    payload[5] = fw_size >> 24;

    make_packet(buffer, 0x28, sizeof(payload), payload, sizeof(payload));
    debug_dump_bytes("> write", buffer, HID_BUFFER_SIZE);

    bool ok = hid_write(hid, buffer, HID_BUFFER_SIZE) != -1;
    return ok && listen_for_response(hid, erase_timeout_ms);
}

static bool fw_upload_write_data(hid_device *hid, size_t i, void *data, size_t len)
{
    hid_buffer_t buffer;
    const int write_timeout_ms = 1000 * 10;

    // Indexes count from 1 here
    i += 1;

    uint8_t i_low = i;
    uint8_t i_high= i >> 8;

    uint32_t addr = (0x3a << 0)  // always 3a
                  | ((i % 0x32) << 8) // wraps around at 0x31
                  | (i_high << 16)
                  | (i_low << 24);

    make_packet(buffer, 0x2b, addr, data, len);
    debug_dump_bytes("> write data", buffer, HID_BUFFER_SIZE);

    bool ok = hid_write(hid, buffer, HID_BUFFER_SIZE) != -1;
    return ok && listen_for_response(hid, write_timeout_ms);
}

static bool fw_update_with_handle(hid_device *handle, void *fw, size_t fw_size,
                                  uint16_t fw_version)
{
    bool ok;

    printf("clearing flash...\n");

    ok = fw_start_update(handle, fw_version, fw_size);
    if (!ok) {
        printf("unable to start fw upload\n");
        return false;
    }

    printf("starting the write cycle...\n");

    size_t nwritten = 0;
    size_t chunk_id = 0;

    for (;;) {
        size_t remaining = fw_size - nwritten;

        if (remaining == 0) {
            break;
        }

        size_t len = (remaining < MAX_PACKET_PAYLOAD_SIZE)?
                              remaining : MAX_PACKET_PAYLOAD_SIZE;

        printf("writing %zu byte(s) at index %zu\n", len, chunk_id);

        ok = fw_upload_write_data(handle, chunk_id, fw + nwritten, len);
        if (!ok) {
            break;
        }

        nwritten += len;
        chunk_id++;
    }

    return ok;
}

static bool open_dev_and_update(void *fw, size_t fw_size, uint16_t fw_version)
{
    const uint16_t vid = 0x0483;
    const uint16_t pid = 0x0038;

    hid_device *handle = hid_open(vid, pid, NULL);
    if (!handle) {
        printf("unable to open device\n");
        return false;
    }

    bool ok = fw_update_with_handle(handle, fw, fw_size, fw_version);

    hid_close(handle);
    return ok;
}

/* Derive the version code the device expects from the firmware file name.
 *
 * The .ufn payload is fully encrypted (no header, no magic, size always a
 * multiple of the cipher block), so the file name is the only place the
 * version appears in plain form. FNIRSI's own naming maps to the code by
 * simply dropping the dots:
 *
 *   Fnb58V0.68.ufn  -> 68    (the value the original tool hardcoded)
 *   Fnb58V1.0.3.ufn -> 103   (three-component versions do exist)
 *   Fnb58V1.15.ufn  -> 115
 *
 * A candidate is a 'v'/'V' followed by a digit, then a run of digits and
 * dots. Real versions always contain a dot, so a dotted candidate wins over
 * a bare one ("v2_final" style words in a name do not hijack the result).
 */
static bool parse_version_at(const char *p, uint16_t *out, bool *had_dot)
{
    unsigned long code = 0;
    bool any_digit = false;

    *had_dot = false;

    for (const char *q = p; isdigit((unsigned char)*q) || *q == '.'; q++) {
        if (*q == '.') {
            *had_dot = true;
            continue;
        }

        code = code * 10 + (unsigned long)(*q - '0');
        any_digit = true;

        if (code > UINT16_MAX) {
            return false;
        }
    }

    if (!any_digit) {
        return false;
    }

    *out = (uint16_t)code;
    return true;
}

static bool version_from_filename(const char *path, uint16_t *out)
{
    const char *base = strrchr(path, '/');
    base = base? base + 1 : path;

    bool found = false;
    uint16_t fallback = 0;

    for (const char *p = base; *p; p++) {
        if ((*p != 'v' && *p != 'V') || !isdigit((unsigned char)p[1])) {
            continue;
        }

        uint16_t code;
        bool had_dot;

        if (!parse_version_at(p + 1, &code, &had_dot)) {
            continue;
        }

        if (had_dot) {
            *out = code;
            return true;
        }

        if (!found) {
            fallback = code;
            found = true;
        }
    }

    if (found) {
        *out = fallback;
    }

    return found;
}
int main(int argc, char **argv)
{
    const char *filename = NULL;
    bool have_override = false;
    uint16_t fw_version = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            if (i + 1 >= argc) {
                printf("--version needs a value\n");
                return 1;
            }

            char *end;
            unsigned long v = strtoul(argv[++i], &end, 10);

            if (*end || v > UINT16_MAX) {
                printf("invalid version code '%s'\n", argv[i]);
                return 1;
            }

            fw_version = (uint16_t)v;
            have_override = true;
        } else if (!filename) {
            filename = argv[i];
        } else {
            filename = NULL;
            break;
        }
    }

    if (!filename) {
        printf("usage: %s [--version <code>] <filename.ufn>\n", argv[0]);
        printf("  the version code is normally derived from the file name\n");
        printf("  (Fnb58V1.15.ufn -> 115); --version overrides that.\n");
        return 1;
    }

    void *fw;
    size_t fw_size = 0;

    bool ok = read_file(filename, &fw, &fw_size);
    if (!ok) {
        printf("unable to read fw file\n");
        return 1;
    }

    printf("'%s' read, %zu bytes\n", filename, fw_size);

    if (have_override) {
        printf("declaring firmware version code %u (from --version)\n", fw_version);
    } else if (version_from_filename(filename, &fw_version)) {
        printf("declaring firmware version code %u (from file name)\n", fw_version);
    } else {
        printf("unable to derive a version code from file name '%s'\n", filename);
        printf("pass --version <code> explicitly (e.g. 115 for V1.15)\n");
        free(fw);
        return 1;
    }

    ok = open_dev_and_update(fw, fw_size, fw_version);
    free(fw);

    if (ok) {
        printf("all done!\n");
    }

    return ok? EXIT_SUCCESS : EXIT_FAILURE;
}

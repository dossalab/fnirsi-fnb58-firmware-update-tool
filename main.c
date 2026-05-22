/*
 * Quick tool to perform DFU update on FNB58
 * Just feed it a .unf firmware file, and enjoy!
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    int result = hid_read_timeout(hid, buffer, MAX_PACKET_PAYLOAD_SIZE, timeout_ms);
    bool ok = (result > 0);
    if (!ok) {
        if (result == 0) {
            printf("read timeout!\n");
        } else {
            printf("read error!\n");
        }
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

static bool fw_update_with_handle(hid_device *handle, void *fw, size_t fw_size)
{
    // XXX:I'm not sure whether that has any effect. This is extracted from FW file name.
    const uint8_t fw_version = 68;
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

static bool open_dev_and_update(void *fw, size_t fw_size)
{
    const uint16_t vid = 0x0483;
    const uint16_t pid = 0x0038;

    hid_device *handle = hid_open(vid, pid, NULL);
    if (!handle) {
        printf("unable to open device\n");
        return false;
    }

    bool ok = fw_update_with_handle(handle, fw, fw_size);

    hid_close(handle);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: %s: <filename.ufn>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    void *fw;
    size_t fw_size = 0;

    bool ok = read_file(filename, &fw, &fw_size);
    if (!ok) {
        printf("unable to read fw file\n");
        return 1;
    }

    printf("'%s' read, %zu bytes\n", filename, fw_size);

    ok = open_dev_and_update(fw, fw_size);
    free(fw);

    if (ok) {
        printf("all done!\n");
    }

    return ok? EXIT_SUCCESS : EXIT_FAILURE;
}

# Firmware Bug Fixes Design

Date: 2026-05-21

## Overview

Three targeted bug fixes to `main.c` and `util.c`. No new features or refactoring beyond what is needed to thread the parsed `fw_version` through the call chain.

## Bug 1 — `realloc()` return not checked (`util.c:60`)

**Problem:** `realloc()` return value overwrites `buffer` before being checked. On OOM, `buffer` becomes NULL, `buffer_size` is incremented anyway, and the next `fread` call dereferences NULL. The original allocation is also leaked.

**Fix:** Use a temporary pointer:

```c
void *new_buf = realloc(buffer, buffer_size + chunk_size);
if (!new_buf) {
    free(buffer);
    return false;
}
buffer = new_buf;
buffer_size += chunk_size;
```

## Bug 2 — HID read `-1` cast to `bool` (`main.c:42`)

**Problem:** `hid_read_timeout` returns `int` (positive = bytes read, `0` = timeout, `-1` = error). Assigning directly to `bool` maps `-1` → `true`, so the error branch is unreachable on actual HID errors.

**Fix:** Capture the `int` and threshold at `> 0`:

```c
int result = hid_read_timeout(hid, buffer, MAX_PACKET_PAYLOAD_SIZE, timeout_ms);
bool ok = (result > 0);
```

## Bug 3 — Hardcoded `fw_version = 68` (`main.c:100`)

**Problem:** Version `68` only matches `Fnb58V0.68.ufn`. Any other firmware file will send the wrong version to the device.

**Fix:** Parse the minor version from the filename basename in `main()`, with a warning + fallback to `68` if parsing fails. Thread the result down as a new `uint8_t fw_version` parameter through `open_dev_and_update` and `fw_update_with_handle`.

```c
// Parsing in main():
uint8_t fw_version = 68;
const char *base = strrchr(filename, '/');
base = base ? base + 1 : filename;
if (sscanf(base, "Fnb58V%*u.%hhu", &fw_version) != 1) {
    fprintf(stderr, "warning: cannot parse fw_version from '%s', using %u\n",
            base, (unsigned)fw_version);
}
```

**Signature changes:**
- `open_dev_and_update(void *fw, size_t fw_size)` → `open_dev_and_update(void *fw, size_t fw_size, uint8_t fw_version)`
- `fw_update_with_handle(hid_device *handle, void *fw, size_t fw_size)` → `fw_update_with_handle(hid_device *handle, void *fw, size_t fw_size, uint8_t fw_version)`
- Remove `const uint8_t fw_version = 68;` from `fw_update_with_handle`

## Files Changed

- `util.c` — Bug 1 only
- `main.c` — Bugs 2 and 3

## Testing

Build with `make` and verify no compiler warnings. Manual test: run the tool against the device with a correctly-named firmware file and confirm version is parsed from the filename.

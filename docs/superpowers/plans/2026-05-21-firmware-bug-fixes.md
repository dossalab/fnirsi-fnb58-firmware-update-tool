# Firmware Bug Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix three robustness bugs in the firmware update tool: unchecked `realloc`, a bool/int cast that swallows HID errors, and a hardcoded firmware version that should be parsed from the filename.

**Architecture:** Three independent, surgical patches across two C files (`util.c` and `main.c`). Task 3 requires threading a new `uint8_t fw_version` parameter through two internal functions and adding parsing logic in `main()`. No new files, no new dependencies.

**Tech Stack:** C (C99+), hidapi, POSIX (`strrchr`), standard `sscanf`

---

### Task 1: Fix unchecked `realloc` in `util.c`

**Files:**
- Modify: `util.c:59-62`

The current code overwrites `buffer` with the `realloc` return before checking it. If `realloc` returns NULL the original allocation is lost and the next `fread` call dereferences NULL.

- [ ] **Step 1: Open `util.c` and replace lines 59–62**

Replace this block:
```c
        buffer = realloc(buffer, buffer_size + chunk_size);
        buffer_size += chunk_size;
        continue;
```

With:
```c
        void *new_buf = realloc(buffer, buffer_size + chunk_size);
        if (!new_buf) {
            free(buffer);
            return false;
        }
        buffer = new_buf;
        buffer_size += chunk_size;
        continue;
```

- [ ] **Step 2: Build and verify no warnings**

```bash
make
```

Expected: clean build, zero warnings. Fix any compiler complaints before continuing.

- [ ] **Step 3: Commit**

```bash
git add util.c
git commit -m "fix: check realloc return value in read_file_handle"
```

---

### Task 2: Fix HID read `-1` cast to `bool` in `main.c`

**Files:**
- Modify: `main.c:42`

`hid_read_timeout` returns `int`: positive = bytes read, `0` = timeout, `-1` = error. Assigning directly to `bool` maps `-1` → `true`, making the error branch unreachable on actual HID errors.

- [ ] **Step 1: Open `main.c` and replace line 42**

Replace:
```c
    bool ok = hid_read_timeout(hid, buffer, MAX_PACKET_PAYLOAD_SIZE, timeout_ms);
```

With:
```c
    int result = hid_read_timeout(hid, buffer, MAX_PACKET_PAYLOAD_SIZE, timeout_ms);
    bool ok = (result > 0);
```

The surrounding context for reference (lines 36–49):
```c
static bool listen_for_response(hid_device *hid, int timeout_ms)
{
    hid_buffer_t buffer;

    memset(buffer, 0, MAX_PACKET_PAYLOAD_SIZE);

    int result = hid_read_timeout(hid, buffer, MAX_PACKET_PAYLOAD_SIZE, timeout_ms);
    bool ok = (result > 0);
    if (!ok) {
        printf("read error!\n");
        return false;
    }

    debug_dump_bytes("> response", buffer, MAX_PACKET_PAYLOAD_SIZE);
    return true;
}
```

- [ ] **Step 2: Build and verify no warnings**

```bash
make
```

Expected: clean build, zero warnings.

- [ ] **Step 3: Commit**

```bash
git add main.c
git commit -m "fix: correctly handle hid_read_timeout error return (-1) in listen_for_response"
```

---

### Task 3: Parse `fw_version` from filename instead of hardcoding `68`

**Files:**
- Modify: `main.c` — three locations:
  1. `fw_update_with_handle` signature + body (~line 97)
  2. `open_dev_and_update` signature + body (~line 140)
  3. `main()` body (~line 157)

The filename pattern is `Fnb58V<major>.<minor>.ufn` (e.g. `Fnb58V0.68.ufn`). We parse the minor version using `sscanf` on the basename, fall back to `68` with a warning if parsing fails.

- [ ] **Step 1: Update `fw_update_with_handle` — add parameter, remove hardcoded constant**

Change the function signature and remove the hardcoded constant. Before (lines 97–101):
```c
static bool fw_update_with_handle(hid_device *handle, void *fw, size_t fw_size)
{
    // XXX:I'm not sure whether that has any effect. This is extracted from FW file name.
    const uint8_t fw_version = 68;
    bool ok;
```

After:
```c
static bool fw_update_with_handle(hid_device *handle, void *fw, size_t fw_size, uint8_t fw_version)
{
    bool ok;
```

- [ ] **Step 2: Update `open_dev_and_update` — add parameter, pass it through**

Before (lines 140–155):
```c
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
```

After:
```c
static bool open_dev_and_update(void *fw, size_t fw_size, uint8_t fw_version)
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
```

- [ ] **Step 3: Update `main()` — parse fw_version, pass to `open_dev_and_update`**

In `main()`, after the `read_file` call succeeds and before `open_dev_and_update`, insert the parsing block and update the call.

Before (lines 168–176):
```c
    bool ok = read_file(filename, &fw, &fw_size);
    if (!ok) {
        printf("unable to read fw file\n");
        return 1;
    }

    printf("'%s' read, %zu bytes\n", filename, fw_size);

    ok = open_dev_and_update(fw, fw_size);
```

After:
```c
    bool ok = read_file(filename, &fw, &fw_size);
    if (!ok) {
        printf("unable to read fw file\n");
        return 1;
    }

    printf("'%s' read, %zu bytes\n", filename, fw_size);

    uint8_t fw_version = 68;
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    if (sscanf(base, "Fnb58V%*u.%hhu", &fw_version) != 1) {
        fprintf(stderr, "warning: cannot parse fw_version from '%s', using %u\n",
                base, (unsigned)fw_version);
    }

    ok = open_dev_and_update(fw, fw_size, fw_version);
```

- [ ] **Step 4: Build and verify no warnings**

```bash
make
```

Expected: clean build, zero warnings. If you see "too few arguments to function" errors, check that both call sites (`open_dev_and_update` and `fw_update_with_handle`) were updated in Steps 1–3.

- [ ] **Step 5: Smoke-test filename parsing**

No hardware needed. Verify the parsing logic with a quick one-liner using the same `sscanf` pattern:

```bash
python3 -c "
import ctypes, subprocess
names = ['Fnb58V0.68.ufn', 'Fnb58V0.72.ufn', 'Fnb58V1.100.ufn', 'my-firmware.ufn']
for n in names:
    print(n)
"
```

Then mentally trace `sscanf(name, "Fnb58V%*u.%hhu", &fw_version)`:
- `Fnb58V0.68.ufn` → `fw_version = 68` ✓
- `Fnb58V0.72.ufn` → `fw_version = 72` ✓
- `Fnb58V1.100.ufn` → `fw_version = 100` (truncates to uint8_t → 100) ✓
- `my-firmware.ufn` → parse fails, warning printed, `fw_version = 68` ✓

- [ ] **Step 6: Commit**

```bash
git add main.c
git commit -m "fix: parse fw_version from firmware filename instead of hardcoding 68"
```

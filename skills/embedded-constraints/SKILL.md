---
name: embedded-constraints
description: >
  Working within the limits of the ESP32-S3 knob board: where memory actually runs out
  (internal SRAM, not PSRAM), why the round display is bandwidth-bound, how to lay out tasks so
  LVGL never stalls, how to size task stacks, how to make failures loud instead of silent, and
  how to measure before optimising. Use whenever firmware is designed, when something crashes
  or reboots (out-of-memory, StoreProhibited, a Guru Meditation backtrace, heap corruption, a
  stack overflow, a task watchdog) or the UI is stuck, when data comes out truncated or empty,
  when a feature is "too slow", or when an assistant is about to write code for this board.
  Includes the AI-specific pitfalls seen on real projects (invented pins and component names,
  LVGL 8 API in an LVGL 9 project, guessed panel init sequences, non-existent CONFIG symbols)
  and a verification routine to run before trusting generated code.
---

# Embedded constraints on the ESP32-S3 knob board

The reference firmware (a production recorder on this exact board) hit every limit below at
least once. The numbers are from its logs, not from datasheets.

## 1. Memory: internal SRAM is the scarce resource

| Pool | Size | Who needs it |
|---|---|---|
| Internal SRAM | ~512 KB total, ~300 KB usable heap after Wi-Fi + Bluetooth stacks and task stacks | DMA buffers, lwIP, mbedTLS, FreeRTOS stacks, anything latency-critical |
| Octal PSRAM | 8 MB at 80 MHz | Everything that tolerates ~2× latency: LVGL object pool, big BSS, file chunks, audio rings |
| Flash | 16 MB | App 9 MB + spiffs 4 MB in the starter partition table |

Rules:

- **Put big, cold things in PSRAM.** `heap_caps_malloc(n, MALLOC_CAP_SPIRAM)` for buffers over a
  few KB. Let large static arrays go there too: `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`
  plus `EXT_RAM_BSS_ATTR` (or LVGL's `LV_ATTRIBUTE_LARGE_RAM_ARRAY`).
- **Keep DMA and hot things internal.** The LVGL draw buffers (`buff_spiram = false`,
  `buff_dma = true`), SD/I2S DMA descriptors, and anything touched every millisecond.
- **The lesson that cost a day:** LVGL's 128 KB object pool was in internal SRAM. During an
  HTTPS upload the log showed **8 KB free, largest block 1.5 KB** and lwIP's TCP send buffer
  could not allocate. Moving the pool to PSRAM freed it. Networking and the UI compete for the
  same internal heap; budget them together.
- **No malloc in hot paths.** Allocate at init, reuse. One shared chunk buffer for streaming, not
  one per request.
- **Stacks are internal RAM.** Size them from a measurement, not a guess: §3.
- **Someone owns the total.** Keep a small table in the project notes of who gets how much
  internal RAM and PSRAM (LVGL pool, draw buffers, lwIP windows, TLS, each task stack, DMA) and
  update it when a line changes. The pool fix above starved the network because nobody added
  the lines up.
- **Peak RAM must not depend on input size.** Anything that grows with recording length, file
  size or list length is processed in fixed chunks, end to end: sensor to storage as well as
  storage to network. The reference firmware made this mistake three times (capture silently
  stopped at 30 s when its buffer filled; uploads over 6 MB were skipped; 15 MB could not be
  allocated at all) before the rule was written down.

## 2. The display is bandwidth-bound, not RAM-bound

360×360×2 B = 253 KB per frame over a QSPI bus at 80 MHz. A full-frame redraw lands around
25–40 fps on a good day; with other SPI traffic it drops.

- **Draw buffers:** 40 rows × 360 × 2 B ≈ 28.8 KB, two of them, internal DMA RAM, so LVGL renders
  the next dirty region while the previous one flushes. Bigger buffers do not help; PSRAM
  buffers make it worse.
- **Partial rendering.** Design for localized change: an arc that moves, a number that updates,
  a ring that pulses. Never animate the whole background.
- **No TE line on this board** → no vsync → tearing on full-screen motion. Live with it or keep
  motion local.
- **Compile at `-O2`** (`CONFIG_COMPILER_OPTIMIZATION_PERF=y`) and run at **240 MHz**. Both are
  measurable on software-rendered LVGL.
- **Round canvas.** The corners are clipped; the usable circle is 340 px. Radial layouts beat
  grids.

## 3. Tasks: size the stacks, never block the LVGL task

**Stacks.** The reference firmware overflowed a stack four times, and every time the symptom
was somewhere else: heap corruption, a double-free assert, a boot loop that damaged the SD
card's directory.

- **Start from the workload, then measure.** 4 KB for a worker that only computes; 6 KB for
  `esp_http_client` alone (the template's upload task); **16 KB once one task does HTTP + cJSON
  + LVGL calls**, which overflowed 8 KB twice in the reference firmware. Log
  `uxTaskGetStackHighWaterMark(NULL)` (bytes never used) when the task finishes its biggest job
  and size from that, leaving about a quarter spare.
- **No large buffers on the stack.** Anything over a few hundred bytes is `static` or
  heap-allocated. A 4 KB array inside an `httpd` handler (4 KB stack) or a 4 KB task is an
  overflow by construction.
- **Re-measure when a task takes on new work.** Adding a JSON parse, a `snprintf` of a float or
  a UI call to an existing task is a stack change. The same bug came back in a new task that
  inherited the old one's workload.
- **Leave the checks on.** The template's `sdkconfig.defaults` sets
  `CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=y` (traps the write that runs off the stack, so the
  backtrace points at the cause) and `CONFIG_HEAP_POISONING_LIGHT=y` (catches writes past a
  heap block). The FreeRTOS canary check is on by default in IDF but only fires at the next
  context switch, after the damage.

**The LVGL task.**

`esp_lvgl_port` owns the LVGL tick, timer handler and flush, and a mutex. Everything that
touches LVGL objects runs either inside that task (timers, event callbacks) or under
`ui_lock(timeout)` / `ui_unlock()` from another task.

- **No I/O, no TLS, no SD, no `vTaskDelay` while holding the LVGL lock.** The reference firmware
  hit the task watchdog when a task spun in `lv_malloc` while the port task held the lock
  during an upload.
- **Nothing slow in an event handler either, CPU work included.** PBKDF2 inside a PIN-entry
  handler froze the screen for 3 s and the last digit never painted. Crypto, hashing, file and
  network work go to a worker task that posts the result back.
- **Never take the lock with a zero timeout and skip the update on failure.** A try-lock that
  gives up under contention left the reference firmware's screen on "sending…" after the upload
  had finished. Wait a bounded time (`ui_lock(100)` or more) or publish through an atomic or a
  queue that the LVGL task drains, as the template does; if the lock still fails, log it.
- **State created on first use and shared between tasks needs a mutex from the start.** Two
  tasks hitting "if not created yet, create" at boot minted two different IDs for one SD card.
- **One network task**, its own stack, results posted back to the UI through a callback or a
  queue. Strings passed to a callback are valid only during the call; copy them.
- **The knob is polled on a 3 ms `esp_timer`** and decoded by pure code; steps accumulate in an
  atomic and the LVGL task drains them. No per-detent interrupts.
- **FreeRTOS tick at 1000 Hz** so `vTaskDelay(pdMS_TO_TICKS(3))` means 3 ms.
- **Watchdogs are your friends.** Leave the task WDT on; a trip tells you which task hogged
  the CPU.

## 4. Power (only matters on battery)

- The backlight is the dominant draw; dim on idle.
- The sidecar ESP32 runs its Bluetooth stack unless reflashed: tens of mA, irrelevant on USB.
- Wi-Fi modem sleep is on by default in station mode; leave it unless latency matters.

## 5. Measure before optimising, read the device before theorising

```c
ESP_LOGI(TAG, "heap[%s]: free-int=%u min-int=%u free-psram=%u", phase,
         heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
         heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
         heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

Log that at named phases: after init, before the first TLS session, after each transfer. The
**minimum-free-internal** number is the one that decides whether a feature fits.

- `idf.py size-components` for flash and static RAM per component.
- `heap_caps_print_heap_info(MALLOC_CAP_INTERNAL)` when fragmentation is suspected (small
  "free" but tiny "largest block" is fragmentation).
- `esp_timer_get_time()` around a suspect section; log milliseconds.
- `lv_obj_get_child_count` and the LVGL memory monitor when the object pool grows.

**On any crash or reboot, the first move is the backtrace; the hypothesis comes after.** The
reference firmware spent two commits on plausible theories for a crash that moved around
between boots. Reading the panic output named the cause (the LVGL pool was exhausted) at once.

1. Capture the serial output across the crash: the `Guru Meditation Error` line, the register
   dump and the `Backtrace: 0x4200…:0x3fc… …` line. A crash that happens in a different place
   every boot is itself the clue: suspect exhausted or corrupted memory, not the code at the PC.
2. Decode the addresses against the ELF from the same build (no IDF needed on the host):

   ```bash
   docker run --rm -v "$PWD":/project -w /project espressif/idf:release-v5.3 \
     xtensa-esp32s3-elf-addr2line -pfiaC -e build/knob_starter.elf 0x42012345 0x4200abcd
   ```

   Pass the first half of each `pc:sp` pair. A host with IDF installed gets the same from
   `idf.py monitor`, which decodes as it prints.
3. Read the heap line nearest the crash (above) and the stack low-water marks (§3).
4. Only then change code, and say which line of the output the change answers.

## 6. No silent failures

The most expensive bugs in the reference firmware were the ones where nothing pointed at
them: a recording that stopped at 30 s with no error, files skipped for being too big, a JSON
parse that failed after a 200 with no log line.

- **"Didn't fit" is a failure, never a shorter answer.** Every truncation, cap or skip returns
  an error and logs one line with both sizes (`needed 912, have 512`).
- **Check the result of every bounded write and parse.** `snprintf` returning `>= cap` means
  the string was cut; `cJSON_Parse` returning NULL, a short `fread`, an NVS read that needs a
  larger buffer than you passed are all errors to handle. An 8-byte read of a 170-byte token
  made a paired device look unpaired.
- **Watch integer width at the boundary.** A `Content-Length` of 2³¹ or more went negative in
  an `int`, the body was skipped and the device answered 200 over a zeroed file. Negating
  `INT16_MIN` overflowed, so a full-scale clip read as peak 0. Sizes are `size_t` or `uint64_t`.
- **Size buffers against production data.** Dev URLs are short; presigned storage URLs are
  800–950 characters and overflowed three buffers on the first production run. Keep one
  worst-case fixture per input (the longest URL, the largest response, a 10-minute recording)
  and run it before calling a feature done; both times the reference project built such a
  check it found bugs on its first run.
- **Shared limits live in the contract** so the other side can test against them; see
  `backend-integration-patterns` §1.

The template follows this: `net_http_get()` returns -1 and logs both sizes when the body does
not fit, `serve_gate.c` answers 413 before reading an oversized body.

## 7. AI pitfalls seen on this board

These are the mistakes generated code made on real projects here. Check for them explicitly.

| Pitfall | What it looked like | Guard |
|---|---|---|
| **Invented pin numbers** | Pins from the K718C, from a Waveshare *W*-series board, from an ESPHome post for a different PCB | Pins exist only as `BOARD_*` macros in one header. Run `scripts/check-board-pins.sh`; it fails the build on any GPIO literal elsewhere. |
| **Invented components** | `espressif/knob` for the rotary (reads zero: not quadrature), `esp_lcd_gc9a01` for an ST77916 panel, `esp_lcd_touch_cst816d` (the driver is `cst816s`, it covers the D) | Only add managed components that appear in `idf_component.yml`; look the name up on components.espressif.com before adding one. |
| **LVGL 9.2 idioms on LVGL 9.6** | `lv_obj_add_flag/remove_flag` (deprecated), `CONFIG_LV_MEM_SIZE_KILOBYTES` (ignored; the pool silently falls back to 64 KB) | The lock pins lvgl 9.6.0. Use `lv_obj_set_hidden/clickable/scrollable` and `CONFIG_LV_MEM_SIZE=131072`. Treat every deprecation warning as a bug. |
| **LVGL 8 API in an LVGL 9 project** | `lv_disp_drv_t`, `lv_obj_set_style_local_*`, `lv_btn_*`, `LV_STATE_PRESSED` as a style selector arg | The template pins LVGL ^9.2. Check the symbol exists in `managed_components/lvgl__lvgl/src/` before using it. |
| **Guessed panel init sequences** | A plausible ST7789 table, or an extra `esp_lcd_panel_invert_color(true)` on top of the table's `0x21` | Use `ui_panel_init_cmds.c` unchanged. Invert once. |
| **Non-existent `CONFIG_` symbols** | `CONFIG_LVGL_...` (the real prefix is `CONFIG_LV_`), `CONFIG_SPIRAM_USE_MALLOC` variations from other IDF versions | Grep `sdkconfig` after a build; an unknown symbol in `sdkconfig.defaults` is silently ignored. |
| **Quadrature assumptions** | PCNT setup, `rotary_encoder` component | The knob is two switches. Use `knob_decode.c`. |
| **Driving GPIO0** | "Set the I2S mux to the S3 side" | Never. The on-board pull-up already selects the S3. |
| **Blocking inside LVGL callbacks** | HTTP GET inside a button event | Post to the network task; update the UI from a callback under the lock. |
| **Whole-file buffers** | `malloc(file_size)` before upload | Stream through one 16 KB chunk; see `networking-under-memory-constraints`. |
| **Buffers on the stack** | `char buf[4096];` inside a task function or an HTTP handler | `static`, or heap in PSRAM. Check the task's stack low-water mark after the change; §3. |
| **Truncate and carry on** | `snprintf` / `strncpy` / a capped read whose result is never checked; a buffer sized for the dev URL | Check the return, log both sizes, fail; §6. |
| **Theory before evidence** | Two or three confident "fixes" for a crash nobody has decoded | Backtrace first; §5. |
| **Level from raw PDM samples** | RMS straight over `i2s_channel_read` output; a "calibrated baseline" of −28 dBFS that never moves; `hp_en` set on the S3 (ESP32-only) | Block DC before any level maths and publish a peak-hold, not the latest block. See `jc3636k518-device/references/microphone.md`. |
| **"The hardware is defective"** | A long, careful chain of electrical checks ending in an RMA recommendation, where the last link was measured with a metric that could not show the effect | A negative result needs a positive control. Ask what the reading would look like if the part worked, then produce a stimulus and look at raw data. |
| **Wrong IDF version idioms** | Legacy `driver/i2c.h` instead of `driver/i2c_master.h`; `esp_netif_sntp` missing on old IDF | The template is IDF 5.3; use the new I2C master driver and LEDC APIs as written there. |

### Verification routine (run before trusting generated code)

1. `scripts/check-board-pins.sh` — no GPIO literals outside `components/board`.
2. `grep -rn "GPIO_NUM_0\b\|gpio_num = 0\b" components main` — nothing drives GPIO0.
3. `idf.py build` in Docker — zero warnings in your own components.
4. `scripts/test.sh` — host tests pass for every pure module you touched.
5. Boot log: no `E (` lines, I2C scan shows 0x15 and 0x5A, heap log shows internal
   minimum-free above your floor, stack low-water mark logged for any task whose work changed.
6. Every managed component you added appears in `dependencies.lock` with a real version.
7. Run the worst-case input (§6) through the changed path on the device, not the 10-second one.
8. Have the change reviewed with the brief "find the bug", by a second assistant session or a
   person who did not write it. In the reference project that kind of review found a race, an
   integer truncation and an undersized stack before the hardware did, and a flash-and-test
   cycle costs far more than a review.

## 8. Quick reference numbers

| Thing | Value |
|---|---|
| CPU | 2× Xtensa LX7, 240 MHz |
| Internal heap after Wi-Fi init | roughly 250–300 KB; log it |
| PSRAM | 8 MB octal, 80 MHz |
| LVGL draw buffer | 2 × 28.8 KB internal (40 rows) |
| LVGL object pool | 128 KB, in PSRAM |
| Panel clock | 80 MHz QSPI |
| Knob poll | 3 ms, debounce 2 samples (~6 ms) |
| TCP send/receive window | 16 KB each (`CONFIG_LWIP_TCP_SND_BUF_DEFAULT`, `_WND_DEFAULT`) |
| Streaming chunk | 16 KB in PSRAM |
| Task stack | 4 KB compute, 6 KB HTTP only, 16 KB HTTP + cJSON + LVGL; measure |
| FreeRTOS tick | 1000 Hz |

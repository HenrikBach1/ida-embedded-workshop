---
name: verify-on-device
description: >
  Close the loop on the real JC3636K518C knob: drive the firmware from the laptop through a
  serial test console (inject knob detents, taps, holds; read the model state) and pull a
  pixel-exact LVGL screenshot over USB. Use after every flash, in any build → flash → look
  iteration, when asked "can you see the screen", "verify on the device", "take a screenshot
  of the device", "walk through the screens", or when a change must be proven working and
  not just compiled. The starter template ships the console (components/testctl) and the
  host scripts; nothing else is needed.
---

# Verify on the device, not in your head

Two sources of truth over the USB cable, each answering a different question, and one that
needs a person.

| Source | Answers | Misses |
|---|---|---|
| `scripts/testctl.py state` | What the app model thinks (screen, counter, state) | Anything about pixels |
| `scripts/testctl.py snap` | What LVGL rendered, pixel-exact, no blur | A panel that never got the frame |
| A look at the knob | Backlight, colour, inversion, a black panel | Nothing you can script |

A bug you will meet: a layout is off by 10 px. Only the snapshot shows it. The model line
tells you which half (logic or rendering) is wrong. Neither can see the glass: a 120 px font
once blanked the panel with a clean boot log, a "ready" line and a correct snapshot. So after
a change that touches fonts, the display driver or the backlight, **ask the person at the
desk to look at the knob and say what they see**; do not report the panel as working from
the snapshot alone. If nobody is there to look, the optional `verify-with-webcam` skill puts
a camera on it.

## 1. The serial test console (in the template)

`components/testctl/` is a dev-only console on the USB Serial/JTAG port, the same port as the
log, gated by `CONFIG_KNOB_TESTCTL` (default on; turn it off for a production image). It
injects events through the app's **real** handlers under the LVGL lock, so there is no second
code path to drift. One command per line, every reply starts with `> `:

```
knob +3 | knob -2     inject detents (sign = direction)        > state=idle counter=3
tap                   short tap                                > state=active counter=3
hold                  long press                               > state=uploading counter=3
reset                 boot state                               > state=idle counter=0
state                 report only
snap                  framebuffer: SNAP BEGIN w h RGB565 / S:<base64 row>… / SNAP END
ping                  > pong
```

The host wrapper hides the protocol:

```bash
scripts/testctl.py state
scripts/testctl.py knob +3
scripts/testctl.py tap
scripts/testctl.py snap out.png        # PNG via ffmpeg
scripts/verify-screens.sh [out_dir]    # scripted walk: assert model, snap; exit 1 on mismatch
```

`testctl.py` imports pyserial. A pip-installed esptool brings it along, so it is normally
there; with esptool from pipx or a second Python it is not, and the script exits with the
install command (`python3 -m pip install pyserial`). Run that yourself and carry on; it is not
a question for the person.

Rules the console follows, keep them when you extend it:

- **Inject on the LVGL task or under `ui_lock()`**: the handlers touch widgets.
- **Expose hooks, not internals**: `app_inject_knob/tap/hold/reset` and `app_state_line` in
  `app.h`. Add a hook per new input; never let `testctl` reach into the app's statics.
- **Replies are one line, machine-readable, prefixed `> `**, so a test can grep them out of
  the interleaved log.
- **The snapshot is one `fwrite` per row with logging muted** (`esp_log_level_set("*", NONE)`
  around the dump). The first version lost rows to a Wi-Fi "reconnecting" line landing inside
  a base64 row.
- **Wait for animations before snapping** (`KNOB_SETTLE`, 300 ms). Forty injected detents
  under one lock start one easing animation; snap too early and the arc is half drawn.
- **Make the walk idempotent**: start with `reset`. The second run of a walk that assumed a
  fresh boot failed on carried-over state.

Only one process may hold `/dev/ttyACM0`. `testctl.py` opens it per call; a `miniterm` left
open swallows the replies.

## 2. The loop

1. Edit → `scripts/build.sh` → `scripts/flash.sh /dev/ttyACM0 app`.
2. Wait for `app: ready` (or ~4 s), then `scripts/testctl.py ping`.
3. `scripts/verify-screens.sh out/` for the scripted states; or hand-drive with `knob`/`tap`
   and `snap` per state.
4. Compare each snapshot with what the code should draw, and the model line with both. State
   plainly which source disagrees and what differs: wrong colour, missing label, clipped
   text. Say what was checked by snapshot and what a person still has to look at.
5. Extend `verify-screens.sh` whenever you add a state. A state that is not in the walk is
   not verified.
6. Change `verify-screens.sh` in the same commit as the model. Its `EXPECT` lines are the
   output of `app_state_line()`; rename a field there (`counter` becomes `birds`) and every
   step fails until the script follows. A walk nobody runs any more is how a project ends up
   with passing host tests and no proof of what is on the panel.

Six iterations is a sensible budget for a design loop; if a screen still is not right, the
plan is wrong, not the pixels.

## 3. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `no reply from /dev/ttyACM0` | Another reader holds the port; or console is not on USB (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`) |
| `corrupt row N (log noise…)` | A write slipped into the stream; snapshot code must mute logs and write rows atomically |
| Snapshot shows a half-drawn arc | Snapped mid-animation; raise `KNOB_SETTLE` |
| Walk passes once, fails on rerun | It assumes boot state; start with `reset` |
| A sensor "never responds" (mic, ADC, touch) | Check the measurement before the part. Prove the test can see the effect: fire a stimulus the board controls (the haptic motor is one for the mic) and look at raw samples, not a derived level. See `jc3636k518-device/references/microphone.md` |
| The device reboots, or `ping` stops answering | Read the serial output before changing code: capture the panic and decode the backtrace (`embedded-constraints` §5). A crash in a different place each boot points at memory (a stack, the heap, the LVGL pool), not at the line it stopped on |
| Text shows as empty boxes | The font has no glyph for that character; see `round-display-ux-navigation` §5 |
| Snapshot fine, panel black | Rendering never reached the panel: font, flush, backlight. Trust the person looking at it |

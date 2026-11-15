---
name: verify-with-webcam
description: >
  Optional, advanced: point a USB webcam at the JC3636K518C knob so the laptop can see the
  physical panel, the one thing a framebuffer screenshot cannot show (backlight off, a frame
  that never reached the glass, inverted or wrong colours). Use only when a webcam is set up
  and someone asks for "cam verify", "look at the panel with the camera", "webcam check", an
  unattended verification loop, or when the snapshot looks right and a person says the panel
  does not. Needs Linux (V4L2), ffmpeg and python3. Start with verify-on-device; most
  projects never need this one.
---

# Verify with a webcam

`verify-on-device` gives you the app's model and a pixel-exact LVGL snapshot. Both come from
inside the firmware, so both can be right while the panel is black. Normally a person
glancing at the knob closes that gap. A webcam closes it when nobody is looking: an
unattended loop, a remote session, a photo for the record.

| Source | Answers | Misses |
|---|---|---|
| `scripts/testctl.py snap` (verify-on-device) | What LVGL rendered, pixel-exact | A panel that never got the frame |
| webcam `capture.sh` (this skill) | What a human sees: backlight, colour, inversion, a black panel | Small text (it is soft) |

The bug that justified it: a 120 px font blanked the panel with a clean boot log, a "ready"
line and a correct snapshot. Only the camera caught it.

## 1. Set-up

A UVC webcam on a stand about 25 cm from the knob, in a dim room, on a Linux host
(`ls /dev/video*`). `capture.sh` and `set_exposure.py` are in this skill's `scripts/`
directory; they need `ffmpeg` and `python3`, not `v4l2-ctl`. On macOS or Windows the exposure
script does not apply: take the frame with whatever the system offers and set exposure by hand.

## 2. Capture

```bash
<skill-dir>/scripts/capture.sh out.jpg                     # then look at out.jpg
EXPOSURE=70 <skill-dir>/scripts/capture.sh out.jpg         # bright, full-colour screens
CROP=1920:1080:0:0 <skill-dir>/scripts/capture.sh full.jpg # find the panel after moving the camera
```

- **Manual exposure is mandatory.** A bright round panel in a dim room makes auto-exposure
  render a white disc. `set_exposure.py` forces manual mode through a raw V4L2 ioctl; it
  sticks until the camera is unplugged. 250 suits the dark token theme, 50–70 a full-colour
  background. White disc → lower; black frame → raise, then check the backlight line in the
  boot log.
- **Settle 1 s** after changing exposure or the previous setting leaks into the frame
  (`capture.sh` does this).
- **Crop** is the panel's position in the frame (`CROP=w:h:x:y`); re-measure with the
  full-frame capture when anything moves. `ROTATE=180` if the camera hangs upside down.
  `CAM=/dev/video2` picks another camera.
- **Describe what is actually there**: background colour, every text, its position. Never
  claim a number you cannot read; say it is unreadable and change the exposure.

## 3. In the loop

The scripted walk in the template takes a camera frame beside every snapshot when told where
the capture script is:

```bash
KNOB_CAM=<skill-dir>/scripts/capture.sh scripts/verify-screens.sh out/
# out/03-tap-active.png      the LVGL snapshot
# out/03-tap-active.cam.jpg  the panel as the camera saw it
```

Compare the camera with the snapshot, state by state, and say plainly which one disagrees
and how: wrong colour, missing label, black panel. When they disagree, the camera is right
about what a person sees.

## 4. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `no camera at /dev/video0` | Another index (`ls /dev/video*`, set `CAM=`), or the camera is in use by another program |
| `Invalid too big or non positive size for width` | The default crop assumes a 1920×1080 frame and this camera delivers less. Take the whole frame with `CROP=iw:ih:0:0`, then measure the panel |
| White disc / black frame | Exposure too high / too low, or the backlight is off |
| Panel off-centre or cut | Re-measure `CROP` from a full-frame capture |
| Snapshot fine, camera black | Rendering never reached the panel: font, flush, backlight. Trust the camera |
| Colours look washed out | Exposure too high for a bright screen; try 50–70 |

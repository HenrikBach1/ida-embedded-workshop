#!/usr/bin/env bash
# Walk the sound detector through its states over the serial test console (components/testctl)
# and record what the device shows: the model line (asserted) and the LVGL framebuffer (snap);
# with a camera script, the webcam too. One directory of evidence per run; exits 1 on a
# mismatch. Extend the walk when you add a state; its EXPECT lines are app_state_line()'s output.
#   scripts/verify-screens.sh [out_dir]
#   KNOB_CAM=/path/to/verify-with-webcam/scripts/capture.sh # optional webcam capture per step
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/build/verify}"
TC="$ROOT/scripts/testctl.py"
CAM="${KNOB_CAM:-}"
mkdir -p "$OUT"
fail=0

step() {   # step <name> <command...> ; EXPECT holds the expected reply substring
  local name="$1"; shift
  local reply; reply="$(python3 "$TC" "$@")" || { echo "FAIL $name: $reply"; fail=1; return; }
  if [[ "$reply" != *"$EXPECT"* ]]; then echo "FAIL $name: got '$reply', want '$EXPECT'"; fail=1; else echo "ok   $name: $reply"; fi
  # A failed snapshot is a failed step with its reason shown, never a silent exit (set -e).
  local shot; shot="$(python3 "$TC" snap "$OUT/$name.png" 2>&1)" || { echo "FAIL $name: snapshot: $shot"; fail=1; }
  if [ -n "$CAM" ] && [ -x "$CAM" ]; then "$CAM" "$OUT/$name.cam.jpg" >/dev/null; fi
}

wait_learned() {   # the room is learned for 5 s on a first boot and after "relearn"
  for _ in $(seq 1 40); do
    [[ "$(python3 "$TC" state)" == *"cal=ok"* ]] && return 0
    sleep 0.25
  done
  echo "FAIL: still learning the room after 10 s"; fail=1
}

python3 "$TC" ping >/dev/null || { echo "device not answering on the test console"; exit 1; }
wait_learned
# The microphone is live: a real sound during the walk would change the counts. Ignore it for
# the duration and switch it back on however the script ends.
trap 'python3 "$TC" listen 1 >/dev/null || echo "WARNING: could not re-enable listening"' EXIT
python3 "$TC" listen 0 >/dev/null
EXPECT="screen=home count=0 sens=5 cal=ok dim=0"      step 00-reset          reset
EXPECT="screen=home count=1 sens=5"                   step 01-one-yellow     loud 2
EXPECT="screen=home count=2 sens=5"                   step 02-few-orange     loud 4
python3 "$TC" loud 1 >/dev/null; python3 "$TC" loud 3 >/dev/null
EXPECT="screen=home count=5 sens=5"                   step 03-five-red       loud 5
EXPECT="screen=events count=5"                        step 04-events         knob +1
EXPECT="screen=settings count=5 sens=5"               step 05-settings       knob +1
EXPECT="screen=settings count=5 sens=9"               step 06-sens-9         sens 9
EXPECT="screen=settings count=5 sens=10"              step 07-sens-clamped   sens 42
EXPECT="screen=home count=5"                          step 08-wraps-home     knob +1
EXPECT="screen=settings count=5"                      step 09-wraps-back     knob -1
EXPECT="screen=settings count=0 sens=10"              step 10-cleared        clear
EXPECT="screen=events count=0"                        step 11-events-empty   knob -1
for i in $(seq 1 24); do python3 "$TC" loud $((i % 5 + 1)) >/dev/null; done
EXPECT="screen=events count=25"                       step 12-events-full    loud 5
EXPECT="screen=home count=25 sens=10 cal=ok dim=1"    step 13-idle-dimmed    idle
EXPECT="screen=events count=25 sens=10 cal=ok dim=0"  step 14-knob-wakes     knob +1
python3 "$TC" listen 1 >/dev/null                    # learning needs the microphone
EXPECT="screen=home count=25 sens=10 cal=learning"    step 15-relearning     relearn
wait_learned
python3 "$TC" listen 0 >/dev/null
EXPECT="screen=home count=0 sens=5 cal=ok dim=0"      step 16-reset-again    reset
EXPECT="listen=1"                                     step 17-listening      listen 1

echo; echo "evidence in $OUT"; ls "$OUT"
[ "$fail" -eq 0 ] && echo "ALL SCREEN CHECKS PASSED" || { echo "SCREEN CHECKS FAILED"; exit 1; }

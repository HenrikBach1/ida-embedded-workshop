#pragma once
// app — the sound detector: learns the room's normal level, counts loud moments, shows them on
// three screens (home, last 20, settings). The only component that knows all the others.
// Depends on ui, theme, haptic, net, mic, sound, settings; nothing depends on app except the
// dev-only test console.
// Threading: everything LVGL happens on the LVGL task. The microphone task runs the detector and
// reports through a queue and atomics that a 100 ms LVGL timer reads; it never touches a widget.
#include <stdbool.h>
#include <stddef.h>

// Build the screens, wire knob + touch, start the microphone. Call last in app_main, after
// settings_init(), ui_init(), haptic_init() and net_time_init().
void app_init(void);

// Test hooks (components/testctl): they run the SAME handlers hardware input runs. The caller
// holds ui_lock(). One per input; the console never reaches into this component's statics.
void app_inject_knob(int steps);        // sign = direction, |steps| detents (max 100). As with the
                                        // real knob, detents arriving during a slide are dropped.
void app_inject_loud(int strength);     // a loud moment of strength 1..5, as the detector reports it
void app_inject_sens(int value);        // move the sensitivity slider to 1..10
void app_inject_clear(void);            // the "hold to clear" button
void app_inject_relearn(void);          // the "relearn" button
void app_inject_idle(void);             // what 20 s without input does: home + dim
void app_inject_listen(int on);         // 0 = ignore the microphone (scripted walks), 1 = listen
void app_inject_reset(void);            // empty list, default sensitivity, home, full brightness

// One-line reports for the console. Each returns false (and logs both sizes) if buf was too
// small; buf then holds an empty string.
bool app_state_line(char *buf, size_t cap);   // "screen=home count=0 sens=5 cal=ok dim=0 listen=1"
// "level=-74.20 peak=-71.05 base=-75.00 thr=-57.00" in dBFS; peak = loudest 10 ms block since
// the previous call.
bool app_mic_line(char *buf, size_t cap);

// Microphone self-test with nobody in the room: measures the quiet level, fires the haptic motor
// and measures again. Blocks for about a second: call it WITHOUT the LVGL lock, from a task that
// may sleep. Writes "quiet=-74.10 click=-45.30 delta=28.80" (dB).
bool app_selftest_line(char *buf, size_t cap);

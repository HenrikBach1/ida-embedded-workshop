# JC3636K518C — the PDM microphone

MSM261D4030H1CPM MEMS mic, PDM, mono. Clock on `BOARD_MIC_CLK` (GPIO45, an **output** from the
S3), data on `BOARD_MIC_DIN` (GPIO46, input). Everything below was measured on one real unit
with ESP-IDF 5.3; where something was not measured it says so.

## Known-good configuration

```c
i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
i2s_pdm_rx_config_t cfg = {
    .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(24000),
    .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = { .clk = BOARD_MIC_CLK, .din = BOARD_MIC_DIN, .invert_flags = { .clk_inv = false } },
};
cfg.slot_cfg.slot_mask = I2S_PDM_SLOT_LEFT;
```

- 16, 24 and 48 kHz all capture sound. With the IDF default (`DSR_8S`) the PDM clock is
  sample rate × 64: 1.024 / 1.536 / 3.072 MHz.
- Read mono from the LEFT slot. An earlier firmware that read both slots as stereo reported a
  level pinned near −3.5 dBFS (one slot at full scale).
- Drop the first ~200 ms after enabling the channel: the stream starts with a transient large
  enough to look like a loud sound.

## The trap: the stream carries a large DC offset

The ESP32-S3 PDM decimator has **no high-pass filter** (`hp_en` exists only on the original
ESP32). The mic's idle stream is not centred on zero:

| Sample rate | Mean of raw samples (LSB, 16-bit) |
|---|---|
| 16 kHz | ≈ +1450 |
| 24 kHz | ≈ +1230 … +1300 |
| 48 kHz | ≈ +1050 |

That is about −28 dBFS of DC. **RMS over raw samples reads −28 dBFS in a silent room and
barely moves for real sound**: speech adds on the order of 100 LSB, which changes that RMS by
hundredths of a dB; a loud clap by 1–2 dB. A level meter, a threshold detector or a
"calibrated baseline" built on raw RMS looks exactly like a dead microphone. It was diagnosed
as one, down to an RMA recommendation, before anyone subtracted the mean.

Always remove DC before measuring level. A one-pole blocker with its state carried across
blocks is enough:

```c
// y[n] = x[n] − x[n−1] + a·y[n−1],  a = 1 − 2π·fc/fs   (fc = 150 Hz at 24 kHz → a ≈ 0.961)
if (!primed) { x1 = buf[0]; y = 0; primed = true; }   // prime, or the DC step rings through
for (i = 0; i < n; i++) { y = buf[i] - x1 + a * y; x1 = buf[i]; acc += y * y; }
```

## What a healthy mic looks like (24 kHz, DC removed)

| Condition | Level |
|---|---|
| Quiet room | 5–13 LSB stdev, 25–55 LSB peak-to-peak, ≈ −75 dBFS after a 150 Hz blocker |
| Haptic click (`haptic_click()`, strong click effect) | up to ~900 LSB peak-to-peak, ≈ −46 dBFS, spectral peak near 200 Hz |

A raw capture with a mean around +1200, a standard deviation of 10–30 and no sign changes is
a **healthy idle mic**, not a fault signature.

## Two more ways to miss sound that is there

- **Sample every block.** An app that ticks every 100 ms and reads "the latest 10 ms level"
  looks at one block in ten; a clap falls between two samples. Publish the loudest block since
  the last read (a 120 ms peak-hold covers a 100 ms tick).
- **The mic hears the board's own haptic motor**, about 28 dB above a quiet room. A sound
  detector that clicks on its events must ignore the mic for ~0.4 s after each click (click
  length plus the peak-hold), or re-arm trips itself and every event is scored on its own
  click.

## Testing the mic with nobody in the room

The board has no speaker, so there is nothing to play a tone with, but the haptic motor is a
stimulus firmware can fire on its own. Capture raw samples around a click, subtract the mean,
and compare peak-to-peak with a quiet capture. A 20× jump proves the whole chain from MEMS
element to PCM buffer. It does not prove the acoustic port is open to the room: part of that
signal travels through the PCB. Airborne sound (a clap, speech) still needs a person, and on
this unit it has not been measured yet.

Do not accept "sample value > 500" style checks, from a vendor demo or from generated code:
the DC offset alone passes them.

## Probing the pins without breaking them

Learned while proving the clock really reached the mic:

- `gpio_set_direction()` with an output mode re-routes the pad to plain GPIO output and
  **disconnects the I2S clock** from it. To read back a pad a peripheral is driving, enable
  only the input path (`gpio_ll_input_enable`).
- The fourth argument of `esp_rom_gpio_connect_out_signal()` is output-enable *invert*, not
  "enable".
- Each peripheral input signal has exactly one source pad;
  `esp_rom_gpio_connect_in_signal()` re-points it. Pointing the I2S data input at the clock
  pad is a quick loopback test of the receive path.
- `i2s_channel_init_pdm_rx_mode()` needs a freshly created channel. To change rate or slot at
  runtime, delete and re-create the channel with the reader task parked, and give
  `i2s_channel_read` a finite timeout so the task can park.
- Software polling of a ~1.5 MHz clock aliases to random bits: thousands of transitions per
  4000 polls means "toggling", nothing more. It does not measure frequency.

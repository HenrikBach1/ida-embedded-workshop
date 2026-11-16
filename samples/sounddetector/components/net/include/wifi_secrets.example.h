#pragma once
// Copy this file to `wifi_secrets.h` (gitignored — never commit the real one) and fill it in.
// Credentials saved later with net_wifi_join() live in NVS and override these at boot.
// Wi-Fi is optional: without it the detector works and shows time since power-on instead of
// clock time.

// The ESP32-S3 radio is 2.4 GHz only. If your router has separate names for 2.4 and 5 GHz, use
// the 2.4 GHz one.
#define KNOB_WIFI_SSID      "your-ssid"
#define KNOB_WIFI_PASS      "your-password"

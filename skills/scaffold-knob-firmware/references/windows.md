# Windows quick start (PowerShell)

The template's `scripts/*.sh` are written for Linux and macOS: they assume `/dev/ttyACM0`,
`lsusb`, `id -u` and a host `gcc`. On Windows, skip them and run the commands below from
PowerShell in the project folder. Same toolchain, same result: Docker builds, the host flashes.

Everything here was run on Windows 11 with Docker Desktop (WSL 2 backend) and esptool 5.6,
except the one line marked untested.

## Where the time goes

| Step                                             | Measured         | Note                                                                          |
| ------------------------------------------------ | ---------------- | ----------------------------------------------------------------------------- |
| First build, `build/` on the Windows folder      | more than 25 min | The slow path. Measured once, with a firmware backup running at the same time |
| First build, `build/` on a Docker volume (below) | 1 min 30 s       | Clean, including the managed-component download                               |
| Rebuild with nothing changed                     | 3 s              |                                                                               |
| Flash the app image                              | about 20 s       |                                                                               |
| Stock firmware backup                            | 30 min           | Optional, see step 4                                                          |

The one rule: **never let `idf.py` write its build tree into a Windows folder mounted into the
container.** Keep the source on Windows, put the build tree and the managed components on named
Docker volumes, and copy the three `.bin` files back out.

## 0. Prerequisites

```powershell
docker pull espressif/idf:release-v5.3      # ~4.5 GB, do it before the workshop
python -m pip install esptool               # brings pyserial, which scripts/testctl.py uses
python -m esptool version
```

Docker Desktop must be running. No `gcc`, no `lsusb`, no native ESP-IDF, no WSL shell needed.
pyserial is a dependency of esptool, so it needs no separate install as long as `testctl.py`
runs under the same `python` that esptool was installed into.
If `python` is not found, try `python3` or `py`.

## 1. Find the board

```powershell
$port = (Get-CimInstance Win32_SerialPort |
         Where-Object PNPDeviceID -match 'VID_303A&PID_1001').DeviceID
$port        # e.g. COM5
```

Empty? Look at what is plugged in:

```powershell
Get-PnpDevice -PresentOnly | Where-Object InstanceId -match 'VID_303A|VID_1A86' |
  Format-Table FriendlyName, InstanceId
```

`VID_1A86` is the sidecar's CH340: flip the USB-C plug. Nothing at all: charge-only cable, or
hold BOOT while plugging in. Windows 10 may need the Espressif USB JTAG/serial driver (Zadig);
Windows 11 did not.

## 2. Copy the template

```powershell
Copy-Item -Recurse <plugin-root>\templates\knob-starter .\my-knob
cd .\my-knob
Copy-Item components\net\include\wifi_secrets.example.h components\net\include\wifi_secrets.h
```

Edit `wifi_secrets.h` (a 2.4 GHz network). It is gitignored.

## 3. Build

```powershell
$copy = 'mkdir -p build/bootloader build/partition_table && ' +
        'cp /build/knob_starter.bin build/ && ' +
        'cp /build/bootloader/bootloader.bin build/bootloader/ && ' +
        'cp /build/partition_table/partition-table.bin build/partition_table/'
docker run --rm `
  -v "${PWD}:/project" `
  -v knob-build:/build `
  -v knob-mc:/project/managed_components `
  -w /project -e HOME=/tmp `
  espressif/idf:release-v5.3 sh -c "idf.py -B /build build && $copy"
```

- `-B /build` puts the build tree on the `knob-build` volume; `knob-mc` holds the downloaded
  components. The host's `build\` ends up with only the three images, at the same relative
  paths the Linux scripts use.
- Other `idf.py` actions: replace `build` in the same command (`idf.py -B /build size`).
- Clean build: `docker volume rm knob-build knob-mc`.
- A second project needs its own volume names, or the two will overwrite each other.
- `managed_components\` on the host stays empty. To read a component's source (an LVGL 9
  name, for instance), look inside the container:
  `docker run --rm -v knob-mc:/mc espressif/idf:release-v5.3 ls /mc`.

## 4. Optional: back up the stock firmware (about 30 minutes)

The board ships with a vendor demo, and the first flash erases it. Reading 16 MB back with
`--no-stub` ran at 75 kbit/s, close to 30 minutes. The workshop never returns to the demo, so
**the default is to skip this.** Do it only if you want the factory image back one day, and do
it before step 5:

```powershell
python -m esptool --chip esp32s3 --port $port --no-stub read-flash 0 0x1000000 stock.bin
```

The port is busy for the whole read; the build in step 3 can run meanwhile.

## 5. Flash

First flash, all three images:

```powershell
python -m esptool --chip esp32s3 --port $port --no-stub `
  --before default-reset --after hard-reset write-flash `
  --flash-mode dio --flash-size 16MB --flash-freq 80m `
  0x0     build\bootloader\bootloader.bin `
  0x8000  build\partition_table\partition-table.bin `
  0x10000 build\knob_starter.bin
```

Later iterations, app only: the same command with just the `0x10000 build\knob_starter.bin`
pair. Success ends with `Hash of data verified.` and `Hard resetting via RTS pin...`.

esptool 5 spells commands and options with dashes (`write-flash`, `--flash-mode`); the Linux
recipe in the board skill shows the older underscore spelling. `--no-stub` is mandatory on
both.

## 6. Drive it from the laptop

```powershell
$env:KNOB_PORT = $port
python scripts\testctl.py ping            # pong
python scripts\testctl.py knob +3         # state=idle counter=3
python scripts\testctl.py snap shot.png   # pixel-exact LVGL screenshot
```

The first `snap` after a boot failed twice with `corrupt row N (log noise in the stream?)`
while the firmware was still logging. Run it again; the second attempt worked both times.

Serial monitor (untested here, stock pyserial): `python -m serial.tools.miniterm $port 115200`.
Close it before flashing or using `testctl.py`; only one program can hold the COM port.

## 7. Host tests without gcc

The IDF image has a host `gcc`, so the Unity tests run in the same container:

```powershell
docker run --rm -v "${PWD}:/project" -w /project espressif/idf:release-v5.3 `
  sh -c "sed -i 's/\r$//' scripts/*.sh && bash scripts/test.sh"
```

Expected last line: `ALL HOST TESTS PASSED`. The `sed` strips Windows line endings: a checkout
with `core.autocrlf=true` gives the `.sh` files CRLF, and bash then fails with
`set: pipefail: invalid option name`. It is a no-op on files that are already LF.

## Symptoms

| Symptom                                            | Cause                                                                      |
| -------------------------------------------------- | -------------------------------------------------------------------------- |
| First build runs for tens of minutes               | The build tree is on the Windows folder; use the volumes in step 3         |
| `no build/knob_starter.bin` or stale image flashed | The copy-out at the end of step 3 did not run (the build failed before it) |
| `set: pipefail: invalid option name`               | CRLF in a `.sh` file; see step 7                                           |
| `could not open port 'COM5'` / access denied       | A serial monitor or another tool holds the port                            |
| `testctl.py`: no reply from `/dev/ttyACM0`         | `$env:KNOB_PORT` is not set in this shell                                  |
| `testctl.py`: `pyserial is missing for this Python` | esptool lives in another Python or in pipx; run the command it prints      |
| `scripts/verify-screens.sh`                        | Not tried on Windows; run the `testctl.py` verbs it wraps by hand          |

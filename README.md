# SmallTV Mac monitor

A GeekMagic SmallTV-Ultra (240×240) as a Wi-Fi display for one Mac. The Mac runs `macmetrics`, a small daemon that publishes `GET /metrics`. The cube fetches that JSON about once a second and draws it.

Validated on the machine this repo was built on: MacBook Pro, Mac17,6, Apple M5 Max, 128 GB. The cube's USB-C cable only supplies power. There is no serial port on the Mac.

The build story, with photos and a step-by-step setup walkthrough: [I Turned a Tiny Wi-Fi Cube Into a Stats Display for My Mac Studios](https://www.kevin.md/smalltv-mac-stats-cube.md).

The build story, with photos and a step-by-step setup walkthrough: [kevin.md/cube](https://kevin.md/cube).

## Where to buy

- GeekMagic SmallTV-Ultra: [AliExpress](https://kevin.md/go/smalltv-ultra) (around $10–15 on sale), or a generic-branded [Amazon listing](https://kevin.md/go/smalltv-ultra-amazon) that looks identical (unverified; make sure it says Ultra, since the Pro uses an ESP32 and won't run this firmware).
- Optional: a short male-to-male USB-C adapter ([AliExpress](https://kevin.md/go/usb-c-adapter), [Amazon](https://kevin.md/go/usb-c-adapter-amazon)) lets the cube plug straight into a Mac Studio's front USB-C port with no cable.

These are affiliate links. As an Amazon Associate I earn from qualifying purchases.

## Hardware

| | |
| --- | --- |
| Model | GeekMagic SmallTV-Ultra |
| Stock firmware | Ultra-V9.0.50 (official bin MD5 `a5fe50093ada3c2637225bbf306a62c3`, 505200 bytes) |
| MCU | ESP-12F, ESP8266, 4 MB flash |
| Display | 1.54 in 240×240 ST7789, hardware SPI mode 3, IPS (INVON) |
| USB | Power only. No CH340, no CDC serial. `ioreg` shows no device. |
| Pins | SCLK GPIO14, MOSI GPIO13, DC GPIO0, RST GPIO2, CS GPIO15, backlight GPIO5 active-low |

Same board as the original SmallTV. The Ultra difference is the stock firmware: it keeps a small OTA slot (the 505 KB official image fits; a ~694 KB image does not) and reports `Not Enough Space` for anything larger. This firmware stays under 500 KB so it can be uploaded the same way as the official bin, and the official bin can be uploaded back.

Factory setup network, seen here at about -24 dBm: open SSID `GIFTV`, web UI `http://192.168.4.1`.

A full flash dump is not possible until the case is opened. Pads and restore commands are in [backups/README.md](backups/README.md). The official Ultra-V9.0.50 image is the Wi-Fi restore file.

## Architecture

```
Mac  macmetrics :9123/metrics   (no root, LaunchAgent)
        ^
        |  HTTP, once a second, no TLS
Cube ESP8266  ST7789 dashboard
```

The cube stores Wi-Fi and the metrics URL in LittleFS (`/mac.cfg`). Two cubes share one firmware image. Change the URL and the cube's hostname in the web UI. Nothing in the source is a specific IP address.

`.local` names work. The ESP8266 core has no host lookup helper, so the firmware sends a unicast mDNS A query (QU bit) for names ending in `.local`.

## Metrics

`GET /metrics` and `GET /` return the same compact JSON. `GET /health` returns `ok`.

```json
{"name":"MacBook Pro","host":"macbook-pro.local","cpu":23.8,"gpu":26.0,"memory_used_gb":93.3,"memory_total_gb":128.0,"cpu_temp_c":74.4,"gpu_temp_c":65.2,"fan_rpm":1915,"power_w":47.3,"timestamp":1790314037}
```

Missing sensors are omitted, not invented. There is no `soc_temp_c`.

| Field | Source | Root |
| --- | --- | --- |
| `cpu` | `host_statistics` `HOST_CPU_LOAD_INFO`, busy ticks over the last second (user + system + nice) | no |
| `gpu` | IOAccelerator `PerformanceStatistics` → `Device Utilization %` (same key Stats uses) | no |
| `memory_*` | Stats' RAM formula: active + inactive + speculative + wired + compressor − purgeable − external, in GiB | no |
| `cpu_temp_c` | Mean of the SMC keys Stats averages for this chip generation | no |
| `gpu_temp_c` | Same, GPU keys | no |
| `fan_rpm` | Fastest fan, `F0Ac`… via `FNum` | no |
| `power_w` | SMC `PSTR`, which Stats labels System Total | no |

On this M5 Max the map is `m5` from exelban/stats `Modules/Sensors/values.swift` (fetched 2026-09-24): CPU `Tp00 Tp04 Tp08 Tp0C Tp0G Tp0K Tp0O Tp0R Tp0U Tp0X Tp0a Tp0d Tp0g Tp0j Tp0m Tp0p Tp0u Tp0y`, GPU `Tg0U Tg0X Tg0d Tg0g Tg0j Tg1Y Tg1c Tg1g`. Keys that are missing or outside 5–115 °C are skipped. One GPU key does not read on this machine, so the GPU mean is 7 of 8. M1–M4 maps are in the binary and selected from `machdep.cpu.brand_string`.

Apple's HID "SOC MTR Temp" sensors are not present on this M5 Max. PMU die sensors exist and are not the CPU, GPU, or SoC temperature, so they are not shown.

Checked against the Stats `smc` tool and `top` on 2026-09-24:

- CPU within about 1 point of `top`'s user+system (24.1 vs 25.0 on one sample).
- `PSTR` matched the daemon within 1 W.
- `Tg0U` matched the GPU mean's constituent reading.
- Fan RPM tracked `smc fans` (samples a second apart, so not identical).
- Memory is Stats' figure, not `top`'s PhysMem. PhysMem counts the file cache and sits near 100% on a 128 GB Mac. This number is the cache-excluded one Stats shows.

No metric needs root, and the LaunchAgent does not ask for a password.

## Build

```
make macmetrics
make firmware
```

`make firmware` fails if `firmware.bin` is 500000 bytes or larger. That is the gate for a direct stock OTA.

PlatformIO lives in `.venv` (`pip install platformio`). The first build downloads the ESP8266 toolchain into `~/.platformio`.

## Run macmetrics

```
./scripts/install-macmetrics.sh
```

Installs `~/.local/bin/macmetrics` and `~/Library/LaunchAgents/com.kevintang.macmetrics.plist`. It starts at login, restarts if it exits, and does not need sudo. Log: `/tmp/macmetrics.log`.

```
curl -s http://127.0.0.1:9123/metrics
macmetrics/macmetrics --once
```

Port is 9123. Override once with `--port`. The plist uses the default.

The socket is `0.0.0.0`, so it is also reachable on Tailscale. The payload is utilization and temperatures, with no auth. Do not publish the port.

## Preconfigure Wi-Fi (optional, no compiler)

Skip the setup-network step by writing your settings into the image before flashing. Copy `cube.conf.example` to `cube.conf` (gitignored) and fill it in:

```
SSID=My Network
PASS=my-wifi-password
URL=http://192.168.1.50:9123/metrics
HOST=smalltv-mac
```

Then patch the prebuilt image (Python 3 standard library only):

```
python3 scripts/configure-bin.py cube.conf smalltv-mac-firmware.bin
```

That writes `smalltv-<HOST>.bin`. The firmware has a fixed 512-byte settings slot in flash; the script fills it and changes nothing else, so the same prebuilt image works for everyone. Optional keys: `FALLBACK`, `ROTATION` (0–3, 90° clockwise steps), `BRIGHTNESS` (5–100, default 30), `UNITS` (`F` or `C`, default `F`).

On first boot the cube joins that Wi-Fi by itself. A newly configured image replaces a cube's saved setup once; later changes on the cube's web page stick until you flash another configured image. A plain image keeps the saved setup.

**The configured `.bin` contains your Wi-Fi password.** Don't share it or commit it.

## Flash the cube

The cube must be on `GIFTV` (unconfigured) or already on your Wi-Fi. Upload either the plain image or a configured one from the previous section; a configured image skips steps 3–4.

1. Open `http://<cube>/`. Stock Ultra serves an updater at `/update`.
2. Upload `firmware/.pio/build/smalltv/firmware.bin` (the app image, not a factory image).

```
./scripts/ota.sh 192.168.4.1
```

The stock form field is the usual ESP8266 name `firmware`. If the updater rejects the file, see the loader section below. Do not upload the loader if the direct image is accepted.

3. The cube reboots with no Wi-Fi saved and opens an open AP `SmallTV-Mac` at `http://192.168.4.1`.
4. Join that AP and submit the form: your 2.4 GHz SSID, password, and metrics URL, for example `http://MacBook-Pro.local:9123/metrics`. Set the cube hostname if this is the second unit (`smalltv-mac-2`).
5. It reboots onto your Wi-Fi. The web UI is then `http://<hostname>.local/`.

Later updates: the System-style page on the cube, or `./scripts/ota.sh <hostname>.local`.

### If the stock updater says Not Enough Space

Only if the image no longer fits the Ultra slot:

```
make loader
./scripts/ota.sh <cube-ip> firmware/.pio/build/loader/firmware.bin
```

The loader opens `SmallTV-Loader`. Join it and upload the full `firmware.bin` to `http://192.168.4.1/update`. After that, restoring stock over Wi-Fi is not the same slot anymore. Use the UART steps in `backups/README.md`.

## Screen

Changed fields and bars stream their final pixels in one window transfer, using
a 480-byte row buffer. They are never erased before redraw. Connection-state
changes refresh the affected fields without clearing the whole screen. Run
`make test` for host-side raster regression tests (no cube required).

`GET /api/status` includes `firmware` and `sample_age_ms` (`-1` before the first
successful fetch), plus saved rotation and brightness. The redraw fix identifies
itself as `2026-10-06-row-redraw-1`. Generic OTA images preserve saved setup.

Firmware `2026-10-06-fallback-2` adds a saved fallback metrics URL. If the primary
cannot resolve, connect, or return a successful metrics response, the cube tries
the fallback. It stays on a working fallback and probes the primary every 30
seconds; if the fallback fails, it immediately tries the primary again.
`/api/status` exposes `fallback_url` and the last successful `active_url`.
Existing settings load with no fallback until one is saved. Set it with the web
form or a URL-only POST: `curl --data-urlencode 'fallback=http://192.168.1.50:9123/metrics' http://<cube>/save`.
A reserved (static DHCP) IP for the Mac makes a good fallback. It must actually
serve macmetrics for failover to succeed. `make test` also covers failover,
failback, both endpoints failing, and clock rollover.

Live: computer name, CPU percent and temperature, GPU percent and temperature, memory used/total and percent, power in watts, fastest fan in RPM. Bars under CPU, GPU, and memory. The Mac sends Celsius; the cube shows °F by default. Set **Temperature units** to `C` on the cube's web page (or `UNITS=C` in `cube.conf`, or `curl --data-urlencode 'units=C' http://<cube>/save`) for °C. `/api/status` reports `units`.

Firmware `2026-10-10-units-8` is the temperature-squares layout with the °F/°C setting: a 36×36 square to the right of each of CPU and GPU, green under 60 °C, yellow under 75, orange under 90, red at 90 and above. Bars go green under 50%, yellow under 80%, red above. An experimental history-trace layout is not part of this release.

| Banner | Meaning |
| --- | --- |
| computer name | last fetch succeeded within 2.5 s |
| STALE | Wi-Fi up, or was up, but the JSON is older than 2.5 s. Numbers stay, drawn dim |
| OFFLINE | Wi-Fi down and nothing has been received. Numbers are `--` |
| WAIT | joined Wi-Fi, URL set, no sample yet |
| SETUP | no Wi-Fi or no URL. Bottom line is `AP SmallTV-Mac` or `SET URL` |

A fetch that fails does not keep looking like a live sample. Wi-Fi reconnects on its own; if that fails for 20 s the setup AP comes back.

## A second Mac and a second cube

1. Copy this repo, or just `macmetrics/macmetrics`, to the other Mac.
2. Run `./scripts/install-macmetrics.sh` there. Same binary, arm64.
3. Flash the same `firmware.bin` (or OTA it from the first cube's `/update` flow onto the second cube).
4. In that cube's web UI set a different hostname and `http://<that-mac>.local:9123/metrics`.

No rebuild. Point the URL at the machine whose numbers you want on the glass.

## Limits

- USB cannot program or back up this Ultra. Recovery without opening the case is an OTA of the official 505 KB bin, and only while our image still uses the original slot.
- CPU and GPU temperatures are zone averages, the same keys Stats uses. They are not per-core labels and they are not a separate SoC package temperature.
- `power_w` is SMC system total (`PSTR`), not a sum of CPU+GPU+ANE rails. Those rails disagree with system total on recent Apple silicon in Stats itself.
- Memory ignores the file cache, matching Stats rather than `top` PhysMem.
- The cube needs 2.4 GHz Wi-Fi. ESP8266 has no 5 GHz radio.
- The ESP runs at 80 MHz with Wi-Fi modem sleep and transmits at 8 dBm instead of the default 20.5 dBm. Light sleep was dropped: it left the radio reporting connected while packets had stopped, and the screen sat on STALE until a power cycle. A dead metrics link now rejoins Wi-Fi after about 12 seconds and reboots the cube after about 60 seconds. The backlight slider is scaled down: a saved 80 is about 15% LED on-time, and 100 is about 18%.
- No TLS. The ESP8266 does not have the RAM for it on this path, and the link is the LAN.

## Credits

- The 5×7 bitmap font is Adafruit-GFX's `glcdfont.c` (BSD-3-Clause). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- SMC sensor key maps and the memory formula follow [exelban/stats](https://github.com/exelban/stats).
- Stock firmware and hardware: [GeekMagicClock/smalltv-ultra](https://github.com/GeekMagicClock/smalltv-ultra). This project is not affiliated with GeekMagic.

## License

[PolyForm Noncommercial 1.0.0](LICENSE.md). Free to read, build, and run for personal, noncommercial use.

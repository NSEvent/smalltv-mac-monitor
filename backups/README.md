# Stock firmware backup

This directory is gitignored except for this note. The chip itself could not be dumped: the SmallTV-Ultra USB-C port is power only (ESP-12F, no USB-serial chip), so `esptool read_flash` has nothing to talk to until the case is opened and a 3.3 V UART adapter is on the pads.

What is here after a session that downloaded it:

- `FW-Smalltv-Ultra-V9.0.50.zip` — official image from GeekMagicClock/smalltv-ultra
- `FW-Smalltv-Ultra-V9.0.50.bin` — the app image inside that zip, 505200 bytes
- `md5sum.txt` — upstream checksum. Expected MD5 of the bin: `a5fe50093ada3c2637225bbf306a62c3`

Header of the bin is `e9 02 02 40`: ESP8266 image, flash mode DIO, 4 MB, 40 MHz. It is the file the stock web updater accepts. It is not a full 4 MB dump (no Wi-Fi calibration, no photos).

Re-download if this folder is empty:

```
curl -fL -o backups/FW-Smalltv-Ultra-V9.0.50.zip \
  https://github.com/GeekMagicClock/smalltv-ultra/raw/refs/heads/main/Ultra-V9.0.50/FW-Smalltv-Ultra-V9.0.50.zip
```

## Put stock back over Wi-Fi

While our firmware is running and on the LAN:

```
curl -f --max-time 180 -F "firmware=@backups/FW-Smalltv-Ultra-V9.0.50.bin" \
  http://smalltv-mac.local/update
```

That works only if our image was installed into the original Ultra slot (direct OTA, no loader). The official bin is 505200 bytes and fits that slot, which is why the project keeps its own image under 500000 bytes.

## Put stock back over the UART pads

Open the case (two screws on the underside, lift the tray). Pads, same as the original SmallTV:

| Pad | Adapter |
| --- | --- |
| 3V3 | 3.3 V |
| GND | GND |
| TXD0 | adapter RX |
| RXD0 | adapter TX |
| GPIO0 | GND only while powering on, then release |
| RST | optional, tap to GND to reset |

```
python3 -m esptool --chip esp8266 --port /dev/cu.usbserial-XXXX --baud 115200 \
  read_flash 0x0 0x400000 backups/stock-full.bin

python3 -m esptool --chip esp8266 --port /dev/cu.usbserial-XXXX --baud 460800 \
  write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x0 backups/FW-Smalltv-Ultra-V9.0.50.bin
```

`read_flash` is the only complete backup. Do that before any UART erase. `write_flash` of the official bin restores the app and leaves the tail of the flash (RF calibration) in place. A full `stock-full.bin` restores everything, including whatever Wi-Fi config was in the chip:

```
python3 -m esptool --chip esp8266 --port /dev/cu.usbserial-XXXX --baud 460800 \
  write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x0 backups/stock-full.bin
```

If a loader was used and the flash map was rewritten, prefer the full image or the official bin at `0x0`, not an OTA upload of the stock file into the new layout.

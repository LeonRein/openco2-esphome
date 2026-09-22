# ESPHome for the OpenCO2 Sensor

ESPHome firmware for the [OpenCO2 Sensor](https://github.com/davidkreidler/OpenCO2_Sensor)
by David Kreidler (ESP32-S2, Sensirion SCD4x, 1.54" 200x200 e-paper, WS2812 LED, LiPo).

It covers the three things this config was built for:

- **the e-paper display works** — CO2, temperature, humidity, power source
- **battery vs. power adapter is detected** (GPIO4) and changes behaviour
- **it works with no Wi-Fi at all** — take it to the office, it just keeps measuring

## Tooling

No virtualenv to manage; everything runs through [uv](https://docs.astral.sh/uv/) and
always uses the newest ESPHome release:

```bash
uvx esphome@latest config openco2.yaml
```

`pyproject.toml` declares the (unpinned) dependency for anyone who prefers a project
environment — `uv run -U esphome ...` works too.

## Before you flash: back up the stock firmware

The stock firmware is what implements the USB-drive and OTA update paths, so once
ESPHome is on the device those are gone. Flashing ESPHome overwrites the bootloader
and partition table as well, so the `FIRMWARE.BIN` the sensor exposes as a USB drive
is **not** enough to get back. Dump the whole flash first:

```bash
uvx --from esptool esptool --port /dev/ttyACM0 --baud 921600 read-flash 0 0x400000 stock-openco2-4mb.bin
```

Keep that file. To go back later, flash it at offset 0 from the ROM bootloader.

## Entering the ROM bootloader

The ESP32-S2 has no USB-serial bridge, so it only shows up as a serial port in
download mode:

1. Hold the **menu button** on the back.
2. Toggle the **power switch** on the side off and on (USB-C stays connected).
3. Release the button.

`/dev/ttyACM0` appears. Confirm with `ls /dev/serial/by-id/`.

## Flashing

```bash
cp secrets.yaml.example secrets.yaml   # then fill in your Wi-Fi
uvx esphome@latest run openco2.yaml --device /dev/ttyACM0
```

Use `run`, not `upload` — `upload` skips compilation and will happily flash a stale
binary that predates your `secrets.yaml` edits.

In practice esptool resets this board into download mode by itself via RTS, so the
button dance above is usually unnecessary. Once Wi-Fi works, update over the air:

```bash
uvx esphome@latest run openco2.yaml --device openco2.local
```

## Hardware revisions — check this first

The board changed pins between revisions, and the two community configs
([Yourigh](https://github.com/Yourigh/OpenCO2_Sensor_esphome) and its
[fork](https://github.com/rahuljawale/OpenCO2_Sensor_esphome)) were written for the
older one:

| | HWSubRev 1-2 (older) | HWSubRev 3 (recent) |
|---|---|---|
| I2C | SDA 33 / SCL 34 | **SDA 3 / SCL 2** |
| LED | APA102 (data 40, clk 39) | **WS2812 on 39** |
| `LED_POWER` (GPIO9) | active low | **active high** |

This config defaults to **HWSubRev 3**, which is confirmed for this device: the
`eeprom` NVS blob in the flash backup reads `01 03`, and byte 1 is `HWSubRev`.

`i2c: scan: true` is left on anyway, so the first boot log double-checks it:

```
[i2c] Found i2c device at address 0x62
```

If nothing is found, set `i2c_sda: GPIO33` / `i2c_scl: GPIO34` in `substitutions:`,
add `inverted: true` to the `led_power` switch, and swap the light for
`spi_led_strip` (APA102) as the fork does.

## How the two power modes behave

| | Power adapter | Battery |
|---|---|---|
| Wi-Fi | on | **off** (`wifi.disable`) |
| Sleep | never (`deep_sleep.prevent`) | deep sleep, wakes every `battery_sleep` (5 min) |
| Display | refreshed every 30 s | refreshed once per wake |
| LED | CO2 traffic light | off |
| Temp offset | 12.2 K | 0 K |

The SCD4x is polled every 5 s in both modes. That interval sets how long the sensor
has to stay awake on battery waiting for its first reading, so a slow poll is
expensive: 30 s costs roughly 4x the battery of 5 s. Screen layout:

```
 21.6°C        48%rH
 ─────────────────────
        2632
         ppm
 ─────────────────────
 USB 49% 192.168.178.79     <- or "BAT 49% 3.69V" on battery
```

The status line is deliberately one `printf`. Drawing the label and the value as two
separate left/right-aligned calls made them look mismatched in size on the panel.

## Temperature offset

The board self-heats, so the reading needs compensating. The stock firmware uses
12.2 K on adapter+Wi-Fi, 4.4 K on adapter alone and 0 K on battery. Since Wi-Fi here
is on exactly when the adapter is, a lambda filter on the temperature sensor applies
12.2 K or 0 K to match. The chip-side `temperature_offset` stays at 4.4 K because the
SCD4x also uses it to compensate humidity.

If the reading is consistently off against a reference thermometer, adjust the
constants in that filter. Give it ~20 min to settle thermally first — it drifts for a
while after a reboot. `esp32: cpu_frequency: 80MHz` is the other lever: less heat to
compensate for, and less battery burnt.

GPIO4 is both the `Power adapter` binary sensor and the ext1 deep-sleep wake source,
so plugging in the adapter wakes the sensor immediately instead of waiting out the
sleep interval. Unplugging it hands control back to deep sleep.

Below `low_battery_volts` (3.30 V) the sensor writes "BATTERY EMPTY" to the display —
which an e-paper panel keeps showing with no power — and parks itself in 12 h sleeps
until the adapter is connected.

## Working without Wi-Fi

`wifi: enable_on_boot: false` means the radio only comes up when the adapter is
plugged in, and `reboot_timeout: 0s` on both `wifi:` and `api:` is what makes the
sensor usable away from home. Those default to 15 min, i.e. stock ESPHome reboots in
a loop when it cannot reach the network or Home Assistant. With them at `0s` the
sensor measures and displays offline indefinitely, and re-joins Home Assistant by
itself once back in range.

## What you give up vs. the stock firmware

ESPHome replaces the whole application, so these stock features go away: the menu
button UI, the 24 h history graphs and QR export, the guided outdoor calibration,
ventilation reminders, the captive-portal Wi-Fi setup and the USB-drive/OTA update
paths. Stock battery life is quoted at up to 11 weeks; this config's 5-minute wake
cycle is in the same ballpark but is not as finely tuned.

Worth knowing: the stock firmware already talks to Home Assistant over
Prometheus/REST and has optional MQTT, so ESPHome is not required for the HA side —
it's the better choice if you want native HA entities and full control of the display.

## Calibration

`Calibrate to 420 ppm` runs a forced recalibration (`scd4x.perform_forced_calibration`).
Only press it after the sensor has been outdoors in fresh air for 3+ minutes —
otherwise it will skew every later reading.

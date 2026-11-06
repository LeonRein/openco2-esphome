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

## Flashing a prebuilt image from a browser

`.esphome/build/openco2/build/firmware.factory.bin` (written by `compile`) holds
bootloader, partition table and app, and goes to **offset 0**. It can be flashed from
[ESPHome Web](https://web.esphome.io) in Chrome/Edge: enter the ROM bootloader (see
above), *Connect*, *Install*, pick the file. Afterwards toggle the power switch so the
firmware starts, *Connect* again (the port changes) and use *Configure Wi-Fi*:
`improv_serial` stores the credentials on the device, so the image itself can be built
with the placeholder `secrets.yaml`. Do this within 2 minutes of power-up, before the
Wi-Fi give-up switches the radio off. An image built that way has the OTA password
from `secrets.yaml.example` (`CHANGE_ME`); use the same one for the first OTA update.

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
| Wi-Fi | on; off again after 2 min without a network, retried every 30 min | **off** (`wifi.disable`) |
| Sleep | never (`deep_sleep.prevent`) | deep sleep, wakes every `battery_sleep` (5 min) |
| Display | partial refresh every 30 s, full every 2 h | partial refresh once per wake, full every 2 h |
| LED | CO2 traffic light | off |
| Temp offset | 12.2 K with Wi-Fi connected, 4.4 K without | 0 K |
| SCD4x between readings | measuring every 5 s | stopped (idle) during deep sleep |
| Charging | stops above 4.0 V, resumes below 3.9 V (stock default) | — |

The SCD4x is polled every 5 s in both modes. That interval sets how long the sensor
has to stay awake on battery waiting for its first reading, so a slow poll is
expensive: 30 s costs roughly 4x the battery of 5 s. Screen layout:

```
 [▮▮  ] 96% USB      192.168.178.79   <- "18% BAT  3.52V" on battery
 ──────────────────────────────────
              2632
              ppm
 [██████████|███|█████|██████████]    <- 400..2000 ppm, ticks at 800/1000/1400
 ▐LÜFTEN▌                  61 %rH     <- GUT / OK / MÄSSIG / LÜFTEN (inverted)
```

There is no temperature on the display: the SCD4x sits on the same small board as
the ESP32-S2, charger and LED, and its reading could not be compensated well enough
to be useful. The `Temperature` entity is still published (the humidity correction
uses it), but treat it as a board temperature.

## E-paper driver

`components/openco2_epaper` replaces ESPHome's `waveshare_epaper` for the panel.
ESPHome's driver (model `1.54inv2`) resets the SSD1681 before every refresh and then
writes only the new image. A partial refresh is computed per pixel from the
controller's "previous" RAM (`0x26`) and "new" RAM (`0x24`), and with nothing valid
in `0x26` it never erases what was on the glass — partial refreshes did not work,
neither on the adapter nor on battery.

This driver keeps the image that is on the panel in RTC slow memory, which survives
deep sleep, and writes it to `0x26` before every partial refresh — the same idea as
the stock firmware's `writePrevImage()`. Init sequence and waveforms (LUTs) are taken
from the stock firmware; RAM addressing matches ESPHome's, so `rotation: 270°` still
applies.

- every refresh is partial (no flashing) except one full refresh per
  `full_update_interval` (2 h), on the adapter and on battery alike. The interval
  is timed with the RTC clock, which keeps running through deep sleep.
- a cold boot (power switch, flashing) always starts with a full refresh, since
  the panel contents are unknown then.
- the `Display full refresh` button forces one in between if ghosting shows up.

## Temperature offset

The SCD4x shares a small board with the ESP32-S2, the charger and the LED, so it
reads warm. The stock firmware (`getTempOffset()`, v6.3) subtracts:

| Mode | Offset |
|---|---|
| power adapter, Wi-Fi on | 12.2 K |
| power adapter, no Wi-Fi | 4.4 K |
| battery (single-shot, sensor powered down in between) | 0.0 K |

This config uses the same three values, picked at runtime from the `Power adapter`
sensor and whether Wi-Fi is connected. Away from home (no known network) the radio
switches itself off after `wifi_give_up` (2 min) and only retries every `wifi_retry`
(30 min), so the 4.4 K case is what actually applies there.

Those offsets were measured on hardware that behaved in specific ways, and this
config now matches them:

- **CPU at 80 MHz** (`esp32: cpu_frequency: 80MHZ`), like the stock build. ESPHome's
  default for the S2 is 240 MHz, which heats the board noticeably more — the main
  reason readings were too high before.
- **SCD4x stopped before deep sleep** on battery. Left in periodic mode it draws
  ~15 mA for the whole sleep and warms itself.
- **Chip-side `temperature_offset: 0`**, all compensation in software. The SCD4x
  computes RH relative to its own temperature, so the humidity filter rescales it to
  the corrected temperature (Magnus formula). With a fixed chip offset RH was badly
  off whenever the real offset differed from it.

The stock firmware also light-sleeps the ESP between measurements on the adapter
without Wi-Fi, which ESPHome cannot do while running; the 4.4 K may therefore come
out a little low. Each offset is a number entity (`Temperature offset ...`) you can
tune against a reference thermometer after the sensor has sat in that mode for an
hour (it needs that long to warm up / cool down). If it reads 1.5 K too high, raise
that offset by 1.5. Values set there are stored on the device and take precedence
over the `temp_offset_*` substitutions from then on.

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

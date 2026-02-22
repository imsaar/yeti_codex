# Companion 309 (ESP32-C3 mini + 0.96 OLED)

This project builds a desktop robot companion firmware and desktop control client using:

- Board: ESP32-C3 mini (`lolin_c3_mini`)
- Display: 0.96 inch SSD1306 OLED (I2C, 128x64)
- Toolchain: PlatformIO CLI + Arduino framework

## Features

- Expressive face states: `neutral`, `happy`, `sad`, `sleepy`, `angry`, `surprised`, `thinking`
- Blink animation and responsive face redraw
- Speech line on OLED
- Notes memory (up to 8 entries)
- Reminder scheduler (up to 8 reminders)
- HTTP API for desktop control
- AP fallback mode if Wi-Fi credentials are not available

## Wiring (ESP32-C3 mini)

Default pins in `include/config.h`:

- `SDA` -> GPIO8
- `SCL` -> GPIO9
- OLED address -> `0x3C`

If your OLED uses other pins, update `include/config.h`.

## PlatformIO CLI setup

1. Install PlatformIO Core (if needed):

```bash
python3 -m pip install -U platformio
```

2. Build firmware:

```bash
pio run
```

3. Upload firmware:

```bash
pio run -t upload
```

4. Open serial monitor:

```bash
pio device monitor -b 115200
```

## Wi-Fi configuration (gitignored secrets)

1. Copy template:

```bash
cp include/wifi_secrets.example.h include/wifi_secrets.h
```

2. Edit `include/wifi_secrets.h` with your real credentials:

- `WIFI_SSID`
- `WIFI_PASSWORD`

`include/wifi_secrets.h` is gitignored and should never be committed.

If connection fails after boot, device starts an AP:

- SSID: `Companion-309`
- Password: `companion309`
- Robot IP in AP mode: `192.168.4.1`

## HTTP API

- `GET /status`
- `POST /emotion` with JSON: `{"emotion":"happy"}`
- `POST /speak` with JSON: `{"text":"Hello"}`
- `GET /notes`
- `POST /notes` with JSON: `{"note":"Focus block at 2pm"}`
- `POST /reminders` with JSON: `{"minutes":20,"message":"Stretch"}`
- `POST /clear`

## Desktop companion CLI

From `desktop_companion/`:

1. Install deps:

```bash
python3 -m pip install -r requirements.txt
```

2. Examples:

```bash
python3 app.py --host 192.168.4.1 status
python3 app.py --host 192.168.4.1 emotion happy
python3 app.py --host 192.168.4.1 speak "Time to hydrate"
python3 app.py --host 192.168.4.1 reminder 25 "Break done"
python3 app.py --host 192.168.4.1 watch --interval 2
```

## Notes

- This is structured to match an expressive desk companion workflow on ESP32 + OLED with local reminders and desktop control.
- You can extend behavior by adding sensors, touch input, audio output, or a richer desktop UI while keeping the same API.

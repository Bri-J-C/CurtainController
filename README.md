# CurtainController

ESP32-C3 Super Mini curtain controller using an A4988 stepper driver. Provides MQTT integration with Home Assistant auto-discovery, a browser-based setup page, WebSerial console, and OTA firmware updates.

**Firmware version:** v4.4
**Target board:** Nologo ESP32-C3 Super Mini
**FQBN:** `esp32:esp32:nologo_esp32c3_super_mini`

---

## Hardware

See [HARDWARE.md](HARDWARE.md) for the bill of materials, wiring diagram, A4988 current adjustment, and physical mounting guide.

---

## Pinout

| GPIO | Function | Notes |
|------|----------|-------|
| 0 | ENABLE | LOW = driver enabled; HIGH = disabled |
| 1 | MS1 | Microstepping bit 1 |
| 2 | MS2 | Microstepping bit 2 |
| 3 | MS3 | Microstepping bit 3 |
| 4 | RESET | Held HIGH during normal operation; pulse LOW to reset A4988 |
| 6 | DIR | Direction control |
| 7 | SLEEP | LOW = A4988 sleep mode; HIGH = awake |
| 8 | STATUS LED | Onboard blue LED (inverted logic — LOW = on) |
| 9 | BOOT button | Input with internal pull-up; used for button actions |
| 10 | STEP | Step pulse output |

---

## Required Libraries

Install these through the Arduino Library Manager:

- `WiFiManager` (tzapu)
- `PubSubClient` (knolleary)
- `ArduinoJson`
- `ESPAsyncWebServer`
- `MycilaWebSerial`
- `ArduinoOTA` (bundled with ESP32 Arduino core)

ESP-IDF components used directly (bundled with ESP32 Arduino core):
`esp_netif`, `esp_wifi`, `esp_system`, `esp_task_wdt`, `mdns`

---

## Building and Flashing

Open `CurtainController.ino` in Arduino IDE. Select:

- **Board:** `Nologo ESP32C3 Super Mini`
- **Port:** the device's serial port

Click **Upload**. First flash must be over USB. Subsequent updates can be done over OTA.

**OTA upload (after first boot):**

The device registers itself as `<hostname>.local` on mDNS and listens on port 3232. In Arduino IDE, the device will appear under **Tools > Port** as a network port once it is on the same LAN. An OTA password can be set through the setup page or config portal.

---

## First Boot and Initial Configuration

### If no WiFi credentials are stored

The device launches the **WiFiManager config portal**:

1. Connect your phone or laptop to the `CurtainSetup` WiFi network (password: `12345678`).
2. A captive portal opens automatically (or navigate to `192.168.4.1`).
3. Enter your WiFi credentials and MQTT settings.
4. Click Save. The device reboots and connects.

### If credentials are already stored

The device attempts to connect within a 15-second timeout. If connection fails, it reboots to retry.

### Config portal parameters

| Field | Description | Default |
|-------|-------------|---------|
| Device Hostname | mDNS hostname and MQTT client ID suffix | `CurtainController` |
| MQTT Server IP | IP address of your MQTT broker | — |
| MQTT Port | MQTT broker port | `1883` |
| MQTT Username | MQTT authentication username | — |
| MQTT Password | MQTT authentication password | — |
| MQTT Root Topic | Base topic for all MQTT messages | `home/room/curtains` |
| OTA Password | Password required for OTA updates | — |
| Steps per Revolution | Total steps for full travel range | `2000` |

---

## Web Setup Page

After connecting to WiFi, navigate to `http://<device-ip>/setup` (or `http://<hostname>.local/setup`).

The setup page lets you change all configuration parameters and reboot the device without using the config portal. It covers: hostname, MQTT server/port/user/password/topic, travel steps, microstepping mode, and OTA password. The root path `/` redirects to `/setup`.

The WebSerial console is linked from the setup page at `/webserial`.

---

## MQTT

### Topic Structure

All topics share a configurable root topic (default: `home/room/curtains`).

| Topic | Direction | Description |
|-------|-----------|-------------|
| `<root>/cmd` | Subscribe | Movement commands and position percentage |
| `<root>/status` | Publish | Current state: `open`, `closed`, `opening`, `closing`, `stopped`, `error_timeout` |
| `<root>/position` | Publish | Current position as a percentage (0–100), retained |
| `<root>/availability` | Publish | `online` / `offline` (LWT), retained |
| `<root>/speed/set` | Subscribe | Step delay in µs (100–10000) |
| `<root>/speed/state` | Publish | Current step delay, retained |
| `<root>/invert/set` | Subscribe | Direction invert: `ON` / `OFF` |
| `<root>/invert/state` | Publish | Current invert state, retained |
| `<root>/stepmode/set` | Subscribe | Step mode: `full`, `half`, `quarter`, `eighth`, `sixteenth` |
| `<root>/stepmode/state` | Publish | Current step mode, retained |
| `<root>/totalsteps/set` | Subscribe | Total travel range in steps (1–500000) |
| `<root>/totalsteps/state` | Publish | Current total steps, retained |

### Command Topic Payloads

Publish to `<root>/cmd`:

| Payload | Action |
|---------|--------|
| `open` | Move to fully open position (100%) |
| `close` | Move to fully closed position (0%) |
| `stop` | Stop movement immediately |
| `0`–`100` | Move to that position percentage |

### Retained Command Guard

The firmware ignores any retained messages received within the first 2 seconds after subscribing, to prevent stale commands from triggering movement on reconnect.

### MQTT Reconnection

If the MQTT broker is unreachable, the firmware retries with exponential backoff starting at 2 seconds and capping at 60 seconds.

### Home Assistant Auto-Discovery

The device publishes HA MQTT discovery payloads on first connection (once per boot sequence, stored in NVS). Discovery creates five entities:

| Entity Type | Name | Function |
|-------------|------|----------|
| `cover` | *(device name)* | Main curtain control (open/close/stop/position) |
| `number` | Speed | Step delay in µs; lower = faster |
| `number` | Total Steps | Full travel range in steps |
| `switch` | Invert Direction | Swap open/close motor direction |
| `select` | Step Mode | Microstepping mode |

To force re-publication (e.g. after changing the root topic), send `hadiscovery` to the WebSerial console or use the `hadiscovery` command.

---

## Microstepping Modes

| Mode index | Name | MS1 | MS2 | MS3 | Resolution |
|------------|------|-----|-----|-----|------------|
| 0 | Full | LOW | LOW | LOW | 1/1 |
| 1 | Half | HIGH | LOW | LOW | 1/2 |
| 2 | Quarter | LOW | HIGH | LOW | 1/4 |
| 3 | Eighth | HIGH | HIGH | LOW | 1/8 (default) |
| 4 | Sixteenth | HIGH | HIGH | HIGH | 1/16 |

The default mode is **1/8 step** (index 3). Higher microstepping produces smoother, quieter motion but requires more steps for the same travel distance. Adjust `Travel Steps` to match your physical setup.

---

## Console Commands

Commands are available in the WebSerial console at `/webserial` and are processed identically whether typed in the browser or published to MQTT (where applicable).

### Movement

| Command | Description |
|---------|-------------|
| `open` | Move to fully open position |
| `close` | Move to fully closed position |
| `stop` | Stop movement immediately and save position |
| `<0-100>` | Move to position percentage |
| `position` | Print current position (steps and percentage) |

### Settings (persisted to NVS)

| Command | Arguments | Description |
|---------|-----------|-------------|
| `speed` | `<µs>` | Step delay: 100–10000 µs (lower = faster). Default: 2000 |
| `mode` | `<0-4>` | Microstepping mode (0=Full … 4=Sixteenth). Default: 3 |
| `steps` | `<n>` | Total travel range in steps: 1–500000. Default: 2000 |
| `invert` | — | Toggle open/close direction and save |
| `sleep` | `<ms>` | Motor auto-sleep timeout in ms (0 = never sleep). Default: 30000 |

### Diagnostics

| Command | Description |
|---------|-------------|
| `status` | Position, movement state, motor state, MQTT state, heap, uptime, last reset reason |
| `config` | Full configuration dump including network, MQTT, and motor settings |
| `loglevel` | `error` / `warn` / `info` / `debug` — sets and persists the log level |
| `ledon` | Force STATUS LED on (manual mode) |
| `ledoff` | Force STATUS LED off (manual mode) |
| `ledauto` | Return LED to automatic control |
| `help` | Print command reference |

### System

| Command | Description |
|---------|-------------|
| `setposition <n>` | Override stored position counter without moving (use with care) |
| `resetdriver` | Hardware reset A4988 via RESET pin, then restore microstepping |
| `hadiscovery` | Clear and re-publish all Home Assistant discovery payloads |
| `restart` | Reboot the device |

---

## Button Behavior (GPIO 9 / BOOT)

| Action | Behavior |
|--------|----------|
| Short press during movement | Emergency stop |
| Hold 3–5 s (any time) | Launch WiFiManager config portal |
| Hold 10–13 s (any time) | Factory reset: clears all NVS settings and WiFi credentials, then reboots |
| Hold at boot (before WiFi connects) | Launch config portal instead of normal WiFi connect |

The LED provides visual feedback during holds: solid ON at the 3–5 s window, rapid blink at the 10–13 s window.

---

## WiFi Behavior

### Boot

1. Attempts connection using stored credentials, 15-second timeout.
2. If timeout with credentials stored: reboots to retry.
3. If timeout with no credentials stored: launches config portal.
4. If BOOT button is held during boot: launches config portal.

### Runtime Reconnection (4-state machine)

| State | Description |
|-------|-------------|
| `IDLE` | Connected; monitors for dropped connection |
| `LOST` | Disconnected detected; initiates reconnect attempt |
| `RECONNECTING` | Waiting up to 15 s for association |
| `BACKOFF` | Waiting before next attempt (attempt × 2 s, max 30 s) |

After 8 failed attempts the device saves position, stops any movement, and reboots.

WiFi power saving is disabled (`WIFI_PS_NONE`) for reliability.

---

## Position Tracking

- Position is stored in NVS and restored on every reboot.
- Position is written every 500 steps during movement and immediately on any stop.
- Movement times out after 2 minutes; an `error_timeout` status is published.
- Position is expressed as 0 (closed) to `steps_per_revolution` (open) internally, and as a 0–100% percentage in MQTT.

---

## Motor Sleep

The A4988 SLEEP pin is driven LOW after `motor_sleep_timeout` milliseconds of inactivity (default 30 seconds). This reduces idle power draw and motor heating. Set `sleep 0` to disable. The motor wakes automatically (with a 1 ms delay for the A4988 wake-up requirement) when a movement command is issued.

---

## OTA Updates

ArduinoOTA runs on port 3232. The device hostname is used as the OTA target name. An OTA password can be set through the setup page or config portal; if no password is set, OTA is unauthenticated.

mDNS advertises `_arduino._tcp` on port 3232 and `_http._tcp` on port 80. The mDNS record is re-announced every 60 seconds as a safety net for missed multicasts.

---

## NVS Storage

Settings are stored in the `curtains` NVS namespace. The following keys are used:

| Key | Type | Description |
|-----|------|-------------|
| `hostname` | String | Device hostname |
| `mqtt_server` | String | MQTT broker IP |
| `mqtt_port` | Int | MQTT broker port |
| `mqtt_user` | String | MQTT username |
| `mqtt_pass` | String | MQTT password |
| `mqtt_root_topic` | String | MQTT base topic |
| `ota_pass` | String | OTA password |
| `steps_per_rev` | Int | Total travel steps |
| `step_mode` | Int | Microstepping mode index |
| `step_delay` | Int | Step delay in µs |
| `sleep_timeout` | ULong | Motor sleep timeout in ms |
| `invert_dir` | Bool | Direction invert flag |
| `position` | Int | Last known position in steps |
| `log_level` | UChar | Log level (0=error … 3=debug) |
| `ha_disc_done` | Bool | HA discovery already published flag |

---

## License

MIT License — see the `LICENSE` file for details.

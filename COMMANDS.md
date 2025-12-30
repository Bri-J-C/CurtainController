# Command Reference

Complete reference for all available commands via WebSerial and MQTT. 

Entering "help" in webserial will list all available commands.

## WebSerial Commands

Access WebSerial at: `http://<device-ip>/webserial` or `http://<hostname>.local/webserial`


## Commands

### Movement Commands
```
open                    - Open curtain fully
close                   - Close curtain fully
stop                    - Stop current movement
position:<steps>        - Move to specific position (0 to steps_per_revolution)
```

### Configuration Commands
```
set:speed:<us>          - Set delay between steps (2-10000 microseconds per step)
set:mode:<0-4>          - Set microstepping mode
set:steps:<n>           - Set steps per revolution (1-20000)
set:position:<n>        - Reset current position counter
set:sleep:<ms>          - Set motor sleep timeout (0-300000 ms)
set:hostname:<name>     - Set device hostname (requires reboot)
```

### Information Commands
```
status                  - Show current system status
config                  - Show configuration
wifi                    - Show WiFi information
mqtt                    - Show MQTT information
```

### Utility Commands
```
reset_driver            - Pulse A4988 RESET pin
ha_discovery            - Republish Home Assistant discovery
restart                 - Restart ESP32
led:on                  - Turn STATUS LED on
led:off                 - Turn STATUS LED off
help                    - Show command list
```

### MQTT Commands

Send commands via MQTT to `<root>/cmd` topic:
```
open
close
stop
<position_percentage>   - Send number 0-100 to set position
```





### Movement Commands

#### `open`
Opens the curtain to fully open position (100%).
```
open
```
- Moves motor to `steps_per_revolution`
- Publishes status: "opening" → "open"
- Position saved to flash on completion

#### `close`
Closes the curtain to fully closed position (0%).
```
close
```
- Moves motor to position 0
- Publishes status: "closing" → "closed"
- Position saved to flash on completion

#### `stop`
Immediately stops any ongoing movement.
```
stop
```
- Halts motor at current position
- Saves position to flash
- Publishes current position and status

#### `position:<steps>`
Moves to a specific step position.
```
position:1000
```
- Range: 0 to `steps_per_revolution`
- Example: `position:1000` moves to step 1000
- Useful for partial opening/closing

---

### Configuration Commands

#### `set:speed:<microseconds>`
Sets the delay between motor steps (lower = faster).
```
set:speed:2000
```
- **Range:** 2-10000 microseconds
- **Default:** 2000µs
- **Lower values** = faster movement (may skip steps if too low)
- **Higher values** = slower movement (more torque, can make operation louder)
- Saved to flash (persists across reboots)

**Examples:**
```
set:speed:200    # Very fast (test for skipping)
set:speed:2000   # Default

```

#### `set:mode:<mode>`
Sets microstepping resolution mode.
```
set:mode:4
```
- **Range:** 0-4
- **Modes:**
  - `0` = Full step (1/1) - Most torque, loudest
  - `1` = Half step (1/2)
  - `2` = Quarter step (1/4)
  - `3` = Eighth step (1/8)
  - `4` = Sixteenth step (1/16) - Smoothest, quietest (default)
- Saved to flash

#### `set:steps:<number>`
Sets total steps taken.
```
set:steps:3200
```
- **Range:** 1-20000 steps
- **Default:** 2000
- Saved to flash
- Adjust when changing motor or microstepping mode

#### `set:position:<step>`
Resets the current position counter (calibration).
```
set:position:0
```
- **Range:** 0 to `steps_per_revolution`
- Does not move motor - only updates position counter
- Useful for recalibration if position drifts
- Saved to flash

**Use case:** If curtain is physically at closed position but controller thinks it's at step 500:
```
set:position:0
```

#### `set:sleep:<milliseconds>`
Sets motor sleep timeout for power saving.
```
set:sleep:30000
```
- **Range:** 0-300000 milliseconds (0-5 minutes)
- **Default:** 30000ms (30 seconds)
- **0 = disabled** (motor always awake)
- Motor automatically sleeps after inactivity timeout
- Wakes automatically before movement
- Reduces power consumption and motor heating
- Saved to flash

#### `set:hostname:<name>`
Changes device hostname (requires reboot).
```
set:hostname:bedroom-curtain
```
- **Length:** 1-40 characters
- **Allowed:** Letters, numbers, hyphens, underscores
- **No spaces** or special characters
- Saved to flash
- Device reboots automatically after 3 seconds
- New mDNS address: `<hostname>.local`

---

### Information Commands

#### `status`
Displays current system status.
```
status
```
**Output:**
```
=== Status ===
Position: 1000 (50%)
Moving: No
Motor: Sleeping
Speed: 2000 us/step
Mode: Sixteenth (1/16)
Free Heap: 245632 bytes
Uptime: 3600 seconds
==============
```

#### `config`
Shows all configuration parameters.
```
config
```
**Output:**
```
=== Configuration ===
Hostname: esp32-curtain
IP: 192.168.1.100
Speed: 2000 us/step
Mode: 4 (Sixteenth (1/16))
Steps/Rev: 2000
Position: 0 (0%)
Sleep Timeout: 30000 ms
MQTT: 192.168.1.10:1883
====================
```
Also publishes configuration to MQTT config topic.

#### `wifi`
Displays WiFi connection information.
```
wifi
```
**Output:**
```
=== WiFi Info ===
SSID: MyNetwork
IP: 192.168.1.100
MAC: A1:B2:C3:D4:E5:F6
RSSI: -45 dBm
=================
```

#### `mqtt`
Shows MQTT connection status and topics.
```
mqtt
```
**Output:**
```
=== MQTT Info ===
Server: 192.168.1.10:1883
Connected: Yes
Command Topic: home/bedroom/curtain/cmd
Status Topic: home/bedroom/curtain/status
=================
```

---

### Utility Commands

#### `reset_driver`
Pulses the A4988 RESET pin.
```
reset_driver
```
- Sends brief LOW pulse to A4988 RESET
- Resets driver internal state
- Use if motor behavior is erratic
- Does not reset ESP32 or position

#### `ha_discovery`
Republishes Home Assistant MQTT discovery.
```
ha_discovery
```
- Forces republish of discovery message
- Useful if HA loses device configuration
- Device appears as "Cover" entity in HA
- Discovery topic: `homeassistant/cover/<hostname>/config`

#### `restart`
Reboots the ESP32.
```
restart
```
- Saves current position before restart
- Device reboots after 2 seconds
- All configuration persists (stored in flash)
- Reconnects to WiFi and MQTT automatically

#### `led:on`
Turns the onboard STATUS LED on.
```
led:on
```
- Forces LED to stay on
- Overrides automatic LED behavior
- Persists until `led:off` or restart

#### `led:off`
Turns the onboard STATUS LED off.
```
led:off
```
- Forces LED to stay off
- Useful for reducing light in bedroom at night
- Persists until `led:on` or restart

#### `help`
Displays all available commands.
```
help
```
Shows categorized list of all commands with brief descriptions.

---

## MQTT Commands

Commands can be sent via MQTT to the command topic: `<root>/cmd`

### Supported MQTT Commands

#### Basic Movement
```
open        # Fully open
close       # Fully close
stop        # Stop movement
```

#### Position Control
```
0           # Move to 0% (fully closed)
50          # Move to 50% (half open)
100         # Move to 100% (fully open)
```
- Send any number 0-100 to set position percentage
- Converts to steps automatically

#### All WebSerial Commands
Any command that works in WebSerial also works via MQTT:
```
set:speed:3000
set:mode:3
status
config
```

### MQTT Topics (Default)

Replace `<root>` with your configured root topic (e.g., `home/bedroom/curtain`):

| Topic | Direction | Purpose | Example Payload |
|-------|-----------|---------|-----------------|
| `<root>/cmd` | Subscribe | Receive commands | `open` |
| `<root>/status` | Publish | Current status | `opening` |
| `<root>/position` | Publish | Current position | `50` |
| `<root>/cmd/config` | Subscribe | Config requests | `config` |
| `<root>/cmd/config/response` | Publish | Config response | `{...}` |

### Status Values

Published to `<root>/status`:
- `online` - Device connected
- `offline` - Device disconnected (LWT message)
- `opening` - Curtain opening
- `closing` - Curtain closing
- `open` - Fully open (100%)
- `closed` - Fully closed (0%)
- `partial` - Partially open
- `error_timeout` - Movement timeout error

---

## Command Examples

### Common Use Cases

#### Initial Calibration
```
# 1. Manually move curtain to closed position
# 2. Reset position counter
set:position:0

# 3. Test full movement
open
# Watch curtain - does it fully open?

# 4. If not, adjust steps
set:steps:3200

# 5. Test again
close
open
```

#### Speed Optimization
```
# Start conservative
set:speed:2000

# Test movement
open

# Gradually decrease
set:speed:1500
open

set:speed:1000
open

# If motor skips steps or sounds rough, increase
set:speed:1200
```

#### Changing Microstepping
```
# Switch to 1/8 microstepping
set:mode:3

# Update steps per revolution
# New steps = 200 × 8 = 1600
set:steps:1600

# Test
open
close
```

#### Partial Opening Examples
```
# Open to 25%
position:500    # (assuming 2000 steps/rev)

# Half open
position:1000

# 75% open
position:1500
```

#### Via MQTT
```bash
# Using mosquitto_pub
mosquitto_pub -h 192.168.1.10 -t "home/bedroom/curtain/cmd" -m "open"
mosquitto_pub -h 192.168.1.10 -t "home/bedroom/curtain/cmd" -m "50"
mosquitto_pub -h 192.168.1.10 -t "home/bedroom/curtain/cmd" -m "stop"
```

---

## Command Response

### WebSerial
Commands executed via WebSerial show immediate feedback:
```
> open
Opening curtain...

> set:speed:1000
Speed set to 1000 us/step
```

### MQTT
MQTT commands trigger:
1. Serial log output (check Serial Monitor)
2. Status updates published to status topic
3. Position updates published to position topic

---

## Error Messages

### Common Errors

#### Invalid Speed
```
[ERROR] Speed must be 2-10000 us
```
Fix: Use value within valid range

#### Invalid Mode
```
[ERROR] Mode must be 0-4
```
Fix: Use microstepping mode 0-4

#### Invalid Steps
```
[ERROR] Steps must be 1-20000
```
Fix: Use value within valid range

#### Invalid Position
```
[ERROR] Position must be 0-2000
```
Fix: Use position within 0 to `steps_per_revolution`

#### Invalid Hostname
```
[ERROR] Hostname must be 1-40 characters
[ERROR] Hostname can only contain letters, numbers, hyphens, and underscores
```
Fix: Use valid hostname characters

#### Movement Timeout
```
[ERROR] Movement timeout!
```
- Motor took >2 minutes to complete movement
- Check mechanical binding
- Increase speed (lower delay value)

---

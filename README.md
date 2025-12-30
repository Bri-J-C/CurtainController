# CurtainController

ESP32-C3 based smart curtain controller with MQTT integration, Wifi Configuration, Home Assistant discovery, WebSerial control interface, and OTA.

## Features

- **MQTT Integration** - Full MQTT support with Home Assistant auto-discovery
- **WebSerial Control** - Browser-based control and configuration interface
- **Motor Control** - Precise stepper motor control with configurable microstepping
- **OTA Updates** - Over-the-air firmware updates
- **Position Tracking** - Persistent position storage across reboots
- **Motor Sleep** - Automatic motor sleep after inactivity to reduce power and heat
- **LED Control** - Manual control of onboard STATUS LED
- **Factory Reset** - 5-second button press for complete device reset
- **WiFi Manager** - Easy WiFi configuration via captive portal


## Parts List And Construction
[Hardware Page](https://github.com/Bri-J-C/CurtainController/blob/main/HARDWARE.md)

## Arduino IDE Setup

## Initial Setup

### First Boot - WiFi Configuration

1. Power on the ESP32-C3
2. Connect to WiFi network: `ESP32-Curtain-Setup`
3. Password: `12345678`
4. Captive portal opens automatically
5. Configure:
   - WiFi SSID and password
   - Device hostname
   - MQTT broker IP and port
   - MQTT username/password (optional)
   - MQTT root topic (e.g., `home/bedroom/curtain`) child topics will be created from this in code ie:
     
          home/bedroom/curtain/status
          home/bedroom/curtain/cmd
          home/bedroom/curtain/position
     
   - Steps per revolution (default: 2000)
   - OTA password (optional)
6. Click Save
7. Device reboots and connects to your WiFi

### WebSerial Access

Once connected to WiFi:
1. Open browser to: `http://<device-ip>/webserial`
2. Or use hostname: `http://<hostname>.local/webserial`
3. Type `help` to see all available commands

### MQTT Topics

The controller uses the following MQTT topic structure (based on your configured root topic):

- **Command Topic:** `<root>/cmd` - Send commands here
- **Status Topic:** `<root>/status` - Current status (opening/closing/open/closed/partial/online/offline)
- **Position Topic:** `<root>/position` - Current position (0-100%)
- **Config Topic:** `<root>/cmd/config` - Configuration requests

### Home Assistant Discovery

The controller automatically publishes Home Assistant MQTT discovery on first connection:
- Discovery topic: `homeassistant/cover/<hostname>/config`
- Appears as a "Cover" entity in Home Assistant
- Supports: open, close, stop, set position
- also works for openhab, just need to create item from the channel this creates.

## Configuration

### Motor Configuration

Default settings work for most applications, but can be adjusted:

**speed really means delay between steps i was too lazy to fix**
| Parameter | Default | Range | Command |
|-----------|---------|-------|---------|
| Speed | 2000 µs/step | 2-10000 | `set:speed:<value>` |
| Microstepping | 1/16 | 0-4 | `set:mode:<0-4>` |
| Steps/Revolution | 2000 | 1-100000 | `set:steps:<value>` |
| Sleep Timeout | 30000 ms | 0-300000 | `set:sleep:<value>` |

### Microstepping Modes

| Mode | Resolution | Description |
|------|-----------|-------------|
| 0 | Full step (1/1) | louder, high torque |
| 1 | Half step (1/2) | |
| 2 | Quarter step (1/4) | |
| 3 | Eighth step (1/8) | |
| 4 | Sixteenth step (1/16) | Smoothest, quietest (recommended) |

### Finding Optimal Speed

1. Start with default: `set:speed:2000`
2. Test movement: `open` then `close`
3. Gradually decrease: `set:speed:1500`, test again
4. Continue lowering until motor:
   - Makes grinding noises
   - Skips steps
   - Stalls under load
5. Typical range: 800-1500µs for quiet, reliable operation ive found

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

## Factory Reset

Hold the onboard BOOT button (GPIO 9) for 5 seconds:
1. LED starts blinking after 1 second (hold)
2. LED blinks rapidly 10 times after 5 seconds (reset confirmed)
3. All settings cleared, WiFi reset
4. Device reboots into setup mode

## Troubleshooting

### Motor Not Moving
- Check power supply (12V, 2A+)
- Verify wiring connections
- Check POT on motor controller if set too low it wont move
- Check motor sleep timeout: `set:sleep:0` (disable sleep)

### Motor Running Backwards
- unplug the motor and flip the connector over, plug back in.

### Position Drift
- Motor is missing steps
- Increase step delay: `set:speed:2000`
- Increase microstepping: `set:mode:4`
- Check for mechanical binding
- Ensure adequate power supply
- Reduce as much friction on the track/curtains against window,couch etc

### WiFi Connection Issues
- Hold BOOT button 5 seconds for factory reset
- Reconnect to `ESP32-Curtain-Setup` network
- Reconfigure WiFi settings

### MQTT Not Connecting
- Check broker IP: `mqtt` command in WebSerial
- Verify broker is running and accessible
- Check username/password if authentication enabled
- Verify topic permissions on broker

### WebSerial Not Responding
- Clear browser cache
- Try different browser (Chrome/Edge recommended)
- Check device IP address is correct
- Ensure on same network as ESP32

### Home Assistant Not Discovering
- Republish discovery: `ha_discovery` command
- Check MQTT integration is configured in HA
- Verify discovery prefix is `homeassistant`
- Check MQTT broker logs for published messages

### OpenHAB Showing Offline
- OpenHAB may require regular status updates
- Check availability topic configuration
- Verify MQTT thing configuration matches topics
- Monitor MQTT broker to see published messages

## Technical Details

### Position Tracking
- Position saved every 50 steps during movement
- Final position saved when movement completes
- Position restored from flash on boot
- Protected against power loss during movement

### Motor Protection
- Automatic sleep after configurable timeout (default 30s)
- Reduces power consumption and motor heating
- Motor wakes automatically before movement

### Safety Features
- Movement timeout: 2 minutes (prevents runaway)
- Watchdog timer: 3 minutes (auto-recovery from hangs)
- Overflow-safe timing (handles micros() rollover)
- Factory reset lockout during movement

### Performance
- Constant speed movement (no acceleration/deceleration)
- Configurable step delay: 2-10000 microseconds
- Maximum step rate: ~500,000 steps/second (2µs delay)
- Practical limits: 500-2100µs for reliable operation

## License

MIT License - See LICENSE file for details

## Version History

See CHANGELOG.md for version history and changes.

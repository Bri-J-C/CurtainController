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
- **Status Updates - Publishes real time status updates used to update item state


## Parts List And Construction
   - [Hardware](https://github.com/Bri-J-C/CurtainController/blob/main/HARDWARE.md)

## Arduino IDE Setup
   - [ArduinoSetup](https://github.com/Bri-J-C/CurtainController/blob/main/ArduinoSetup.md)
     
## Initial Setup
   - [FirstBoot](https://github.com/Bri-J-C/CurtainController/blob/main/FIRSTBOOT.md)
     
## Configuration
   - [Configuration](https://github.com/Bri-J-C/CurtainController/blob/main/CONFIGURATION.md)
     
## Technical Details

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

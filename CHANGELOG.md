# Changelog

All notable changes to this project will be documented in this file.

## [4.1] - 2025-12-28

### Added
- Manual LED control commands (`led:on`, `led:off`)
- LED state enforcement in main loop for consistent control
- MycilaWebSerial library integration for improved WebSerial stability

### Changed
- Updated pin mapping for optimized PCB layout:
  - ENABLE: GPIO 10 → GPIO 0
  - MS1: GPIO 7 → GPIO 1
  - MS2: GPIO 6 → GPIO 2
  - MS3: GPIO 5 → GPIO 3
  - RESET: GPIO 4 (unchanged)
  - DIR: GPIO 0 → GPIO 6
  - SLEEP: GPIO 3 → GPIO 7
  - STEP: GPIO 1 → GPIO 10
- Replaced standard WebSerial library with MycilaWebSerial for bug fixes
- Maximum steps configurable up to 20,000

### Removed
- Acceleration/deceleration ramps (constant speed movement)
- Removed ~37 lines of acceleration code
- Removed `calculate_ramp_speed()` function
- Removed `current_speed_us` variable tracking

### Fixed
- WebSerial command response visibility issue
- LED control now properly overrides all automatic behaviors
- micros() overflow handling for long-running operations

## [4.0] - 2025-12-24

### Added
- Complete code refactor for maintainability
- WebSerial debug console (password-protected)
- Acceleration/deceleration ramps for smooth movement (later removed in 4.1)
- Fixed micros() overflow handling for >71 minute uptimes
- MQTT discovery only publishes once (prevents spam)
- Command table system for easier command management
- Clean serial output with tagged messages
- Extended watchdog timeout (180 seconds)
- Factory reset via 5-second button hold
- mDNS support for .local hostname resolution

### Changed
- Cleaner MQTT position reporting (only on movement complete/stop)
- Improved motor state management with proper sequencing
- Better error messages throughout
- Reduced memory usage
- Optimized MQTT reconnection with exponential backoff

### Fixed
- Motor spinning on boot (critical safety fix)
- Critical pin mapping errors on ESP32-C3 Super Mini
- "Close" command not working (GPIO 9 was BOOT button)
- ENABLE pin unreliability (was on GPIO 0 strapping pin)
- Position drift during movement
- MQTT discovery spam on every reconnection
- WiFi reconnection handling

### Technical Details
- Proper A4988 initialization sequence (DISABLE → SLEEP first)
- Overflow-safe timing for micros() rollover
- Position saved every 50 steps during movement
- Motor state: DISABLE → SLEEP → WAKE → ENABLE sequence
- Watchdog protection against firmware hangs

## [3.x] - Prior Versions

### Features
- Basic MQTT integration
- WiFiManager configuration portal
- OTA updates
- Position tracking
- Telnet control (replaced with WebSerial in 4.0)
- Basic motor control

### Known Issues (Fixed in 4.0)
- Motor would spin during ESP32 boot
- Incorrect pin assignments for ESP32-C3 Super Mini
- Close command unreliable
- Position reporting spammed MQTT broker
- HA discovery republished on every connection
- micros() overflow after 71 minutes uptime

## Version History Summary

| Version | Date | Key Feature |
|---------|------|-------------|
| 4.1 | 2024-12-28 | Constant speed, LED control, pin remap |
| 4.0 | 2024-12-24 | Complete refactor, WebSerial, safety fixes |
| 3.x | 2024 | Basic functionality, Telnet control |

## Upgrade Notes

### From 4.0 to 4.1
- **Pin changes:** If upgrading hardware, rewire according to new pin mapping
- **No acceleration:** Movement is now constant speed throughout
- **LED control:** New commands available for manual LED control
- **Library change:** Install MycilaWebSerial instead of standard WebSerial

### From 3.x to 4.0
- **Critical:** Update wiring to correct pin assignments
- **Breaking:** Telnet removed, use WebSerial instead
- **Breaking:** Some configuration parameters changed
- **Recommended:** Factory reset and reconfigure after upgrade

## Future Roadmap

Potential features under consideration:
- TMC2208/TMC2209 driver support for silent operation
- Position profiles (favorite positions)
- Scheduling via MQTT
- Sunrise/sunset automation integration
- Web UI for configuration
- Multi-curtain synchronization
- End-stop sensor support
- Current sensing for obstacle detection

## Contributing

Contributions welcome! Please feel free to submit pull requests or open issues for:
- Bug reports
- Feature requests
- Documentation improvements
- Hardware compatibility reports
- Example configurations

## Support

For issues, questions, or discussions, please use the GitHub Issues page.

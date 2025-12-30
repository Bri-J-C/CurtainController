# First Boot 

## WiFi Configuration

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

## WebSerial Access

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

## Home Assistant Discovery

The controller automatically publishes Home Assistant MQTT discovery on first connection:
- Discovery topic: `homeassistant/cover/<hostname>/config`
- Appears as a "Cover" entity in Home Assistant
- Supports: open, close, stop, set position
- also works for openhab, just need to create item from the channel this creates.


## Arduino Setup

1. **Install ESP32 Board Support:**
   - File → Preferences → Additional Board Manager URLs
   - Add: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Boards Manager → Search "ESP32" → Install

2. **Install Required Libraries:**
   ```
   - WiFiManager by tzapu
   - PubSubClient by Nick O'Leary
   - ArduinoJson by Benoit Blanchon
   - ESPAsyncWebServer by lacamera
   - AsyncTCP by dvarrel
   - MycilaWebSerial by Mathieu Carbou
   ```

3. **Board Configuration:**
   - Board: "ESP32C3 Dev Module"
   - Upload Speed: 921600
   - USB CDC On Boot: "Enabled"
   - Flash Size: 4MB

4. **Upload:**
   - Open `CurtainController.ino`
   - Select correct COM port
   - Click Upload

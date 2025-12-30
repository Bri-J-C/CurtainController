## Motor Configuration

Default settings work but should be adjusted:

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
5. Typical range: 500-1500µs for quiet, reliable operation ive found
6. Using the POT on the motor controller will GREATLY determine the volume of operation. aim for JUST enough power to move the curtains
   while still being able to move the curtains.


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


# Technical Details

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

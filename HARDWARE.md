# Hardware Guide

## Bill of Materials (BOM)

### Required Components

| Component | Quantity | Notes |
|-----------|----------|-------|
| ESP32-C3 Super Mini | 1 | Development board with onboard USB-C |
| A4988 Stepper Driver | 1 | Includes heat sink recommended |
| Bipolar Stepper Motor | 1 | NEMA 17 typical, 200 steps/rev |
| 12V Power Supply | 1 | 2A minimum, 3A+ recommended |
| 5V Power Supply | 1 | For ESP32 (USB or buck converter) |
| Electrolytic Capacitor | 1 | 100µF 25V, across motor power |
| Jumper Wires | ~15 | Male-to-female or dupont |

### Optional Components

| Component | Purpose |
|-----------|---------|
| Heat Sink | For A4988 driver cooling |
| Buck Converter | Step down 12V to 5V for ESP32 |
| Enclosure | Project box for finished installation |
| Screw Terminals | Easier motor connection |
| PCB | Custom board for permanent installation |

## A4988 Driver Module

### Pinout
```
           ┌─────────┐
    ENABLE │1      16│ VDD (3-5.5V logic)
       MS1 │2      15│ GND
       MS2 │3      14│ 2B (Motor coil 2)
       MS3 │4      13│ 2A (Motor coil 2)
     RESET │5      12│ 1A (Motor coil 1)
     SLEEP │6      11│ 1B (Motor coil 1)
      STEP │7      10│ VMOT (8-35V motor)
       DIR │8       9│ GND
           └─────────┘
```

### Power Connections
- **VMOT & GND (motor power):** 12V supply for stepper motor
- **VDD & GND (logic power):** 5V from ESP32 (or 3.3V)
- **Add 100µF capacitor** between VMOT and GND near driver

### Current Limit Adjustment
The A4988 has a potentiometer for current limiting:
1. Measure motor's rated current (typically 1-2A per phase)
2. Set to 70% of rated current initially
3. Adjust if motor:
   - Too hot → Decrease current
   - Skipping steps → Increase current
4. Formula: `Vref = Current × 8 × Rcs` (Rcs typically 0.1Ω)

## ESP32-C3 Super Mini

### Pinout Reference
```
         USB-C
    ┌──────────────┐
    │   ┌────┐     │
    │   └────┘     │
    │              │
 0  │●            ●│ 5V
 1  │●            ●│ GND
 2  │●            ●│ 3V3
 3  │●            ●│ 10
 4  │●            ●│ 9  (BOOT)
 5  │●            ●│ 8  (LED)
 6  │●            ●│ 7
    │              │
    └──────────────┘
```

### Important Notes
- GPIO 8: Onboard blue LED (inverted logic)
- GPIO 9: Onboard BOOT button
- GPIO 0: Boot strapping pin (has internal pull-up)
- All GPIOs are 3.3V logic level

## Wiring Diagram

### Text-Based Connection Map

```
ESP32-C3          A4988 Driver          Stepper Motor
--------          ------------          -------------
GPIO 0  ────────► ENABLE
GPIO 1  ────────► MS1
GPIO 2  ────────► MS2
GPIO 3  ────────► MS3
GPIO 4  ────────► RESET
GPIO 6  ────────► DIR
GPIO 7  ────────► SLEEP
GPIO 10 ────────► STEP

3.3V or 5V ─────► VDD
GND      ────────► GND (logic)

12V PSU+ ───┬───► VMOT
            │
         [100µF]
            │
12V PSU- ───┴───► GND (motor)
                   GND (logic)

                   1A ─────────► Coil 1 Wire A (BLK)
                   1B ─────────► Coil 1 Wire B (GRN)
                   2A ─────────► Coil 2 Wire A (RED)
                   2B ─────────► Coil 2 Wire B (BLU)
```

### Connection Steps

1. **Power the A4988:**
   - Connect 12V PSU positive to VMOT
   - Connect 12V PSU negative to GND (motor ground)
   - Add 100µF capacitor across VMOT and GND
   - Connect ESP32 3.3V or 5V to A4988 VDD
   - Connect ESP32 GND to A4988 GND (logic ground)
   - **Ground must be common between ESP32 and A4988**

2. **Connect Control Signals:**
   - GPIO 0 → ENABLE
   - GPIO 1 → MS1
   - GPIO 2 → MS2
   - GPIO 3 → MS3
   - GPIO 4 → RESET
   - GPIO 6 → DIR
   - GPIO 7 → SLEEP
   - GPIO 10 → STEP

3. **Connect Motor:**
   - Identify motor coils (typically color-coded)
   - Coil 1 → 1A and 1B
   - Coil 2 → 2A and 2B
   - Use multimeter to verify coil pairs (continuity test)

4. **Test Connection:**
   - Power on ESP32 (via USB)
   - Motor should NOT move (ENABLE is HIGH)
   - LED should be ON
   - Connect to WebSerial and test commands

## Motor Identification

### Finding Coil Pairs

Bipolar stepper motors have 4 wires forming 2 coils. To identify:

1. **Using Multimeter (Continuity/Resistance mode):**
   - Test between wire pairs
   - Same coil: Low resistance (~2-20Ω) with continuity beep
   - Different coils: No continuity (open circuit)
   - Example: If BLK-GRN beeps, and RED-BLU beeps, you have your pairs

2. **Using Motor Datasheet:**
   - Check manufacturer specifications
   - Common patterns:
     - Wire colors: A-A', B-B'
     - Terminals: 1-2, 3-4

### Motor Specifications

Typical NEMA 17 stepper specs:
- **Steps per revolution:** 200 (1.8° per step)
- **Voltage:** 12V
- **Current per phase:** 1.0-2.0A
- **Holding torque:** 40-60 Ncm
- **Resistance:** 2-4Ω per coil

## Power Supply Sizing

### Motor Power (12V)
- **Minimum:** Motor current × 1.5
  - Example: 1.5A motor = 2.25A supply
- **Recommended:** Motor current × 2
  - Allows headroom for startup current
  - Example: 1.5A motor = 3A supply
- **Voltage:** Match motor rating (typically 12V)

### ESP32 Power (5V or 3.3V)
- **Current draw:** ~200-300mA typical
- **Options:**
  1. USB power (easiest for testing)
  2. Buck converter from 12V motor supply
  3. Separate 5V wall adapter

### Ground Connection
**CRITICAL:** ESP32 ground and A4988 ground must be connected together (common ground). Otherwise:
- Logic signals won't work properly
- Risk of damage to components
- Erratic behavior

## Heat Management

### A4988 Driver
- Gets hot during operation (normal)
- Attach heat sink for prolonged use
- Reduce current limit if excessively hot (>80°C)
- Ensure airflow in enclosure

### Stepper Motor
- Will warm up during operation (normal)
- Enable motor sleep to reduce idle heating
- Should not exceed 80°C during operation
- If too hot: reduce current or improve cooling

## Physical Mounting

### Considerations for Curtain Installation

1. **Motor Mounting:**
   - Rigid mounting to window frame or wall
   - Use motor mounting bracket
   - Ensure shaft is aligned with curtain rod/pulley
   - Minimize vibration transmission

2. **Cable Management:**
   - Use flexible cable for motor wires
   - Avoid sharp bends near motor
   - Secure cables to prevent snagging
   - Consider cable chain for moving parts

3. **Coupling:**
   - Shaft coupler between motor and curtain mechanism
   - GT2 pulley/belt system common for curtains
   - 3D printed adapters often used
   - Ensure no backlash in coupling

## Safety Considerations

- **Electrical:**
  - Use proper wire gauge for motor current
  - Secure all connections
  - Add fuse in 12V supply line
  - Ensure waterproof enclosure if near moisture

- **Mechanical:**
  - Test motor direction before mounting
  - Add end stops to prevent over-travel
  - Ensure curtain won't bind or jam
  - Child safety: keep cords and moving parts inaccessible

- **Thermal:**
  - Ensure adequate ventilation
  - Monitor temperatures during first operation
  - Don't enclose in sealed box without cooling

## Testing Checklist

Before final installation:

- [ ] Verify all wiring connections
- [ ] Check power supply voltages
- [ ] Test motor rotation both directions
- [ ] Verify WebSerial access
- [ ] Test MQTT connectivity
- [ ] Confirm motor doesn't overheat
- [ ] Test emergency stop function
- [ ] Verify position tracking accuracy
- [ ] Test factory reset procedure
- [ ] Document final configuration settings

## Troubleshooting Hardware Issues

### Motor Not Moving
1. Check 12V power supply is connected
2. Verify A4988 has both VMOT and VDD powered
3. Check common ground connection
4. Test with multimeter: VMOT should read 12V
5. Verify motor coil connections (swap if needed)

### Motor Vibrating/Noisy
1. Adjust A4988 current limit
2. Increase microstepping mode
3. Check motor mounting is rigid
4. Verify no mechanical binding

### Excessive Heat
1. Reduce A4988 current limit
2. Enable motor sleep feature
3. Add heat sink to A4988
4. Improve enclosure ventilation
5. Check for mechanical resistance

### Intermittent Operation
1. Check all wire connections are secure
2. Verify power supply is adequate
3. Add bulk capacitor if not present
4. Check for electromagnetic interference
5. Ensure common ground connection

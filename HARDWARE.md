# Hardware Guide

## Bill of Materials (BOM)

### Core Components
- **ESP32-C3 Super Mini** development board https://a.co/d/arRZOZd
- **A4988 Stepper Motor Driver** module https://a.co/d/e3AHq4l
- **Bipolar Stepper Motor** (200 steps/revolution typical) https://a.co/d/01f4S5y
- **12-24V Power Supply** (2A+ recommended for motor) i have a ton of these laying around.
- **Buck Converter** for ESP32 5v Power https://a.co/d/2wXIIM7
- **Perf Board** 4cm x 6cm Used this kit, which included headers, pins, and terminal blocks which are all necessary for this project. https://a.co/d/gyRBBOJ

### Optional
- Capacitor (100µF) across motor power supply for stability ( i didnt use but read youre supposed to)
- Heat sink for A4988 driver (Comes with module linked)

### Curtain Components 
- **Curtains**
- **Curtain Tracks** Need to be as low friction as possible to take full advantage of quiet motor operation. https://a.co/d/g6LfgsY
- **GT2 Timing Belt 6mm Width** used as the pulley to move curtains (comes with kit linked) https://a.co/d/hmIUbOe
- **5mm Bore Belt Pulley Wheel** Pulley attached to motor(comes with kit linked) https://a.co/d/hmIUbOe
- **5mm Idler Pulley** Pulley on shaft(Comes with kit linked) https://a.co/d/hmIUbOe
- **Belt Tensioner** Used to take slack out of belt (Comes with kit linked) https://a.co/d/hmIUbOe
- **Belt Clamp** Used to hold the belt together (Comes with kit linked) https://a.co/d/hmIUbOe
- **Stepper Motor Mount** used to hold stepper motor to wall https://a.co/d/9G7hsNz
- **idler pulley mount**  used to hold idler components to wall ( i custom made this out of the old curtain rod holder i had drilling a hole for screw and nut)

## A4988 Driver Module

### Pinout
```
           ┌─────────┐
    ENABLE │1      16│ VMOT (12-24V)
       MS1 │2      15│ GND
       MS2 │3      14│ 2B (Motor coil 2)
       MS3 │4      13│ 2A (Motor coil 2)
     RESET │5      12│ 1A (Motor coil 1)
     SLEEP │6      11│ 1B (Motor coil 1)
      STEP │7      10│ VDD (5V)
       DIR │8       9│ GND
           └─────────┘
```

### Power Connections
- **VMOT & GND (motor power):** 12-30V supply for stepper motor
- **VDD & GND (logic power):** 5V from ESP32/buck converter
- **(Optional)Add 100µF capacitor** between VMOT and GND near driver

### Current Limit Adjustment
The A4988 has a potentiometer for current limiting:
1. Measure motor's rated current (typically 1-2A per phase)
2. Set to 70% of rated current initially
3. Adjust if motor:
   -Not enough torque to move curtains.
   -Adjust until quiet with enough power.

## ESP32-C3 Super Mini

### Pinout Reference
```
          ┌───────────────────────────┐
          │           ┌────┐          │
          │           └────┘          │
       5  │●                         ●│ 5V
       6  │●                         ●│ GND
       7  │●                         ●│ 3V3
 (LED) 8  │●                         ●│ 4
(BOOT) 9  │●                         ●│ 3
       10 │●                         ●│ 2  
       20 │●                         ●│ 1
       21 │●                         ●│ 0
          └───────────────────────────┘
```
 

## Wiring Diagram

### Text-Based Connection Map (gameFAQ style) 
#### IMPORTANT NOTES
       -  This view is from the top down. wiring is done from underneath so it will be opposite in practice.
       -  Used headers to socket the componnents so they could be removed.
       -  Pulled the pins out of the socket NOT the esp32 for GPIO pins 20 and 21 to make wiring easier.

wiring goes under the componenets

```
                               
              ┌──────────────────────────────────┐
              │              ┌────┐              │
              │              └────┘              │
            5 │●             ESP32C3            ●│ 5V---------------┐
         ┌--6-|●                                ●│-GND--┐           |
         |  7 │●-----┐                          ●│ 3V3  |           |
         |  8 │●     |     ┌--------------------●│ 4    |           |
         |  9 │●     |     |     ┌ -------------●│ 3    |           |
         | 10 │●     |     |     |    ┌---------●│ 2    |           |
         | 20 │●     |     |     |    |    ┌----●│ 1    |           | 
         | 21 │●     |     |     |    |    |    ●│ 0    |           |
         |    └|─────|─────|─────|────|────|────|┘    ┌─|───────────|─┐    
         |     |     |     |     |    |    |    |  GND| ●           ● |5V+
         |     |     |     |     |    |    |    |     | |           | |
         |     |     |     |     |    |    |    |     | |           | |
         |     |     |     |     |    |    |    |     | |           | |
        DIR  STEP  SLEEP RESET  MS3  MS2  MS1   EN    | |   BUCK    | |
         8     7     6      5     4    3    2    1    | | Converter | |
        ┌─────────────────────────────────────────┐   | |           | |
        | ┌---------------------------------------|---|-┘           | |
        │ |   ┌-----------------------------------|---|-------------┘ |
        │ |   |           A4988           +-------│---|-● GND    V+ ● |
        │ |   |                           |       │   └─────────────|─┘
        └─|───|───────────────────────────|───────┘        ┌--------┘
          9   10    11   12    13   14   15    16----------+
         GND  VDD   1B   1A    2A   2B   GND   VMOT   ┌────|─┐
                     |    └-┐ ┌-┘   |    └------------|-●  ● | 2Pin Connector
                     └----┐ | | ┌---┘                 └─|──|─┘
                          ● ● ● ●                       |  |                                     
                  Pins for Motor Connector            GND  +12-24v       
                                                    ┌───|──|──────┐
                                                    | PowerSupply |
                                                    └─────────────┘
```

## Solder Connections
![alt text](https://github.com/Bri-J-C/CurtainController/blob/main/solder_underneath.jpg)

## PCB Layout
![alt text](https://github.com/Bri-J-C/CurtainController/blob/main/top_layout.jpg)

## Idler Bracket
![alt text](https://github.com/Bri-J-C/CurtainController/blob/main/idler_pulley_bracket.jpg)
#### Note: 
    This is just the old curtain bracket, screw and nuts that come with the stepper motor mounts.
    Drilled hole about the same size as the nut so that when tightening together it sat in the hole to prevent unscrewing itself.


### Motor Specifications

Typical NEMA 17 stepper specs:
- **Steps per revolution:** 200 (1.8° per step)
- **Voltage:** 12-24V
- **Current per phase:** 1.0-2.0A
- **Holding torque:** 40-60 Ncm
- **Resistance:** 2-4Ω per coil

### Power Supply Sizing
- **Minimum:** 12V 2A
- **Maximum:** 24v 3A

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
   - Minimize vibration transmission - i used the rubber stoppers for cabinets. i got a pack of like 500 on amazon for a couple bucks

2. **Cable Management:**
   - Avoid sharp bends
   - Secure cables to prevent snagging

## Safety Considerations

- **Electrical:**
  - Use proper wire gauge for motor current
  - Secure all connections

- **Mechanical:**
  - Ensure curtain won't bind or jam
  - Reduce friction where possible

- **Thermal:**
  - Ensure adequate ventilation
  - Monitor temperatures during first operation
  - Don't enclose in sealed box without cooling

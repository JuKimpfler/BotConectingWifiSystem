# Hardware Documentation – BotConnectingWifiSystem

This document describes the hardware components, wiring, pin assignments, and physical setup of the BotConnectingWifiSystem.

## Table of Contents

1. [System Overview](#system-overview)
2. [Hardware Components](#hardware-components)
3. [Wiring Diagrams](#wiring-diagrams)
4. [Pin Assignments](#pin-assignments)
5. [Power Requirements](#power-requirements)
6. [Physical Assembly](#physical-assembly)
7. [Troubleshooting](#troubleshooting)

---

## System Overview

The BotConnectingWifiSystem consists of 6 physical devices organized in a 3-node wireless mesh:

```
┌─────────────┐
│  Browser    │
│  (User PC)  │
└──────┬──────┘
       │ WiFi (AP Mode)
       │
┌──────▼──────────┐
│   ESP #3 HUB    │
│ (ESP32-C3 #3)   │
└─────────────────┘
       │
       │ ESP-NOW (Wireless)
       │
       ├──────────────────────┬──────────────────────┐
       │                      │                      │
┌──────▼──────────┐    ┌─────▼──────────┐    ┌─────▼──────────┐
│  ESP #1 SAT1    │◄──►│  ESP #2 SAT2   │    │                │
│  (ESP32-C3 #1)  │    │  (ESP32-C3 #2) │    │                │
└────────┬────────┘    └────────┬───────┘    │                │
         │ UART                 │ UART        │   P2P Bridge   │
         │ 115200 baud          │ 115200      │   (7ms cycle)  │
┌────────▼────────┐    ┌────────▼────────┐   │                │
│  Teensy #1      │    │  Teensy #2      │   │                │
│  (Teensy 4.0)   │    │  (Teensy 4.0)   │   │                │
│  Robot 1 Brain  │    │  Robot 2 Brain  │   │                │
└─────────────────┘    └─────────────────┘   └────────────────┘
```

**Key Points:**
- **Hub (ESP #3):** Hosts Web UI via WiFi AP, routes commands via ESP-NOW
- **Satellites (ESP #1, #2):** Bridge UART to Teensy, maintain P2P link
- **Teensy (1, 2):** Motor control, sensor processing, robot logic

---

## Hardware Components

### Required Components

| Component | Qty | Model/Type | Purpose | Link |
|-----------|-----|------------|---------|------|
| ESP32-C3 Development Board | 3 | Seeed Studio XIAO ESP32-C3 | Wireless communication & control | [Seeed Wiki](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) |
| Teensy Board | 2 | Teensy 4.0 | Robot motor control & sensors | [PJRC Teensy 4.0](https://www.pjrc.com/store/teensy40.html) |
| USB-C Cable | 3+ | Standard USB-C | Flashing & power for ESP32-C3 | – |
| Micro-USB Cable | 2+ | Standard Micro-USB | Flashing & power for Teensy | – |
| Jumper Wires | 6+ | Male-to-Male or Female-to-Male | UART connections | – |

### Optional Components

| Component | Purpose |
|-----------|---------|
| Power Banks (5V) | Portable power for ESP32 & Teensy |
| Breadboards | Prototyping and testing |
| Voltage Regulator | If using battery power >5V |
| Heat Shrink Tubing | Wire protection |

### Component Specifications

#### Seeed Studio XIAO ESP32-C3

- **MCU:** ESP32-C3 (RISC-V, 160 MHz)
- **Flash:** 4 MB
- **RAM:** 400 KB SRAM
- **Wireless:** WiFi 802.11 b/g/n, BLE 5.0
- **I/O:** 11 digital pins, 4 analog inputs
- **UART:** 2 hardware UARTs
- **Dimensions:** 21mm × 17.8mm
- **Power:** 5V via USB-C or 5V/3.3V on pins

**Pinout Reference:**
```
         ┌─────────┐
     5V  │●       ●│ GND
    GND  │●       ●│ 3V3
     D0  │●       ●│ D10
     D1  │●       ●│ D9
     D2  │●       ●│ D8
     D3  │●       ●│ D7 (RX - GPIO20)
     D4  │●       ●│ D6 (TX - GPIO21)
     D5  │●       ●│ USB
         └─────────┘
```

#### Teensy 4.0

- **MCU:** ARM Cortex-M7 (600 MHz)
- **Flash:** 2 MB
- **RAM:** 1 MB
- **I/O:** 40 digital pins
- **UART:** 8 hardware UARTs (Serial1-Serial8)
- **Dimensions:** 35.6mm × 17.8mm
- **Power:** 5V via USB or Vin pin

---

## Wiring Diagrams

### ESP Satellite ↔ Teensy Connection

Each satellite ESP32 connects to its corresponding Teensy via UART (Serial1).

#### Connection Table

| Signal | ESP32-C3 Pin | GPIO | Teensy 4.0 Pin | Notes |
|--------|--------------|------|----------------|-------|
| TX (ESP→Teensy) | D6 | GPIO21 | Pin 0 (Serial1 RX) | Crossed connection |
| RX (ESP←Teensy) | D7 | GPIO20 | Pin 1 (Serial1 TX) | Crossed connection |
| GND | GND | – | GND | Common ground required |

> **Important:** TX of ESP must connect to RX of Teensy, and vice versa (crossover connection).

#### Visual Diagram

```
ESP32-C3 SAT1                    Teensy 4.0 #1
┌──────────────┐                ┌──────────────┐
│              │                │              │
│  D6 (TX) ────┼────────────────┼───► Pin 0    │ Serial1 RX
│              │                │     (RX)     │
│  D7 (RX) ◄───┼────────────────┼──── Pin 1    │ Serial1 TX
│              │                │     (TX)     │
│  GND     ────┼────────────────┼──── GND      │
│              │                │              │
└──────────────┘                └──────────────┘
```

**Same connection for SAT2 ↔ Teensy #2**

### Complete System Wiring

```
                      User Computer / Smartphone
                              │
                              │ WiFi AP (ESP-Hub)
                              │ SSID: ESP-Hub
                              │ IP: 192.168.4.1
                              │
                    ┌─────────▼──────────┐
                    │   ESP32-C3 #3      │
                    │   (HUB)            │
                    │   - Hosts Web UI   │
                    │   - Routes cmds    │
                    └─────────┬──────────┘
                              │
                  ┌───────────┴──────────┐
                  │   ESP-NOW Wireless   │
                  │   Channel 6          │
                  │   Encrypted (LTK)    │
                  └───────────┬──────────┘
                              │
        ┌─────────────────────┴─────────────────────┐
        │                                           │
┌───────▼────────┐  P2P ESP-NOW Link    ┌──────────▼────────┐
│  ESP32-C3 #1   │◄─────────────────────►│  ESP32-C3 #2     │
│  (SAT1)        │    ~7ms cycle          │  (SAT2)          │
│                │                        │                  │
└───────┬────────┘                        └──────────┬───────┘
        │                                            │
        │ UART Serial                                │ UART Serial
        │ 115200 baud                                │ 115200 baud
        │ TX: D6 (GPIO21) → Teensy RX (Pin 0)        │
        │ RX: D7 (GPIO20) ← Teensy TX (Pin 1)        │
        │                                            │
┌───────▼────────┐                        ┌──────────▼───────┐
│  Teensy 4.0 #1 │                        │  Teensy 4.0 #2   │
│  Robot 1       │                        │  Robot 2         │
│  - Motors      │                        │  - Motors        │
│  - Sensors     │                        │  - Sensors       │
└────────────────┘                        └──────────────────┘
```

---

## Pin Assignments

### ESP32-C3 Hub (ESP #3)

The hub uses WiFi only; no GPIO pins are required for operation.

| Pin | Function | Notes |
|-----|----------|-------|
| USB-C | Power & Programming | Connect to computer or 5V power |
| Built-in LED | Status indicator | Optional – can be used for status |

### ESP32-C3 Satellite (ESP #1, #2)

Both satellites use identical pin assignments.

| Pin | GPIO | Function | Connected To | Voltage |
|-----|------|----------|--------------|---------|
| D6 | GPIO21 | UART TX | Teensy Serial1 RX (Pin 0) | 3.3V logic |
| D7 | GPIO20 | UART RX | Teensy Serial1 TX (Pin 1) | 3.3V logic |
| GND | – | Ground | Teensy GND | – |
| 5V | – | Power In | USB-C or external 5V | – |
| 3V3 | – | Power Out | (Optional) Can power 3.3V sensors | Max 700mA |

> **Note:** ESP32-C3 operates at 3.3V logic, but Teensy 4.0 pins are 3.3V compatible. No level shifters required.

### Teensy 4.0 (Both)

| Pin | Function | Connected To | Notes |
|-----|----------|--------------|-------|
| Pin 0 | Serial1 RX | ESP32-C3 D6 (TX) | Receives commands from ESP |
| Pin 1 | Serial1 TX | ESP32-C3 D7 (RX) | Sends telemetry to ESP |
| GND | Ground | ESP32-C3 GND | Common ground |
| VIN | Power In (5V) | USB or external | Do not exceed 5.5V |
| 3.3V | Power Out | (Optional) Sensors | Max 250mA |

**Other pins** are available for motors, sensors, etc., as needed by your robot application.

---

## Power Requirements

### Power Consumption

| Device | Typical | Peak | Notes |
|--------|---------|------|-------|
| ESP32-C3 (Hub) | 80 mA | 350 mA | Higher when WiFi transmitting |
| ESP32-C3 (Satellite) | 70 mA | 300 mA | Higher during ESP-NOW TX |
| Teensy 4.0 | 100 mA | 150 mA | Plus motor/sensor load |

### Power Supply Options

#### Option 1: USB Power (Development)

**Pros:**
- Simple, no additional components
- Stable 5V supply
- Easy debugging via serial monitor

**Cons:**
- Tethered to computer or USB power banks
- Multiple cables required

**Setup:**
- Connect each ESP32-C3 via USB-C cable
- Connect each Teensy via Micro-USB cable
- Total: 5 USB cables required (or use powered USB hubs)

#### Option 2: Shared 5V Power (Deployment)

**Pros:**
- Cleaner wiring
- Portable (use battery packs)
- Fewer cables

**Cons:**
- Requires power distribution
- Need proper current capacity

**Setup:**
```
5V Battery Pack (≥2A)
├─► ESP32-C3 #3 (HUB) – 5V pin
├─► ESP32-C3 #1 (SAT1) – 5V pin
├─► ESP32-C3 #2 (SAT2) – 5V pin
├─► Teensy #1 – VIN pin
└─► Teensy #2 – VIN pin
```

**Recommended Battery:**
- **Capacity:** ≥5000mAh for ~8 hours runtime
- **Output:** 5V, ≥2A
- **Type:** USB power bank with multiple outputs

#### Option 3: On-Robot Power (Competition)

Each robot typically has its own battery. In this case:
- **Hub:** Powered separately (USB power bank, wall adapter, etc.)
- **SAT1 + Teensy #1:** Powered by Robot 1 battery
- **SAT2 + Teensy #2:** Powered by Robot 2 battery

```
Robot 1 Battery (7.4V LiPo)
├─► Buck Converter (7.4V → 5V)
    ├─► ESP32-C3 #1 (SAT1)
    └─► Teensy #1

Robot 2 Battery (7.4V LiPo)
├─► Buck Converter (7.4V → 5V)
    ├─► ESP32-C3 #2 (SAT2)
    └─► Teensy #2

Hub Power (separate)
└─► ESP32-C3 #3 (HUB) – USB power bank or wall adapter
```

### Power Budget Example

For a complete system:

| Device | Current | Qty | Total |
|--------|---------|-----|-------|
| ESP32-C3 | 100 mA avg | 3 | 300 mA |
| Teensy 4.0 | 120 mA avg | 2 | 240 mA |
| **Subtotal** | | | **540 mA** |
| **Motors/Sensors** | | | +500-2000 mA |
| **Total System** | | | **~1-2.5A** |

> **Recommendation:** Use a power supply rated for at least **3A** to provide headroom.

---

## Physical Assembly

### Development Setup

For development and testing:

1. **Hub (ESP #3):**
   - Connect to computer via USB-C
   - Place near computer for easy access

2. **Satellites (ESP #1, #2):**
   - Mount on breadboards or standoffs
   - Position within 5-10 meters of hub (ESP-NOW range)

3. **Teensy Boards (1, 2):**
   - Mount on breadboards next to respective satellites
   - Connect UART wires (TX/RX/GND)

4. **Cable Management:**
   - Use color-coded wires (red=TX, yellow=RX, black=GND)
   - Label wires with tape or heat shrink
   - Keep UART wires short (<30cm) to reduce noise

### Production/Competition Setup

1. **Hub Placement:**
   - Central location with good line-of-sight to playing field
   - Elevated position (e.g., on table or tripod) improves range
   - Connect to stable power source

2. **Robot Integration:**
   - Mount ESP32-C3 satellite on each robot
   - Mount Teensy on each robot
   - Use standoffs or 3D-printed mounts
   - Route wires through robot chassis
   - Strain relief for all connections

3. **Antenna Considerations:**
   - Keep ESP32-C3 antennas unobstructed
   - Avoid mounting near metal objects
   - Orient antennas perpendicular to ground for best range

4. **Environmental Protection:**
   - Consider enclosures for electronics (dust, impacts)
   - Ensure ventilation for heat dissipation
   - Protect USB connectors from stress

---

## Troubleshooting

### Hardware Issues

| Symptom | Likely Cause | Solution |
|---------|-------------|----------|
| ESP32 not detected by computer | Bad USB cable or driver issue | Try different cable; install CH340 drivers |
| ESP32 keeps rebooting | Insufficient power supply | Use better USB cable or power source (≥500mA) |
| No UART communication | Wrong TX/RX wiring | Verify crossover: ESP TX → Teensy RX, ESP RX → Teensy TX |
| Intermittent UART errors | Loose connections | Check all wire connections; solder if possible |
| Short ESP-NOW range | Antenna obstruction or interference | Reposition hub; check for WiFi interference on channel 6 |

### Wiring Verification

Use a multimeter to verify connections:

1. **Continuity Test:**
   - ESP D6 ↔ Teensy Pin 0 (should beep)
   - ESP D7 ↔ Teensy Pin 1 (should beep)
   - ESP GND ↔ Teensy GND (should beep)

2. **Voltage Test:**
   - ESP 3.3V pin: Should read ~3.3V
   - ESP 5V pin: Should read ~5.0V (when powered)
   - Teensy 3.3V pin: Should read ~3.3V (when powered)

3. **No Short Circuits:**
   - 5V to GND: Should read open circuit (OL)
   - 3.3V to GND: Should read open circuit (OL)

### Power Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| Brown-out resets | Voltage drops below threshold | Use power supply with higher current rating |
| ESP32 gets hot | Short circuit or overcurrent | Check for shorts; verify no reversed polarity |
| Teensy not powering on | Insufficient voltage | Ensure 5V supply; check polarity |

### Physical Assembly Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| Vibration disconnects wires | Poor connections | Use locking connectors or solder joints |
| Wires break at connector | Stress/fatigue | Add strain relief; use flexible wire |
| Interference from motors | EMI from motor brushes | Add capacitors across motor terminals; route wires away |

---

## Safety Considerations

1. **Voltage Limits:**
   - Never exceed 5.5V on ESP32-C3 or Teensy 4.0
   - Use proper voltage regulators for battery power

2. **Current Limits:**
   - Do not draw more than 700mA from ESP32 3.3V pin
   - Do not draw more than 250mA from Teensy 3.3V pin

3. **Heat:**
   - Voltage regulators may get hot under load
   - Ensure adequate ventilation
   - Consider heat sinks for regulators

4. **Static Electricity:**
   - Ground yourself before handling boards
   - Use ESD-safe work surface if possible

5. **Short Circuit Protection:**
   - Double-check wiring before powering on
   - Use fused power supplies when possible

---

## Reference Diagrams

### ESP32-C3 XIAO Pinout

```
                  USB-C
                    │
        ┌───────────┴───────────┐
        │                       │
    5V  │●                     ●│ GND
   GND  │●                     ●│ 3V3
    D0  │●   SEEED XIAO       ●│ D10
    D1  │●    ESP32-C3         ●│ D9
    D2  │●                     ●│ D8
    D3  │●                     ●│ D7 (GPIO20 - UART RX)
    D4  │●                     ●│ D6 (GPIO21 - UART TX)
    D5  │●                     ●│ USB_DP
        │                       │
        └───────────────────────┘
```

### Teensy 4.0 Top View (USB at top)

```
                 USB (Micro)
                     │
    ┌────────────────┴────────────────┐
    │  GND                         VIN │ 5V In
    │  Pin 0 (RX1) ◄───ESP D6         │
    │  Pin 1 (TX1) ─────►ESP D7       │
    │  Pin 2                           │
    │  ...                        3.3V │ 3.3V Out
    │                              GND │
    └─────────────────────────────────┘
```

---

## Additional Resources

- **Seeed XIAO ESP32-C3:**
  - [Getting Started Guide](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/)
  - [Pinout Diagram](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/#pinout-diagram)

- **Teensy 4.0:**
  - [Teensy 4.0 Page](https://www.pjrc.com/store/teensy40.html)
  - [Pinout Card (PDF)](https://www.pjrc.com/teensy/card11a_rev2.pdf)

- **ESP-NOW:**
  - [ESP-NOW Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/network/esp_now.html)

---

**Document Version:** 1.0
**Last Updated:** 2026-03-19

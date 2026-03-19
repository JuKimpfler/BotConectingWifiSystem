# BotConnectingWifiSystem – Complete Documentation

Welcome to the comprehensive documentation for the BotConnectingWifiSystem. This system enables wireless control of two competing robots through a 3-node ESP-NOW mesh network.

## 📖 Documentation Structure

### Quick Start

- **[../README.md](../README.md)** – Quick start guide and basic setup

### Detailed Guides

#### Setup & Installation
- **[Setup.md](Setup.md)** – Complete step-by-step setup instructions
  - Prerequisites (hardware & software)
  - Building and flashing firmware
  - Installing Teensy library
  - First boot and pairing
  - Verification and troubleshooting

#### Hardware
- **[Hardware.md](Hardware.md)** – Hardware documentation with diagrams
  - Component specifications (ESP32-C3, Teensy 4.0)
  - Wiring diagrams and pin assignments
  - Power requirements and options
  - Physical assembly guide
  - Troubleshooting hardware issues

#### Software Architecture
- **[Software.md](Software.md)** – Software architecture and protocol
  - System architecture overview
  - Folder structure
  - Message protocol specification
  - Hub and satellite software components
  - Configuration system
  - Unit tests
  - Development guide

#### Communication
- **[Bridge.md](Bridge.md)** – P2P bridge documentation
  - P2P ESP-NOW bridge between satellites
  - Message routing logic
  - Transparent UART bridge
  - Performance characteristics (~7ms latency)
  - Implementation details
  - Advanced use cases

#### Web Interface
- **[Webserver.md](Webserver.md)** – Web UI documentation
  - Accessing the web interface
  - UI architecture and layout
  - Debug tab (telemetry and logs)
  - Manual control tab (D-pad, sliders, buttons)
  - Modes tab (autonomous behaviors)
  - Calibrate tab (sensor calibration)
  - Settings tab (configuration and pairing)
  - WebSocket protocol
  - Development guide

#### Teensy Integration
- **[Teensy.md](Teensy.md)** – BotConnect library reference
  - Library installation
  - Quick start guide
  - Complete API reference
  - Command callbacks (control, mode, calibrate)
  - Telemetry functions
  - Examples and best practices
  - Troubleshooting

### Additional Documentation

- **[USB_PROTOCOL.md](USB_PROTOCOL.md)** – USB debugging commands (German)
  - USB service commands (mac, debug, clearmac, help)
  - USB telemetry injection
  - Transparent UART bridge details
  - Routing overview

- **[../BugFixes.txt](../BugFixes.txt)** – Bug fixes and improvements (German)
  - Fixed critical and high-priority bugs
  - Test results and verification
  - Recommendations for future improvements

---

## 🚀 Quick Navigation

### I want to...

#### ...get started quickly
→ Start with [../README.md](../README.md) for the quick start guide

#### ...set up the system from scratch
→ Follow [Setup.md](Setup.md) for detailed installation steps

#### ...understand the hardware connections
→ See [Hardware.md](Hardware.md) for wiring diagrams and specifications

#### ...learn how the system works
→ Read [Software.md](Software.md) for architecture and protocol details

#### ...use the web interface
→ Check [Webserver.md](Webserver.md) for UI features and controls

#### ...program my Teensy board
→ Refer to [Teensy.md](Teensy.md) for the BotConnect library API

#### ...implement robot-to-robot communication
→ Explore [Bridge.md](Bridge.md) for P2P bridge capabilities

#### ...troubleshoot problems
→ Each guide has a dedicated troubleshooting section

---

## 📋 System Overview

### Architecture

The BotConnectingWifiSystem consists of:
- **1 Hub (ESP32-C3 #3):** Hosts WiFi AP and web interface
- **2 Satellites (ESP32-C3 #1, #2):** Bridge UART to Teensy, maintain P2P link
- **2 Teensy (4.0 #1, #2):** Motor control and sensor processing

```
 [Browser]
     │  WebSocket
 [ESP #3 – HUB]  ─ ESP-NOW ──►  [ESP #1 – SAT1]  ─ UART ──►  [Teensy #1]
                  ◄──────────    [ESP #1 – SAT1]  ◄─ UART ─   [Teensy #1]
                                       │ ▲
                              P2P ~7ms │ │
                                       ▼ │
                  ─ ESP-NOW ──►  [ESP #2 – SAT2]  ─ UART ──►  [Teensy #2]
                  ◄──────────    [ESP #2 – SAT2]  ◄─ UART ─   [Teensy #2]
```

### Key Features

✅ **Wireless Control:** Browser-based UI via WiFi AP
✅ **Low Latency P2P:** 7ms direct satellite-to-satellite communication
✅ **Real-Time Telemetry:** Live sensor data in web interface
✅ **Multiple Control Modes:** Manual, autonomous, calibration
✅ **Secure Communication:** ESP-NOW encryption with PMK/LTK
✅ **Easy Integration:** Arduino library for Teensy
✅ **Extensible:** Open protocol for custom commands

---

## 🛠️ Technology Stack

| Component | Technology | Purpose |
|-----------|------------|---------|
| **Microcontrollers** | ESP32-C3, Teensy 4.0 | Wireless comm & motor control |
| **Wireless Protocol** | ESP-NOW | Low-latency mesh network |
| **Web Frontend** | Vanilla JS + Vite | Browser-based control UI |
| **Web Backend** | AsyncWebServer | ESP32 web server |
| **Communication** | WebSocket, UART | Real-time bidirectional |
| **Build Tools** | PlatformIO, npm, CMake | Firmware & UI compilation |
| **Testing** | CTest | Unit tests for protocol |

---

## 📦 Repository Structure

```
BotConnectingWifiSystem/
├── README.md                   ← Quick start guide
├── BugFixes.txt                ← Bug fix documentation (German)
│
├── Doku/                       ← Comprehensive documentation
│   ├── README.md               ← This file (documentation index)
│   ├── Setup.md                ← Setup guide
│   ├── Hardware.md             ← Hardware documentation
│   ├── Software.md             ← Software architecture
│   ├── Bridge.md               ← P2P bridge documentation
│   ├── Webserver.md            ← Web UI documentation
│   ├── Teensy.md               ← Teensy library reference
│   └── USB_PROTOCOL.md         ← USB debugging (German)
│
├── shared/                     ← Protocol definitions
│   ├── messages.h              ← Frame structs and message types
│   ├── crc16.h                 ← CRC-16/MODBUS implementation
│   ├── config_schema.json      ← Configuration schema
│   └── config_default.json     ← Default configuration
│
├── ESP_Hub/                    ← Hub firmware + Web UI
│   ├── platformio.ini
│   ├── include/                ← Hub headers
│   ├── src/                    ← Hub implementation
│   └── ui/                     ← Vite Web UI
│       ├── index.html
│       ├── package.json
│       └── src/
│
├── ESP_Satellite/              ← Satellite firmware
│   ├── platformio.ini
│   ├── include/                ← Satellite headers
│   └── src/                    ← Satellite implementation
│
├── Teensy_lib/                 ← BotConnect Arduino library
│   ├── src/
│   │   ├── BotConnect.h
│   │   └── BotConnect.cpp
│   └── examples/
│       └── BasicUsage/
│
└── test/unit/                  ← Unit tests
    ├── CMakeLists.txt
    ├── test_crc16.cpp
    ├── test_messages.cpp
    └── test_command_parser.cpp
```

---

## 🔧 Development Workflow

### 1. Initial Setup
Follow [Setup.md](Setup.md) to install tools and flash firmware.

### 2. Development Cycle
1. Make code changes in `ESP_Hub`, `ESP_Satellite`, or `Teensy_lib`
2. Build: `pio run -e esp_hub` (or `esp_sat1`, `esp_sat2`)
3. Flash: `pio run -e esp_hub -t upload`
4. Test: Monitor serial output with `pio device monitor`

### 3. UI Development
1. Edit files in `ESP_Hub/ui/src/`
2. Test: `npm run dev` (dev server with hot reload)
3. Build: `npm run build`
4. Upload: `pio run -e esp_hub -t uploadfs`

### 4. Testing
```bash
cd test/unit && mkdir build && cd build
cmake .. -DSAT_ID=1 && make && ctest
```

---

## 📊 Message Protocol Summary

### Frame Format

```
┌────────┬──────────┬─────┬──────────┬──────────┬───────┬─────┬──────┬─────────┬────────┐
│ Magic  │ Msg Type │ Seq │ Src Role │ Dst Role │ Flags │ Len │ Rsvd │ Payload │ CRC-16 │
│ 0xBE   │  1 byte  │ 1 B │  1 byte  │  1 byte  │ 1 B   │ 1 B │  1 B │ 0-180 B │  2 B   │
└────────┴──────────┴─────┴──────────┴──────────┴───────┴─────┴──────┴─────────┴────────┘
```

### Message Types

| Type | Value | Description | ACK? |
|------|-------|-------------|------|
| DBG | 0x01 | Telemetry | No |
| CTRL | 0x02 | Control (speed, angle, switches, buttons) | No |
| MODE | 0x03 | Mode selection | Yes |
| CAL | 0x04 | Calibration | Yes |
| PAIR | 0x05 | Pairing | Yes |
| HB | 0x06 | Heartbeat | No |
| ACK | 0x07 | Acknowledgement | No |
| ERR | 0x08 | Error response | No |
| SET | 0x09 | Settings update | Yes |
| DISC | 0x0A | Discovery | No |

See [Software.md](Software.md) for complete protocol specification.

---

## 🎯 Use Cases

### Competition Robots
- **Two competing robots** in RoboCup, VEX, or similar competitions
- **Real-time coordination** via P2P bridge
- **Manual override** for testing or emergency control

### Research & Education
- **Multi-robot systems** research
- **Wireless communication** protocols
- **Embedded systems** education

### Prototyping
- **Rapid prototyping** of robot behaviors
- **Easy telemetry** visualization
- **Web-based tuning** of control parameters

---

## 🤝 Contributing

When adding new features or fixing bugs:
1. Update relevant documentation files
2. Add unit tests for protocol changes
3. Test on actual hardware
4. Document configuration changes

---

## 📝 Version History

- **Version 1.0 (2026-03-19)**
  - Complete documentation restructure
  - Added dedicated documentation files for each component
  - Fixed all critical and high-priority bugs
  - Improved protocol documentation

---

## 💡 Tips for Success

1. **Read Setup.md first** – Don't skip the setup guide
2. **Check wiring carefully** – Most issues are wiring errors (TX/RX crossed)
3. **Use serial monitors** – Essential for debugging
4. **Start simple** – Test basic examples before complex code
5. **Monitor telemetry** – Watch Debug tab for real-time feedback
6. **Check connection status** – Verify satellites are online before testing
7. **Use calibration tools** – Proper calibration improves performance

---

## 🆘 Getting Help

### Troubleshooting Steps
1. Check the troubleshooting section in the relevant guide
2. Verify all connections and power supplies
3. Check serial monitor output for errors
4. Verify firmware versions match
5. Try factory reset if configuration is corrupted

### Common Issues
- **No WiFi connection:** Check hub is powered and booted
- **Satellites offline:** Check ESP-NOW channel and pairing
- **No telemetry:** Verify `DBG:` prefix and UART connection
- **Commands not working:** Check satellite online status and ACK responses

---

## 📄 License

This project is open-source. See repository root for license details.

---

## 📧 Contact

For questions or issues, please open an issue on the GitHub repository.

---

**Documentation Version:** 1.0
**Last Updated:** 2026-03-19
**Language:** English (with some German documents as noted)

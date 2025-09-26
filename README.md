# ESPGuard

A proof-of-concept BadUSB project using the ESP32-S3 DevKitC. Demonstrates HID (Human Interface Device) emulation, keystroke injection, and custom payload deployment for security research, red teaming, and penetration testing.
This project provides a web-based control panel for the ESP32-S3 BadUSB.  
It allows uploading, editing, and executing BadUSB scripts directly from a browser over WiFi.

Board: ESP32-S3 DevKitC-1 (N16R8)
SD Card Interface: SPI

# SD Card Pin	ESP32-S3 DevKitC-1 Pin
| SD Card Pin | ESP32-S3 DevKitC-1 Pin |
| ----------: | :--------------------- |
|       `3V3` | `3V3`                  |
|       `GND` | `GND`                  |
|   `CS (SS)` | `GPIO10`               |
|      `MOSI` | `GPIO11`               |
|      `MISO` | `GPIO13`               |

---
✅ Working as expected

⏳ Not working as expected

⏳ Broken, needs fixing

❌ Removed / Not implemented

## 📌 Current Status (Roadmap)

| #  | Feature / Example                | Status |
|----|----------------------------------|--------|
| 1  | Basic Key Combinations           | ✅ |
| 2  | Strings & Variables              | ✅ (basic only, without math concat) |
| 3  | Math Operations                  | ❌ |
| 4  | Function Variables               | ⏳ |
| 5  | WiFi Detection                   | ⏳ |
| 6  | Blocks                           | ❌ |
| 7  | Hold Keys                        | ❌ |
| 8  | Rower Payloads (multiple scripts)| ❌ |
| 9  | Boolean Variables                | ❌ |
| 10 | Repeat / Replay Commands         | ❌ |
| 11 | LED Control                      | ⏳ |
| 12 | Raw Keycodes                     | ✅ |

---

## 🗂️ Planned File Structure

- `index.html` → main frontend (UI for writing & executing scripts)
- `syntaxes.json` → list of all available commands (`STRING`, `DELAY`, `REPEAT`, etc.) with description + examples
- `main.cpp` (firmware) → ESP32 BadUSB logic
- `README.md` → project documentation (this file)

---

## 🔧 Features Implemented

- Basic key press & combinations (Win+R, Alt+Tab, etc.)
- String typing
- Script management (save, delete, execute via UI)
- WiFi AP hosting and WebUI

---

## ⏳ Work in Progress

- Fixing WiFi conditional execution (`IF_PRESENT`, `IF_NOTPRESENT`)
- Function-style variables
- Cleaning up script parser for unsupported commands

---

## ⌛ Next Steps

1. Split `index.html` and `syntaxes.json` for cleaner design
2. Define full JSON schema for supported commands
3. Add status indicators in UI for working / not working features
4. Gradually implement missing features (#3, #4, #5, #6, #7, #8, #9, #10, #11)

---

## ✅ Usage

1. Flash firmware onto ESP32-S3
2. Connect to WiFi AP
3. Open `192.168.4.1` in your browser
4. Upload and run scripts from the WebUI

---

## ⚠️ Disclaimer

This project is for **educational and testing purposes only**.  
Do not use it for malicious activities.

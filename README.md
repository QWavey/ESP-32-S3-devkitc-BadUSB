





## ESP THAT I USED:
<img width="479" height="450" alt="image" src="https://github.com/user-attachments/assets/b76b5615-5280-4249-b5c5-efd52f53d6e5" />

# EspGuard

---
A proof-of-concept BadUSB project using the ESP32-S3 DevKitC. Demonstrates HID (Human Interface Device) emulation, keystroke injection, and custom payload deployment for security research, red teaming, and penetration testing.
This project provides a web-based control panel for the ESP32-S3 BadUSB.  
It allows uploading, editing, and executing BadUSB scripts directly from a browser over WiFi.

# ⚠️ ATTENTION ⚠️:
This repository does not include or redistribute Hak5’s DuckyScript language files or any other copyrighted Hak5 content.

The language files referenced in this project are owned by Hak5 LLC and are subject to copyright and trademark protections. For those who wish to use the official language files, please visit the official Hak5 repository: [Hak5 DuckyScript Languages](https://github.com/hak5/usbrubberducky-payloads/tree/master/languages)

IF THE URL IS DOWN, GO TO: [Waybackmachine](http://web.archive.org/web/20250000000000*/https://github.com/hak5/usbrubberducky-payloads/tree/master/languages)

This project is intended solely for educational purposes, experimentation, and authorized security testing where permitted by local laws. It is not affiliated with, endorsed by, or sponsored by Hak5 LLC. Users are responsible for ensuring that their use of this project and any related tools complies with all applicable local, national, and international laws.

By referencing Hak5’s resources rather than including them, this repository respects copyright and trademark protections and encourages users to obtain any original files directly from Hak5. USB Rubber Ducky and DuckyScript are the trademarks of Hak5 LLC.
---
If Hak5 LLC at any time determines that this reference to their DuckyScript language files or any related content is inappropriate, unauthorized, or otherwise requires removal, this repository will promptly comply with their request and remove the reference immediately, without contest or delay, in order to respect their intellectual property rights and legal requirements.
---

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

⏳ Not working as expected / Particially Working

⏳ Broken, needs fixing

❌ Removed / Not implemented

## 📌 Current Status (Roadmap)
| #  | Feature / Example                                                                | Status |
| -- | -------------------------------------------------------------------------------- | ------ |
| 1  | Basic Key Combinations                                                           | ✅      |
| 2  | Strings & Variables                                                              | ✅      |
| 5  | WiFi Detection                                                                   | ✅      |
| 11 | LED Control                                                                      | ✅      |
| 12 | Raw Keycodes                                                                     | ✅      |
| 16 | SD Card Detection with Status Light (error when removed, approval when inserted) | ✅      |
| 4  | Function Variables                                                               | ⏳      |
| 10 | Repeat / Replay Commands                                                         | ⏳      |
| 14 | Custom Fonts in the WebGUI (stored on SD card)                                   | ⏳      |
| 3  | Math Operations                                                                  | ❌      |
| 6  | Blocks                                                                           | ❌      |
| 7  | Hold Keys                                                                        | ❌      |
| 8  | Rower Payloads (multiple scripts)                                                | ❌      |
| 9  | Boolean Variables                                                                | ❌      |
| 13 | Syntax Error Highlight in Editor                                                 | ❌      |
| 15 | Customisation in the WebGUI                                                      | ❌      |
|

---


## File Structure:
`/languages` (inside of sd card, containing all Language files, You need to find the Language files yourself. Putting them here in the repository would get me into legal trouble.)
  en.json 
  de.json
  .....
  
`config.json` (Will be created automatically)

## 🗂️ Planned File Structure

- `index.html` → main frontend (UI for writing & executing scripts)
- `syntaxes.json` → list of all available commands (`STRING`, `DELAY`, `REPEAT`, etc.) with description + examples
- `main.cpp` (firmware) → ESP32 BadUSB logic
- `README.md` → project documentation (this file)
- Folder for payloads `/payloads`
- Folder for Fonts `/fonts`
- Folder for custom Designs / Items such as CSS, JS, HTML. `/items`
- `/languages` (inside of sd card, containing all files)

---


## HELPING ME:

If you want to help me, contact me on Discord and send me your code suggestions: sniper74


## 🔧 Features Implemented

- Basic key press & combinations (Win+R, Alt+Tab, etc.)
- String typing
- Script management (save, delete, execute via UI)
- WiFi AP hosting and WebUI

---

## ⏳ Work in Progress

- Function-style variables
- Cleaning up script parser for unsupported commands
- Wiki. To see the incomplete structure, go to : [Wiki](https://github.com/XQuantumWaveX/ESP-32-S3-devkitc-BadUSB/wiki)
---

## ⌛ Next Steps

1. Split `index.html` and `syntaxes.json` for cleaner design
2. Define full JSON schema for supported commands
3. Add status indicators in UI for working / not working features
4. Gradually implement missing features 
5. Adding Customisation and Designing
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
I am not responsible for any misuse.

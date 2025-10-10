





## ESP THAT I USED:
[<img width="479" height="450" alt="image" src="https://github.com/user-attachments/assets/b76b5615-5280-4249-b5c5-efd52f53d6e5" />](https://www.amazon.de/dp/B0F3XMYYQY?ref=cm_sw_r_cso_wa_apan_dp_LIGHTSABER281DP7UB21&ref_=cm_sw_r_cso_wa_apan_dp_LIGHTSABER281DP7UB21&social_share=cm_sw_r_cso_wa_apan_dp_LIGHTSABER281DP7UB21&th=1)

# [`codeV4.cpp`](https://github.com/QWavey/ESP-32-S3-devkitc-BadUSB/blob/main/codeV4.cpp) IS THE NEWEST CODE RIGHT NOW!

# [`index.html`](https://github.com/QWavey/ESP-32-S3-devkitc-BadUSB/blob/main/index.html) THIS IS THE FILE YOU NEED TO PUT ON THE SD CARD, BELOW FIRMWARE 4 YOU DONT NEED THIS!

# EspGuard

---

ESP32-S3 BadUSB Proof-of-Concept

A proof-of-concept BadUSB project using the ESP32-S3 DevKitC. It demonstrates HID (Human Interface Device) emulation, keystroke injection, and custom payload deployment for security research, red teaming, and penetration testing.

This project includes a web-based control panel for the ESP32-S3 BadUSB, enabling users to upload, edit, and execute BadUSB scripts directly from a browser over WiFi.

---
# ⚠️ LEGAL DISCLAIMER (IMPORTANT FOR LANGUAGE FILES!)
<details> 
  <summary>CLICK TO EXPAND</summary>
This repository does not include or redistribute Hak5’s DuckyScript language files or any other copyrighted Hak5 content.
The language files referenced in this project are owned by Hak5 LLC and are subject to copyright and trademark protections. 
  
For those who wish to use the official language files, please visit the official Hak5 repository: 
[Hak5 DuckyScript Languages](https://github.com/hak5/usbrubberducky-payloads/tree/master/languages) . You ned to put the languages folder inside of the SD card.

IF THE URL IS DOWN, GO TO:
[Waybackmachine](http://web.archive.org/web/20250000000000*/https://github.com/hak5/usbrubberducky-payloads/tree/master/languages)

This project is intended solely for educational purposes, experimentation, and authorized security testing where permitted by local laws. It is not affiliated with, endorsed by, or sponsored by Hak5 LLC. Users are responsible for ensuring that their use of this project and any related tools complies with all applicable local, national, and international laws.
By referencing Hak5’s resources rather than including them, this repository respects copyright and trademark protections and encourages users to obtain any original files directly from Hak5. USB Rubber Ducky and DuckyScript are the trademarks of Hak5 LLC.
If Hak5 LLC at any time determines that this reference to their DuckyScript language files or any related content is inappropriate, unauthorized, or otherwise requires removal, this repository will promptly comply with their request and remove the reference immediately, without contest or delay, in order to respect their intellectual property rights and legal requirements.

</details>

---

# Board: ESP32-S3 DevKitC-1 (N16R8)

# SD Card Interface: SPI

---

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

⏳ Planned

❌ Not implemented yet, but planned

## 📌 Current Status (Roadmap)
| #  | Feature / Example                                                                                           | Status |
| -- | ----------------------------------------------------------------------------------------------------------- | ------ |
| 1  | Basic Key Combinations<br>(press multiple keys simultaneously)                                              | ✅      |
| 2  | Strings & Variables<br>(store and manipulate text)                                                          | ✅      |
| 3  | WiFi Detection<br>(detect available wireless networks and run commands based of "is the wifi there or not") | ✅      |
| 4  | LED Control<br>(turn LEDs on/off)                                                                           | ✅      |
| 5  | Raw Keycodes<br>(send specific key signals)                                                                 | ✅      |
| 6  | SD Card Detection<br>(detect SD card status)                                                                | ✅      |
| 7  | Uploading files<br>(add non-text files)                                                                     | ✅      |
| 19 | Noisy OS Detection<br>(detect operating system)                                                             | ⏳      |
| 8  | Function Variables<br>(store values for functions)                                                          | ⏳      |
| 9  | Repeat / Replay Commands<br>(repeat key sequences)                                                          | ⏳      |
| 10 | Custom Fonts WebGUI<br>(use fonts from SD)                                                                  | ⏳      |
| 11 | Math Operations<br>(perform calculations)                                                                   | ❌      |
| 12 | Blocks<br>(visual programming blocks)                                                                       | ❌      |
| 13 | Hold Keys<br>(hold keys across time intervals)                                                              | ❌      |
| 14 | Rower Payloads<br>(run multiple scripts sequentially)                                                       | ❌      |
| 15 | Boolean Variables<br>(store true or false)                                                                  | ❌      |
| 16 | Syntax Error Highlight<br>(show errors in editor)                                                           | ❌      |
| 17 | Customisation WebGUI<br>(adjust GUI settings)                                                               | ❌      |
| 18 | Expose SD Card over USB<br>(show/hide SD card access)                                                       | ❌      |
| 20 | Keylog Addon<br>(record keystrokes)                                                                         | ❌      |
| 21 | Connecting to WiFi<br>(establish wireless connection)                                                       | ❌      |
| 22 | Web Actions<br>(perform tasks using WiFi)                                                                   | ❌      |
| 23 | Pasting/Cutting/Copying files from the SD card to the PC.                                                   | ❌      |
| 24 | Disabling USB Function on boot and only draw power                                                          | ❌      |
| 25 | Mouse functionality                                                                                         | ❌      |
| 26 | Silent OS Detection                                                                                         | ❌      |
| 27 | HID over Bluetooth                                                                                          | ❌      |
| 28 | Starting the script when someone connects/disconnects to the ESP(involving Bluetooth, WiFi)                 | ❌      |
| 29 | Able to turn on/off Bluetooth/WiFi                                                                          | ❌      |
| 30 | Able to turn on/off Bluetooth/WiFi when an WiFi was detected                                                | ❌      |
| 31 | Chaining Commands together, making them work in universal conditions                                        | ❌      |


## File Structure:
`/languages` (inside of sd card, containing all Language files, You need to find the Language files yourself. Putting them here in the repository would get me into legal trouble. You need to create them yourself)
  en.json 
  de.json

`/DESIGNS` (inside of the sd card, containing the `/CSS`, `/JSS`, and `/FONTS` folders. You need to create them yourself)
  .....

`index.html` (inside of the sd card, containing the Website. Copy/Paste the [`index.html`](https://github.com/QWavey/ESP-32-S3-devkitc-BadUSB/edit/main/index.html)

## 🗂️ Planned File Structure

- `index.html` → main frontend (UI for writing & executing scripts)
- `syntaxes.json` → list of all available commands (`STRING`, `DELAY`, `REPEAT`, etc.) with description + examples
- `main.cpp` (firmware) → ESP32 BadUSB logic
- `README.md` → project documentation (this file)
- Folder for payloads `/payloads` ✅
- Folder for Fonts `/FONTS` ✅ (not working correctly tho)
- Folder for custom Designs / Items such as CSS, JS, HTML. `/items`
- `/languages` (inside of sd card, containing all files) ✅
- `config.json` (inside of the Root of the SD card. Main config for WiFi hosting, password etc  etc....)
- `/uploads` For saving uploaded files ✅
---


## HELPING ME:

If you want to help me, contact me on Discord and send me your code suggestions: sniper74


## 🔧 Features Implemented

- Basic key press & combinations (Win+R, Alt+Tab, etc.)
- String typing
- Script management (save, delete, execute via UI)
- WiFi AP hosting and WebUI

---

## ⏳ Work in Progress Wiki

- To see the incomplete structure, go to : [Wiki](https://github.com/XQuantumWaveX/ESP-32-S3-devkitc-BadUSB/wiki)
---

## ⌛ Next Steps

1. Integrate an `syntaxes.json` to integrate all commands
2. Define full JSON schema for supported commands
3. Add status indicators in UI for working / not working features
4. Gradually implement missing features 
5. Adding Customisation and Designing
---

## ✅ Usage

1. Flash firmware onto ESP32-S3
2. Move the `index.html` to the SD Card
3. Connect the SD Card with the Pins to the ESP (DO IT WHILE THE POWER IS OFF)
4. Connect to WiFi AP
5. Open `192.168.4.1` in your browser
6. Upload and run scripts from the WebUI

---

## ⚠️ Disclaimer

This project is for educational and testing purposes only.

Do not use it to conduct illegal, harmful, or malicious activities.

I am not responsible for any misuse of the information or code provided.

I am not responsible for any bodily injury that occurs while soldering, handling tools, or connecting wires.

Follow standard safety procedures (eye protection, ventilation, insulated tools) when working with electronics.

If you are unsure about a procedure, stop and seek help from a qualified person.

Do not attempt high-voltage, medical, or otherwise hazardous experiments without professional supervision.

Always disconnect power sources before making wiring changes or adjustments.

Test circuits with appropriate measurement tools and verify voltages before touching components.

Use correct-rated components and fuses — substituting parts may create fire or shock risks.

Keep liquids away from electronics and work on a non-conductive surface.

Keep small parts and tools away from children and pets.

Do NOT eat any components or materials.

Do NOT cut open batteries, capacitors, or sealed components.

Do NOT pour water or any liquids on the device or circuit.

This project is provided as-is with no warranty, express or implied.

I make no guarantees about the accuracy, safety, or suitability of the instructions for your situation.

You are responsible for ensuring compliance with local laws, regulations, and safety codes.

Any modifications you make are at your own risk; back up important data before experimenting.

If a step seems unclear or risky, consult official documentation, contact me, or an expert before proceeding.

By using this project you accept full responsibility for results and consequences.

If you plan to publish or share derived work, attribute appropriately and avoid distributing instructions that enable harm.

For medical or life-critical applications, consult licensed professionals, do not rely on hobby projects.

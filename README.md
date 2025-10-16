# TTGO T-Display Alarm Clock  
---

## Overview
This project implements a Wi-Fi and MQTT-based alarm clock for the TTGO T-Display board (or emulator) using the ESP-IDF framework in PlatformIO.

The alarm clock:
- Displays the current time (HH:MM:SS AM/PM) in landscape mode.  
- Synchronizes time using an SNTP server (`pool.ntp.org`).  
- Connects to the open Wi-Fi network “MasseyWifi”.  
- Subscribes to the MQTT topic `/topic/a159236/alarm` on `mqtt.webhop.org`.  
- Receives alarm messages in the format `YYYY-MM-DD HH:MM`.  
- When the alarm triggers:
  - Flashes the display red and green every 300 ms.  
  - Sets GPIO 25 to HIGH until a button is pressed.  
  - Returns to the clock display afterward.  

---

## Based on Starter Demo
Developed using the official TTGO Demo Starter Code as the base for graphics, Wi-Fi, and time synchronization.

Demo Repository:  
[TTGO Demo by a159x36](https://github.com/a159x36/TTGODemo)

![ttgodemo](https://github.com/a159x36/TTGODemo/assets/53783/c8e037c2-7b99-41db-97b1-4945b738eee4)

---

## Features
**Time Synchronization:** Uses `esp_sntp` to sync with an NTP server and display 12-hour format with AM/PM.  
**Wi-Fi:** Connects automatically to “MasseyWifi”.  
**MQTT:** Subscribes to `/topic/a159236/alarm` and triggers on matching time.  
**Display:** Uses `TDisplayGraphics` for drawing time and flashing colors.  
**Input and GPIO:** Buttons stop the alarm; GPIO 25 outputs HIGH while ringing.

---

## Build and Run
**Requirements:**  
- PlatformIO (VS Code)  
- ESP-IDF 5.5.0 or newer  
- Emulator environment from starter code  

**To build and upload:**  
```bash

pio run -e emulator -t upload

```
---
## Connect with MQTT Explorer

1. Open MQTT Explorer
2. Connect to broker: mqtt.webhop.org
3. Publish to topic: /topic/a159236/alarm
4. Add message payload (ex.2025-10-16 16:28)

- When current time matches the alarm, the screen flashes.

## Screenshots
board.png - Clock display
mqtt.png - MQTT Explorer setup
running.png - Alarm triggered

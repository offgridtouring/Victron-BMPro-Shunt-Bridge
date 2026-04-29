# Victron BLE to BMPRO BC300 CAN Bridge

An embedded C++ bridge running on an ESP32-S3 that seamlessly integrates a Victron SmartShunt into a BMPRO RV-C (JPro) ecosystem. It decrypts Victron's proprietary "Instant Readout" BLE advertisements and repackages them into 125kbps JPro CAN bus frames, completely emulating a physical BMPRO BC300 external shunt.

## 🚀 Features
* **No Extra Transceivers:** Uses the ESP32-S3's internal TWAI controller.
* **Air-Gapped Integration:** Reads Victron data via BLE—no VE.Direct cable required.
* **Zero-Setup Boot:** Runs a 60-second captive portal (`192.168.4.1`) on first boot to enter Shunt MAC, AES Key, and Battery Capacity. 
* **100% BC300 Emulation:** Perfectly mimics the BC300 boot roll-call, capacity injection, and 1Hz heartbeat required by the BMPRO ControlNode.
* **Lean Architecture:** Custom-written, highly optimized Victron parsing and BMPRO CAN abstraction libraries.

## 🛠 Hardware Stack
* **Microcontroller:** Waveshare ESP32-S3-RS485-CAN (or equivalent S3 with onboard CAN transceiver).
* **CAN Transceiver Enable:** Configured for `GPIO 14` (update in `main.ino` if using a different board revision).
* **Network:** 125kbps CAN Bus, internally terminated with 120Ω if installed at the end of the BMPRO trunk.

## 🔌 Integration Logic (The "Magic")
BMPRO systems running at 125kbps do not use standard RV-C PGNs for the BC300. Instead, they require specific `1802XXXX` Broadcast IDs.

| Metric | Victron BLE Source | BMPRO CAN ID | Notes |
| :--- | :--- | :--- | :--- |
| **Voltage** | Bytes 2-3 | `0x18020062` | Scaled to mV |
| **Current** | Bytes 8-10 (22-bit) | `0x18020061` | Signed, scaled to mA |
| **Consumed Ah**| Bytes 11-13 (20-bit) | `0x180200A0` | Forced Negative, mAh |
| **Temperatures**| Bytes 6-7 (Aux) | `0x18020083` | Ext=Probe, Int=Faked to 25C |
| **Heartbeat** | N/A | `0x18020014` | Incremental rolling counter |

## 📦 Dependencies
Ensure the following libraries are installed in your Arduino environment:
* `ESPAsyncWebServer`
* `AsyncTCP`
* Native ESP32 libraries: `BLE`, `WiFi`, `Preferences`, `DNSServer`

## ⚠️ Disclaimer
This is an unofficial, reverse-engineered integration for Off-Grid Touring applications. Not affiliated with Victron Energy or BMPRO. Use at your own risk on your 12V backbone.

#include "BMProCAN.h"

BMProCAN::BMProCAN() : heartbeatCounter(0) {}

void BMProCAN::begin() {
    Serial.println("[BMPRO] Application Protocol Handler Initialized.");
}

void BMProCAN::printDebugTX(uint32_t id, uint8_t* data, uint8_t len) {
    Serial.printf("[CAN TX] ID: %08X | LEN: %d | DATA: ", id, len);
    for (int i = 0; i < len; i++) {
        Serial.printf("%02X ", data[i]);
    }
    Serial.println();
}

// --- FIXED BIG-ENDIAN PAYLOAD PACKER ---
// Packs standard 32-bit integers in Big-Endian alignment (MSB first)
// This exactly matches your working Node-RED layout rule string outputs
void BMProCAN::send32BitPayload(uint32_t id, int32_t value) {
    uint8_t data[4];
    data[0] = (value >> 24) & 0xFF;  // Byte 0 (Most Significant Byte)
    data[1] = (value >> 16) & 0xFF;  // Byte 1
    data[2] = (value >> 8) & 0xFF;   // Byte 2
    data[3] = value & 0xFF;          // Byte 3 (Least Significant Byte)
    
    send_message(id, data, 4, 1);
}

// --- FIXED TEMPERATURE FRAMER ---
// Emits the exact 5-byte arrays defined in your working Node-RED system variables
void BMProCAN::sendTempPayload(uint8_t index, float tempC) {
    int32_t temp_mC = (int32_t)(tempC * 1000.0f);
    uint8_t data[5];
    data[0] = index;
    data[1] = (temp_mC >> 24) & 0xFF;
    data[2] = (temp_mC >> 16) & 0xFF;
    data[3] = (temp_mC >> 8) & 0xFF;
    data[4] = temp_mC & 0xFF;
    
    send_message(0x18020083, data, 5, 1);
}

void BMProCAN::registerShunt(float batteryCapAh) {
    Serial.println("[BMPRO] Broadcasting BC300 Roll Call & Capacity...");
    
    // 1. Identity Roll-Call (Fake Shunt MAC allocation address)
    uint8_t rollCall[] = {0x00, 0x4E, 0xDE, 0x00};
    send_message(0x18020014, rollCall, 4, 1);
    delay(100); 

    // 2. Set Battery Capacity ( Ah mapped straight to mAh )
    int32_t cap_mAh = (int32_t)(batteryCapAh * 1000.0f);
    send32BitPayload(0x1E00406A, cap_mAh);
}

void BMProCAN::sendData(float volts, float amps, float consumedAh) {
    // 1. Voltage Conversion: Round to 1 decimal place before multiplying by 1000
    // This removes the precise floating variations that confuse the display computer
    float rounded_volts = round(volts * 10.0f) / 10.0f;
    int32_t mv = (int32_t)(rounded_volts * 1000.0f);

    // 2. Current Amps Conversion: Round to 1 decimal place before mapping to mA
    float rounded_amps = round(amps * 10.0f) / 10.0f;
    int32_t ma = (int32_t)(rounded_amps * 1000.0f);
    
    // 3. Consumed Ah Conversion: Enforce an absolute negative bound
    int32_t c_mah = -abs((int32_t)round(consumedAh * 1000.0f)); 

    // 4. Transmit Big-Endian Telemetry Frames across the bus trunk lines
    send32BitPayload(0x18020062, mv);    // Voltage Register (cmd1)
    send32BitPayload(0x18020061, ma);    // Current Amps Register (cmd2)
    send32BitPayload(0x180200A0, c_mah); // Consumed Ah Register (cmd3)

    // 5. Environmental Probes (cmd4 and cmd5 payloads matched 1:1)
    sendTempPayload(0x01, 29.0f); // External Ambient Sensor
    sendTempPayload(0x02, 25.0f); // Internal Shunt Sub-case Sensor

    // 6. Output Incremental Running Heartbeat Counter (cmd6)
    uint8_t hb_payload[] = {0x00, 0x4E, 0xDE, heartbeatCounter++};
    send_message(0x18020014, hb_payload, 4, 1);
}
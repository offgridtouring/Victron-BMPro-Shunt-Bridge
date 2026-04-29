#include "BMProCAN.h"

BMProCAN::BMProCAN() : heartbeatCounter(0) {}

void BMProCAN::begin() {
    Serial.println("[BMPRO] Application Protocol Handler Initialized.");
    // Hardware CAN_Init() should still be handled in setup() via WS_CAN
}

void BMProCAN::send32BitPayload(uint32_t id, int32_t value) {
    uint8_t data[4];
    data[0] = (value >> 24) & 0xFF;
    data[1] = (value >> 16) & 0xFF;
    data[2] = (value >> 8) & 0xFF;
    data[3] = value & 0xFF;
    send_message(id, data, 4, 1); 
}

void BMProCAN::sendTempPayload(uint8_t index, float tempC) {
    int32_t temp_mC = (int32_t)(tempC * 1000.0f);
    uint8_t data[6] = {
        0x83, index, 
        (uint8_t)((temp_mC >> 24) & 0xFF), 
        (uint8_t)((temp_mC >> 16) & 0xFF), 
        (uint8_t)((temp_mC >> 8) & 0xFF), 
        (uint8_t)(temp_mC & 0xFF)
    };
    send_message(0x18020083, data, 6, 1);
}

void BMProCAN::registerShunt(float batteryCapAh) {
    Serial.println("[BMPRO] Broadcasting BC300 Roll Call & Capacity...");
    
    // 1. Identity Signature (MAC: 00 4E DE)
    uint8_t rollCall[] = {0x00, 0x4E, 0xDE, 0x00};
    send_message(0x18020014, rollCall, 4, 1);
    delay(50); // Crucial spacing for ControlNode buffer

    // 2. Set Capacity
    int32_t cap_mAh = (int32_t)(batteryCapAh * 1000.0f);
    send32BitPayload(0x1E00406A, cap_mAh);
}

void BMProCAN::sendData(float volts, float amps, float consumedAh) {
    // 1. Scale metrics to BMPRO units
    int32_t mv = (int32_t)round(volts * 1000.0f);
    int32_t ma = (int32_t)round(amps * 1000.0f);
    int32_t c_mah = -abs((int32_t)round(consumedAh * 1000.0f)); // Forced negative

    // 2. Transmit Core PGNs
    send32BitPayload(0x18020062, mv);
    send32BitPayload(0x18020061, ma);
    send32BitPayload(0x180200A0, c_mah);

    // 3. Faked Temperatures (Index 01=Ext, 02=Int)
    sendTempPayload(0x01, 29.0f); 
    sendTempPayload(0x02, 25.0f); 

    // 4. Rolling Heartbeat
    uint8_t hb_payload[] = {0x00, 0x4E, 0xDE, heartbeatCounter++};
    send_message(0x18020014, hb_payload, 4, 1);
}
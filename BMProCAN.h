/**
 * BMProCAN - Emulates a BC300 Shunt on the JHub/Odyssey 125k Bus
 * Requires WS_CAN.h for low-level TWAI driver access.
 */

#ifndef BMPRO_CAN_H
#define BMPRO_CAN_H

#include <Arduino.h>
#include "WS_CAN.h" // Your existing TWAI driver wrapper

class BMProCAN {
private:
    uint8_t heartbeatCounter;

    // Helper to format and send Big Endian 32-bit PGNs
    void send32BitPayload(uint32_t id, int32_t value);
    // Helper to format the 6-byte temperature PGNs
    void sendTempPayload(uint8_t index, float tempC);

public:
    BMProCAN();
    
    // Call once during boot
    void begin();
    
    // Call once the bridge connects (Registration phase)
    void registerShunt(float batteryCapAh);
    
    // Call every 1 second with fresh Victron data
    void sendData(float volts, float amps, float consumedAh);
};

#endif
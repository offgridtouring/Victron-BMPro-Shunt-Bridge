/**
 * VictronBLE - Optimized for OffGridTouring Shunt Bridge
 * Stripped to include only Battery Monitor (0x02) logic.
 */

#ifndef VICTRON_BLE_H
#define VICTRON_BLE_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEAdvertisedDevice.h>
#include <BLEScan.h>
#include "mbedtls/aes.h"

// --- Constants ---
static constexpr uint16_t VICTRON_MANUFACTURER_ID = 0x02E1;
static constexpr int VICTRON_MAX_DEVICES = 4; // Lean memory footprint
static constexpr int VICTRON_MAC_LEN = 13;
static constexpr int VICTRON_NAME_LEN = 32;
static constexpr int VICTRON_ENCRYPTED_LEN = 21;

enum VictronDeviceType {
    DEVICE_TYPE_UNKNOWN = 0x00,
    DEVICE_TYPE_BATTERY_MONITOR = 0x02
};

// --- Wire-format structures ---
struct victronManufacturerData {
    uint16_t vendorID;
    uint8_t beaconType;
    uint8_t unknownData1[3];
    uint8_t victronRecordType; 
    uint16_t nonceDataCounter;
    uint8_t encryptKeyMatch;
    uint8_t victronEncryptedData[VICTRON_ENCRYPTED_LEN];
} __attribute__((packed));

// --- Parsed Data Structure ---
struct VictronBatteryData {
    float voltage;
    float current;
    float consumedAh;
    float soc;
};

struct VictronDevice {
    char name[VICTRON_NAME_LEN];
    char mac[VICTRON_MAC_LEN];
    VictronDeviceType deviceType;
    int8_t rssi;
    uint32_t lastUpdate;
    bool dataValid;
    VictronBatteryData battery; 
};

typedef void (*VictronCallback)(const VictronDevice* device);
class VictronBLEAdvertisedDeviceCallbacks;

// --- Main Class ---
class VictronBLE {
public:
    VictronBLE();

    bool begin(uint32_t scanDuration = 5);
    bool addDevice(const char* name, const char* mac, const char* hexKey,
                   VictronDeviceType type = DEVICE_TYPE_BATTERY_MONITOR);
    
    void setCallback(VictronCallback cb) { callback = cb; }
    void setDebug(bool enable) { debugEnabled = enable; }
    void setMinInterval(uint32_t ms) { minIntervalMs = ms; }
    void loop();

    VictronDevice* getDevice(const char* name); // Declaration only

private:
    friend class VictronBLEAdvertisedDeviceCallbacks;

    struct DeviceEntry {
        VictronDevice device;
        uint8_t key[16];
        uint16_t lastNonce;
        bool active;
    };

    DeviceEntry devices[VICTRON_MAX_DEVICES];
    size_t deviceCount;
    BLEScan* pBLEScan;
    VictronBLEAdvertisedDeviceCallbacks* scanCallbackObj;
    VictronCallback callback;
    bool debugEnabled;
    uint32_t scanDuration;
    uint32_t minIntervalMs;
    bool initialized;

    static bool hexToBytes(const char* hex, uint8_t* out, size_t len);
    static void normalizeMAC(const char* input, char* output);
    DeviceEntry* findDevice(const char* normalizedMAC);
    bool decryptData(const uint8_t* encrypted, size_t len,
                     const uint8_t* key, const uint8_t* iv, uint8_t* decrypted);
    void processDevice(BLEAdvertisedDevice& dev);
    bool parseBatteryMonitor(const uint8_t* data, size_t len, VictronBatteryData& result);
};

class VictronBLEAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
    VictronBLEAdvertisedDeviceCallbacks(VictronBLE* parent) : victronBLE(parent) {}
    void onResult(BLEAdvertisedDevice advertisedDevice) override;
private:
    VictronBLE* victronBLE;
};

#endif
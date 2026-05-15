/**
 * VictronBLE - Optimized for OffGridTouring Shunt Bridge
 * Includes: AES Decryption & Battery Monitor (0x02) Parser
 */

#include "VictronShuntBLE.h"
#include <string.h>
#include <ctype.h> // Fixed: Required for tolower()

// Fixed: File scope variable for scan status
static bool s_scanning = false;
static void onScanDone(BLEScanResults results) {
    s_scanning = false;
}

VictronBLE::VictronBLE()
    : deviceCount(0), pBLEScan(nullptr), scanCallbackObj(nullptr),
      callback(nullptr), debugEnabled(false), scanDuration(5),
      minIntervalMs(1000), initialized(false) {
    memset(devices, 0, sizeof(devices));
}

bool VictronBLE::begin(uint32_t scanDuration) {
    if (initialized) return true;
    this->scanDuration = scanDuration;

    BLEDevice::init("VictronBridge");
    pBLEScan = BLEDevice::getScan();
    if (!pBLEScan) return false;

    scanCallbackObj = new VictronBLEAdvertisedDeviceCallbacks(this);
    pBLEScan->setAdvertisedDeviceCallbacks(scanCallbackObj, true);
    pBLEScan->setActiveScan(false);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    initialized = true;
    return true;
}

bool VictronBLE::addDevice(const char* name, const char* mac, const char* hexKey,
                           VictronDeviceType type) {
    if (deviceCount >= VICTRON_MAX_DEVICES) return false;
    if (!hexKey || strlen(hexKey) != 32 || !mac) return false;

    char normalizedMAC[VICTRON_MAC_LEN];
    normalizeMAC(mac, normalizedMAC);

    DeviceEntry* entry = &devices[deviceCount];
    memset(entry, 0, sizeof(DeviceEntry));
    entry->active = true;

    strncpy(entry->device.name, name ? name : "Shunt", VICTRON_NAME_LEN - 1);
    memcpy(entry->device.mac, normalizedMAC, VICTRON_MAC_LEN);
    entry->device.deviceType = type;
    
    if (!hexToBytes(hexKey, entry->key, 16)) return false;

    deviceCount++;
    return true;
}

void VictronBLE::loop() {
    if (!initialized) return;
    if (!s_scanning) {
        pBLEScan->clearResults();
        s_scanning = pBLEScan->start(scanDuration, onScanDone, false); // Fixed
    }
}

VictronDevice* VictronBLE::getDevice(const char* name) {
    for(size_t i = 0; i < deviceCount; i++) {
        if(devices[i].active && strcmp(devices[i].device.name, name) == 0) {
            return &devices[i].device;
        }
    }
    return nullptr;
}

void VictronBLEAdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice advertisedDevice) {
    if (victronBLE) victronBLE->processDevice(advertisedDevice);
}

void VictronBLE::processDevice(BLEAdvertisedDevice& advertisedDevice) {
    if (!advertisedDevice.haveManufacturerData()) return;

    // FIX: ESP32 Core 3.x uses Arduino String for BLE payloads
    String raw = advertisedDevice.getManufacturerData();
    if (raw.length() < 10) return;

    // Vendor ID Check (0x02E1 for Victron)
    uint16_t vendorID = (uint8_t)raw[0] | ((uint8_t)raw[1] << 8);
    if (vendorID != VICTRON_MANUFACTURER_ID) return;

    char normalizedMAC[VICTRON_MAC_LEN];
    normalizeMAC(advertisedDevice.getAddress().toString().c_str(), normalizedMAC);

    DeviceEntry* entry = findDevice(normalizedMAC);
    if (!entry) return;

    victronManufacturerData mfgData;
    // FIX: Use .c_str() to extract the binary buffer from the Arduino String
    size_t copyLen = raw.length() > sizeof(mfgData) ? sizeof(mfgData) : raw.length();
    memcpy(&mfgData, raw.c_str(), copyLen);

    // Instant Readout Check: Only decrypt Battery Monitor (0x02)
    if (mfgData.victronRecordType != 0x02) return;

    if (entry->device.dataValid && mfgData.nonceDataCounter == entry->lastNonce) return;

    uint8_t iv[16] = {0};
    iv[0] = mfgData.nonceDataCounter & 0xFF;
    iv[1] = (mfgData.nonceDataCounter >> 8) & 0xFF;

    uint8_t decrypted[VICTRON_ENCRYPTED_LEN];
    if (decryptData(mfgData.victronEncryptedData, VICTRON_ENCRYPTED_LEN, entry->key, iv, decrypted)) {
        if (parseBatteryMonitor(decrypted, VICTRON_ENCRYPTED_LEN, entry->device.battery)) {
            entry->lastNonce = mfgData.nonceDataCounter;
            entry->device.dataValid = true;
            entry->device.lastUpdate = millis();
            if (callback) callback(&entry->device);
        }
    }
}

// --- The Core Battery Parser (0x02) ---
bool VictronBLE::parseBatteryMonitor(const uint8_t* data, size_t len, VictronBatteryData& result) {
    if (len < 15) return false;
    
    // 1. Voltage
    result.voltage = (int16_t)(data[2] | (data[3] << 8)) * 0.01f;

    // 2. Your Golden Parser Block (Strictly untouched for Current, Ah, SOC)
    uint32_t b8_10 = data[8] | (data[9] << 8) | (data[10] << 16);
    int32_t c_raw = (b8_10 >> 2) & 0x3FFFFF;
    if (c_raw & 0x200000) c_raw |= 0xFFC00000; 
    result.current = c_raw * 0.001f;

    uint32_t b11_13 = data[11] | (data[12] << 8) | (data[13] << 16);
    int32_t ah_raw = b11_13 & 0xFFFFF; 
    result.consumedAh = (ah_raw != 0xFFFFF) ? ah_raw * 0.1f : 0.0f;

    uint16_t b13_14 = data[13] | (data[14] << 8);
    uint16_t soc_raw = (b13_14 >> 4) & 0x3FF; 
    result.soc = (soc_raw != 0x3FF) ? soc_raw * 0.1f : -1.0f;

    return true;
}

// --- Decryption Engine ---
bool VictronBLE::decryptData(const uint8_t* encrypted, size_t len, const uint8_t* key, const uint8_t* iv, uint8_t* decrypted) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    if (mbedtls_aes_setkey_enc(&aes, key, 128) != 0) { mbedtls_aes_free(&aes); return false; }

    size_t nc_off = 0;
    uint8_t nonce_counter[16];
    uint8_t stream_block[16];
    memcpy(nonce_counter, iv, 16);
    memset(stream_block, 0, 16);

    int ret = mbedtls_aes_crypt_ctr(&aes, len, &nc_off, nonce_counter, stream_block, encrypted, decrypted);
    mbedtls_aes_free(&aes);
    return (ret == 0);
}

// --- Utils ---
bool VictronBLE::hexToBytes(const char* hex, uint8_t* out, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char s[3] = { hex[i*2], hex[i*2+1], 0 };
        out[i] = (uint8_t)strtol(s, NULL, 16);
    }
    return true;
}

void VictronBLE::normalizeMAC(const char* input, char* output) {
    int j = 0;
    for (int i = 0; input[i] && j < VICTRON_MAC_LEN - 1; i++) {
        if (input[i] == ':' || input[i] == '-') continue;
        output[j++] = tolower(input[i]);
    }
    output[j] = '\0';
}

VictronBLE::DeviceEntry* VictronBLE::findDevice(const char* normalizedMAC) {
    for (size_t i = 0; i < deviceCount; i++) {
        if (devices[i].active && strcmp(devices[i].device.mac, normalizedMAC) == 0) return &devices[i];
    }
    return nullptr;
}
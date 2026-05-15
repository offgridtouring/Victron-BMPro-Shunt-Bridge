/**
 * Victron BLE to BMPRO BC300 CAN Bridge
 * Hardware: Waveshare ESP32-S3-RS485-CAN
 * Emulation: BC300 Shunt (1802XXXX IDs) at 125kbps
 */

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include "WS_CAN.h"
#include "WS_GPIO.h"
#include "WS_RTC.h"
#include "WS_RS485.h"

// --- Custom Libraries ---
#include "VictronShuntBLE.h"
#include "BMProCAN.h"
#include "WebUI.h"  

Preferences prefs;
AsyncWebServer server(80);
DNSServer dnsServer;
VictronBLE victron;
BMProCAN bmpro;

// --- Config & State ---
String savedMAC = "";
String savedKey = "";
float batteryCap = 300.0;
bool bridgeStarted = false;
bool wifiActive = false;
unsigned long wifiWindowStart = 0;
const unsigned long WIFI_TIMEOUT = 60000; // 60 seconds setup window
unsigned long lastInjection = 0;

#define CAN_EN_PIN 14 // Transceiver power enable for Waveshare board

// --- Transition to Normal Bridge Operation ---
void startBridge() {
    Serial.println("\n[SYSTEM] Setup window closing. Turning off Hotspot...");
    dnsServer.stop();
    server.end();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiActive = false;
    
    Serial.println("[SYSTEM] Starting Bridge Shunt Routing Engine...");
    
    // CRITICAL AP CONNECTIVITY FIX: Pin the intensive CAN tracking loop 
    // to Core 1 at low priority. This prevents it from starving the internal 
    // network stack processor running on Core 0.
    xTaskCreatePinnedToCore(CANTask, "CANTask", 4096, NULL, 1, NULL, 1);
    delay(200);

    bmpro.begin();
    bmpro.registerShunt(batteryCap); 

    // Initialize BLE decryption scanner
    victron.addDevice("Shunt", savedMAC.c_str(), savedKey.c_str(), DEVICE_TYPE_BATTERY_MONITOR);
    victron.begin();
    bridgeStarted = true;
    Serial.println("[SYSTEM] --- VICTRON TO BMPRO BRIDGE ACTIVE ---");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[SYSTEM] Booting Off-Grid Touring Bridge...");

    // 1. Core Hardware Line Energization
    pinMode(CAN_EN_PIN, OUTPUT);
    digitalWrite(CAN_EN_PIN, HIGH); 
    pinMode(47, OUTPUT);
    digitalWrite(47, HIGH); // Powers up isolated optocoupler sub-rails
    delay(100);

    // 2. Load stored NVS Settings
    prefs.begin("shunt-bridge", false);
    savedMAC = prefs.getString("mac", "");
    savedKey = prefs.getString("key", "");
    batteryCap = prefs.getFloat("cap", 300.0);

    // 3. Hardware Initialization (Uses Waveshare's working sequence)
    I2C_Init();
    RS485_Init();
    
    // Set low-level bitrate flag BEFORE initialization call
    extern uint32_t CAN_bitrate_kbps;
    CAN_bitrate_kbps = 125; 
    CAN_Init();             
    
    RTC_Init();
    
    // NOTE: We DO NOT call WIFI_Init() here anymore. We handle our own clean AP 
    // below to stop Waveshare's legacy file from forcing client connections.

    // 4. Fire up Standalone Configuration Hotspot
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP("Victron-BMPro-Bridge-Setup");
    
    // Wildcard DNS redirect sets up the captive portal context immediately
    dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));

    // Bind Root Web UI Routing Template
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){ 
        r->send(200, "text/html", buildIndexHtml(savedMAC, savedKey, batteryCap)); 
    });
    server.onNotFound([](AsyncWebServerRequest *r){ 
        r->send(200, "text/html", buildIndexHtml(savedMAC, savedKey, batteryCap)); 
    });
    
    // Bind Post Form Endpoint Processor
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *r){
        if(r->hasParam("mac",true)) prefs.putString("mac", r->getParam("mac",true)->value());
        if(r->hasParam("key",true)) prefs.putString("key", r->getParam("key",true)->value());
        if(r->hasParam("cap",true)) prefs.putFloat("cap", r->getParam("cap",true)->value().toFloat());
        r->send(200, "text/plain", "Settings Saved! Rebooting bridge now...");
        vTaskDelay(pdMS_TO_TICKS(1500));
        ESP.restart();
    });

    server.begin();
    wifiWindowStart = millis();
    wifiActive = true;
    Serial.println("[SYSTEM] Hotspot Config Portal online at 192.168.4.1");
}

void injectToBmpro() {
    VictronDevice* dev = victron.getDevice("Shunt");
    if (!dev || !dev->dataValid) return;

    // Send data over CAN Bus using Waveshare's native driver calls
    bmpro.sendData(dev->battery.voltage, dev->battery.current, dev->battery.consumedAh);
    
    Serial.printf("[BRIDGE] %.2fV | %.2fA | %.1f%% SOC | %.2fAh\n", 
                  dev->battery.voltage, dev->battery.current, dev->battery.soc, dev->battery.consumedAh);
}

void loop() {
    // Phase 1: Captive Portal Hotspot Window
    if (wifiActive) {
        dnsServer.processNextRequest();
        
        if (millis() - wifiWindowStart > WIFI_TIMEOUT) {
            wifiActive = false;
            if (savedMAC != "" && savedKey != "") {
                startBridge();
            } else {
                Serial.println("[ERROR] No parameters configured. Disabling timeout window.");
            }
        }
    }
    
    // Phase 2: Active System Bridging Operation
    if (bridgeStarted) {
        victron.loop(); // Pull BLE advertisements continuously

        // Send updates onto the caravan bus once every 1000ms
        unsigned long currentMillis = millis();
        if (currentMillis - lastInjection >= 1000) {
            injectToBmpro();
            lastInjection = currentMillis;
        }
    }
}
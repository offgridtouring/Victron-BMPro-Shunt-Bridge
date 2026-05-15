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
#include "WebUI.h"  // <-- Your new ultra-clean Web UI Header

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
const unsigned long WIFI_TIMEOUT = 60000; // 60 seconds
unsigned long lastInjection = 0;

void startBridge() {
    Serial.println("[SYSTEM] Starting Bridge Mode...");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    
    // Start Waveshare CAN Task on Core 0
    xTaskCreatePinnedToCore(CANTask, "CANTask", 4096, NULL, 3, NULL, 0);
    delay(500);

    bmpro.begin();
    bmpro.registerShunt(batteryCap); 

    // Start BLE Scanner
    victron.addDevice("Shunt", savedMAC.c_str(), savedKey.c_str(), DEVICE_TYPE_BATTERY_MONITOR);
    victron.begin();
    bridgeStarted = true;
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[SYSTEM] Booting Off-Grid Touring Bridge...");

    // 2. Hardware Initialization (Uses Waveshare's proven sequence)
    I2C_Init();
    RS485_Init();
    
    CAN_bitrate_kbps = 125; // Force BMPRO Speed
    CAN_Init();             
    
    RTC_Init();

    prefs.begin("shunt-bridge", false);
    savedMAC = prefs.getString("mac", "");
    savedKey = prefs.getString("key", "");
    batteryCap = prefs.getFloat("cap", 300.0);

    CAN_Init(); // Ensure this is set to 125kbps in WS_CAN.h

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    WiFi.softAP("Victron-BMPro-Bridge-Setup");
    dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    
    // Pass the saved preferences directly into your new buildIndexHtml function
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){ 
        r->send(200, "text/html", buildIndexHtml(savedMAC, savedKey, batteryCap)); 
    });
    server.onNotFound([](AsyncWebServerRequest *r){ 
        r->send(200, "text/html", buildIndexHtml(savedMAC, savedKey, batteryCap)); 
    });
    
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *r){
        if(r->hasParam("mac",true)) prefs.putString("mac", r->getParam("mac",true)->value());
        if(r->hasParam("key",true)) prefs.putString("key", r->getParam("key",true)->value());
        if(r->hasParam("cap",true)) prefs.putFloat("cap", r->getParam("cap",true)->value().toFloat());
        r->send(200, "text/plain", "Settings Saved! Rebooting bridge now...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP.restart();
    });

    server.begin();
    wifiWindowStart = millis();
    wifiActive = true;
    Serial.println("Setup Portal Ready at 192.168.4.1 (Active for 60s)");
}

void injectToBmpro() {
    VictronDevice* dev = victron.getDevice("Shunt");
    if (!dev || !dev->dataValid) return; 

    // The entire CAN logic abstracted perfectly
    bmpro.sendData(dev->battery.voltage, dev->battery.current, dev->battery.consumedAh);

    Serial.printf("[BRIDGE] %.2fV | %.2fA | %.1f%% SOC | %.2fAh\n", 
                  dev->battery.voltage, dev->battery.current, dev->battery.soc, dev->battery.consumedAh);
}

void loop() {
    if (wifiActive) {
        dnsServer.processNextRequest();
        if (millis() - wifiWindowStart > WIFI_TIMEOUT) {
            wifiActive = false;
            if (savedMAC != "" && savedKey != "") startBridge();
            else Serial.println("[ERROR] No MAC/Key saved. Reboot to try again.");
        }
    }
    
    if (bridgeStarted) {
        victron.loop(); // Listen for BLE

        // Fire the CAN injection once per second
        if (millis() - lastInjection >= 1000) {
            injectToBmpro();
            lastInjection = millis();
        }
    }
}
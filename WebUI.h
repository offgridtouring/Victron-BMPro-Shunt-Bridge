/**
 * WebUI.h - Captive Portal HTML Template
 * Stored in Flash Memory (PROGMEM) to save RAM.
 */

#ifndef WEB_UI_H
#define WEB_UI_H

#include <Arduino.h>

// Store the raw HTML in Flash Memory
const char INDEX_HTML_TEMPLATE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Victron Bridge</title><style>
body{font-family:'Segoe UI',sans-serif;background:#f4f4f9;display:flex;flex-direction:column;align-items:center;margin:0;}
.header{width:100%;background:#333;color:white;padding:20px;text-align:center;box-shadow:0 2px 5px rgba(0,0,0,0.2);}
.container{background:white;padding:30px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.1);width:90%;max-width:400px;margin-top:20px;}
h2{color:#333;margin-top:0;}
label{display:block;margin-top:15px;font-size:0.8em;font-weight:bold;color:#666;}
input{width:100%;padding:12px;margin:5px 0 15px 0;border:1px solid #ddd;border-radius:6px;box-sizing:border-box;font-size:16px;}
.btn{background:#f16f31;color:white;border:none;padding:15px;width:100%;border-radius:6px;font-size:16px;font-weight:bold;cursor:pointer;}
.help-section{background:#eee;padding:15px;border-radius:6px;margin-top:20px;font-size:0.85em;line-height:1.4;color:#444;}
</style></head><body>
<div class="header"><h1>OFF-GRID TOURING</h1></div>
<div class="container">
    <h2>BC300 Bridge Config</h2>
    <form action="/save" method="POST">
        <label>SMARTSHUNT MAC</label><input name="mac" value="{MAC_VAL}" placeholder="AA:BB:CC:DD:EE:FF">
        <label>ENCRYPTION KEY</label><input name="key" value="{KEY_VAL}" placeholder="32-char hex key">
        <label>BATTERY CAPACITY (Ah)</label><input name="cap" type="number" value="{CAP_VAL}">
        <button type="submit" class="btn">SAVE & START BRIDGE</button>
    </form>
    <div class="help-section">
        <b>How to find MAC & Key?</b><br>
        1. Open VictronConnect > Product Info.<br>
        2. Toggle 'Instant Readout' to ON.<br>
        3. Copy MAC & Key from the 'Show' data button.<br>
        <i>Note: Portal active for 60s after power-on.</i>
    </div>
</div>
<p style="color:#aaa; font-size:0.8em; margin-top:20px;">www.offgridtouring.au</p>
</body></html>
)rawliteral";

// Helper function to build the final HTML string with saved values
String buildIndexHtml(const String& mac, const String& key, float cap) {
    String html = String(INDEX_HTML_TEMPLATE);
    html.replace("{MAC_VAL}", mac);
    html.replace("{KEY_VAL}", key);
    html.replace("{CAP_VAL}", String(cap, 0));
    return html;
}

#endif
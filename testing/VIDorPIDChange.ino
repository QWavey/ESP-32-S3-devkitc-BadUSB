#include "USB.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#if CONFIG_TINYUSB_ENABLED

WebServer server(80);

struct Config {
  String vid = "0x303a";
  String pid = "0x0002";
  String mfr = "Espressif";
  String prod = "ESP32-S3";
  String url = "https://espressif.github.io/arduino-esp32/webusb.html";
  bool rndVid = false;
  bool rndPid = false;
} cfg;

const char* HTML = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>USB Config</title></head><body>
<h2>USB Configuration</h2>
<form id="f">
<label>VID: <input id="vid" pattern="0x[0-9A-Fa-f]{4}" required></label>
<label><input type="checkbox" id="rndVid" onchange="toggleVid()"> Randomize VID</label><br><br>
<label>PID: <input id="pid" pattern="0x[0-9A-Fa-f]{4}" required></label>
<label><input type="checkbox" id="rndPid" onchange="togglePid()"> Randomize PID</label><br><br>
<label>Manufacturer: <input id="mfr"></label><br><br>
<label>Product: <input id="prod"></label><br><br>
<label>WebUSB URL: <input id="url"></label><br><br>
<button type="button" onclick="load()">Refresh</button>
<button type="submit">Save and Restart</button>
<p id="msg"></p>
</form>
<script>
function toggleVid(){vid.disabled=rndVid.checked;}
function togglePid(){pid.disabled=rndPid.checked;}
async function load(){
  let r=await fetch('/api');
  let d=await r.json();
  vid.value=d.vid;pid.value=d.pid;mfr.value=d.mfr;prod.value=d.prod;url.value=d.url;
  rndVid.checked=d.rndVid;rndPid.checked=d.rndPid;
  toggleVid();togglePid();
  msg.textContent='Loaded';
}
f.onsubmit=async(e)=>{
  e.preventDefault();
  let r=await fetch('/api',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({vid:vid.value,pid:pid.value,mfr:mfr.value,prod:prod.value,url:url.value,rndVid:rndVid.checked,rndPid:rndPid.checked})});
  let d=await r.json();
  msg.textContent=d.ok?'Saved! Restarting...':'Error';
  if(d.ok)setTimeout(()=>location.reload(),3000);
};
window.onload=load;
</script></body></html>
)rawliteral";

String genRandom() {
  char buf[7];
  sprintf(buf, "0x%04x", (uint16_t)(esp_random() & 0xFFFF));
  return String(buf);
}

void loadCfg() {
  if (!SPIFFS.exists("/cfg.json")) return;
  File f = SPIFFS.open("/cfg.json", "r");
  if (!f) return;
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, f) == DeserializationError::Ok) {
    cfg.vid = doc["vid"] | cfg.vid;
    cfg.pid = doc["pid"] | cfg.pid;
    cfg.mfr = doc["mfr"] | cfg.mfr;
    cfg.prod = doc["prod"] | cfg.prod;
    cfg.url = doc["url"] | cfg.url;
    cfg.rndVid = doc["rndVid"] | false;
    cfg.rndPid = doc["rndPid"] | false;
  }
  f.close();
}

void saveCfg() {
  File f = SPIFFS.open("/cfg.json", "w");
  if (!f) return;
  StaticJsonDocument<512> doc;
  
  // Generate random VID if enabled
  if (cfg.rndVid) {
    cfg.vid = genRandom();
    Serial.printf("Generated random VID: %s\n", cfg.vid.c_str());
  }
  
  // Generate random PID if enabled
  if (cfg.rndPid) {
    cfg.pid = genRandom();
    Serial.printf("Generated random PID: %s\n", cfg.pid.c_str());
  }
  
  doc["vid"] = cfg.vid;
  doc["pid"] = cfg.pid;
  doc["mfr"] = cfg.mfr;
  doc["prod"] = cfg.prod;
  doc["url"] = cfg.url;
  doc["rndVid"] = cfg.rndVid;
  doc["rndPid"] = cfg.rndPid;
  serializeJson(doc, f);
  f.close();
}

void applyCfg() {
  if (!::USB) {
    ::USB.VID((uint16_t)strtol(cfg.vid.c_str(), NULL, 16));
    ::USB.PID((uint16_t)strtol(cfg.pid.c_str(), NULL, 16));
    ::USB.manufacturerName(cfg.mfr.c_str());
    ::USB.productName(cfg.prod.c_str());
    ::USB.webUSB(true);
    ::USB.webUSBURL(cfg.url.c_str());
    ::USB.begin();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  SPIFFS.begin(true);
  loadCfg();
  applyCfg();
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-S3", "");
  
  Serial.printf("\nConnect to WiFi: ESP32-S3\nOpen: http://%s\n", WiFi.softAPIP().toString().c_str());
  
  server.on("/", []() { server.send(200, "text/html", HTML); });
  
  server.on("/api", HTTP_GET, []() {
    StaticJsonDocument<512> doc;
    doc["vid"] = cfg.vid;
    doc["pid"] = cfg.pid;
    doc["mfr"] = cfg.mfr;
    doc["prod"] = cfg.prod;
    doc["url"] = cfg.url;
    doc["rndVid"] = cfg.rndVid;
    doc["rndPid"] = cfg.rndPid;
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });
  
  server.on("/api", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
    
    String newVid = doc["vid"];
    String newPid = doc["pid"];
    if (newVid.startsWith("0x")) cfg.vid = newVid;
    if (newPid.startsWith("0x")) cfg.pid = newPid;
    cfg.mfr = doc["mfr"] | cfg.mfr;
    cfg.prod = doc["prod"] | cfg.prod;
    cfg.url = doc["url"] | cfg.url;
    cfg.rndVid = doc["rndVid"] | false;
    cfg.rndPid = doc["rndPid"] | false;
    
    saveCfg();
    
    server.send(200, "application/json", "{\"ok\":true}");
    server.client().flush();
    server.stop();
    
    delay(1000);
    
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(500);
    
    ESP.restart();
  });
  
  server.begin();
}

void loop() {
  server.handleClient();
  delay(10);
}

#endif

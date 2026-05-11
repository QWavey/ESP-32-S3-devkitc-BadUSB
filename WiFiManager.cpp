#include "WiFiManager.h"
#include "LogManager.h"
#include <HTTPClient.h>

// ============================================================
// AP Management
// ============================================================
void setupAP() {
  // AP_STA mode lets us scan for networks while the AP is running
  WiFi.mode(WIFI_AP_STA);
  delay(100);

  if (!WiFi.softAP(ap_ssid.c_str(), ap_password.c_str())) {
    Serial.println("[WiFi] Failed to setup AP with password — trying open AP");
    WiFi.softAP(ap_ssid.c_str());
  }

  IPAddress IP = WiFi.softAPIP();
  Serial.print("[WiFi] AP started. IP: ");
  Serial.println(IP);
  logDebug("AP started, IP: " + IP.toString() + ", SSID: " + ap_ssid);
}

void stopAP() {
  WiFi.softAPdisconnect(true);
  Serial.println("[WiFi] AP stopped");
  logDebug("AP stopped");
}

// ============================================================
// Async WiFi Scan — safe to run while AP is active (AP_STA mode)
// ============================================================
static bool wifiScanStarted = false;
static unsigned long wifiScanStartedAt = 0;

void startWiFiScan() {
  if (wifiScanStarted || WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
    Serial.println("[WiFi] Scan already in progress, skipping");
    return;
  }

  // Delete any stale scan results first
  WiFi.scanDelete();
  
  // Use async scan. 
  // Note: On ESP32, if SoftAP is active, scanning may cause temporary disconnection of clients
  // but it shouldn't crash if we are in WIFI_AP_STA mode and don't toggle modes.
  int result = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true);
  
  if (result == WIFI_SCAN_FAILED) {
    Serial.println("[WiFi] Failed to start async scan");
    logDebug("WiFi scan failed to start");
    return;
  }

  wifiScanStarted = true;
  wifiScanStartedAt = millis();
  availableSSIDs.clear();
  Serial.println("[WiFi] Async scan started");
  logDebug("WiFi async scan started");
}

bool wifiScanComplete() {
  return !wifiScanStarted;
}

// Called every loop() tick — collects async scan results without blocking
void pollWiFiScan() {
  if (!wifiScanStarted) return;

  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    // Timeout guard — if scan runs >10s something is very wrong
    if (millis() - wifiScanStartedAt > 10000) {
      Serial.println("[WiFi] Scan timeout — deleting stale results");
      logDebug("WiFi scan timeout after 10s");
      WiFi.scanDelete();
      wifiScanStarted = false;
    }
    return;
  }

  // n == WIFI_SCAN_FAILED or >= 0: scan done
  availableSSIDs.clear();
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      availableSSIDs.push_back(ssid);
      Serial.println("[WiFi] Found: " + ssid + " (" + String(rssi) + " dBm)");
    }
  } else {
    Serial.println("[WiFi] Scan complete — no networks found (n=" + String(n) + ")");
  }
  logDebug("WiFi scan done, found: " + String(availableSSIDs.size()) + " networks");
  WiFi.scanDelete();
  wifiScanStarted = false;
}

// ============================================================
// Blocking scan — used during DuckyScript IF_PRESENT commands
// ============================================================
void scanWiFi() {
  Serial.println("[WiFi] Blocking scan (during script)...");
  logDebug("WiFi blocking scan started");

  // DO NOT change WiFi mode — already in AP_STA
  WiFi.scanDelete();
  int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);

  availableSSIDs.clear();
  if (n == WIFI_SCAN_FAILED || n < 0) {
    Serial.println("[WiFi] Blocking scan failed");
    lastError = "WiFi scan failed";
    errorCount++;
    logDebug("WiFi blocking scan FAILED (n=" + String(n) + ")");
    return;
  }
  for (int i = 0; i < n; i++) {
    availableSSIDs.push_back(WiFi.SSID(i));
  }
  WiFi.scanDelete();
  Serial.println("[WiFi] Blocking scan done, found " + String(n) + " networks.");
  logDebug("WiFi blocking scan done, found: " + String(n));
}

bool isSSIDPresent(String ssid) {
  for (String& s : availableSSIDs) {
    if (s == ssid) return true;
  }
  return false;
}

// ============================================================
// Non-blocking WiFi Join — creates background task
// ============================================================
void joinWiFi(String ssid, String password) {
  if (wifiJoining) {
    Serial.println("[WiFi] Aborting previous join attempt before starting new one");
    WiFi.disconnect(false); // false = keep STA config
    wifiJoining = false;
    // Remove old WIFI_JOINING tasks
    for (auto it = activeTasks.begin(); it != activeTasks.end();) {
      if (it->type == "WIFI_JOINING") it = activeTasks.erase(it);
      else ++it;
    }
  }

  Serial.println("[WiFi] Joining network: " + ssid);
  logDebug("WiFi join started: " + ssid);

  // DO NOT call WiFi.mode() — already in AP_STA
  current_sta_ssid = ssid;
  current_sta_password = password;
  WiFi.begin(ssid.c_str(), password.c_str());

  wifiJoining = true;
  wifiJoinStartTime = millis();

  BackgroundTask task;
  task.id = nextTaskId++;
  task.description = "Connecting to WiFi: " + ssid;
  task.type = "WIFI_JOINING";
  task.condition = "";
  task.payload = ssid;
  task.active = true;
  activeTasks.push_back(task);
  Serial.println("[WiFi] Join background task created (ID " + String(task.id) + ")");
}

void stopJoiningWiFi() {
  if (!wifiJoining) return;
  WiFi.disconnect(false);
  wifiJoining = false;
  for (auto it = activeTasks.begin(); it != activeTasks.end();) {
    if (it->type == "WIFI_JOINING") it = activeTasks.erase(it);
    else ++it;
  }
  Serial.println("[WiFi] Join aborted by user");
  logDebug("WiFi join aborted by user");
}

void leaveWiFi() {
  current_sta_ssid = "";
  current_sta_password = "";
  WiFi.disconnect(false); // Disconnect STA, keep AP alive
  wifiJoining = false;
  variables["WIFI_CONNECTED"] = "false";
  variables["WIFI_SSID"] = "";
  Serial.println("[WiFi] Disconnected from internet WiFi");
  logDebug("WiFi STA disconnected");
}

// ============================================================
// Utility
// ============================================================
String getTime(String region) {
  if (WiFi.status() != WL_CONNECTED) return "00:00:00";
  
  // Basic offset logic
  long offset = 0;
  if (region.equalsIgnoreCase("de")) offset = 3600; // CET (UTC+1) - neglecting DST for simplicity unless requested
  
  configTime(offset, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Failed to get time";
  char timeStr[9];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
  return String(timeStr);
}

String getDay(String region) {
  if (WiFi.status() != WL_CONNECTED) return "Unknown";
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Failed to get day";
  char dayStr[10];
  strftime(dayStr, sizeof(dayStr), "%A", &timeinfo); // %A is full weekday name
  return String(dayStr);
}

String makeHttpRequest(String url) {
  if (WiFi.status() != WL_CONNECTED) return "Error: Not connected";
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  String payload = (httpCode > 0) ? http.getString() : "Error: " + String(httpCode);
  http.end();
  return payload;
}

// ============================================================
// WiFi Credential Management & Auto-Connect
// ============================================================
void saveWiFiCredentials(String ssid, String pass) {
    if (!sdCardPresent) return;
    
    // Check if already exists to avoid duplicates
    String existing = getSavedWiFiCredentials();
    if (existing.indexOf("SSID=\"" + ssid + "\"") != -1) {
        Serial.println("[WiFi] Credentials for " + ssid + " already saved.");
        return;
    }

    File f = SD.open("/wifi_creds.txt", FILE_APPEND);
    if (f) {
        f.println("SSID=\"" + ssid + "\" PASSWORD=\"" + pass + "\"");
        f.close();
        Serial.println("[WiFi] Credentials saved for: " + ssid);
    } else {
        Serial.println("[WiFi] Failed to open /wifi_creds.txt for writing");
    }
}

String getSavedWiFiCredentials() {
    if (!sdCardPresent || !SD.exists("/wifi_creds.txt")) return "[]";
    
    File f = SD.open("/wifi_creds.txt");
    if (!f) return "[]";
    
    String json = "[";
    bool first = true;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        
        int s1 = line.indexOf("SSID=\"");
        int s2 = line.indexOf("\"", s1 + 6);
        int p1 = line.indexOf("PASSWORD=\"");
        int p2 = line.indexOf("\"", p1 + 10);
        
        if (s1 != -1 && s2 != -1) {
            if (!first) json += ",";
            String ssid = line.substring(s1 + 6, s2);
            String pass = (p1 != -1 && p2 != -1) ? line.substring(p1 + 10, p2) : "";
            json += "{\"ssid\":\"" + ssid + "\",\"pass\":\"" + pass + "\"}";
            first = false;
        }
    }
    f.close();
    json += "]";
    return json;
}

void deleteWiFiCredential(String ssid) {
    if (!sdCardPresent || !SD.exists("/wifi_creds.txt")) return;
    
    File f = SD.open("/wifi_creds.txt");
    File temp = SD.open("/temp_creds.txt", FILE_WRITE);
    
    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.indexOf("SSID=\"" + ssid + "\"") == -1) {
            temp.println(line);
        }
    }
    f.close();
    temp.close();
    SD.remove("/wifi_creds.txt");
    SD.rename("/temp_creds.txt", "/wifi_creds.txt");
    Serial.println("[WiFi] Deleted credentials for: " + ssid);
}

void processAutoConnect() {
    static unsigned long lastAutoAttempt = 0;
    if (!autoConnectEnabled || scriptRunning || wifiJoining || WiFi.status() == WL_CONNECTED) return;
    
    // Attempt every 60 seconds
    if (millis() - lastAutoAttempt < 60000) return;
    lastAutoAttempt = millis();
    
    Serial.println("[WiFi] Auto-connect: Searching for saved networks...");
    scanWiFi();
    
    String credsJson = getSavedWiFiCredentials();
    // Simple manual parsing of the mini-JSON we just built
    for (String ssid : availableSSIDs) {
        if (credsJson.indexOf("\"ssid\":\"" + ssid + "\"") != -1) {
            // Found a saved network that is currently visible
            int start = credsJson.indexOf("\"ssid\":\"" + ssid + "\"");
            int passStart = credsJson.indexOf("\"pass\":\"", start) + 8;
            int passEnd = credsJson.indexOf("\"", passStart);
            String pass = credsJson.substring(passStart, passEnd);
            
            Serial.println("[WiFi] Auto-connect: Found visible saved network " + ssid + ". Joining...");
            joinWiFi(ssid, pass);
            break; 
        }
    }
}

#include "WebServerManager.h"
#include "WebServerHandlers.h"
#include "FSManager.h"
#include "DuckyInterpreter.h"
#include "LogManager.h"
#include "LEDManager.h"
#include "WiFiManager.h"
#include "BTManager.h"
#include <ArduinoJson.h>

void setupWebServer() {
  server.enableCORS(true);

  // API Endpoints
  server.on("/api/change-directory", HTTP_POST, handleChangeDirectory);
  server.on("/api/current-directory", handleGetCurrentDirectory);
  server.on("/api/detect-os", HTTP_POST, handleDetectOS);
  server.on("/api/use-file", HTTP_POST, handleUseFile);
  server.on("/api/copy-file", HTTP_POST, handleCopyFile);
  server.on("/api/cut-file", HTTP_POST, handleCutFile);
  server.on("/api/paste-file", HTTP_POST, handlePasteFile);
  server.on("/api/upload", HTTP_POST, []() {
    server.send(200, "text/plain", "Upload complete: " + uploadFilename);
  }, handleFileUpload);
  server.on("/api/download", handleFileDownload);
  server.on("/api/list-files", handleListFiles);
  server.on("/api/delete-file", HTTP_DELETE, handleDeleteFile);
  server.on("/api/create-directory", HTTP_POST, handleCreateDirectory);
  server.on("/api/file-info", handleFileInfo);

  // Main UI
  server.on("/", []() {
    if (!sdCardPresent) { server.send(500, "text/plain", "SD Card not present"); return; }
    if (!SD.exists(FILE_INDEX)) { server.send(404, "text/plain", "index.html not found on SD card"); return; }
    File file = SD.open(FILE_INDEX);
    if (!file) { server.send(500, "text/plain", "Failed to open index.html"); return; }
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.streamFile(file, "text/html");
    file.close();
  });

  server.on("/style.css", []() {
    if (!sdCardPresent || !SD.exists("/style.css")) { server.send(404, "text/plain", "style.css not found"); return; }
    File file = SD.open("/style.css");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.streamFile(file, "text/css");
    file.close();
  });

  server.on("/script.js", []() {
    if (!sdCardPresent || !SD.exists("/script.js")) { server.send(404, "text/plain", "script.js not found"); return; }
    File file = SD.open("/script.js");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.streamFile(file, "application/javascript");
    file.close();
  });

  server.onNotFound([]() {
    if (!sdCardPresent) { server.send(404, "text/plain", "Not Found"); return; }
    String path = server.uri();
    if (SD.exists(path)) {
      File file = SD.open(path);
      server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      if (path.endsWith(".html")) server.streamFile(file, "text/html");
      else if (path.endsWith(".css")) server.streamFile(file, "text/css");
      else if (path.endsWith(".js")) server.streamFile(file, "application/javascript");
      else server.streamFile(file, "text/plain");
      file.close();
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });


  server.on("/execute", HTTP_POST, []() {
    String script = server.arg("plain");
    executeScript(script);
    server.send(200, "text/plain; charset=utf-8", "OK");
  });

  server.on("/stop", HTTP_POST, []() {
    stopRequested = true;
    server.send(200, "text/plain; charset=utf-8", "Stop requested - waiting for current command to finish");
  });

  server.on("/language", []() {
    if (server.hasArg("lang")) {
      String lang = server.arg("lang");
      if (loadLanguage(lang)) {
        // Persist immediately so it survives reboots
        preferences.putString("language", currentLanguage);
        server.send(200, "text/plain; charset=utf-8", "OK");
      } else {
        server.send(400, "text/plain; charset=utf-8", "Language not found");
      }
    } else {
      server.send(400, "text/plain; charset=utf-8", "No language specified");
    }
  });

  server.on("/status", []() {
    String status = "Ready - Language: " + currentLanguage;
    status += " - Scripts: " + String(availableScripts.size());
    status += " - Clients: " + String(WiFi.softAPgetStationNum());
    status += " - Scan Time: " + String(wifiScanTime) + "ms";
    status += " - LED: " + String(ledEnabled ? "ON" : "OFF");
    status += " - Logging: " + String(loggingEnabled ? "ON" : "OFF");
    status += " - SD Card: " + String(sdCardPresent ? "OK" : "REMOVED");
    status += " - Errors: " + String(errorCount);
    status += " - Total Scripts: " + String(totalScriptsExecuted);
    status += " - Total Commands: " + String(totalCommandsExecuted);
    status += " - Detected OS: " + detectedOS;
    status += " - Current Dir: " + currentDirectory;
    if (scriptRunning) status += " - Script Running";
    if (bootModeEnabled) status += " - Boot: " + (currentBootScriptFiles.size() > 0 ? currentBootScriptFiles[0] : "Active");
    if (WiFi.status() == WL_CONNECTED) status += " - WiFi: " + WiFi.SSID();
    server.send(200, "text/plain; charset=utf-8", status);
  });

  server.on("/selfdestruct", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      server.send(400, "text/plain; charset=utf-8", "Invalid JSON");
      return;
    }

    String password = doc["password"].as<String>();
    if (password == ap_password) {
      server.send(200, "text/plain; charset=utf-8", "Self-destruct initiated");
      delay(1000);
      selfDestruct();
    } else {
      server.send(403, "text/plain; charset=utf-8", "Invalid password");
    }
  });

  server.on("/api/toggle-led", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(128);
    if (!deserializeJson(doc, body) && doc.containsKey("enabled")) {
      ledEnabled = doc["enabled"];
    } else {
      ledEnabled = !ledEnabled;
    }
    preferences.putBool("led_enabled", ledEnabled);

    if (ledEnabled) {
      setLEDMode(0);
    } else {
      pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      pixels.show();
    }

    DynamicJsonDocument resp(128);
    resp["ledEnabled"] = ledEnabled;
    String response;
    serializeJson(resp, response);
    server.send(200, "application/json", response);
    logDebug("LED " + String(ledEnabled ? "enabled" : "disabled"));
  });

  server.on("/api/toggle-logging", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      server.send(400, "text/plain; charset=utf-8", "Invalid JSON");
      return;
    }

    loggingEnabled = doc["enabled"];
    preferences.putBool("logging_enabled", loggingEnabled);
    server.send(200, "text/plain; charset=utf-8", "Logging " + String(loggingEnabled ? "enabled" : "disabled"));
  });

  server.on("/api/toggle-wifi", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);
    if (!deserializeJson(doc, body)) {
      wifiToggleEnabled = doc["enabled"];
      preferences.putBool("wifi_toggle", wifiToggleEnabled);
      if (wifiToggleEnabled) setupAP(); else stopAP();
    }
    server.send(200, "application/json", "{\"enabled\":" + String(wifiToggleEnabled ? "true" : "false") + "}");
  });

  server.on("/api/toggle-bluetooth", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);
    if (!deserializeJson(doc, body)) {
      bluetoothToggleEnabled = doc["enabled"];
      preferences.putBool("bt_toggle", bluetoothToggleEnabled);
      if (bluetoothToggleEnabled) setupBT(); else stopBT();
    }
    server.send(200, "application/json", "{\"enabled\":" + String(bluetoothToggleEnabled ? "true" : "false") + "}");
  });

  server.on("/api/toggle-bt-discovery", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);
    if (!deserializeJson(doc, body)) {
      btDiscoveryEnabled = doc["enabled"];
      preferences.putBool("bt_discovery", btDiscoveryEnabled);
    }
    server.send(200, "application/json", "{\"enabled\":" + String(btDiscoveryEnabled ? "true" : "false") + "}");
  });


  server.on("/api/save-bt-settings", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);
    if (!deserializeJson(doc, body)) {
      bluetoothName = doc["name"].as<String>();
      preferences.putString("bt_name", bluetoothName);
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/api/save-usb", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(512);
    if (!deserializeJson(doc, body)) {
      currentUSBConfig.vid = doc["vid"].as<String>();
      currentUSBConfig.pid = doc["pid"].as<String>();
      currentUSBConfig.rndVid = doc["rndVid"];
      currentUSBConfig.rndPid = doc["rndPid"];
      currentUSBConfig.mfr = doc["mfr"].as<String>();
      currentUSBConfig.prod = doc["prod"].as<String>();

      preferences.putString("usb_vid", currentUSBConfig.vid);
      preferences.putString("usb_pid", currentUSBConfig.pid);
      preferences.putBool("usb_rndVid", currentUSBConfig.rndVid);
      preferences.putBool("usb_rndPid", currentUSBConfig.rndPid);
      preferences.putString("usb_mfr", currentUSBConfig.mfr);
      preferences.putString("usb_prod", currentUSBConfig.prod);
      
      server.send(200, "text/plain", "USB settings saved. Rebooting for changes to take effect...");
      delay(1000);
      ESP.restart();
    } else {
      server.send(400, "text/plain", "Invalid JSON");
    }
  });

  server.on("/api/stop-ap", HTTP_POST, []() {
    logDebug("HTTP: /api/stop-ap called");
    stopAP();
    server.send(200, "text/plain", "AP stopped. You may lose connection if not connected to another WiFi.");
  });

  server.on("/api/tasks", []() {
    String json = "[";
    for (size_t i = 0; i < activeTasks.size(); i++) {
      if (i > 0) json += ",";
      json += "{\"id\":" + String(activeTasks[i].id) + ",\"description\":\"" + activeTasks[i].description + "\"}";
    }
    json += "]";
    server.send(200, "application/json", json);
  });

  server.on("/api/cancel-task", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);
    if (!deserializeJson(doc, body)) {
      int id = doc["id"];
      for (auto it = activeTasks.begin(); it != activeTasks.end(); ++it) {
        if (it->id == id) {
          activeTasks.erase(it);
          server.send(200, "text/plain", "Task cancelled");
          return;
        }
      }
    }
    server.send(404, "text/plain", "Task not found");
  });


  server.on("/api/scripts", []() {
    String json = "[";
    for (size_t i = 0; i < availableScripts.size(); i++) {
      if (i > 0) json += ",";
      json += "\"" + availableScripts[i] + "\"";
    }
    json += "]";
    server.send(200, "application/json; charset=utf-8", json);
  });

  server.on("/api/languages", []() {
    String json = "[";
    for (size_t i = 0; i < availableLanguages.size(); i++) {
      if (i > 0) json += ",";
      json += "\"" + availableLanguages[i] + "\"";
    }
    json += "]";
    server.send(200, "application/json; charset=utf-8", json);
  });

  server.on("/api/load", []() {
    if (server.hasArg("file")) {
      String filename = server.arg("file");
      String content = loadScript(filename);
      if (content.length() > 0) {
        server.send(200, "text/plain; charset=utf-8", content);
      } else {
        server.send(404, "text/plain; charset=utf-8", "File not found or empty");
      }
    } else {
      server.send(400, "text/plain; charset=utf-8", "No file specified");
    }
  });

  server.on("/api/save", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      server.send(400, "text/plain; charset=utf-8", "Invalid JSON");
      return;
    }

    String filename = doc["filename"].as<String>();
    String content = doc["content"].as<String>();

    if (saveScript(filename, content)) {
      server.send(200, "text/plain; charset=utf-8", "Script saved: " + filename);
    } else {
      server.send(500, "text/plain; charset=utf-8", "Failed to save script");
    }
  });

  server.on("/api/delete", HTTP_DELETE, []() {
    if (server.hasArg("file")) {
      String filename = server.arg("file");
      if (deleteScript(filename)) {
        server.send(200, "text/plain; charset=utf-8", "Script deleted: " + filename);
      } else {
        server.send(500, "text/plain; charset=utf-8", "Failed to delete script");
      }
    } else {
      server.send(400, "text/plain; charset=utf-8", "No file specified");
    }
  });

  server.on("/api/check-file", []() {
    if (server.hasArg("file")) {
      String filename = server.arg("file");
      if (!filename.endsWith(".txt")) filename += ".txt";
      String filePath = String(DIR_SCRIPTS) + "/" + filename;
      bool exists = SD.exists(filePath);
      server.send(200, "application/json", "{\"exists\":" + String(exists ? "true" : "false") + "}");
    } else {
      server.send(400, "text/plain; charset=utf-8", "No file specified");
    }
  });

  server.on("/api/save-settings", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      server.send(400, "text/plain; charset=utf-8", "Invalid JSON");
      return;
    }

    String type = doc["type"].as<String>();
    if (type == "language") {
      String newLang = doc["language"].as<String>();
      if (loadLanguage(newLang)) {
        saveSettings();
        server.send(200, "text/plain; charset=utf-8", "Language applied: " + newLang);
      } else {
        server.send(500, "text/plain; charset=utf-8", "Failed to load language file");
      }
    } else {
      server.send(400, "text/plain; charset=utf-8", "Unknown settings type");
    }
  });

  server.on("/api/save-wifi", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      server.send(400, "text/plain; charset=utf-8", "Invalid JSON");
      return;
    }

    ap_ssid = doc["ssid"].as<String>();
    ap_password = doc["password"].as<String>();
    wifiScanTime = doc["scanTime"];
    saveSettings();

    server.send(200, "text/plain; charset=utf-8", "WiFi settings saved. Rebooting...");
    delay(1000);
    ESP.restart();
  });

  server.on("/api/set-boot-script", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      server.send(400, "text/plain; charset=utf-8", "Invalid JSON");
      return;
    }

    JsonArray files = doc["filenames"].as<JsonArray>();
    currentBootScriptFiles.clear();
    bootScript = "";
    String prefString = "";

    for (JsonVariant v : files) {
      String filename = v.as<String>();
      if (SD.exists(String(DIR_SCRIPTS) + "/" + filename)) {
        currentBootScriptFiles.push_back(filename);
        if (prefString.length() > 0) prefString += ",";
        prefString += filename;
        bootScript += loadScript(filename) + "\n";
      }
    }

    if (currentBootScriptFiles.size() > 0) {
      bootModeEnabled = true;
      preferences.putString("boot_script", prefString);
      server.send(200, "text/plain; charset=utf-8", "Boot scripts set: " + prefString);
    } else {
      bootModeEnabled = false;
      preferences.remove("boot_script");
      server.send(200, "text/plain; charset=utf-8", "Boot scripts disabled");
    }
  });

  server.on("/api/test-boot-script", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      server.send(400, "text/plain; charset=utf-8", "Invalid JSON");
      return;
    }

    String filename = doc["filename"].as<String>();

    if (SD.exists(String(DIR_SCRIPTS) + "/" + filename)) {
      String script = loadScript(filename);
      executeScript(script);
      server.send(200, "text/plain; charset=utf-8", "Testing boot script: " + filename);
    } else {
      server.send(404, "text/plain; charset=utf-8", "Script file not found");
    }
  });

  server.on("/api/stats", []() {
    DynamicJsonDocument doc(4096);
    doc["errorCount"] = errorCount;
    doc["totalScripts"] = totalScriptsExecuted;
    doc["totalCommands"] = totalCommandsExecuted;
    doc["clientCount"] = WiFi.softAPgetStationNum();
    doc["lastError"] = lastError;
    doc["sdCardPresent"] = sdCardPresent;
    doc["uptime"] = millis() / 1000;
    doc["freeMemory"] = ESP.getFreeHeap();
    doc["detectedOS"] = detectedOS;
    doc["currentLanguage"] = currentLanguage;
    doc["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
    doc["wifiSSID"] = ap_ssid; 
    doc["wifiPassword"] = ap_password;
    doc["staSSID"] = (WiFi.status() == WL_CONNECTED) ? current_sta_ssid : "";
    doc["staPassword"] = (WiFi.status() == WL_CONNECTED) ? current_sta_password : "";
    doc["wifiScanTime"] = wifiScanTime;
    doc["usbVID"] = currentUSBConfig.vid;
    doc["usbPID"] = currentUSBConfig.pid;
    doc["usbRndVID"] = currentUSBConfig.rndVid;
    doc["usbRndPID"] = currentUSBConfig.rndPid;
    doc["usbMfr"] = currentUSBConfig.mfr;
    doc["usbProd"] = currentUSBConfig.prod;
    doc["ledEnabled"] = ledEnabled;
    doc["loggingEnabled"] = loggingEnabled;
    doc["wifiToggleEnabled"] = wifiToggleEnabled;
    doc["btToggleEnabled"] = bluetoothToggleEnabled;
    doc["btDiscoveryEnabled"] = btDiscoveryEnabled;
    doc["autoConnectEnabled"] = autoConnectEnabled;
    doc["saveOnConnectEnabled"] = saveOnConnectEnabled;
    // Delay progress (0-100)
    if (currentDelayTotal > 0) {
      unsigned long elapsed = millis() - currentDelayStart;
      int progress = (int)min(100UL, (elapsed * 100UL) / currentDelayTotal);
      doc["delayProgress"] = progress;
      doc["delayTotal"] = currentDelayTotal;
    } else {
      doc["delayProgress"] = 0;
      doc["delayTotal"] = 0;
    }

    String response;
    serializeJson(doc, response);
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.send(200, "application/json", response);
  });


  server.on("/api/history", []() {
    String json = "[";
    for (size_t i = 0; i < commandHistory.size(); i++) {
      if (i > 0) json += ",";
      json += "\"" + commandHistory[i] + "\"";
    }
    json += "]";
    server.send(200, "application/json", json);
  });

  server.on("/api/clear-history", HTTP_POST, []() {
    commandHistory.clear();
    saveCommandHistory();
    server.send(200, "text/plain; charset=utf-8", "Command history cleared");
  });

  server.on("/api/export-history", []() {
    String historyContent = "";
    for (String cmd : commandHistory) {
      historyContent += cmd + "\n";
    }
    server.send(200, "text/plain", historyContent);
  });

  server.on("/api/clear-errors", HTTP_POST, []() {
    clearErrors();
    server.send(200, "text/plain; charset=utf-8", "Error log cleared");
  });

  server.on("/api/factory-reset", HTTP_POST, []() {
    preferences.clear();
    server.send(200, "text/plain; charset=utf-8", "Factory reset complete. Rebooting...");
    delay(1000);
    ESP.restart();
  });

  server.on("/api/validate-script", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      server.send(400, "text/plain; charset=utf-8", "Invalid JSON");
      return;
    }

    String script = doc["script"].as<String>();
    int lineCount = 0;
    int errCount = 0;
    String errs = "";

    std::vector<String> lines;
    int startIndex = 0;
    int endIndex = script.indexOf('\n');

    while (endIndex != -1) {
      String line = script.substring(startIndex, endIndex);
      line.trim();
      if (line.length() > 0 && !line.startsWith("REM") && !line.startsWith("//")) {
        lines.push_back(line);
      }
      startIndex = endIndex + 1;
      endIndex = script.indexOf('\n', startIndex);
    }
    if (startIndex < script.length()) {
      String line = script.substring(startIndex);
      line.trim();
      if (line.length() > 0 && !line.startsWith("REM") && !line.startsWith("//")) {
        lines.push_back(line);
      }
    }

    lineCount = lines.size();

    for (String line : lines) {
      if (line.startsWith("STRING ") && line.length() == 7) {
        errs += "Empty STRING command\\n";
        errCount++;
      }
      if (line.startsWith("DELAY ") && line.substring(6).toInt() <= 0) {
        errs += "Invalid DELAY value\\n";
        errCount++;
      }
      if (line.startsWith("REPEAT ") && line.substring(7).toInt() <= 0) {
        errs += "Invalid REPEAT count\\n";
        errCount++;
      }
    }

    if (errCount == 0) {
      server.send(200, "text/plain; charset=utf-8", "Script validation passed! " + String(lineCount) + " commands found.");
    } else {
      server.send(200, "text/plain; charset=utf-8", "Script validation found " + String(errCount) + " issues:\\n" + errs);
    }
  });

  // Trigger async WiFi scan — returns immediately, results via /api/scan-results
  server.on("/api/scan-wifi", []() {
    logDebug("HTTP: /api/scan-wifi called");
    startWiFiScan();
    server.send(202, "application/json", "{\"status\":\"scanning\"}");
  });

  // Poll for async scan results
  // Returns: {"done": false, "networks": []}  while scanning
  //          {"done": true,  "networks": [{ssid,rssi},...]} when complete
  server.on("/api/scan-results", []() {
    bool done = wifiScanComplete();
    String json = "{\"done\":" + String(done ? "true" : "false") + ",\"networks\":[";
    if (done) {
      for (size_t i = 0; i < availableSSIDs.size(); i++) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + availableSSIDs[i] + "\",\"rssi\":0}";
      }
      logDebug("HTTP: /api/scan-results — done, " + String(availableSSIDs.size()) + " networks");
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  // Non-blocking WiFi join
  server.on("/api/join-internet", HTTP_POST, []() {
    String body = server.arg("plain");
    logDebug("HTTP: /api/join-internet body=" + body);
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, body)) {
      server.send(400, "text/plain", "Invalid JSON");
      logDebug("HTTP: /api/join-internet — invalid JSON");
      return;
    }
    String ssid = doc["ssid"].as<String>();
    String password = doc["password"].as<String>();
    bool saveCredentials = doc.containsKey("save") ? (bool)doc["save"] : false;
    if (ssid.length() == 0) {
      server.send(400, "text/plain", "SSID required");
      return;
    }
    logDebug("HTTP: joining WiFi SSID=" + ssid);
    joinWiFi(ssid, password);
    if (saveCredentials && sdCardPresent) {
      saveWiFiCredentials(ssid, password);
      logDebug("WiFi credentials saved for: " + ssid);
    }
    server.send(200, "application/json", "{\"status\":\"connecting\",\"ssid\":\"" + ssid + "\"}");
  });

  // WiFi join status polling
  server.on("/api/wifi-join-status", []() {
    String status;
    if (wifiJoining) {
      unsigned long elapsed = (millis() - wifiJoinStartTime) / 1000;
      status = "connecting";
      logDebug("HTTP: wifi-join-status=connecting, elapsed=" + String(elapsed) + "s");
    } else if (WiFi.status() == WL_CONNECTED) {
      status = "connected";
    } else {
      status = "idle";
    }
    String ssid = (WiFi.status() == WL_CONNECTED) ? current_sta_ssid : "";
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.send(200, "application/json",
      "{\"status\":\"" + status + "\",\"ssid\":\"" + ssid + "\",\"ip\":\"" + WiFi.localIP().toString() + "\"}");
  });

  // Abort WiFi join
  server.on("/api/stop-wifi-join", HTTP_POST, []() {
    logDebug("HTTP: /api/stop-wifi-join");
    stopJoiningWiFi();
    server.send(200, "text/plain", "WiFi join aborted");
  });

  // Leave internet (disconnect STA)
  server.on("/api/leave-internet", HTTP_POST, []() {
    logDebug("HTTP: /api/leave-internet");
    leaveWiFi();
    server.send(200, "application/json", "{\"message\":\"Disconnected from WiFi\"}");
  });

  // ---- Saved WiFi Credentials ----
  server.on("/api/saved-wifi", []() {
    server.send(200, "application/json", getSavedWiFiCredentials());
  });

  server.on("/api/delete-saved-wifi", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);
    if (!deserializeJson(doc, body) && doc.containsKey("ssid")) {
      deleteWiFiCredential(doc["ssid"].as<String>());
      server.send(200, "text/plain", "Deleted");
    } else {
      server.send(400, "text/plain", "Invalid request");
    }
  });

  server.on("/api/toggle-autoconnect", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(128);
    if (!deserializeJson(doc, body) && doc.containsKey("enabled")) {
      autoConnectEnabled = doc["enabled"];
      preferences.putBool("autoconnect", autoConnectEnabled);
    }
    server.send(200, "application/json", "{\"enabled\":" + String(autoConnectEnabled ? "true" : "false") + "}");
  });

  server.on("/api/toggle-save-on-connect", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(128);
    if (!deserializeJson(doc, body) && doc.containsKey("enabled")) {
      saveOnConnectEnabled = doc["enabled"];
      preferences.putBool("save_creds", saveOnConnectEnabled);
    }
    server.send(200, "application/json", "{\"enabled\":" + String(saveOnConnectEnabled ? "true" : "false") + "}");
  });

  server.begin();
  Serial.println("Web server started");
  logDebug("Web server started on port 80");
}

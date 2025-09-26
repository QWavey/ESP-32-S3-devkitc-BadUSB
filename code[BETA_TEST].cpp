#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <vector>
#include <map>
#include <algorithm>
#include <Preferences.h>

// SD Card pin configuration
#define SD_CS_PIN 10
#define SD_MOSI_PIN 11
#define SD_MISO_PIN 13
#define SD_SCK_PIN 12

// Reset button pin
#define RESET_BUTTON_PIN 0

// Default WiFi AP configuration
String ap_ssid = "ESP32-BadUSB";
String ap_password = "badusb123";

// Web server
WebServer server(80);

// USB HID Keyboard
USBHIDKeyboard keyboard;

// Preferences for persistent settings
Preferences preferences;

// Global variables
std::vector<String> availableLanguages;
std::vector<String> availableScripts;
std::map<String, String> currentKeymap;
String currentLanguage = "us";
int defaultDelay = 0;
std::map<String, String> variables;
String lastCommand = "";
bool scriptRunning = false;
bool stopRequested = false;
bool bootModeEnabled = false;
String bootScript = "";
String currentBootScriptFile = "";
unsigned long lastExecutionTime = 0;
int executionDelay = 0;

// Conditional execution variables
bool skipConditionalBlock = false;
String skipUntilSSID = "";

// LED control (built-in RGB LED)
#define LED_PIN 48
#define LED_R_PIN 45
#define LED_G_PIN 46
#define LED_B_PIN 47

// WiFi scanning
std::vector<String> availableSSIDs;
int wifiScanTime = 5000; // Default scan time (5 seconds)

// Self-destruct
bool selfDestructEnabled = false;

struct KeyCode {
  uint8_t modifier;
  uint8_t key;
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize preferences
  preferences.begin("badusb", false);
  
  // Load saved WiFi settings
  ap_ssid = preferences.getString("ap_ssid", "ESP32-BadUSB");
  ap_password = preferences.getString("ap_password", "badusb123");
  currentLanguage = preferences.getString("language", "us");
  wifiScanTime = preferences.getInt("wifi_scan_time", 5000);
  
  // Initialize built-in LED
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  setLED(0, 255, 0); // Green - starting up
  
  // Initialize reset button
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  
  // Initialize SD card
  if (!initSDCard()) {
    Serial.println("SD Card initialization failed!");
    setLED(255, 0, 0); // Red - error
    while (1) delay(1000);
  }
  
  // Load available languages
  loadAvailableLanguages();
  
  // Load available scripts
  loadAvailableScripts();
  
  // Load default language
  if (!loadLanguage(currentLanguage)) {
    Serial.println("Failed to load default language, trying 'us'");
    if (!loadLanguage("us")) {
      Serial.println("Failed to load 'us' language");
    }
  }
  
  // Check for boot script
  currentBootScriptFile = preferences.getString("boot_script", "");
  if (currentBootScriptFile != "" && SD.exists("/scripts/" + currentBootScriptFile)) {
    bootScript = loadScript(currentBootScriptFile);
    bootModeEnabled = true;
    Serial.println("Boot script found: " + currentBootScriptFile);
  } else if (SD.exists("/scripts/boot.txt")) {
    bootScript = loadScript("boot.txt");
    bootModeEnabled = true;
    currentBootScriptFile = "boot.txt";
    Serial.println("Boot script found - will execute on client connection");
  }
  
  // Initialize USB HID
  keyboard.begin();
  USB.begin();
  delay(1000);
  
  // Setup WiFi AP
  setupAP();
  
  // Setup web server routes
  setupWebServer();
  
  setLED(0, 0, 255); // Blue - ready
  Serial.println("ESP32-S3 BadUSB Ready!");
  Serial.print("Connect to WiFi: ");
  Serial.println(ap_ssid);
  Serial.print("Password: ");
  Serial.println(ap_password);
  Serial.println("Open browser and go to: 192.168.4.1");
}

void loop() {
  server.handleClient();
  
  // Check reset button
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (scriptRunning) {
      stopRequested = true;
      Serial.println("Stop requested via reset button");
      setLED(255, 165, 0); // Orange - stopping
      delay(1000);
      setLED(0, 0, 255); // Blue - ready
    }
    while (digitalRead(RESET_BUTTON_PIN) == LOW) {
      delay(50); // Wait for button release
    }
  }
  
  // Handle boot script execution
  if (bootModeEnabled && WiFi.softAPgetStationNum() > 0 && !scriptRunning) {
    Serial.println("Client connected - executing boot script");
    executeScript(bootScript);
  }
  
  delay(10);
}

bool initSDCard() {
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("Card Mount Failed");
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return false;
  }
  
  // Create directories if they don't exist
  if (!SD.exists("/languages")) {
    SD.mkdir("/languages");
  }
  if (!SD.exists("/scripts")) {
    SD.mkdir("/scripts");
  }
  
  Serial.println("SD Card initialized successfully");
  return true;
}

void setupAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
}

void loadAvailableLanguages() {
  File root = SD.open("/languages");
  if (!root) {
    Serial.println("Failed to open languages directory");
    return;
  }
  
  availableLanguages.clear();
  
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String fileName = file.name();
      if (fileName.endsWith(".json")) {
        String langName = fileName.substring(0, fileName.lastIndexOf('.'));
        availableLanguages.push_back(langName);
        Serial.println("Found language: " + langName);
      }
    }
    file = root.openNextFile();
  }
  root.close();
}

void loadAvailableScripts() {
  File root = SD.open("/scripts");
  if (!root) {
    Serial.println("Failed to open scripts directory");
    return;
  }
  
  availableScripts.clear();
  
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String fileName = file.name();
      if (fileName.endsWith(".txt")) {
        availableScripts.push_back(fileName);
        Serial.println("Found script: " + fileName);
      }
    }
    file = root.openNextFile();
  }
  root.close();
}

bool loadLanguage(String language) {
  String filePath = "/languages/" + language + ".json";
  File file = SD.open(filePath);
  
  if (!file) {
    Serial.println("Failed to open language file: " + filePath);
    return false;
  }
  
  String jsonString = file.readString();
  file.close();
  
  DynamicJsonDocument doc(8192);
  DeserializationError error = deserializeJson(doc, jsonString);
  
  if (error) {
    Serial.println("Failed to parse JSON: " + String(error.c_str()));
    return false;
  }
  
  currentKeymap.clear();
  
  for (JsonPair kv : doc.as<JsonObject>()) {
    String key = kv.key().c_str();
    if (!key.startsWith("comment") && !key.startsWith("_comment")) {
      String value = kv.value().as<String>();
      currentKeymap[key] = value;
    }
  }
  
  currentLanguage = language;
  Serial.println("Loaded language: " + language);
  return true;
}

String loadScript(String filename) {
  String filePath = "/scripts/" + filename;
  File file = SD.open(filePath);
  
  if (!file) {
    Serial.println("Failed to open script file: " + filePath);
    return "";
  }
  
  String scriptContent = file.readString();
  file.close();
  
  Serial.println("Loaded script: " + filename);
  return scriptContent;
}

bool saveScript(String filename, String content) {
  // Ensure filename ends with .txt
  if (!filename.endsWith(".txt")) {
    filename += ".txt";
  }
  
  String filePath = "/scripts/" + filename;
  
  // Delete existing file if it exists
  if (SD.exists(filePath)) {
    SD.remove(filePath);
  }
  
  File file = SD.open(filePath, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to create script file: " + filePath);
    return false;
  }
  
  size_t bytesWritten = file.print(content);
  file.close();
  
  if (bytesWritten > 0) {
    Serial.println("Script saved: " + filename + " (" + String(bytesWritten) + " bytes)");
    loadAvailableScripts(); // Refresh script list
    return true;
  } else {
    Serial.println("Failed to write script: " + filename);
    return false;
  }
}

bool deleteScript(String filename) {
  String filePath = "/scripts/" + filename;
  
  if (SD.exists(filePath)) {
    if (SD.remove(filePath)) {
      Serial.println("Script deleted: " + filename);
      loadAvailableScripts(); // Refresh script list
      return true;
    }
  }
  
  Serial.println("Failed to delete script: " + filename);
  return false;
}

KeyCode parseKeyCode(String keyCodeStr) {
  KeyCode result = {0, 0};
  
  // Parse hex values like "02,00,04"
  int firstComma = keyCodeStr.indexOf(',');
  int secondComma = keyCodeStr.indexOf(',', firstComma + 1);
  
  if (firstComma > 0 && secondComma > firstComma) {
    String modStr = keyCodeStr.substring(0, firstComma);
    String keyStr = keyCodeStr.substring(secondComma + 1);
    
    result.modifier = strtol(modStr.c_str(), NULL, 16);
    result.key = strtol(keyStr.c_str(), NULL, 16);
  }
  
  return result;
}

void fastPressKey(String key) {
  if (currentKeymap.find(key) != currentKeymap.end()) {
    KeyCode kc = parseKeyCode(currentKeymap[key]);
    
    // Handle modifiers first
    if (kc.modifier > 0) {
      // Press modifier keys
      if (kc.modifier & 0x01) keyboard.press(KEY_LEFT_CTRL);
      if (kc.modifier & 0x02) keyboard.press(KEY_LEFT_SHIFT);
      if (kc.modifier & 0x04) keyboard.press(KEY_LEFT_ALT);
      if (kc.modifier & 0x08) keyboard.press(KEY_LEFT_GUI);
      if (kc.modifier & 0x10) keyboard.press(KEY_RIGHT_CTRL);
      if (kc.modifier & 0x20) keyboard.press(KEY_RIGHT_SHIFT);
      if (kc.modifier & 0x40) keyboard.press(KEY_RIGHT_ALT);
      if (kc.modifier & 0x80) keyboard.press(KEY_RIGHT_GUI);
    }
    
    // Press the main key
    if (kc.key > 0) {
      keyboard.pressRaw(kc.key);
    }
    
    delay(5); // Reduced from 50ms to 5ms for faster execution
    keyboard.releaseAll();
  }
}

void fastPressKeyCombination(std::vector<String> keys) {
  std::vector<KeyCode> keyCodes;
  uint8_t combinedModifier = 0;
  uint8_t mainKey = 0;
  
  // Parse all keys and combine modifiers
  for (String key : keys) {
    if (currentKeymap.find(key) != currentKeymap.end()) {
      KeyCode kc = parseKeyCode(currentKeymap[key]);
      keyCodes.push_back(kc);
      combinedModifier |= kc.modifier;
      if (kc.key > 0 && mainKey == 0) {
        mainKey = kc.key;
      }
    }
  }
  
  // Press modifier keys
  if (combinedModifier & 0x01) keyboard.press(KEY_LEFT_CTRL);
  if (combinedModifier & 0x02) keyboard.press(KEY_LEFT_SHIFT);
  if (combinedModifier & 0x04) keyboard.press(KEY_LEFT_ALT);
  if (combinedModifier & 0x08) keyboard.press(KEY_LEFT_GUI);
  if (combinedModifier & 0x10) keyboard.press(KEY_RIGHT_CTRL);
  if (combinedModifier & 0x20) keyboard.press(KEY_RIGHT_SHIFT);
  if (combinedModifier & 0x40) keyboard.press(KEY_RIGHT_ALT);
  if (combinedModifier & 0x80) keyboard.press(KEY_RIGHT_GUI);
  
  // Press main key
  if (mainKey > 0) {
    keyboard.pressRaw(mainKey);
  }
  
  delay(5); // Reduced from 50ms to 5ms
  keyboard.releaseAll();
}

void fastPressKeySequentially(std::vector<String> keys) {
  for (String key : keys) {
    fastPressKey(key);
    if (defaultDelay > 0) {
      delay(defaultDelay);
    } else {
      delay(10); // Reduced from 50ms to 10ms
    }
  }
}

String processVariables(String text) {
  String result = text;
  
  // Process string variables
  for (auto& pair : variables) {
    result.replace(pair.first, pair.second);
  }
  
  return result;
}

void fastTypeString(String text) {
  String processedText = processVariables(text);
  
  for (int i = 0; i < processedText.length(); i++) {
    if (stopRequested) break;
    
    String ch = String(processedText.charAt(i));
    
    if (currentKeymap.find(ch) != currentKeymap.end()) {
      KeyCode kc = parseKeyCode(currentKeymap[ch]);
      
      // Handle modifiers
      if (kc.modifier > 0) {
        if (kc.modifier & 0x01) keyboard.press(KEY_LEFT_CTRL);
        if (kc.modifier & 0x02) keyboard.press(KEY_LEFT_SHIFT);
        if (kc.modifier & 0x04) keyboard.press(KEY_LEFT_ALT);
        if (kc.modifier & 0x08) keyboard.press(KEY_LEFT_GUI);
        if (kc.modifier & 0x10) keyboard.press(KEY_RIGHT_CTRL);
        if (kc.modifier & 0x20) keyboard.press(KEY_RIGHT_SHIFT);
        if (kc.modifier & 0x40) keyboard.press(KEY_RIGHT_ALT);
        if (kc.modifier & 0x80) keyboard.press(KEY_RIGHT_GUI);
      }
      
      // Press main key
      if (kc.key > 0) {
        keyboard.pressRaw(kc.key);
      }
      
      delay(2); // Reduced from 20ms to 2ms
      keyboard.releaseAll();
      delay(2); // Reduced from 20ms to 2ms
    }
  }
}

void scanWiFi() {
  Serial.println("Scanning for WiFi networks...");
  Serial.println("Scan time: " + String(wifiScanTime) + "ms");
  
  // Set scan time (passive scan with longer duration for weak signals)
  WiFi.scanNetworks(true, false, false, wifiScanTime);
  
  // Wait for scan to complete
  int n = WiFi.scanComplete();
  while (n == WIFI_SCAN_RUNNING) {
    delay(100);
    n = WiFi.scanComplete();
  }
  
  availableSSIDs.clear();
  
  if (n == WIFI_SCAN_FAILED) {
    Serial.println("WiFi scan failed");
    return;
  }
  
  for (int i = 0; i < n; ++i) {
    availableSSIDs.push_back(WiFi.SSID(i));
    Serial.println("Found: " + WiFi.SSID(i) + " (RSSI: " + String(WiFi.RSSI(i)) + ")");
  }
  
  WiFi.scanDelete();
  Serial.println("WiFi scan completed. Found " + String(n) + " networks.");
}

bool isSSIDPresent(String ssid) {
  for (String availableSSID : availableSSIDs) {
    if (availableSSID == ssid) {
      return true;
    }
  }
  return false;
}

void executeScript(String script) {
  if (scriptRunning) {
    Serial.println("Script already running");
    return;
  }
  
  scriptRunning = true;
  stopRequested = false;
  setLED(0, 255, 0); // Green - script running
  
  std::vector<String> lines;
  int startIndex = 0;
  int endIndex = script.indexOf('\n');
  
  // Split script into lines
  while (endIndex != -1) {
    String line = script.substring(startIndex, endIndex);
    line.trim();
    if (line.length() > 0) {
      lines.push_back(line);
    }
    startIndex = endIndex + 1;
    endIndex = script.indexOf('\n', startIndex);
  }
  if (startIndex < script.length()) {
    String line = script.substring(startIndex);
    line.trim();
    if (line.length() > 0) {
      lines.push_back(line);
    }
  }
  
  // Execute each line
  for (int i = 0; i < lines.size() && !stopRequested; i++) {
    String line = lines[i];
    line.trim();
    
    if (line.length() == 0 || line.startsWith("REM") || line.startsWith("//")) {
      continue;
    }
    
    // Handle conditional skipping for WiFi presence
    if (skipConditionalBlock) {
      if (line.startsWith("IF_PRESENT SSID=\"") || line.startsWith("IF_NOTPRESENT SSID=\"")) {
        // Look for the matching end condition
        int quoteStart = line.indexOf('"') + 1;
        int quoteEnd = line.indexOf('"', quoteStart);
        if (quoteEnd > quoteStart) {
          String currentSSID = line.substring(quoteStart, quoteEnd);
          // Skip until we find the opposite condition for the same SSID
          continue;
        }
      } else if (line.startsWith("ENDIF") || line.startsWith("END_IF")) {
        skipConditionalBlock = false;
        skipUntilSSID = "";
        continue;
      }
      continue; // Skip this line during conditional block
    }
    
    Serial.println("Executing: " + line);
    
    executeCommand(line);
    
    // Check for stop request after each command
    if (stopRequested) {
      break;
    }
    
    // Handle execution delay
    if (executionDelay > 0 && millis() - lastExecutionTime < executionDelay) {
      delay(executionDelay - (millis() - lastExecutionTime));
    }
    lastExecutionTime = millis();
  }
  
  scriptRunning = false;
  stopRequested = false;
  skipConditionalBlock = false;
  skipUntilSSID = "";
  setLED(0, 0, 255); // Blue - ready
  Serial.println("Script execution completed");
}

void executeCommand(String line) {
  if (stopRequested) return;
  
  lastCommand = line;
  
  // Handle variable assignment first (before other commands)
  if (line.indexOf('=') > 0 && !line.startsWith("STRING") && !line.startsWith("REM") && 
      !line.startsWith("IF_") && !line.startsWith("DELAY") && !line.startsWith("LED") &&
      !line.startsWith("WIFI_SCAN_TIME")) {
    
    int equalIndex = line.indexOf('=');
    String varName = line.substring(0, equalIndex);
    String varValue = line.substring(equalIndex + 1);
    varName.trim();
    varValue.trim();
    
    // Process variables in the value
    varValue = processVariables(varValue);
    
    variables[varName] = varValue;
    
    Serial.println("Variable set: " + varName + " = " + varValue);
    return;
  }
  
  if (line.startsWith("DEFAULTDELAY ") || line.startsWith("DEFAULT_DELAY ")) {
    int spaceIndex = line.indexOf(' ');
    defaultDelay = line.substring(spaceIndex + 1).toInt();
  }
  else if (line.startsWith("DELAY ")) {
    int delayTime = line.substring(6).toInt();
    unsigned long startTime = millis();
    while (millis() - startTime < delayTime && !stopRequested) {
      delay(10);
    }
  }
  else if (line.startsWith("STRING ")) {
    String text = line.substring(7);
    fastTypeString(text);
  }
  else if (line.startsWith("LOCALE ")) {
    String locale = line.substring(7);
    loadLanguage(locale);
  }
  else if (line.startsWith("LED ")) {
    String rgbStr = line.substring(4);
    int r = 0, g = 0, b = 0;
    int firstSpace = rgbStr.indexOf(' ');
    int secondSpace = rgbStr.indexOf(' ', firstSpace + 1);
    
    if (firstSpace > 0 && secondSpace > firstSpace) {
      r = rgbStr.substring(0, firstSpace).toInt();
      g = rgbStr.substring(firstSpace + 1, secondSpace).toInt();
      b = rgbStr.substring(secondSpace + 1).toInt();
      setLED(r, g, b);
    }
  }
  else if (line.startsWith("KEYCODE ")) {
    String keycodeStr = line.substring(8);
    std::vector<uint8_t> keycodes;
    int startIdx = 0;
    int spaceIdx = keycodeStr.indexOf(' ');
    
    while (spaceIdx != -1) {
      String hexVal = keycodeStr.substring(startIdx, spaceIdx);
      if (hexVal.startsWith("0x")) hexVal = hexVal.substring(2);
      keycodes.push_back(strtol(hexVal.c_str(), NULL, 16));
      startIdx = spaceIdx + 1;
      spaceIdx = keycodeStr.indexOf(' ', startIdx);
    }
    
    if (startIdx < keycodeStr.length()) {
      String hexVal = keycodeStr.substring(startIdx);
      if (hexVal.startsWith("0x")) hexVal = hexVal.substring(2);
      keycodes.push_back(strtol(hexVal.c_str(), NULL, 16));
    }
    
    if (keycodes.size() >= 2) {
      uint8_t modifier = keycodes[0];
      uint8_t keyCode = keycodes[1];
      
      if (modifier & 0x01) keyboard.press(KEY_LEFT_CTRL);
      if (modifier & 0x02) keyboard.press(KEY_LEFT_SHIFT);
      if (modifier & 0x04) keyboard.press(KEY_LEFT_ALT);
      if (modifier & 0x08) keyboard.press(KEY_LEFT_GUI);
      if (modifier & 0x10) keyboard.press(KEY_RIGHT_CTRL);
      if (modifier & 0x20) keyboard.press(KEY_RIGHT_SHIFT);
      if (modifier & 0x40) keyboard.press(KEY_RIGHT_ALT);
      if (modifier & 0x80) keyboard.press(KEY_RIGHT_GUI);
      
      if (keyCode > 0) {
        keyboard.pressRaw(keyCode);
      }
      
      delay(5);
      keyboard.releaseAll();
    }
  }
  else if (line.startsWith("VAR ")) {
    String varLine = line.substring(4);
    int equalIndex = varLine.indexOf('=');
    if (equalIndex > 0) {
      String varName = varLine.substring(0, equalIndex);
      String varValue = varLine.substring(equalIndex + 1);
      varName.trim();
      varValue.trim();
      
      varValue = processVariables(varValue);
      variables[varName] = varValue;
    }
  }
  else if (line.startsWith("IF_PRESENT SSID=\"")) {
    // Extract SSID and optional scan time
    int quoteStart = line.indexOf('"') + 1;
    int quoteEnd = line.indexOf('"', quoteStart);
    if (quoteEnd > quoteStart) {
      String ssid = line.substring(quoteStart, quoteEnd);
      
      // Check for SCAN_TIME parameter
      int scanTimeIndex = line.indexOf("SCAN_TIME=");
      int scanTime = wifiScanTime; // Use default if not specified
      
      if (scanTimeIndex > quoteEnd) {
        String scanTimeStr = line.substring(scanTimeIndex + 10);
        scanTimeStr.trim();
        scanTime = scanTimeStr.toInt();
        if (scanTime < 1000) scanTime = 1000; // Minimum 1 second
        if (scanTime > 30000) scanTime = 30000; // Maximum 30 seconds
        Serial.println("Using custom scan time: " + String(scanTime) + "ms");
      }
      
      // Save current scan time and set new one
      int originalScanTime = wifiScanTime;
      wifiScanTime = scanTime;
      
      scanWiFi();
      
      // Restore original scan time
      wifiScanTime = originalScanTime;
      
      if (!isSSIDPresent(ssid)) {
        // Skip until ENDIF or IF_NOTPRESENT for same SSID
        skipConditionalBlock = true;
        skipUntilSSID = ssid;
        Serial.println("SSID not present, skipping block: " + ssid);
      } else {
        Serial.println("SSID present, continuing: " + ssid);
      }
    }
  }
  else if (line.startsWith("IF_NOTPRESENT SSID=\"")) {
    int quoteStart = line.indexOf('"') + 1;
    int quoteEnd = line.indexOf('"', quoteStart);
    if (quoteEnd > quoteStart) {
      String ssid = line.substring(quoteStart, quoteEnd);
      
      // Check for SCAN_TIME parameter
      int scanTimeIndex = line.indexOf("SCAN_TIME=");
      int scanTime = wifiScanTime; // Use default if not specified
      
      if (scanTimeIndex > quoteEnd) {
        String scanTimeStr = line.substring(scanTimeIndex + 10);
        scanTimeStr.trim();
        scanTime = scanTimeStr.toInt();
        if (scanTime < 1000) scanTime = 1000; // Minimum 1 second
        if (scanTime > 30000) scanTime = 30000; // Maximum 30 seconds
        Serial.println("Using custom scan time: " + String(scanTime) + "ms");
      }
      
      // Save current scan time and set new one
      int originalScanTime = wifiScanTime;
      wifiScanTime = scanTime;
      
      scanWiFi();
      
      // Restore original scan time
      wifiScanTime = originalScanTime;
      
      if (isSSIDPresent(ssid)) {
        skipConditionalBlock = true;
        skipUntilSSID = ssid;
        Serial.println("SSID present, skipping block: " + ssid);
      } else {
        Serial.println("SSID not present, continuing: " + ssid);
      }
    }
  }
  else if (line.startsWith("ENDIF") || line.startsWith("END_IF")) {
    skipConditionalBlock = false;
    skipUntilSSID = "";
  }
  else if (line.startsWith("WIFI_SCAN_TIME ")) {
    int scanTime = line.substring(15).toInt();
    if (scanTime >= 1000 && scanTime <= 30000) {
      wifiScanTime = scanTime;
      preferences.putInt("wifi_scan_time", scanTime);
      Serial.println("WiFi scan time set to: " + String(scanTime) + "ms");
    } else {
      Serial.println("Invalid scan time. Use 1000-30000 ms.");
    }
  }
  else if (line.startsWith("SELFDESTRUCT ")) {
    String password = line.substring(12);
    if (password == ap_password) {
      selfDestruct();
    }
  }
  else {
    handleKeyInput(line);
  }
}

void selfDestruct() {
  Serial.println("SELF DESTRUCT INITIATED!");
  setLED(255, 0, 0); // Red - self destruct
  
  // Delete all scripts
  File root = SD.open("/scripts");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String fileName = file.name();
        SD.remove("/scripts/" + fileName);
        Serial.println("Deleted: " + fileName);
      }
      file = root.openNextFile();
    }
    root.close();
  }
  
  // Delete language files
  root = SD.open("/languages");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String fileName = file.name();
        SD.remove("/languages/" + fileName);
        Serial.println("Deleted: " + fileName);
      }
      file = root.openNextFile();
    }
    root.close();
  }
  
  delay(2000);
  ESP.restart();
}

void handleKeyInput(String line) {
  std::vector<String> keys;
  int startIdx = 0;
  int spaceIdx = line.indexOf(' ');
  
  while (spaceIdx != -1) {
    keys.push_back(line.substring(startIdx, spaceIdx));
    startIdx = spaceIdx + 1;
    spaceIdx = line.indexOf(' ', startIdx);
  }
  if (startIdx < line.length()) {
    keys.push_back(line.substring(startIdx));
  }
  
  if (keys.size() == 1) {
    fastPressKey(keys[0]);
  } else {
    fastPressKeyCombination(keys);
  }
}

void setLED(int r, int g, int b) {
  analogWrite(LED_R_PIN, r);
  analogWrite(LED_G_PIN, g);
  analogWrite(LED_B_PIN, b);
}

void saveSettings() {
  preferences.putString("ap_ssid", ap_ssid);
  preferences.putString("ap_password", ap_password);
  preferences.putString("language", currentLanguage);
  preferences.putString("boot_script", currentBootScriptFile);
  preferences.putInt("wifi_scan_time", wifiScanTime);
  Serial.println("Settings saved");
}

void setupWebServer() {
  server.enableCORS(true);
  
  server.on("/", []() {
    String html = "<!DOCTYPE html><html><head><title>ESP32-S3 BadUSB</title>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body{font-family:Arial,sans-serif;margin:20px;background:#1a1a1a;color:#fff}";
    html += ".container{max-width:1000px;margin:0 auto;background:#2d2d2d;padding:20px;border-radius:10px}";
    html += "h1{color:#4CAF50;text-align:center;margin-bottom:30px}";
    html += ".section{margin:20px 0;padding:15px;background:#3d3d3d;border-radius:5px}";
    html += "textarea{width:100%;height:300px;background:#1a1a1a;color:#fff;border:1px solid #555;padding:10px;font-family:monospace;font-size:14px;resize:vertical}";
    html += "button{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;margin:5px}";
    html += "button:hover{background:#45a049}";
    html += "button.danger{background:#f44336}";
    html += "button.danger:hover{background:#da190b}";
    html += "select{background:#1a1a1a;color:#fff;border:1px solid #555;padding:8px;margin:5px;border-radius:3px}";
    html += "input{background:#1a1a1a;color:#fff;border:1px solid #555;padding:8px;margin:5px;border-radius:3px}";
    html += ".status{padding:10px;margin:10px 0;border-radius:5px;background:#333}";
    html += ".examples{background:#2a2a2a;padding:15px;border-radius:5px;margin:10px 0}";
    html += "pre{background:#1a1a1a;padding:10px;border-radius:3px;overflow-x:auto;font-size:12px}";
    // Mobile-friendly tabs
    html += ".tab{overflow:hidden;background-color:#2d2d2d;display:flex;flex-wrap:wrap}";
    html += ".tab button{background-color:#2d2d2d;border:none;outline:none;cursor:pointer;padding:12px 16px;transition:0.3s;flex:1;min-width:100px;color:#fff}";
    html += ".tab button:hover{background-color:#3d3d3d}";
    html += ".tab button.active{background-color:#4CAF50}";
    html += "@media (max-width: 768px) {.tab button{font-size:12px;padding:10px 8px}}";
    html += ".tabcontent{display:none;padding:20px 12px;border:1px solid #555;border-top:none;animation:fadeEffect 0.5s}";
    html += "@keyframes fadeEffect{from{opacity:0;} to{opacity:1;}}";
    html += ".file-list{max-height:200px;overflow-y:auto;border:1px solid #555;padding:10px;background:#1a1a1a;border-radius:5px}";
    html += ".file-item{padding:8px;cursor:pointer;border-bottom:1px solid #333;display:flex;justify-content:space-between;align-items:center}";
    html += ".file-item:hover{background:#2a2a2a}";
    html += ".file-buttons{display:flex;gap:5px}";
    html += ".file-buttons button{padding:4px 8px;font-size:11px;margin:0}";
    html += ".control-panel{display:flex;gap:10px;flex-wrap:wrap;margin:10px 0}";
    html += ".control-btn{flex:1;min-width:120px}";
    html += "@media (max-width: 768px) {.control-btn{min-width:100px;font-size:12px}}";
    html += ".setting-group{margin:15px 0;padding:10px;background:#2a2a2a;border-radius:5px}";
    html += ".setting-row{display:flex;align-items:center;margin:10px 0;gap:10px;flex-wrap:wrap}";
    html += ".setting-label{min-width:120px;font-weight:bold}";
    html += "@media (max-width: 768px) {.setting-row{flex-direction:column;align-items:flex-start}}";
    html += ".modal{display:none;position:fixed;z-index:1000;left:0;top:0;width:100%;height:100%;overflow:auto;background-color:rgba(0,0,0,0.4)}";
    html += ".modal-content{background-color:#2d2d2d;margin:15% auto;padding:20px;border:1px solid #888;width:300px;max-width:90%;border-radius:10px}";
    html += ".close{color:#aaa;float:right;font-size:28px;font-weight:bold;cursor:pointer}";
    html += ".close:hover{color:#fff}";
    html += "</style></head><body>";
    
    html += "<div class='container'>";
    html += "<h1>🔌 ESP32-S3 BadUSB Control Panel</h1>";
    
    html += "<div class='tab'>";
    html += "<button class='tablinks active' onclick=\"openTab(event, 'ScriptTab')\">💻 Script</button>";
    html += "<button class='tablinks' onclick=\"openTab(event, 'FilesTab')\">📁 Files</button>";
    html += "<button class='tablinks' onclick=\"openTab(event, 'BootTab')\">👢 Boot</button>";
    html += "<button class='tablinks' onclick=\"openTab(event, 'SettingsTab')\">⚙️ Settings</button>";
    html += "<button class='tablinks' onclick=\"openTab(event, 'ExamplesTab')\">📚 Examples</button>";
    html += "</div>";
    
    // Script Tab
    html += "<div id='ScriptTab' class='tabcontent' style='display:block'>";
    html += "<h3>💻 Script Execution</h3>";
    html += "<textarea id='scriptArea' placeholder='Enter your BadUSB script here...&#10;&#10;Example:&#10;DELAY 1000&#10;GUI r&#10;DELAY 500&#10;STRING notepad&#10;ENTER&#10;DELAY 1000&#10;STRING Hello World!'></textarea>";
    html += "<br>";
    html += "<div class='control-panel'>";
    html += "<button class='control-btn' onclick='executeScript()'>🚀 Execute Script</button>";
    html += "<button class='control-btn' onclick='stopScript()'>🛑 Stop Script</button>";
    html += "<button class='control-btn' onclick='clearScript()'>🗑️ Clear</button>";
    html += "<button class='control-btn' onclick='saveScriptPrompt()'>💾 Save Script</button>";
    html += "</div>";
    html += "</div>";
    
    // Files Tab
    html += "<div id='FilesTab' class='tabcontent'>";
    html += "<h3>📁 Script Files</h3>";
    html += "<div class='file-list' id='fileList'>Loading...</div>";
    html += "<br>";
    html += "<div class='setting-row'>";
    html += "<input type='text' id='newFilename' placeholder='New script name (without .txt)' style='flex:1'>";
    html += "<button onclick='saveScript()'>💾 Save Current Script</button>";
    html += "<button onclick='refreshFiles()'>🔄 Refresh List</button>";
    html += "</div>";
    html += "</div>";
    
    // Boot Tab
    html += "<div id='BootTab' class='tabcontent'>";
    html += "<h3>👢 Boot Script Configuration</h3>";
    html += "<div class='setting-group'>";
    html += "<div class='setting-row'>";
    html += "<span class='setting-label'>Current Boot Script:</span>";
    html += "<select id='bootScriptSelect' style='flex:1'>";
    html += "<option value=''>None</option>";
    for (String script : availableScripts) {
      html += "<option value='" + script + "'";
      if (script == currentBootScriptFile) html += " selected";
      html += ">" + script + "</option>";
    }
    html += "</select>";
    html += "<button onclick='setBootScript()'>Set Boot Script</button>";
    html += "</div>";
    html += "</div>";
    html += "<div class='section'>";
    html += "<h4>Boot Script Preview</h4>";
    html += "<textarea id='bootScriptPreview' readonly style='height:200px' placeholder='Select a boot script to preview...'></textarea>";
    html += "<button onclick='loadBootScriptToEditor()'>Load to Editor</button>";
    html += "<button onclick='testBootScript()'>Test Boot Script</button>";
    html += "</div>";
    html += "</div>";
    
    // Settings Tab
    html += "<div id='SettingsTab' class='tabcontent'>";
    html += "<h3>⚙️ Settings</h3>";
    
    html += "<div class='setting-group'>";
    html += "<h4>🌍 Language Configuration</h4>";
    html += "<div class='setting-row'>";
    html += "<span class='setting-label'>Current Language:</span>";
    html += "<span id='currentLang'>" + currentLanguage + "</span>";
    html += "</div>";
    html += "<div class='setting-row'>";
    html += "<span class='setting-label'>Select Language:</span>";
    html += "<select id='languageSelect' style='flex:1'>";
    for (String lang : availableLanguages) {
      html += "<option value='" + lang + "'";
      if (lang == currentLanguage) html += " selected";
      html += ">" + lang + "</option>";
    }
    html += "</select>";
    html += "<button onclick='changeLanguage()'>Change Language</button>";
    html += "</div>";
    html += "<button onclick='saveLanguageSettings()'>💾 Save Language Settings</button>";
    html += "</div>";
    
    html += "<div class='setting-group'>";
    html += "<h4>📶 WiFi Configuration</h4>";
    html += "<div class='setting-row'>";
    html += "<span class='setting-label'>Network Name (SSID):</span>";
    html += "<input type='text' id='wifiSSID' value='" + ap_ssid + "' style='flex:1'>";
    html += "</div>";
    html += "<div class='setting-row'>";
    html += "<span class='setting-label'>Password:</span>";
    html += "<input type='password' id='wifiPassword' value='" + ap_password + "' style='flex:1'>";
    html += "</div>";
    html += "<div class='setting-row'>";
    html += "<span class='setting-label'>WiFi Scan Time (ms):</span>";
    html += "<input type='number' id='wifiScanTime' value='" + String(wifiScanTime) + "' min='1000' max='30000' step='1000' style='flex:1'>";
    html += "<span style='font-size:12px;color:#ccc'>1000-30000 ms</span>";
    html += "</div>";
    html += "<button onclick='saveWiFiSettings()'>💾 Save WiFi Settings</button>";
    html += "<p style='font-size:12px;color:#ccc'>WiFi changes require device reboot to take effect</p>";
    html += "</div>";
    
    html += "<div class='setting-group'>";
    html += "<h4>⚡ Quick Actions</h4>";
    html += "<div class='control-panel'>";
    html += "<button class='control-btn' onclick='quickAction(\"GUI r\")'>Win+R</button>";
    html += "<button class='control-btn' onclick='quickAction(\"CTRL ALT DEL\")'>Ctrl+Alt+Del</button>";
    html += "<button class='control-btn' onclick='quickAction(\"ALT F4\")'>Alt+F4</button>";
    html += "<button class='control-btn' onclick='quickAction(\"CTRL SHIFT ESC\")'>Task Manager</button>";
    html += "<button class='control-btn' onclick='typeText()'>⌨️ Type Text</button>";
    html += "</div>";
    html += "</div>";
    
    html += "<div class='setting-group'>";
    html += "<h4>⚠️ Danger Zone</h4>";
    html += "<button class='danger' onclick='selfDestructPrompt()'>💥 Self Destruct</button>";
    html += "<p style='font-size:12px;color:#ccc'>This will delete all scripts and reboot the device</p>";
    html += "</div>";
    html += "</div>";
    
    // Examples Tab
    html += "<div id='ExamplesTab' class='tabcontent'>";
    html += "<h3>📚 Script Examples & Documentation</h3>";
    html += "<div class='examples'>";
    
    html += "<h4>🖱️ Basic Key Combinations:</h4>";
    html += "<pre>REM Sequential key presses\nWINDOWS\nr\nREM Simultaneous key presses\nWINDOWS r\nREM Alt+Tab\nALT TAB</pre>";
    html += "<button onclick='loadExample(1)'>Load Example</button>";
    
    html += "<h4>📝 String and Variable Examples:</h4>";
    html += "<pre>VAR1 = Hello\nVAR2 = World\nSTRING VAR1 VAR2\nREM Output: HelloWorld</pre>";
    html += "<button onclick='loadExample(2)'>Load Example</button>";
    
    html += "<h4>🌐 WiFi Detection with Custom Scan Time:</h4>";
    html += "<pre>REM Standard scan (5 seconds)\nIF_PRESENT SSID=\"MyWiFi\"\nSTRING WiFi Found!\nENDIF\n\nREM Custom scan time for weak signals\nIF_PRESENT SSID=\"WeakWiFi\" SCAN_TIME=10000\nSTRING Weak WiFi Found!\nENDIF\n\nIF_NOTPRESENT SSID=\"SecretWiFi\" SCAN_TIME=15000\nSTRING Secret WiFi Not Found!\nENDIF</pre>";
    html += "<button onclick='loadExample(3)'>Load Example</button>";
    
    html += "<h4>💡 LED Control:</h4>";
    html += "<pre>LED 255 0 0\nREM Red LED\nDELAY 1000\nLED 0 255 0\nREM Green LED\nDELAY 1000\nLED 0 0 255\nREM Blue LED</pre>";
    html += "<button onclick='loadExample(4)'>Load Example</button>";
    
    html += "<h4>⌨️ Raw Keycodes:</h4>";
    html += "<pre>REM Send raw HID keycode\nKEYCODE 0x02 0x04\nREM Left Shift + A\n\nKEYCODE 0x01 0x06\nREM Left Ctrl + C</pre>";
    html += "<button onclick='loadExample(5)'>Load Example</button>";
    
    html += "</div>";
    html += "</div>";
    
    html += "<div class='section'>";
    html += "<h3>📊 Device Status</h3>";
    html += "<div id='status' class='status'>Ready</div>";
    html += "</div>";
    
    // File override modal
    html += "<div id='fileOverrideModal' class='modal'>";
    html += "<div class='modal-content'>";
    html += "<span class='close' onclick='closeModal()'>&times;</span>";
    html += "<h3>File Already Exists</h3>";
    html += "<p id='modalMessage'></p>";
    html += "<button onclick='overrideFile()'>Yes, Override</button>";
    html += "<button onclick='autoRename()'>Auto Rename</button>";
    html += "<button onclick='closeModal()'>Cancel</button>";
    html += "</div>";
    html += "</div>";
    
    html += "</div>";
    
    html += "<script>";
    html += "let pendingFilename = '';";
    html += "let pendingContent = '';";
    
    html += "function openTab(evt, tabName) {";
    html += "  var i, tabcontent, tablinks;";
    html += "  tabcontent = document.getElementsByClassName('tabcontent');";
    html += "  for (i = 0; i < tabcontent.length; i++) {";
    html += "    tabcontent[i].style.display = 'none';";
    html += "  }";
    html += "  tablinks = document.getElementsByClassName('tablinks');";
    html += "  for (i = 0; i < tablinks.length; i++) {";
    html += "    tablinks[i].className = tablinks[i].className.replace(' active', '');";
    html += "  }";
    html += "  document.getElementById(tabName).style.display = 'block';";
    html += "  evt.currentTarget.className += ' active';";
    html += "  if (tabName === 'FilesTab') { refreshFiles(); }";
    html += "  if (tabName === 'BootTab') { refreshBootScripts(); }";
    html += "}";
    
    html += "function executeScript() {";
    html += "  const script = document.getElementById('scriptArea').value;";
    html += "  if (!script.trim()) { alert('Please enter a script'); return; }";
    html += "  document.getElementById('status').innerHTML = '🟢 Executing script...';";
    html += "  fetch('/execute', { method: 'POST', body: script })";
    html += "  .then(response => response.text())";
    html += "  .then(data => { document.getElementById('status').innerHTML = '✅ Script executed'; })";
    html += "  .catch(err => { document.getElementById('status').innerHTML = '❌ Error: ' + err; });";
    html += "}";
    
    html += "function stopScript() {";
    html += "  fetch('/stop', { method: 'POST' })";
    html += "  .then(response => response.text())";
    html += "  .then(data => { document.getElementById('status').innerHTML = data; });";
    html += "}";
    
    html += "function changeLanguage() {";
    html += "  const lang = document.getElementById('languageSelect').value;";
    html += "  fetch('/language?lang=' + lang)";
    html += "  .then(response => response.text())";
    html += "  .then(data => { ";
    html += "    document.getElementById('currentLang').innerHTML = lang;";
    html += "    document.getElementById('status').innerHTML = '🌍 Language changed to: ' + lang;";
    html += "  });";
    html += "}";
    
    html += "function saveLanguageSettings() {";
    html += "  fetch('/api/save-settings', {";
    html += "    method: 'POST',";
    html += "    headers: { 'Content-Type': 'application/json' },";
    html += "    body: JSON.stringify({ type: 'language' })";
    html += "  })";
    html += "  .then(response => response.text())";
    html += "  .then(data => { document.getElementById('status').innerHTML = '💾 Language settings saved'; });";
    html += "}";
    
    html += "function saveWiFiSettings() {";
    html += "  const ssid = document.getElementById('wifiSSID').value;";
    html += "  const password = document.getElementById('wifiPassword').value;";
    html += "  const scanTime = document.getElementById('wifiScanTime').value;";
    html += "  if (!ssid || !password) { alert('Please enter both SSID and password'); return; }";
    html += "  fetch('/api/save-wifi', {";
    html += "    method: 'POST',";
    html += "    headers: { 'Content-Type': 'application/json' },";
    html += "    body: JSON.stringify({ ssid: ssid, password: password, scanTime: scanTime })";
    html += "  })";
    html += "  .then(response => response.text())";
    html += "  .then(data => { ";
    html += "    document.getElementById('status').innerHTML = '📶 WiFi settings saved. Reboot required.';";
    html += "    setTimeout(() => { window.location.reload(); }, 2000);";
    html += "  });";
    html += "}";
    
    html += "function quickAction(action) {";
    html += "  document.getElementById('status').innerHTML = '⚡ Executing: ' + action;";
    html += "  fetch('/execute', { method: 'POST', body: action })";
    html += "  .then(response => response.text())";
    html += "  .then(data => { document.getElementById('status').innerHTML = '✅ Executed: ' + action; });";
    html += "}";
    
    html += "function typeText() {";
    html += "  const text = prompt('Enter text to type:');";
    html += "  if (text) { quickAction('STRING ' + text); }";
    html += "}";
    
    html += "function clearScript() {";
    html += "  document.getElementById('scriptArea').value = '';";
    html += "}";
    
    html += "function loadExample(num) {";
    html += "  const examples = {";
    html += "    1: 'REM Sequential key presses\\nWINDOWS\\nr\\nREM Simultaneous key presses\\nWINDOWS r\\nREM Alt+Tab\\nALT TAB',";
    html += "    2: 'VAR1 = Hello\\nVAR2 = World\\nSTRING VAR1 VAR2\\nREM Output: HelloWorld',";
    html += "    3: 'REM Standard scan (5 seconds)\\nIF_PRESENT SSID=\\\"MyWiFi\\\"\\nSTRING WiFi Found!\\nENDIF\\n\\nREM Custom scan time for weak signals\\nIF_PRESENT SSID=\\\"WeakWiFi\\\" SCAN_TIME=10000\\nSTRING Weak WiFi Found!\\nENDIF\\n\\nIF_NOTPRESENT SSID=\\\"SecretWiFi\\\" SCAN_TIME=15000\\nSTRING Secret WiFi Not Found!\\nENDIF',";
    html += "    4: 'LED 255 0 0\\nREM Red LED\\nDELAY 1000\\nLED 0 255 0\\nREM Green LED\\nDELAY 1000\\nLED 0 0 255\\nREM Blue LED',";
    html += "    5: 'REM Send raw HID keycode\\nKEYCODE 0x02 0x04\\nREM Left Shift + A\\n\\nKEYCODE 0x01 0x06\\nREM Left Ctrl + C'";
    html += "  };";
    html += "  document.getElementById('scriptArea').value = examples[num];";
    html += "  openTab(event, 'ScriptTab');";
    html += "}";
    
    html += "function refreshFiles() {";
    html += "  fetch('/api/scripts')";
    html += "  .then(response => response.json())";
    html += "  .then(files => {";
    html += "    const fileList = document.getElementById('fileList');";
    html += "    fileList.innerHTML = '';";
    html += "    files.forEach(file => {";
    html += "      const fileItem = document.createElement('div');";
    html += "      fileItem.className = 'file-item';";
    html += "      const fileName = document.createElement('span');";
    html += "      fileName.textContent = file;";
    html += "      const buttons = document.createElement('div');";
    html += "      buttons.className = 'file-buttons';";
    html += "      ";
    html += "      const loadBtn = document.createElement('button');";
    html += "      loadBtn.innerHTML = '📂';";
    html += "      loadBtn.title = 'Load';";
    html += "      loadBtn.onclick = () => loadFile(file);";
    html += "      ";
    html += "      const deleteBtn = document.createElement('button');";
    html += "      deleteBtn.innerHTML = '🗑️';";
    html += "      deleteBtn.title = 'Delete';";
    html += "      deleteBtn.onclick = () => deleteFile(file);";
    html += "      deleteBtn.className = 'danger';";
    html += "      ";
    html += "      buttons.appendChild(loadBtn);";
    html += "      buttons.appendChild(deleteBtn);";
    html += "      fileItem.appendChild(fileName);";
    html += "      fileItem.appendChild(buttons);";
    html += "      fileList.appendChild(fileItem);";
    html += "    });";
    html += "    if (files.length === 0) {";
    html += "      fileList.innerHTML = '<div style=\"text-align:center;color:#666\">No script files found</div>';";
    html += "    }";
    html += "  });";
    html += "}";
    
    html += "function refreshBootScripts() {";
    html += "  fetch('/api/scripts')";
    html += "  .then(response => response.json())";
    html += "  .then(files => {";
    html += "    const select = document.getElementById('bootScriptSelect');";
    html += "    const currentValue = select.value;";
    html += "    select.innerHTML = '<option value=\"\">None</option>';";
    html += "    files.forEach(file => {";
    html += "      const option = document.createElement('option');";
    html += "      option.value = file;";
    html += "      option.textContent = file;";
    html += "      if (file === currentValue) option.selected = true;";
    html += "      select.appendChild(option);";
    html += "    });";
    html += "  });";
    html += "}";
    
    html += "function previewBootScript() {";
    html += "  const filename = document.getElementById('bootScriptSelect').value;";
    html += "  const preview = document.getElementById('bootScriptPreview');";
    html += "  if (!filename) {";
    html += "    preview.value = 'Select a boot script to preview...';";
    html += "    return;";
    html += "  }";
    html += "  fetch('/api/load?file=' + encodeURIComponent(filename))";
    html += "  .then(response => response.text())";
    html += "  .then(content => {";
    html += "    preview.value = content;";
    html += "  })";
    html += "  .catch(err => {";
    html += "    preview.value = 'Error loading script: ' + err;";
    html += "  });";
    html += "}";
    
    html += "function setBootScript() {";
    html += "  const filename = document.getElementById('bootScriptSelect').value;";
    html += "  fetch('/api/set-boot-script', {";
    html += "    method: 'POST',";
    html += "    headers: { 'Content-Type': 'application/json' },";
    html += "    body: JSON.stringify({ filename: filename })";
    html += "  })";
    html += "  .then(response => response.text())";
    html += "  .then(result => {";
    html += "    document.getElementById('status').innerHTML = result;";
    html += "    previewBootScript();";
    html += "  });";
    html += "}";
    
    html += "function loadBootScriptToEditor() {";
    html += "  const content = document.getElementById('bootScriptPreview').value;";
    html += "  if (content && content !== 'Select a boot script to preview...') {";
    html += "    document.getElementById('scriptArea').value = content;";
    html += "    openTab(event, 'ScriptTab');";
    html += "    document.getElementById('status').innerHTML = '📂 Boot script loaded to editor';";
    html += "  }";
    html += "}";
    
    html += "function testBootScript() {";
    html += "  const filename = document.getElementById('bootScriptSelect').value;";
    html += "  if (!filename) { alert('Select a boot script first'); return; }";
    html += "  fetch('/api/test-boot-script', {";
    html += "    method: 'POST',";
    html += "    headers: { 'Content-Type': 'application/json' },";
    html += "    body: JSON.stringify({ filename: filename })";
    html += "  })";
    html += "  .then(response => response.text())";
    html += "  .then(result => {";
    html += "    document.getElementById('status').innerHTML = '🧪 ' + result;";
    html += "  });";
    html += "}";
    
    html += "function loadFile(filename) {";
    html += "  fetch('/api/load?file=' + encodeURIComponent(filename))";
    html += "  .then(response => response.text())";
    html += "  .then(content => {";
    html += "    document.getElementById('scriptArea').value = content;";
    html += "    document.getElementById('status').innerHTML = '📂 Loaded: ' + filename;";
    html += "    openTab(event, 'ScriptTab');";
    html += "  });";
    html += "}";
    
    html += "function deleteFile(filename) {";
    html += "  if (confirm('Delete ' + filename + '?')) {";
    html += "    fetch('/api/delete?file=' + encodeURIComponent(filename), { method: 'DELETE' })";
    html += "    .then(response => response.text())";
    html += "    .then(result => {";
    html += "      document.getElementById('status').innerHTML = result;";
    html += "      refreshFiles();";
    html += "      refreshBootScripts();";
    html += "    });";
    html += "  }";
    html += "}";
    
    html += "function saveScriptPrompt() {";
    html += "  const filename = prompt('Enter filename (without .txt):');";
    html += "  if (filename) { saveScriptAs(filename); }";
    html += "}";
    
    html += "function checkFileExists(filename) {";
    html += "  return fetch('/api/check-file?file=' + encodeURIComponent(filename))";
    html += "  .then(response => response.json())";
    html += "  .then(data => data.exists);";
    html += "}";
    
    html += "function saveScriptAs(filename) {";
    html += "  const script = document.getElementById('scriptArea').value;";
    html += "  if (!script.trim()) { alert('Script is empty'); return; }";
    html += "  ";
    html += "  if (!filename.endsWith('.txt')) filename += '.txt';";
    html += "  ";
    html += "  checkFileExists(filename).then(exists => {";
    html += "    if (exists) {";
    html += "      pendingFilename = filename;";
    html += "      pendingContent = script;";
    html += "      document.getElementById('modalMessage').textContent = 'File \"' + filename + '\" already exists. What would you like to do?';";
    html += "      document.getElementById('fileOverrideModal').style.display = 'block';";
    html += "    } else {";
    html += "      doSaveScript(filename, script);";
    html += "    }";
    html += "  });";
    html += "}";
    
    html += "function doSaveScript(filename, content) {";
    html += "  fetch('/api/save', {";
    html += "    method: 'POST',";
    html += "    headers: { 'Content-Type': 'application/json' },";
    html += "    body: JSON.stringify({ filename: filename, content: content })";
    html += "  })";
    html += "  .then(response => response.text())";
    html += "  .then(result => {";
    html += "    document.getElementById('status').innerHTML = result;";
    html += "    refreshFiles();";
    html += "    refreshBootScripts();";
    html += "  });";
    html += "}";
    
    html += "function overrideFile() {";
    html += "  doSaveScript(pendingFilename, pendingContent);";
    html += "  closeModal();";
    html += "}";
    
    html += "function autoRename() {";
    html += "  let baseName = pendingFilename.replace('.txt', '');";
    html += "  let counter = 1;";
    html += "  let newName = baseName + counter + '.txt';";
    html += "  ";
    html += "  checkFileExists(newName).then(exists => {";
    html += "    while (exists && counter < 100) {";
    html += "      counter++;";
    html += "      newName = baseName + counter + '.txt';";
    html += "    }";
    html += "    doSaveScript(newName, pendingContent);";
    html += "  });";
    html += "  closeModal();";
    html += "}";
    
    html += "function closeModal() {";
    html += "  document.getElementById('fileOverrideModal').style.display = 'none';";
    html += "  pendingFilename = '';";
    html += "  pendingContent = '';";
    html += "}";
    
    html += "function saveScript() {";
    html += "  const filename = document.getElementById('newFilename').value;";
    html += "  if (!filename) { alert('Enter filename first'); return; }";
    html += "  saveScriptAs(filename);";
    html += "}";
    
    html += "function selfDestructPrompt() {";
    html += "  const password = prompt('Enter AP password to confirm self-destruct:');";
    html += "  if (password) {";
    html += "    fetch('/selfdestruct', {";
    html += "      method: 'POST',";
    html += "      headers: { 'Content-Type': 'application/json' },";
    html += "      body: JSON.stringify({ password: password })";
    html += "    })";
    html += "    .then(response => response.text())";
    html += "    .then(result => {";
    html += "      document.getElementById('status').innerHTML = result;";
    html += "    });";
    html += "  }";
    html += "}";
    
    html += "document.getElementById('bootScriptSelect').addEventListener('change', previewBootScript);";
    
    html += "setInterval(() => {";
    html += "  fetch('/status').then(r => r.text()).then(data => {";
    html += "    const statusEl = document.getElementById('status');";
    html += "    if (!data.includes('Script Running') && !statusEl.innerHTML.includes('🟢')) {";
    html += "      statusEl.innerHTML = data;";
    html += "    }";
    html += "  });";
    html += "}, 2000);";
    
    html += "window.onload = function() {";
    html += "  refreshFiles();";
    html += "  refreshBootScripts();";
    html += "  previewBootScript();";
    html += "};";
    
    html += "window.onclick = function(event) {";
    html += "  if (event.target == document.getElementById('fileOverrideModal')) {";
    html += "    closeModal();";
    html += "  }";
    html += "}";
    
    html += "</script>";
    html += "</body></html>";
    
    server.send(200, "text/html; charset=utf-8", html);
  });
  
  server.on("/execute", HTTP_POST, []() {
    String script = server.arg("plain");
    executeScript(script);
    server.send(200, "text/plain; charset=utf-8", "OK");
  });
  
  server.on("/stop", HTTP_POST, []() {
    stopRequested = true;
    server.send(200, "text/plain; charset=utf-8", "🛑 Stop requested");
  });
  
  server.on("/language", []() {
    if (server.hasArg("lang")) {
      String lang = server.arg("lang");
      if (loadLanguage(lang)) {
        server.send(200, "text/plain; charset=utf-8", "OK");
      } else {
        server.send(400, "text/plain; charset=utf-8", "Language not found");
      }
    } else {
      server.send(400, "text/plain; charset=utf-8", "No language specified");
    }
  });
  
  server.on("/status", []() {
    String status = "📡 Ready - Language: " + currentLanguage;
    status += " - Scripts: " + String(availableScripts.size());
    status += " - Clients: " + String(WiFi.softAPgetStationNum());
    status += " - Scan Time: " + String(wifiScanTime) + "ms";
    if (scriptRunning) status += " - 🟢 Script Running";
    if (bootModeEnabled) status += " - 👢 Boot: " + currentBootScriptFile;
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
    
    String password = doc["password"];
    if (password == ap_password) {
      server.send(200, "text/plain; charset=utf-8", "💥 Self-destruct initiated");
      delay(1000);
      selfDestruct();
    } else {
      server.send(403, "text/plain; charset=utf-8", "❌ Invalid password");
    }
  });
  
  server.on("/api/scripts", []() {
    String json = "[";
    for (int i = 0; i < availableScripts.size(); i++) {
      if (i > 0) json += ",";
      json += "\"" + availableScripts[i] + "\"";
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
    
    String filename = doc["filename"];
    String content = doc["content"];
    
    if (saveScript(filename, content)) {
      server.send(200, "text/plain; charset=utf-8", "💾 Script saved: " + filename);
    } else {
      server.send(500, "text/plain; charset=utf-8", "❌ Failed to save script");
    }
  });
  
  server.on("/api/delete", HTTP_DELETE, []() {
    if (server.hasArg("file")) {
      String filename = server.arg("file");
      if (deleteScript(filename)) {
        server.send(200, "text/plain; charset=utf-8", "🗑️ Script deleted: " + filename);
      } else {
        server.send(500, "text/plain; charset=utf-8", "❌ Failed to delete script");
      }
    } else {
      server.send(400, "text/plain; charset=utf-8", "No file specified");
    }
  });
  
  server.on("/api/check-file", []() {
    if (server.hasArg("file")) {
      String filename = server.arg("file");
      if (!filename.endsWith(".txt")) filename += ".txt";
      String filePath = "/scripts/" + filename;
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
    
    String type = doc["type"];
    if (type == "language") {
      saveSettings();
      server.send(200, "text/plain; charset=utf-8", "Language settings saved");
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
    
    String ssid = doc["ssid"];
    String password = doc["password"];
    int scanTime = doc["scanTime"];
    
    ap_ssid = ssid;
    ap_password = password;
    wifiScanTime = scanTime;
    saveSettings();
    
    server.send(200, "text/plain; charset=utf-8", "WiFi settings saved. Rebooting...");
    delay(1000);
    ESP.restart();
  });
  
  server.on("/api/set-boot-script", HTTP_POST, []() {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, body);
    
    if (error) {
      server.send(400, "text/plain; charset=utf-8", "Invalid JSON");
      return;
    }
    
    String filename = doc["filename"];
    
    if (filename == "") {
      bootModeEnabled = false;
      currentBootScriptFile = "";
      bootScript = "";
      preferences.remove("boot_script");
      server.send(200, "text/plain; charset=utf-8", "👢 Boot script disabled");
    } else if (SD.exists("/scripts/" + filename)) {
      currentBootScriptFile = filename;
      bootScript = loadScript(filename);
      bootModeEnabled = true;
      preferences.putString("boot_script", filename);
      server.send(200, "text/plain; charset=utf-8", "👢 Boot script set to: " + filename);
    } else {
      server.send(404, "text/plain; charset=utf-8", "❌ Script file not found");
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
    
    String filename = doc["filename"];
    
    if (SD.exists("/scripts/" + filename)) {
      String script = loadScript(filename);
      executeScript(script);
      server.send(200, "text/plain; charset=utf-8", "Testing boot script: " + filename);
    } else {
      server.send(404, "text/plain; charset=utf-8", "Script file not found");
    }
  });
  
  server.begin();
  Serial.println("Web server started with enhanced features");
}

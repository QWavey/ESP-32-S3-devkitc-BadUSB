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
#include <Adafruit_NeoPixel.h>

// SD Card pin configuration
#define SD_CS_PIN 10
#define SD_MOSI_PIN 11
#define SD_MISO_PIN 13
#define SD_SCK_PIN 12

// Reset button pin
#define RESET_BUTTON_PIN 0

// NeoPixel LED configuration
#define LED_PIN 48
#define NUMPIXELS 1
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

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
int delayBetweenKeys = 0;
std::map<String, String> variables;
String lastCommand = "";
bool scriptRunning = false;
bool stopRequested = false;
bool bootModeEnabled = false;
String bootScript = "";
String currentBootScriptFile = "";
unsigned long lastExecutionTime = 0;
int executionDelay = 0;

// LED control variables
bool ledEnabled = true;
bool blinkingEnabled = false;
unsigned long lastBlinkTime = 0;
bool blinkState = false;
int blinkInterval = 100;
int ledMode = 0; // 0=idle, 1=running, 2=error, 3=completion, 4=warning
uint8_t currentR = 0, currentG = 255, currentB = 0;
int completionBlinkCount = 0;
unsigned long lastCompletionBlinkTime = 0;
bool completionBlinkState = false;

// Conditional execution variables
bool skipConditionalBlock = false;
String skipUntilSSID = "";

// WiFi scanning
std::vector<String> availableSSIDs;
int wifiScanTime = 5000;

// SD Card monitoring
bool sdCardPresent = true;
unsigned long lastSDCheck = 0;
const unsigned long SD_CHECK_INTERVAL = 1000;

// Logging
bool loggingEnabled = false;
bool logFileOpen = false;
File logFile;

// Command history
std::vector<String> commandHistory;
const int MAX_HISTORY_SIZE = 50;

// Performance monitoring
unsigned long scriptStartTime = 0;
unsigned long lastStatusUpdate = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 5000;

// Error tracking
int errorCount = 0;
String lastError = "";

// Script statistics
unsigned long totalScriptsExecuted = 0;
unsigned long totalCommandsExecuted = 0;

struct KeyCode {
  uint8_t modifier;
  uint8_t key;
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pixels.begin();
  pixels.setBrightness(50);
  setLED(0, 255, 0);
  
  preferences.begin("badusb", false);
  
  ap_ssid = preferences.getString("ap_ssid", "ESP32-BadUSB");
  ap_password = preferences.getString("ap_password", "badusb123");
  currentLanguage = preferences.getString("language", "us");
  wifiScanTime = preferences.getInt("wifi_scan_time", 5000);
  ledEnabled = preferences.getBool("led_enabled", true);
  loggingEnabled = preferences.getBool("logging_enabled", false);
  
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  
  if (!initSDCard()) {
    Serial.println("SD Card initialization failed!");
    setLEDMode(2);
    sdCardPresent = false;
    while (1) {
      handleLED();
      delay(100);
    }
  }
  
  loadAvailableLanguages();
  loadAvailableScripts();
  
  if (!loadLanguage(currentLanguage)) {
    Serial.println("Failed to load default language, trying 'us'");
    if (!loadLanguage("us")) {
      Serial.println("Failed to load 'us' language");
      setLEDMode(2);
    }
  }
  
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
  
  keyboard.begin();
  USB.begin();
  delay(1000);
  
  setupAP();
  setupWebServer();
  
  setLED(0, 255, 0);
  Serial.println("ESP32-S3 BadUSB Ready!");
  Serial.print("Connect to WiFi: ");
  Serial.println(ap_ssid);
  Serial.print("Password: ");
  Serial.println(ap_password);
  Serial.println("Open browser and go to: 192.168.4.1");
  
  // Load command history
  loadCommandHistory();
}

void loop() {
  server.handleClient();
  
  handleLED();
  
  if (millis() - lastSDCheck >= SD_CHECK_INTERVAL) {
    lastSDCheck = millis();
    checkSDCard();
  }
  
  // Handle reset button with improved debouncing
  static unsigned long lastButtonPress = 0;
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (millis() - lastButtonPress > 500) { // 500ms debounce
      lastButtonPress = millis();
      if (scriptRunning) {
        stopRequested = true;
        Serial.println("Stop requested via reset button");
        setLEDMode(4); // Warning mode (orange)
        logCommand("SCRIPT_STOPPED", "User requested stop via reset button");
      } else {
        Serial.println("Reset button pressed - no script running");
      }
    }
  }
  
  // Status updates
  if (millis() - lastStatusUpdate >= STATUS_UPDATE_INTERVAL) {
    lastStatusUpdate = millis();
    if (scriptRunning) {
      unsigned long elapsed = (millis() - scriptStartTime) / 1000;
      Serial.println("Script running for " + String(elapsed) + " seconds");
    }
  }
  
  if (bootModeEnabled && WiFi.softAPgetStationNum() > 0 && !scriptRunning) {
    Serial.println("Client connected - executing boot script");
    logCommand("BOOT_SCRIPT", "Executing boot script on client connection");
    executeScript(bootScript);
  }
  
  delay(10);
}

void checkSDCard() {
  bool cardDetected = false;
  
  // Improved SD card detection with multiple attempts
  for (int i = 0; i < 3; i++) {
    File testFile = SD.open("/.sdtest", FILE_WRITE);
    if (testFile) {
      testFile.print("test");
      testFile.close();
      SD.remove("/.sdtest");
      cardDetected = true;
      break;
    }
    delay(10);
  }
  
  if (cardDetected && !sdCardPresent) {
    Serial.println("SD Card inserted");
    sdCardPresent = true;
    if (!scriptRunning) {
      setLEDMode(0);
      setLED(0, 255, 0);
    }
    loadAvailableLanguages();
    loadAvailableScripts();
    logCommand("SD_CARD", "SD card inserted");
  } else if (!cardDetected && sdCardPresent) {
    Serial.println("SD Card removed - ERROR STATE");
    sdCardPresent = false;
    setLEDMode(2);
    logCommand("SD_CARD", "SD card removed - ERROR");
  } else if (!cardDetected && !sdCardPresent) {
    if (!scriptRunning) {
      ledMode = 2;
    }
  } else if (cardDetected && sdCardPresent) {
    if (!scriptRunning && ledMode != 0) {
      setLEDMode(0);
      setLED(0, 255, 0);
    }
  }
}

void handleLED() {
  if (!ledEnabled) {
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show();
    return;
  }
  
  unsigned long currentTime = millis();
  
  if (ledMode == 3) { // Completion blink (orange)
    if (currentTime - lastCompletionBlinkTime >= 200) {
      lastCompletionBlinkTime = currentTime;
      completionBlinkState = !completionBlinkState;
      
      if (completionBlinkState) {
        pixels.setPixelColor(0, pixels.Color(255, 165, 0));
      } else {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      }
      
      pixels.show();
      
      if (!completionBlinkState) {
        completionBlinkCount++;
        if (completionBlinkCount >= 6) {
          ledMode = 0;
          completionBlinkCount = 0;
          setLED(0, 255, 0);
        }
      }
    }
    return;
  }
  
  if (ledMode == 4) { // Warning mode (orange blinking)
    if (currentTime - lastBlinkTime >= 300) {
      lastBlinkTime = currentTime;
      blinkState = !blinkState;
      if (blinkState) {
        pixels.setPixelColor(0, pixels.Color(255, 165, 0));
      } else {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      }
      pixels.show();
    }
    return;
  }
  
  if (blinkingEnabled || ledMode == 1 || ledMode == 2) {
    if (currentTime - lastBlinkTime >= blinkInterval) {
      lastBlinkTime = currentTime;
      blinkState = !blinkState;
      if (blinkState) {
        pixels.setPixelColor(0, pixels.Color(currentR, currentG, currentB));
      } else {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      }
      pixels.show();
    }
  } else {
    pixels.setPixelColor(0, pixels.Color(currentR, currentG, currentB));
    pixels.show();
  }
}

void setLED(int r, int g, int b) {
  if (!ledEnabled) return;
  currentR = r;
  currentG = g;
  currentB = b;
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
  ledMode = 0;
  blinkingEnabled = false;
  completionBlinkCount = 0;
}

void setLEDMode(int mode) {
  ledMode = mode;
  lastBlinkTime = millis();
  blinkState = false;
  completionBlinkCount = 0;
  
  if (mode == 0) {
    currentR = 0;
    currentG = 255;
    currentB = 0;
    blinkInterval = 500;
    blinkingEnabled = false;
  } else if (mode == 1) {
    currentR = 0;
    currentG = 255;
    currentB = 0;
    blinkInterval = 50;
    blinkingEnabled = true;
  } else if (mode == 2) {
    currentR = 255;
    currentG = 0;
    currentB = 0;
    blinkInterval = 500;
    blinkingEnabled = true;
  } else if (mode == 3) {
    lastCompletionBlinkTime = millis();
    completionBlinkState = true;
    completionBlinkCount = 0;
  } else if (mode == 4) {
    currentR = 255;
    currentG = 165;
    currentB = 0;
    blinkInterval = 300;
    blinkingEnabled = true;
  }
}

void showCompletionBlink() {
  setLEDMode(3);
}

void showWarningBlink() {
  setLEDMode(4);
}

bool initSDCard() {
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  
  // Improved SD card initialization with retry logic
  for (int i = 0; i < 3; i++) {
    if (SD.begin(SD_CS_PIN)) {
      break;
    }
    delay(100);
  }
  
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("Card Mount Failed");
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return false;
  }
  
  // Create necessary directories with error checking
  if (!SD.exists("/languages")) {
    if (!SD.mkdir("/languages")) {
      Serial.println("Failed to create languages directory");
      return false;
    }
  }
  if (!SD.exists("/scripts")) {
    if (!SD.mkdir("/scripts")) {
      Serial.println("Failed to create scripts directory");
      return false;
    }
  }
  if (!SD.exists("/logs")) {
    if (!SD.mkdir("/logs")) {
      Serial.println("Failed to create logs directory");
    }
  }
  if (!SD.exists("/uploads")) {
    if (!SD.mkdir("/uploads")) {
      Serial.println("Failed to create uploads directory");
    }
  }
  
  Serial.println("SD Card initialized successfully");
  return true;
}

void setupAP() {
  WiFi.mode(WIFI_AP);
  
  // Improved AP setup with fallback
  if (!WiFi.softAP(ap_ssid.c_str(), ap_password.c_str())) {
    Serial.println("Failed to setup AP, trying without password");
    WiFi.softAP(ap_ssid.c_str());
  }
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
}

void loadAvailableLanguages() {
  if (!sdCardPresent) return;
  
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
  if (!sdCardPresent) return;
  
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
  if (!sdCardPresent) return false;
  
  String filePath = "/languages/" + language + ".json";
  File file = SD.open(filePath);
  
  if (!file) {
    Serial.println("Failed to open language file: " + filePath);
    lastError = "Language file not found: " + language;
    errorCount++;
    return false;
  }
  
  String jsonString = file.readString();
  file.close();
  
  DynamicJsonDocument doc(16384); // Increased buffer size for larger keymaps
  DeserializationError error = deserializeJson(doc, jsonString);
  
  if (error) {
    Serial.println("Failed to parse JSON: " + String(error.c_str()));
    lastError = "JSON parse error: " + String(error.c_str());
    errorCount++;
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
  if (!sdCardPresent) return "";
  
  String filePath = "/scripts/" + filename;
  File file = SD.open(filePath);
  
  if (!file) {
    Serial.println("Failed to open script file: " + filePath);
    lastError = "Script file not found: " + filename;
    errorCount++;
    return "";
  }
  
  String scriptContent = file.readString();
  file.close();
  
  Serial.println("Loaded script: " + filename);
  return scriptContent;
}

bool saveScript(String filename, String content) {
  if (!sdCardPresent) return false;
  
  if (!filename.endsWith(".txt")) {
    filename += ".txt";
  }
  
  String filePath = "/scripts/" + filename;
  
  if (SD.exists(filePath)) {
    SD.remove(filePath);
  }
  
  File file = SD.open(filePath, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to create script file: " + filePath);
    lastError = "Failed to save script: " + filename;
    errorCount++;
    return false;
  }
  
  size_t bytesWritten = file.print(content);
  file.close();
  
  if (bytesWritten > 0) {
    Serial.println("Script saved: " + filename + " (" + String(bytesWritten) + " bytes)");
    loadAvailableScripts();
    return true;
  } else {
    Serial.println("Failed to write script: " + filename);
    lastError = "Failed to write script: " + filename;
    errorCount++;
    return false;
  }
}

bool deleteScript(String filename) {
  if (!sdCardPresent) return false;
  
  String filePath = "/scripts/" + filename;
  
  if (SD.exists(filePath)) {
    if (SD.remove(filePath)) {
      Serial.println("Script deleted: " + filename);
      loadAvailableScripts();
      return true;
    }
  }
  
  Serial.println("Failed to delete script: " + filename);
  lastError = "Failed to delete script: " + filename;
  errorCount++;
  return false;
}

void openLogFile() {
  if (!sdCardPresent || !loggingEnabled) return;
  
  if (logFileOpen) {
    logFile.close();
  }
  
  // Use logs directory for better organization
  String logPath = "/logs/log.txt";
  logFile = SD.open(logPath, FILE_APPEND);
  if (logFile) {
    logFileOpen = true;
    logFile.println("=== Log Session Started ===");
    logFile.println("Timestamp: " + String(millis()));
    logFile.println("Device: ESP32-S3 BadUSB");
    logFile.flush();
  }
}

void closeLogFile() {
  if (logFileOpen) {
    logFile.println("=== Log Session Ended ===");
    logFile.close();
    logFileOpen = false;
  }
}

void logCommand(String type, String command) {
  if (!sdCardPresent || !loggingEnabled || !logFileOpen) return;
  
  String timestamp = String(millis());
  logFile.println("[" + timestamp + "] [" + type + "] " + command);
  logFile.flush();
}

void loadCommandHistory() {
  if (!sdCardPresent) return;
  
  String historyPath = "/logs/history.txt";
  if (SD.exists(historyPath)) {
    File file = SD.open(historyPath);
    if (file) {
      commandHistory.clear();
      while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
          commandHistory.push_back(line);
        }
      }
      file.close();
      
      // Keep only the most recent commands
      if (commandHistory.size() > MAX_HISTORY_SIZE) {
        commandHistory.erase(commandHistory.begin(), commandHistory.begin() + (commandHistory.size() - MAX_HISTORY_SIZE));
      }
    }
  }
}

void saveCommandHistory() {
  if (!sdCardPresent) return;
  
  String historyPath = "/logs/history.txt";
  if (SD.exists(historyPath)) {
    SD.remove(historyPath);
  }
  
  File file = SD.open(historyPath, FILE_WRITE);
  if (file) {
    for (String cmd : commandHistory) {
      file.println(cmd);
    }
    file.close();
  }
}

void addToHistory(String command) {
  commandHistory.push_back(command);
  
  // Limit history size
  if (commandHistory.size() > MAX_HISTORY_SIZE) {
    commandHistory.erase(commandHistory.begin());
  }
  
  saveCommandHistory();
}

KeyCode parseKeyCode(String keyCodeStr) {
  KeyCode result = {0, 0};
  
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
  if (stopRequested) return;
  
  if (currentKeymap.find(key) != currentKeymap.end()) {
    KeyCode kc = parseKeyCode(currentKeymap[key]);
    
    if (kc.key == 0 && kc.modifier > 0) {
      if (kc.modifier & 0x01) { keyboard.press(KEY_LEFT_CTRL); delay(5); keyboard.release(KEY_LEFT_CTRL); }
      if (kc.modifier & 0x02) { keyboard.press(KEY_LEFT_SHIFT); delay(5); keyboard.release(KEY_LEFT_SHIFT); }
      if (kc.modifier & 0x04) { keyboard.press(KEY_LEFT_ALT); delay(5); keyboard.release(KEY_LEFT_ALT); }
      if (kc.modifier & 0x08) { keyboard.press(KEY_LEFT_GUI); delay(5); keyboard.release(KEY_LEFT_GUI); }
      if (kc.modifier & 0x10) { keyboard.press(KEY_RIGHT_CTRL); delay(5); keyboard.release(KEY_RIGHT_CTRL); }
      if (kc.modifier & 0x20) { keyboard.press(KEY_RIGHT_SHIFT); delay(5); keyboard.release(KEY_RIGHT_SHIFT); }
      if (kc.modifier & 0x04) { keyboard.press(KEY_RIGHT_ALT); delay(5); keyboard.release(KEY_RIGHT_ALT); }
      if (kc.modifier & 0x80) { keyboard.press(KEY_RIGHT_GUI); delay(5); keyboard.release(KEY_RIGHT_GUI); }
    } else {
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
      
      if (kc.key > 0) {
        keyboard.pressRaw(kc.key);
      }
      
      delay(5);
      keyboard.releaseAll();
    }
  } else {
    Serial.println("Key not found in keymap: " + key);
    lastError = "Key not found: " + key;
    errorCount++;
  }
}

void fastPressKeyCombination(std::vector<String> keys) {
  if (stopRequested) return;
  
  std::vector<KeyCode> keyCodes;
  uint8_t combinedModifier = 0;
  uint8_t mainKey = 0;
  
  for (String key : keys) {
    if (currentKeymap.find(key) != currentKeymap.end()) {
      KeyCode kc = parseKeyCode(currentKeymap[key]);
      keyCodes.push_back(kc);
      combinedModifier |= kc.modifier;
      if (kc.key > 0 && mainKey == 0) {
        mainKey = kc.key;
      }
    } else {
      Serial.println("Key not found in keymap: " + key);
      lastError = "Key not found: " + key;
      errorCount++;
    }
  }
  
  if (combinedModifier > 0) {
    if (combinedModifier & 0x01) keyboard.press(KEY_LEFT_CTRL);
    if (combinedModifier & 0x02) keyboard.press(KEY_LEFT_SHIFT);
    if (combinedModifier & 0x04) keyboard.press(KEY_LEFT_ALT);
    if (combinedModifier & 0x08) keyboard.press(KEY_LEFT_GUI);
    if (combinedModifier & 0x10) keyboard.press(KEY_RIGHT_CTRL);
    if (combinedModifier & 0x20) keyboard.press(KEY_RIGHT_SHIFT);
    if (combinedModifier & 0x40) keyboard.press(KEY_RIGHT_ALT);
    if (combinedModifier & 0x80) keyboard.press(KEY_RIGHT_GUI);
  }
  
  if (mainKey > 0) {
    keyboard.pressRaw(mainKey);
  }
  
  delay(5);
  keyboard.releaseAll();
}

String processVariables(String text) {
  String result = text;
  
  for (auto& pair : variables) {
    result.replace(pair.first, pair.second);
  }
  
  return result;
}

void fastTypeString(String text) {
  if (stopRequested) return;
  
  String processedText = processVariables(text);
  
  for (int i = 0; i < processedText.length(); i++) {
    if (stopRequested) break;
    
    String ch = String(processedText.charAt(i));
    
    if (currentKeymap.find(ch) != currentKeymap.end()) {
      KeyCode kc = parseKeyCode(currentKeymap[ch]);
      
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
      
      if (kc.key > 0) {
        keyboard.pressRaw(kc.key);
      }
      
      delay(2);
      keyboard.releaseAll();
      
      if (delayBetweenKeys > 0) {
        delay(delayBetweenKeys);
      } else {
        delay(2);
      }
    } else {
      Serial.println("Character not found in keymap: " + ch);
      lastError = "Character not found: " + ch;
      errorCount++;
    }
  }
}

void scanWiFi() {
  Serial.println("Scanning for WiFi networks...");
  Serial.println("Scan time: " + String(wifiScanTime) + "ms");
  
  WiFi.scanNetworks(true, false, false, wifiScanTime);
  
  int n = WiFi.scanComplete();
  while (n == WIFI_SCAN_RUNNING && !stopRequested) {
    delay(100);
    n = WiFi.scanComplete();
  }
  
  if (stopRequested) {
    WiFi.scanDelete();
    return;
  }
  
  availableSSIDs.clear();
  
  if (n == WIFI_SCAN_FAILED) {
    Serial.println("WiFi scan failed");
    lastError = "WiFi scan failed";
    errorCount++;
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
  scriptStartTime = millis();
  setLEDMode(1);
  
  if (loggingEnabled) {
    openLogFile();
    logCommand("SCRIPT_START", "Script execution started");
  }
  
  addToHistory("Script executed at " + String(millis()));
  totalScriptsExecuted++;
  
  std::vector<String> lines;
  int startIndex = 0;
  int endIndex = script.indexOf('\n');
  
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
  
  int currentLine = 0;
  int totalLines = lines.size();
  
  for (int i = 0; i < lines.size() && !stopRequested; i++) {
    currentLine = i + 1;
    String line = lines[i];
    line.trim();
    
    if (line.length() == 0 || line.startsWith("REM") || line.startsWith("//")) {
      continue;
    }
    
    if (skipConditionalBlock) {
      if (line.startsWith("IF_PRESENT SSID=\"") || line.startsWith("IF_NOTPRESENT SSID=\"")) {
        int quoteStart = line.indexOf('"') + 1;
        int quoteEnd = line.indexOf('"', quoteStart);
        if (quoteEnd > quoteStart) {
          continue;
        }
      } else if (line.startsWith("ENDIF") || line.startsWith("END_IF")) {
        skipConditionalBlock = false;
        skipUntilSSID = "";
        continue;
      }
      continue;
    }
    
    Serial.println("Executing [" + String(currentLine) + "/" + String(totalLines) + "]: " + line);
    if (loggingEnabled) {
      logCommand("EXECUTE", "Line " + String(currentLine) + ": " + line);
    }
    
    executeCommand(line);
    totalCommandsExecuted++;
    
    if (stopRequested) {
      break;
    }
    
    if (defaultDelay > 0) {
      unsigned long delayStart = millis();
      while (millis() - delayStart < defaultDelay && !stopRequested) {
        delay(10);
      }
    }
  }
  
  scriptRunning = false;
  
  if (loggingEnabled) {
    if (stopRequested) {
      logCommand("SCRIPT_STOP", "Script execution stopped by user at line " + String(currentLine));
    } else {
      logCommand("SCRIPT_END", "Script execution completed successfully");
    }
    closeLogFile();
  }
  
  if (stopRequested) {
    stopRequested = false;
    setLEDMode(0);
    Serial.println("Script execution stopped at line " + String(currentLine));
  } else {
    unsigned long executionTime = (millis() - scriptStartTime) / 1000;
    Serial.println("Script execution completed in " + String(executionTime) + " seconds");
    showCompletionBlink();
  }
  
  skipConditionalBlock = false;
  skipUntilSSID = "";
}

void executeCommand(String line) {
  if (stopRequested) return;
  
  lastCommand = line;
  addToHistory(line);
  
  if (line.startsWith("VAR ")) {
    String varLine = line.substring(4);
    int equalIndex = varLine.indexOf('=');
    if (equalIndex > 0) {
      String varName = varLine.substring(0, equalIndex);
      String varValue = varLine.substring(equalIndex + 1);
      varName.trim();
      varValue.trim();
      
      varValue = processVariables(varValue);
      
      variables[varName] = varValue;
      
      Serial.println("Variable set: " + varName + " = " + varValue);
    }
    return;
  }
  
  if (line.startsWith("DEFAULTDELAY ") || line.startsWith("DEFAULT_DELAY ")) {
    int spaceIndex = line.indexOf(' ');
    defaultDelay = line.substring(spaceIndex + 1).toInt();
    Serial.println("Default delay set to: " + String(defaultDelay) + "ms");
  }
  else if (line.startsWith("DELAY_BETWEEN_KEYS ")) {
    int spaceIndex = line.indexOf(' ');
    delayBetweenKeys = line.substring(spaceIndex + 1).toInt();
    Serial.println("Delay between keys set to: " + String(delayBetweenKeys) + "ms");
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
    if (!loadLanguage(locale)) {
      Serial.println("Failed to load language: " + locale);
      setLEDMode(2);
      delay(2000);
      setLEDMode(1);
    }
  }
  else if (line == "BLINKING ON") {
    blinkingEnabled = true;
    Serial.println("Blinking enabled");
  }
  else if (line == "BLINKING OFF") {
    blinkingEnabled = false;
    Serial.println("Blinking disabled");
  }
  else if (line == "RGB ON") {
    ledEnabled = true;
    preferences.putBool("led_enabled", true);
    setLED(currentR, currentG, currentB);
    Serial.println("RGB LED enabled");
  }
  else if (line == "RGB OFF") {
    ledEnabled = false;
    preferences.putBool("led_enabled", false);
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show();
    Serial.println("RGB LED disabled");
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
      
      if (modifier > 0) {
        if (modifier & 0x01) keyboard.press(KEY_LEFT_CTRL);
        if (modifier & 0x02) keyboard.press(KEY_LEFT_SHIFT);
        if (modifier & 0x04) keyboard.press(KEY_LEFT_ALT);
        if (modifier & 0x08) keyboard.press(KEY_LEFT_GUI);
        if (modifier & 0x10) keyboard.press(KEY_RIGHT_CTRL);
        if (modifier & 0x20) keyboard.press(KEY_RIGHT_SHIFT);
        if (modifier & 0x40) keyboard.press(KEY_RIGHT_ALT);
        if (modifier & 0x80) keyboard.press(KEY_RIGHT_GUI);
      }
      
      if (keyCode > 0) {
        keyboard.pressRaw(keyCode);
      }
      
      delay(5);
      keyboard.releaseAll();
    }
  }
  else if (line.startsWith("IF_PRESENT SSID=\"")) {
    int quoteStart = line.indexOf('"') + 1;
    int quoteEnd = line.indexOf('"', quoteStart);
    if (quoteEnd > quoteStart) {
      String ssid = line.substring(quoteStart, quoteEnd);
      
      int scanTimeIndex = line.indexOf("SCAN_TIME=");
      int scanTime = wifiScanTime;
      
      if (scanTimeIndex > quoteEnd) {
        String scanTimeStr = line.substring(scanTimeIndex + 10);
        scanTimeStr.trim();
        scanTime = scanTimeStr.toInt();
        if (scanTime < 1000) scanTime = 1000;
        if (scanTime > 30000) scanTime = 30000;
        Serial.println("Using custom scan time: " + String(scanTime) + "ms");
      }
      
      int originalScanTime = wifiScanTime;
      wifiScanTime = scanTime;
      
      scanWiFi();
      
      wifiScanTime = originalScanTime;
      
      if (!isSSIDPresent(ssid)) {
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
      
      int scanTimeIndex = line.indexOf("SCAN_TIME=");
      int scanTime = wifiScanTime;
      
      if (scanTimeIndex > quoteEnd) {
        String scanTimeStr = line.substring(scanTimeIndex + 10);
        scanTimeStr.trim();
        scanTime = scanTimeStr.toInt();
        if (scanTime < 1000) scanTime = 1000;
        if (scanTime > 30000) scanTime = 30000;
        Serial.println("Using custom scan time: " + String(scanTime) + "ms");
      }
      
      int originalScanTime = wifiScanTime;
      wifiScanTime = scanTime;
      
      scanWiFi();
      
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
    String password = line.substring(13);
    if (password == ap_password) {
      selfDestruct();
    }
  }
  else if (line.startsWith("REPEAT ")) {
    int spaceIndex = line.indexOf(' ');
    int repeatCount = line.substring(7, spaceIndex).toInt();
    String repeatCommand = line.substring(spaceIndex + 1);
    
    for (int i = 0; i < repeatCount && !stopRequested; i++) {
      Serial.println("Repeat " + String(i+1) + "/" + String(repeatCount) + ": " + repeatCommand);
      executeCommand(repeatCommand);
      if (stopRequested) break;
    }
  }
  else if (line.startsWith("WAIT_FOR_SD")) {
    Serial.println("Waiting for SD card to be present...");
    unsigned long waitStart = millis();
    while (!sdCardPresent && (millis() - waitStart < 30000) && !stopRequested) {
      delay(500);
      checkSDCard();
    }
    if (!sdCardPresent) {
      Serial.println("Timeout waiting for SD card");
    } else {
      Serial.println("SD card detected, continuing...");
    }
  }
  else {
    handleKeyInput(line);
  }
}

void selfDestruct() {
  Serial.println("SELF DESTRUCT INITIATED!");
  setLEDMode(2);
  
  if (sdCardPresent) {
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
    
    // Clear logs and uploads
    if (SD.exists("/logs")) {
      root = SD.open("/logs");
      if (root) {
        File file = root.openNextFile();
        while (file) {
          if (!file.isDirectory()) {
            String fileName = file.name();
            SD.remove("/logs/" + fileName);
            Serial.println("Deleted: " + fileName);
          }
          file = root.openNextFile();
        }
        root.close();
      }
    }
    
    if (SD.exists("/uploads")) {
      root = SD.open("/uploads");
      if (root) {
        File file = root.openNextFile();
        while (file) {
          if (!file.isDirectory()) {
            String fileName = file.name();
            SD.remove("/uploads/" + fileName);
            Serial.println("Deleted: " + fileName);
          }
          file = root.openNextFile();
        }
        root.close();
      }
    }
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

void saveSettings() {
  preferences.putString("ap_ssid", ap_ssid);
  preferences.putString("ap_password", ap_password);
  preferences.putString("language", currentLanguage);
  preferences.putString("boot_script", currentBootScriptFile);
  preferences.putInt("wifi_scan_time", wifiScanTime);
  preferences.putBool("led_enabled", ledEnabled);
  preferences.putBool("logging_enabled", loggingEnabled);
  Serial.println("Settings saved");
}

// File upload handler
void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    Serial.println("File upload start: " + filename);
    
    // Check if file exists and delete it
    if (SD.exists(filename)) {
      SD.remove(filename);
    }
    
    // Open file for writing
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to create file: " + filename);
      return;
    }
    file.close();
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    
    File file = SD.open(filename, FILE_APPEND);
    if (file) {
      file.write(upload.buf, upload.currentSize);
      file.close();
      Serial.println("File write: " + filename + " size: " + String(upload.currentSize));
    } else {
      Serial.println("Failed to write to file: " + filename);
    }
    
  } else if (upload.status == UPLOAD_FILE_END) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    
    Serial.println("File upload complete: " + filename + " size: " + String(upload.totalSize));
    
    // Refresh file lists
    loadAvailableScripts();
    loadAvailableLanguages();
    
    server.send(200, "text/plain", "File uploaded successfully: " + filename);
  }
}

// File download handler
void handleFileDownload() {
  if (server.hasArg("file")) {
    String filename = server.arg("file");
    String filepath = filename;
    
    if (!filepath.startsWith("/")) {
      filepath = "/" + filepath;
    }
    
    if (SD.exists(filepath)) {
      File file = SD.open(filepath);
      if (file) {
        server.sendHeader("Content-Type", "text/plain");
        server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
        server.streamFile(file, "application/octet-stream");
        file.close();
        Serial.println("File downloaded: " + filename);
      } else {
        server.send(500, "text/plain", "Failed to open file");
      }
    } else {
      server.send(404, "text/plain", "File not found: " + filename);
    }
  } else {
    server.send(400, "text/plain", "No file specified");
  }
}

// List all files on SD card
void handleListFiles() {
  String path = "/";
  if (server.hasArg("path")) {
    path = server.arg("path");
  }
  
  File root = SD.open(path);
  if (!root) {
    server.send(500, "text/plain", "Failed to open directory");
    return;
  }
  
  String json = "[";
  bool first = true;
  
  File file = root.openNextFile();
  while (file) {
    if (!first) {
      json += ",";
    }
    first = false;
    
    json += "{";
    json += "\"name\":\"" + String(file.name()) + "\",";
    json += "\"size\":" + String(file.size()) + ",";
    json += "\"isDirectory\":" + String(file.isDirectory() ? "true" : "false");
    json += "}";
    
    file = root.openNextFile();
  }
  json += "]";
  
  root.close();
  server.send(200, "application/json", json);
}

// Delete file handler
void handleDeleteFile() {
  if (server.hasArg("file")) {
    String filename = server.arg("file");
    
    if (SD.exists(filename)) {
      if (SD.remove(filename)) {
        server.send(200, "text/plain", "File deleted: " + filename);
        Serial.println("File deleted: " + filename);
        
        // Refresh file lists
        loadAvailableScripts();
        loadAvailableLanguages();
      } else {
        server.send(500, "text/plain", "Failed to delete file");
      }
    } else {
      server.send(404, "text/plain", "File not found");
    }
  } else {
    server.send(400, "text/plain", "No file specified");
  }
}

void setupWebServer() {
  server.enableCORS(true);
  
  // File upload endpoint
  server.on("/api/upload", HTTP_POST, []() {
    server.send(200, "text/plain", "Upload complete");
  }, handleFileUpload);
  
  // File download endpoint
  server.on("/api/download", handleFileDownload);
  
  // List files endpoint
  server.on("/api/list-files", handleListFiles);
  
  // Delete file endpoint
  server.on("/api/delete-file", HTTP_DELETE, handleDeleteFile);
  
  server.on("/", []() {
    String html = "<!DOCTYPE html><html><head><title>ESP32-S3 BadUSB</title>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body{font-family:Arial,sans-serif;margin:20px;background:#1a1a1a;color:#fff}";
    html += ".container{max-width:1200px;margin:0 auto;background:#2d2d2d;padding:20px;border-radius:10px}";
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
    html += ".led-control{display:flex;align-items:center;gap:10px;margin:10px 0}";
    html += ".led-status{padding:8px 12px;border-radius:5px;font-weight:bold}";
    html += ".led-on{background:#4CAF50;color:white}";
    html += ".led-off{background:#666;color:#ccc}";
    html += ".switch{position:relative;display:inline-block;width:60px;height:34px}";
    html += ".switch input{opacity:0;width:0;height:0}";
    html += ".slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#ccc;transition:.4s;border-radius:34px}";
    html += ".slider:before{position:absolute;content:'';height:26px;width:26px;left:4px;bottom:4px;background-color:white;transition:.4s;border-radius:50%}";
    html += "input:checked + .slider{background-color:#4CAF50}";
    html += "input:checked + .slider:before{transform:translateX(26px)}";
    html += ".switch-container{display:flex;align-items:center;gap:10px;margin:10px 0}";
    html += ".stats-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:10px;margin:10px 0}";
    html += ".stat-card{background:#2a2a2a;padding:15px;border-radius:5px;text-align:center}";
    html += ".stat-value{font-size:24px;font-weight:bold;color:#4CAF50}";
    html += ".stat-label{font-size:12px;color:#ccc}";
    html += ".history-list{max-height:300px;overflow-y:auto;border:1px solid #555;padding:10px;background:#1a1a1a;border-radius:5px}";
    html += ".history-item{padding:5px;border-bottom:1px solid #333;font-family:monospace;font-size:12px}";
    html += ".progress-bar{width:100%;height:20px;background:#1a1a1a;border-radius:10px;overflow:hidden;margin:10px 0}";
    html += ".progress-fill{height:100%;background:#4CAF50;transition:width 0.3s}";
    html += ".command-help{background:#2a2a2a;padding:10px;border-radius:5px;margin:5px 0;cursor:pointer}";
    html += ".command-help:hover{background:#3a3a3a}";
    html += ".command-details{display:none;padding:10px;background:#1a1a1a;border-radius:5px;margin-top:5px}";
    html += ".file-manager{background:#2a2a2a;padding:15px;border-radius:5px;margin:10px 0}";
    html += ".upload-area{border:2px dashed #555;padding:20px;text-align:center;border-radius:5px;margin:10px 0}";
    html += ".upload-area:hover{border-color:#4CAF50}";
    html += ".file-browser{max-height:400px;overflow-y:auto;border:1px solid #555;padding:10px;background:#1a1a1a;border-radius:5px}";
    html += ".file-browser-item{padding:8px;border-bottom:1px solid #333;display:flex;justify-content:space-between;align-items:center}";
    html += ".file-browser-item:hover{background:#2a2a2a}";
    html += ".file-size{color:#888;font-size:12px}";
    html += ".file-actions{display:flex;gap:5px}";
    html += ".directory{color:#4CAF50;font-weight:bold}";
    html += ".file{color:#fff}";
    html += "</style></head><body>";
    
    html += "<div class='container'>";
    html += "<h1>ESP32-S3 BadUSB</h1>";
    
    html += "<div class='tab'>";
    html += "<button class='tablinks active' onclick=\"openTab(event, 'ScriptTab')\">Script</button>";
    html += "<button class='tablinks' onclick=\"openTab(event, 'FilesTab')\">Files</button>";
    html += "<button class='tablinks' onclick=\"openTab(event, 'FileManagerTab')\">File Manager</button>";
    html += "<button class='tablinks' onclick=\"openTab(event, 'BootTab')\">Boot</button>";
    html += "<button class='tablinks' onclick=\"openTab(event, 'SettingsTab')\">Settings</button>";
    html += "<button class='tablinks' onclick=\"openTab(event, 'StatsTab')\">Statistics</button>";
    html += "<button class='tablinks' onclick=\"openTab(event, 'HelpTab')\">Help</button>";
    html += "</div>";
    
    html += "<div id='ScriptTab' class='tabcontent' style='display:block'>";
    html += "<h3>Script Execution</h3>";
    html += "<textarea id='scriptArea' placeholder='Enter your BadUSB script here...&#10;&#10;Example:&#10;DELAY 1000&#10;GUI r&#10;DELAY 500&#10;STRING notepad&#10;ENTER&#10;DELAY 1000&#10;STRING Hello World!'></textarea>";
    html += "<br>";
    html += "<div class='control-panel'>";
    html += "<button class='control-btn' onclick='executeScript()'>Execute Script</button>";
    html += "<button class='control-btn' onclick='stopScript()'>Stop Script</button>";
    html += "<button class='control-btn' onclick='clearScript()'>Clear</button>";
    html += "<button class='control-btn' onclick='saveScriptPrompt()'>Save Script</button>";
    html += "<button class='control-btn' onclick='loadFromHistory()'>Load from History</button>";
    html += "<button class='control-btn' onclick='validateScript()'>Validate Script</button>";
    html += "</div>";
    html += "<div class='progress-bar' id='progressBar' style='display:none'>";
    html += "<div class='progress-fill' id='progressFill' style='width:0%'></div>";
    html += "</div>";
    html += "</div>";
    
    html += "<div id='FilesTab' class='tabcontent'>";
    html += "<h3>Script Files</h3>";
    html += "<div class='file-list' id='fileList'>Loading...</div>";
    html += "<br>";
    html += "<div class='setting-row'>";
    html += "<input type='text' id='newFilename' placeholder='New script name (without .txt)' style='flex:1'>";
    html += "<button onclick='saveScript()'>Save Current Script</button>";
    html += "<button onclick='refreshFiles()'>Refresh List</button>";
    html += "<button onclick='uploadScriptFile()'>Upload Script</button>";
    html += "</div>";
    html += "</div>";
    
    html += "<div id='FileManagerTab' class='tabcontent'>";
    html += "<h3>File Manager - Copy Files to SD Card</h3>";
    
    html += "<div class='file-manager'>";
    html += "<h4>Upload Files to SD Card</h4>";
    html += "<div class='upload-area' id='uploadArea'>";
    html += "<p>Drag & drop files here or click to browse</p>";
    html += "<input type='file' id='fileInput' multiple style='display:none'>";
    html += "<button onclick='document.getElementById(\"fileInput\").click()'>Select Files</button>";
    html += "</div>";
    html += "<div id='uploadProgress' style='display:none'>";
    html += "<p>Uploading: <span id='uploadFileName'></span></p>";
    html += "<div class='progress-bar'>";
    html += "<div class='progress-fill' id='uploadProgressFill' style='width:0%'></div>";
    html += "</div>";
    html += "</div>";
    html += "</div>";
    
    html += "<div class='file-manager'>";
    html += "<h4>SD Card File Browser</h4>";
    html += "<div class='setting-row'>";
    html += "<input type='text' id='currentPath' value='/' readonly style='flex:1'>";
    html += "<button onclick='refreshFileBrowser()'>Refresh</button>";
    html += "<button onclick='goToParent()'>Up</button>";
    html += "</div>";
    html += "<div class='file-browser' id='fileBrowser'>Loading...</div>";
    html += "</div>";
    html += "</div>";
    
    html += "<div id='BootTab' class='tabcontent'>";
    html += "<h3>Boot Script Configuration</h3>";
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
    
    html += "<div id='SettingsTab' class='tabcontent'>";
    html += "<h3>Settings</h3>";
    
    html += "<div class='setting-group'>";
    html += "<h4>LED Control</h4>";
    html += "<div class='led-control'>";
    html += "<span class='setting-label'>RGB LED Status:</span>";
    html += "<span id='ledStatus' class='led-status " + String(ledEnabled ? "led-on" : "led-off") + "'>" + String(ledEnabled ? "ON" : "OFF") + "</span>";
    html += "<button id='ledToggleBtn' onclick='toggleLED()'>" + String(ledEnabled ? "Turn Off" : "Turn On") + "</button>";
    html += "</div>";
    html += "<p style='font-size:12px;color:#ccc'>LED States: Solid Green = Idle, Fast Blinking Green = Running, Blinking Red = Error/SD Card Removed, Orange Blink = Completion</p>";
    html += "<p style='font-size:12px;color:#ccc'>Use BLINKING ON/OFF and RGB ON/OFF commands in scripts to control LED</p>";
    html += "</div>";
    
    html += "<div class='setting-group'>";
    html += "<h4>Logging</h4>";
    html += "<div class='switch-container'>";
    html += "<span class='setting-label'>Log Files:</span>";
    html += "<label class='switch'>";
    html += "<input type='checkbox' id='loggingToggle' " + String(loggingEnabled ? "checked" : "") + " onchange='toggleLogging()'>";
    html += "<span class='slider'></span>";
    html += "</label>";
    html += "<span id='loggingStatus'>" + String(loggingEnabled ? "ON" : "OFF") + "</span>";
    html += "</div>";
    html += "<p style='font-size:12px;color:#ccc'>When enabled, all script commands will be logged to /logs/log.txt</p>";
    html += "</div>";
    
    html += "<div class='setting-group'>";
    html += "<h4>Language Configuration</h4>";
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
    html += "<button onclick='saveLanguageSettings()'>Save Language Settings</button>";
    html += "</div>";
    
    html += "<div class='setting-group'>";
    html += "<h4>WiFi Configuration</h4>";
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
    html += "<button onclick='saveWiFiSettings()'>Save WiFi Settings</button>";
    html += "<p style='font-size:12px;color:#ccc'>WiFi changes require device reboot to take effect</p>";
    html += "</div>";
    
    html += "<div class='setting-group'>";
    html += "<h4>Quick Actions</h4>";
    html += "<div class='control-panel'>";
    html += "<button class='control-btn' onclick='quickAction(\"GUI r\")'>Win+R</button>";
    html += "<button class='control-btn' onclick='quickAction(\"CTRL ALT DEL\")'>Ctrl+Alt+Del</button>";
    html += "<button class='control-btn' onclick='quickAction(\"ALT F4\")'>Alt+F4</button>";
    html += "<button class='control-btn' onclick='quickAction(\"CTRL SHIFT ESC\")'>Task Manager</button>";
    html += "<button class='control-btn' onclick='typeText()'>Type Text</button>";
    html += "<button class='control-btn' onclick='quickAction(\"REPEAT 5 STRING Repeated text\")'>Repeat Example</button>";
    html += "</div>";
    html += "</div>";
    
    html += "<div class='setting-group'>";
    html += "<h4>Danger Zone</h4>";
    html += "<button class='danger' onclick='selfDestructPrompt()'>Self Destruct</button>";
    html += "<button class='danger' onclick='clearErrorLog()'>Clear Error Log</button>";
    html += "<button class='danger' onclick='factoryReset()'>Factory Reset</button>";
    html += "<p style='font-size:12px;color:#ccc'>Self Destruct: Deletes all scripts and reboots | Clear Errors: Resets error counter | Factory Reset: Restores all defaults</p>";
    html += "</div>";
    html += "</div>";
    
    html += "<div id='StatsTab' class='tabcontent'>";
    html += "<h3>Device Statistics & History</h3>";
    
    html += "<div class='stats-grid'>";
    html += "<div class='stat-card'>";
    html += "<div class='stat-value' id='errorCount'>0</div>";
    html += "<div class='stat-label'>Total Errors</div>";
    html += "</div>";
    html += "<div class='stat-card'>";
    html += "<div class='stat-value' id='scriptCount'>0</div>";
    html += "<div class='stat-label'>Available Scripts</div>";
    html += "</div>";
    html += "<div class='stat-card'>";
    html += "<div class='stat-value' id='languageCount'>0</div>";
    html += "<div class='stat-label'>Languages</div>";
    html += "</div>";
    html += "<div class='stat-card'>";
    html += "<div class='stat-value' id='clientCount'>0</div>";
    html += "<div class='stat-label'>Connected Clients</div>";
    html += "</div>";
    html += "<div class='stat-card'>";
    html += "<div class='stat-value' id='totalScripts'>0</div>";
    html += "<div class='stat-label'>Scripts Executed</div>";
    html += "</div>";
    html += "<div class='stat-card'>";
    html += "<div class='stat-value' id='totalCommands'>0</div>";
    html += "<div class='stat-label'>Commands Executed</div>";
    html += "</div>";
    html += "</div>";
    
    html += "<div class='setting-group'>";
    html += "<h4>Command History</h4>";
    html += "<div class='history-list' id='historyList'>Loading...</div>";
    html += "<button onclick='clearHistory()'>Clear History</button>";
    html += "<button onclick='exportHistory()'>Export History</button>";
    html += "</div>";
    
    html += "<div class='setting-group'>";
    html += "<h4>System Information</h4>";
    html += "<div class='setting-row'>";
    html += "<span class='setting-label'>Last Error:</span>";
    html += "<span id='lastError'>None</span>";
    html += "</div>";
    html += "<div class='setting-row'>";
    html += "<span class='setting-label'>SD Card Status:</span>";
    html += "<span id='sdStatus'>" + String(sdCardPresent ? "OK" : "REMOVED") + "</span>";
    html += "</div>";
    html += "<div class='setting-row'>";
    html += "<span class='setting-label'>Uptime:</span>";
    html += "<span id='uptime'>0s</span>";
    html += "</div>";
    html += "<div class='setting-row'>";
    html += "<span class='setting-label'>Free Memory:</span>";
    html += "<span id='freeMemory'>0</span>";
    html += "</div>";
    html += "</div>";
    html += "</div>";
    
    html += "<div id='HelpTab' class='tabcontent'>";
    html += "<h3>Command Reference & Help</h3>";
    
    html += "<div class='command-help' onclick='toggleCommandDetails(\"basic\")'>Basic Commands</div>";
    html += "<div id='basic-details' class='command-details'>";
    html += "<pre>STRING text          - Type text\nDELAY ms            - Delay in milliseconds\nGUI key             - Windows key\nENTER               - Enter key\nTAB                 - Tab key\nESC                 - Escape key\nUP / DOWN / LEFT / RIGHT - Arrow keys</pre>";
    html += "</div>";
    
    html += "<div class='command-help' onclick='toggleCommandDetails(\"modifiers\")'>Modifier Keys</div>";
    html += "<div id='modifiers-details' class='command-details'>";
    html += "<pre>CTRL key            - Control key combination\nSHIFT key           - Shift key combination\nALT key             - Alt key combination\nCTRL ALT key        - Control+Alt combination\nCTRL SHIFT key      - Control+Shift combination\nALT SHIFT key       - Alt+Shift combination\nCTRL ALT SHIFT key  - Control+Alt+Shift combination</pre>";
    html += "</div>";
    
    html += "<div class='command-help' onclick='toggleCommandDetails(\"advanced\")'>Advanced Commands</div>";
    html += "<div id='advanced-details' class='command-details'>";
    html += "<pre>VAR name=value      - Set variable\nDEFAULTDELAY ms     - Set default delay between commands\nDELAY_BETWEEN_KEYS ms - Set delay between keystrokes\nLOCALE language     - Change keyboard layout\nREPEAT n command    - Repeat command n times\nWAIT_FOR_SD         - Wait for SD card to be present\nIF_PRESENT SSID=\"name\" - Conditional execution\nIF_NOTPRESENT SSID=\"name\" - Conditional execution\nENDIF               - End conditional block</pre>";
    html += "</div>";
    
    html += "<div class='command-help' onclick='toggleCommandDetails(\"led\")'>LED Control Commands</div>";
    html += "<div id='led-details' class='command-details'>";
    html += "<pre>LED r g b          - Set LED color (0-255)\nBLINKING ON         - Enable LED blinking\nBLINKING OFF        - Disable LED blinking\nRGB ON              - Enable RGB LED\nRGB OFF             - Disable RGB LED</pre>";
    html += "</div>";
    
    html += "<div class='command-help' onclick='toggleCommandDetails(\"wifi\")'>WiFi Commands</div>";
    html += "<div id='wifi-details' class='command-details'>";
    html += "<pre>WIFI_SCAN_TIME ms   - Set WiFi scan time (1000-30000ms)\nIF_PRESENT SSID=\"name\" SCAN_TIME=ms - Conditional with custom scan\nIF_NOTPRESENT SSID=\"name\" SCAN_TIME=ms - Conditional with custom scan</pre>";
    html += "</div>";
    
    html += "<div class='examples'>";
    html += "<h4>Quick Examples</h4>";
    html += "<button onclick='loadExample(1)'>Load Basic Example</button>";
    html += "<button onclick='loadExample(2)'>Load WiFi Example</button>";
    html += "<button onclick='loadExample(3)'>Load LED Example</button>";
    html += "<button onclick='loadExample(4)'>Load Repeat Example</button>";
    html += "</div>";
    html += "</div>";
    
    html += "<div class='section'>";
    html += "<h3>Device Status</h3>";
    html += "<div id='status' class='status'>Ready</div>";
    html += "</div>";
    
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
    
    html += "<div id='historyModal' class='modal'>";
    html += "<div class='modal-content'>";
    html += "<span class='close' onclick='closeHistoryModal()'>&times;</span>";
    html += "<h3>Command History</h3>";
    html += "<div class='history-list' id='modalHistoryList'></div>";
    html += "<button onclick='closeHistoryModal()'>Close</button>";
    html += "</div>";
    html += "</div>";
    
    html += "</div>";
    
    html += "<script>";
    html += "let pendingFilename = '';";
    html += "let pendingContent = '';";
    html += "let scriptProgress = 0;";
    html += "let progressInterval;";
    html += "let currentBrowserPath = '/';";
    
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
    html += "  if (tabName === 'StatsTab') { updateStats(); }";
    html += "  if (tabName === 'FileManagerTab') { refreshFileBrowser(); }";
    html += "}";
    
    html += "function toggleCommandDetails(id) {";
    html += "  const details = document.getElementById(id + '-details');";
    html += "  if (details.style.display === 'block') {";
    html += "    details.style.display = 'none';";
    html += "  } else {";
    html += "    details.style.display = 'block';";
    html += "  }";
    html += "}";
    
    html += "function executeScript() {";
    html += "  const script = document.getElementById('scriptArea').value;";
    html += "  if (!script.trim()) { alert('Please enter a script'); return; }";
    html += "  document.getElementById('status').innerHTML = 'Executing script...';";
    html += "  document.getElementById('progressBar').style.display = 'block';";
    html += "  scriptProgress = 0;";
    html += "  updateProgress(0);";
    html += "  ";
    html += "  progressInterval = setInterval(() => {";
    html += "    scriptProgress += Math.random() * 10;";
    html += "    if (scriptProgress > 90) scriptProgress = 90;";
    html += "    updateProgress(scriptProgress);";
    html += "  }, 500);";
    html += "  ";
    html += "  fetch('/execute', { method: 'POST', body: script })";
    html += "  .then(response => response.text())";
    html += "  .then(data => { ";
    html += "    clearInterval(progressInterval);";
    html += "    updateProgress(100);";
    html += "    setTimeout(() => {";
    html += "      document.getElementById('progressBar').style.display = 'none';";
    html += "    }, 1000);";
    html += "    document.getElementById('status').innerHTML = 'Script executed';";
    html += "  })";
    html += "  .catch(err => { ";
    html += "    clearInterval(progressInterval);";
    html += "    document.getElementById('progressBar').style.display = 'none';";
    html += "    document.getElementById('status').innerHTML = 'Error: ' + err;";
    html += "  });";
    html += "}";
    
    html += "function updateProgress(percent) {";
    html += "  document.getElementById('progressFill').style.width = percent + '%';";
    html += "}";
    
    html += "function stopScript() {";
    html += "  clearInterval(progressInterval);";
    html += "  document.getElementById('progressBar').style.display = 'none';";
    html += "  fetch('/stop', { method: 'POST' })";
    html += "  .then(response => response.text())";
    html += "  .then(data => { ";
    html += "    document.getElementById('status').innerHTML = data;";
    html += "    setTimeout(() => { document.getElementById('status').innerHTML = 'Script stopped'; }, 1000);";
    html += "  });";
    html += "}";
    
    html += "function validateScript() {";
    html += "  const script = document.getElementById('scriptArea').value;";
    html += "  if (!script.trim()) { alert('Please enter a script'); return; }";
    html += "  fetch('/api/validate-script', {";
    html += "    method: 'POST',";
    html += "    headers: { 'Content-Type': 'application/json' },";
    html += "    body: JSON.stringify({ script: script })";
    html += "  })";
    html += "  .then(response => response.text())";
    html += "  .then(result => {";
    html += "    document.getElementById('status').innerHTML = result;";
    html += "  });";
    html += "}";
    
    html += "function changeLanguage() {";
    html += "  const lang = document.getElementById('languageSelect').value;";
    html += "  fetch('/language?lang=' + lang)";
    html += "  .then(response => response.text())";
    html += "  .then(data => { ";
    html += "    document.getElementById('currentLang').innerHTML = lang;";
    html += "    document.getElementById('status').innerHTML = 'Language changed to: ' + lang;";
    html += "  });";
    html += "}";
    
    html += "function saveLanguageSettings() {";
    html += "  fetch('/api/save-settings', {";
    html += "    method: 'POST',";
    html += "    headers: { 'Content-Type': 'application/json' },";
    html += "    body: JSON.stringify({ type: 'language' })";
    html += "  })";
    html += "  .then(response => response.text())";
    html += "  .then(data => { document.getElementById('status').innerHTML = 'Language settings saved'; });";
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
    html += "    document.getElementById('status').innerHTML = 'WiFi settings saved. Reboot required.';";
    html += "    setTimeout(() => { window.location.reload(); }, 2000);";
    html += "  });";
    html += "}";
    
    html += "function quickAction(action) {";
    html += "  document.getElementById('status').innerHTML = 'Executing: ' + action;";
    html += "  fetch('/execute', { method: 'POST', body: action })";
    html += "  .then(response => response.text())";
    html += "  .then(data => { document.getElementById('status').innerHTML = 'Executed: ' + action; });";
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
    html += "    1: 'REM Basic script example\\nDELAY 1000\\nGUI r\\nDELAY 500\\nSTRING notepad\\nENTER\\nDELAY 1000\\nSTRING Hello World!',";
    html += "    2: 'REM WiFi detection example\\nIF_PRESENT SSID=\\\"MyWiFi\\\"\\nSTRING WiFi Found!\\nENTER\\nELSE\\nSTRING WiFi Not Found!\\nENTER\\nENDIF',";
    html += "    3: 'REM LED control example\\nLED 255 0 0\\nDELAY 1000\\nLED 0 255 0\\nDELAY 1000\\nLED 0 0 255\\nDELAY 1000\\nRGB OFF',";
    html += "    4: 'REM Repeat command example\\nREPEAT 5 STRING Hello World!\\nENTER\\nREPEAT 10 DELAY 100 STRING Test'";
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
    html += "      loadBtn.innerHTML = 'Load';";
    html += "      loadBtn.onclick = () => loadFile(file);";
    html += "      ";
    html += "      const deleteBtn = document.createElement('button');";
    html += "      deleteBtn.innerHTML = 'Delete';";
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
    html += "    document.getElementById('status').innerHTML = 'Boot script loaded to editor';";
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
    html += "    document.getElementById('status').innerHTML = result;";
    html += "  });";
    html += "}";
    
    html += "function loadFile(filename) {";
    html += "  fetch('/api/load?file=' + encodeURIComponent(filename))";
    html += "  .then(response => response.text())";
    html += "  .then(content => {";
    html += "    document.getElementById('scriptArea').value = content;";
    html += "    document.getElementById('status').innerHTML = 'Loaded: ' + filename;";
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
    
    html += "function toggleLED() {";
    html += "  fetch('/api/toggle-led', { method: 'POST' })";
    html += "  .then(response => response.json())";
    html += "  .then(data => {";
    html += "    const ledStatus = document.getElementById('ledStatus');";
    html += "    const ledToggleBtn = document.getElementById('ledToggleBtn');";
    html += "    if (data.ledEnabled) {";
    html += "      ledStatus.textContent = 'ON';";
    html += "      ledStatus.className = 'led-status led-on';";
    html += "      ledToggleBtn.textContent = 'Turn Off';";
    html += "    } else {";
    html += "      ledStatus.textContent = 'OFF';";
    html += "      ledStatus.className = 'led-status led-off';";
    html += "      ledToggleBtn.textContent = 'Turn On';";
    html += "    }";
    html += "    document.getElementById('status').innerHTML = 'LED ' + (data.ledEnabled ? 'enabled' : 'disabled');";
    html += "  });";
    html += "}";
    
    html += "function toggleLogging() {";
    html += "  const isEnabled = document.getElementById('loggingToggle').checked;";
    html += "  fetch('/api/toggle-logging', {";
    html += "    method: 'POST',";
    html += "    headers: { 'Content-Type': 'application/json' },";
    html += "    body: JSON.stringify({ enabled: isEnabled })";
    html += "  })";
    html += "  .then(response => response.text())";
    html += "  .then(result => {";
    html += "    document.getElementById('loggingStatus').textContent = isEnabled ? 'ON' : 'OFF';";
    html += "    document.getElementById('status').innerHTML = 'Logging ' + (isEnabled ? 'enabled' : 'disabled');";
    html += "  });";
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
    
    html += "function clearErrorLog() {";
    html += "  if (confirm('Clear error log and reset error counter?')) {";
    html += "    fetch('/api/clear-errors', { method: 'POST' })";
    html += "    .then(response => response.text())";
    html += "    .then(result => {";
    html += "      document.getElementById('status').innerHTML = result;";
    html += "      updateStats();";
    html += "    });";
    html += "  }";
    html += "}";
    
    html += "function factoryReset() {";
    html += "  if (confirm('Factory reset will restore all defaults. Continue?')) {";
    html += "    fetch('/api/factory-reset', { method: 'POST' })";
    html += "    .then(response => response.text())";
    html += "    .then(result => {";
    html += "      document.getElementById('status').innerHTML = result;";
    html += "      setTimeout(() => { window.location.reload(); }, 2000);";
    html += "    });";
    html += "  }";
    html += "}";
    
    html += "function updateStats() {";
    html += "  fetch('/api/stats')";
    html += "  .then(response => response.json())";
    html += "  .then(data => {";
    html += "    document.getElementById('errorCount').textContent = data.errorCount;";
    html += "    document.getElementById('scriptCount').textContent = data.scriptCount;";
    html += "    document.getElementById('languageCount').textContent = data.languageCount;";
    html += "    document.getElementById('clientCount').textContent = data.clientCount;";
    html += "    document.getElementById('totalScripts').textContent = data.totalScripts;";
    html += "    document.getElementById('totalCommands').textContent = data.totalCommands;";
    html += "    document.getElementById('lastError').textContent = data.lastError || 'None';";
    html += "    document.getElementById('sdStatus').textContent = data.sdCardPresent ? 'OK' : 'REMOVED';";
    html += "    document.getElementById('uptime').textContent = data.uptime + 's';";
    html += "    document.getElementById('freeMemory').textContent = data.freeMemory;";
    html += "  });";
    html += "  ";
    html += "  fetch('/api/history')";
    html += "  .then(response => response.json())";
    html += "  .then(history => {";
    html += "    const historyList = document.getElementById('historyList');";
    html += "    historyList.innerHTML = '';";
    html += "    history.forEach(item => {";
    html += "      const historyItem = document.createElement('div');";
    html += "      historyItem.className = 'history-item';";
    html += "      historyItem.textContent = item;";
    html += "      historyList.appendChild(historyItem);";
    html += "    });";
    html += "    if (history.length === 0) {";
    html += "      historyList.innerHTML = '<div style=\"text-align:center;color:#666\">No history available</div>';";
    html += "    }";
    html += "  });";
    html += "}";
    
    html += "function loadFromHistory() {";
    html += "  fetch('/api/history')";
    html += "  .then(response => response.json())";
    html += "  .then(history => {";
    html += "    const modalList = document.getElementById('modalHistoryList');";
    html += "    modalList.innerHTML = '';";
    html += "    history.forEach(item => {";
    html += "      const historyItem = document.createElement('div');";
    html += "      historyItem.className = 'history-item';";
    html += "      historyItem.textContent = item;";
    html += "      historyItem.style.cursor = 'pointer';";
    html += "      historyItem.onclick = () => {";
    html += "        document.getElementById('scriptArea').value = item;";
    html += "        closeHistoryModal();";
    html += "        openTab(event, 'ScriptTab');";
    html += "      };";
    html += "      modalList.appendChild(historyItem);";
    html += "    });";
    html += "    document.getElementById('historyModal').style.display = 'block';";
    html += "  });";
    html += "}";
    
    html += "function closeHistoryModal() {";
    html += "  document.getElementById('historyModal').style.display = 'none';";
    html += "}";
    
    html += "function clearHistory() {";
    html += "  if (confirm('Clear command history?')) {";
    html += "    fetch('/api/clear-history', { method: 'POST' })";
    html += "    .then(response => response.text())";
    html += "    .then(result => {";
    html += "      document.getElementById('status').innerHTML = result;";
    html += "      updateStats();";
    html += "    });";
    html += "  }";
    html += "}";
    
    html += "function exportHistory() {";
    html += "  fetch('/api/export-history')";
    html += "  .then(response => response.blob())";
    html += "  .then(blob => {";
    html += "    const url = window.URL.createObjectURL(blob);";
    html += "    const a = document.createElement('a');";
    html += "    a.style.display = 'none';";
    html += "    a.href = url;";
    html += "    a.download = 'command_history.txt';";
    html += "    document.body.appendChild(a);";
    html += "    a.click();";
    html += "    window.URL.revokeObjectURL(url);";
    html += "  });";
    html += "}";
    
    html += "function uploadScriptFile() {";
    html += "  const input = document.createElement('input');";
    html += "  input.type = 'file';";
    html += "  input.accept = '.txt';";
    html += "  input.onchange = e => {";
    html += "    const file = e.target.files[0];";
    html += "    const reader = new FileReader();";
    html += "    reader.onload = event => {";
    html += "      const content = event.target.result;";
    html += "      const filename = file.name;";
    html += "      doSaveScript(filename, content);";
    html += "    };";
    html += "    reader.readAsText(file);";
    html += "  };";
    html += "  input.click();";
    html += "}";
    
    html += "// File Manager Functions";
    html += "function refreshFileBrowser() {";
    html += "  fetch('/api/list-files?path=' + encodeURIComponent(currentBrowserPath))";
    html += "  .then(response => response.json())";
    html += "  .then(files => {";
    html += "    const fileBrowser = document.getElementById('fileBrowser');";
    html += "    fileBrowser.innerHTML = '';";
    html += "    document.getElementById('currentPath').value = currentBrowserPath;";
    html += "    ";
    html += "    files.forEach(file => {";
    html += "      const fileItem = document.createElement('div');";
    html += "      fileItem.className = 'file-browser-item ' + (file.isDirectory ? 'directory' : 'file');";
    html += "      ";
    html += "      const fileInfo = document.createElement('div');";
    html += "      fileInfo.style.flex = '1';";
    html += "      ";
    html += "      const fileName = document.createElement('div');";
    html += "      fileName.textContent = file.name;";
    html += "      fileName.style.cursor = file.isDirectory ? 'pointer' : 'default';";
    html += "      if (file.isDirectory) {";
    html += "        fileName.onclick = () => {";
    html += "          currentBrowserPath = currentBrowserPath + (currentBrowserPath.endsWith('/') ? '' : '/') + file.name;";
    html += "          refreshFileBrowser();";
    html += "        };";
    html += "      }";
    html += "      ";
    html += "      const fileSize = document.createElement('div');";
    html += "      fileSize.className = 'file-size';";
    html += "      fileSize.textContent = file.isDirectory ? 'Directory' : formatFileSize(file.size);";
    html += "      ";
    html += "      fileInfo.appendChild(fileName);";
    html += "      fileInfo.appendChild(fileSize);";
    html += "      ";
    html += "      const fileActions = document.createElement('div');";
    html += "      fileActions.className = 'file-actions';";
    html += "      ";
    html += "      if (!file.isDirectory) {";
    html += "        const downloadBtn = document.createElement('button');";
    html += "        downloadBtn.innerHTML = 'Download';";
    html += "        downloadBtn.onclick = () => downloadFile(currentBrowserPath + (currentBrowserPath.endsWith('/') ? '' : '/') + file.name);";
    html += "        ";
    html += "        const deleteBtn = document.createElement('button');";
    html += "        deleteBtn.innerHTML = 'Delete';";
    html += "        deleteBtn.className = 'danger';";
    html += "        deleteBtn.onclick = () => deleteBrowserFile(currentBrowserPath + (currentBrowserPath.endsWith('/') ? '' : '/') + file.name);";
    html += "        ";
    html += "        fileActions.appendChild(downloadBtn);";
    html += "        fileActions.appendChild(deleteBtn);";
    html += "      }";
    html += "      ";
    html += "      fileItem.appendChild(fileInfo);";
    html += "      fileItem.appendChild(fileActions);";
    html += "      fileBrowser.appendChild(fileItem);";
    html += "    });";
    html += "    ";
    html += "    if (files.length === 0) {";
    html += "      fileBrowser.innerHTML = '<div style=\"text-align:center;color:#666\">Directory is empty</div>';";
    html += "    }";
    html += "  });";
    html += "}";
    
    html += "function goToParent() {";
    html += "  if (currentBrowserPath !== '/') {";
    html += "    const lastSlash = currentBrowserPath.lastIndexOf('/');";
    html += "    currentBrowserPath = currentBrowserPath.substring(0, lastSlash);";
    html += "    if (currentBrowserPath === '') currentBrowserPath = '/';";
    html += "    refreshFileBrowser();";
    html += "  }";
    html += "}";
    
    html += "function formatFileSize(bytes) {";
    html += "  if (bytes === 0) return '0 B';";
    html += "  const k = 1024;";
    html += "  const sizes = ['B', 'KB', 'MB', 'GB'];";
    html += "  const i = Math.floor(Math.log(bytes) / Math.log(k));";
    html += "  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];";
    html += "}";
    
    html += "function downloadFile(filepath) {";
    html += "  window.open('/api/download?file=' + encodeURIComponent(filepath), '_blank');";
    html += "}";
    
    html += "function deleteBrowserFile(filepath) {";
    html += "  if (confirm('Delete ' + filepath + '?')) {";
    html += "    fetch('/api/delete-file?file=' + encodeURIComponent(filepath), { method: 'DELETE' })";
    html += "    .then(response => response.text())";
    html += "    .then(result => {";
    html += "      document.getElementById('status').innerHTML = result;";
    html += "      refreshFileBrowser();";
    html += "    });";
    html += "  }";
    html += "}";
    
    html += "// File Upload Handling";
    html += "document.getElementById('fileInput').addEventListener('change', function(e) {";
    html += "  const files = e.target.files;";
    html += "  for (let i = 0; i < files.length; i++) {";
    html += "    uploadFile(files[i]);";
    html += "  }";
    html += "});";
    
    html += "document.getElementById('uploadArea').addEventListener('dragover', function(e) {";
    html += "  e.preventDefault();";
    html += "  e.currentTarget.style.borderColor = '#4CAF50';";
    html += "});";
    
    html += "document.getElementById('uploadArea').addEventListener('dragleave', function(e) {";
    html += "  e.preventDefault();";
    html += "  e.currentTarget.style.borderColor = '#555';";
    html += "});";
    
    html += "document.getElementById('uploadArea').addEventListener('drop', function(e) {";
    html += "  e.preventDefault();";
    html += "  e.currentTarget.style.borderColor = '#555';";
    html += "  const files = e.dataTransfer.files;";
    html += "  for (let i = 0; i < files.length; i++) {";
    html += "    uploadFile(files[i]);";
    html += "  }";
    html += "});";
    
    html += "function uploadFile(file) {";
    html += "  const formData = new FormData();";
    html += "  const filepath = currentBrowserPath + (currentBrowserPath.endsWith('/') ? '' : '/') + file.name;";
    html += "  formData.append('file', file, filepath);";
    html += "  ";
    html += "  document.getElementById('uploadProgress').style.display = 'block';";
    html += "  document.getElementById('uploadFileName').textContent = file.name;";
    html += "  ";
    html += "  const xhr = new XMLHttpRequest();";
    html += "  xhr.open('POST', '/api/upload', true);";
    html += "  ";
    html += "  xhr.upload.onprogress = function(e) {";
    html += "    if (e.lengthComputable) {";
    html += "      const percentComplete = (e.loaded / e.total) * 100;";
    html += "      document.getElementById('uploadProgressFill').style.width = percentComplete + '%';";
    html += "    }";
    html += "  };";
    html += "  ";
    html += "  xhr.onload = function() {";
    html += "    if (xhr.status === 200) {";
    html += "      document.getElementById('status').innerHTML = 'File uploaded successfully: ' + file.name;";
    html += "      setTimeout(() => {";
    html += "        document.getElementById('uploadProgress').style.display = 'none';";
    html += "        document.getElementById('uploadProgressFill').style.width = '0%';";
    html += "      }, 2000);";
    html += "      refreshFileBrowser();";
    html += "    } else {";
    html += "      document.getElementById('status').innerHTML = 'Upload failed: ' + xhr.responseText;";
    html += "    }";
    html += "  };";
    html += "  ";
    html += "  xhr.send(formData);";
    html += "}";
    
    html += "document.getElementById('bootScriptSelect').addEventListener('change', previewBootScript);";
    
    html += "setInterval(() => {";
    html += "  fetch('/status').then(r => r.text()).then(data => {";
    html += "    const statusEl = document.getElementById('status');";
    html += "    if (!data.includes('Script Running') && !statusEl.innerHTML.includes('Executing')) {";
    html += "      statusEl.innerHTML = data;";
    html += "    }";
    html += "  });";
    html += "}, 2000);";
    
    html += "setInterval(() => {";
    html += "  if (document.getElementById('StatsTab').style.display === 'block') {";
    html += "    updateStats();";
    html += "  }";
    html += "}, 5000);";
    
    html += "window.onload = function() {";
    html += "  refreshFiles();";
    html += "  refreshBootScripts();";
    html += "  previewBootScript();";
    html += "  updateStats();";
    html += "  refreshFileBrowser();";
    html += "};";
    
    html += "window.onclick = function(event) {";
    html += "  if (event.target == document.getElementById('fileOverrideModal')) {";
    html += "    closeModal();";
    html += "  }";
    html += "  if (event.target == document.getElementById('historyModal')) {";
    html += "    closeHistoryModal();";
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
    server.send(200, "text/plain; charset=utf-8", "Stop requested - waiting for current command to finish");
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
    if (scriptRunning) status += " - Script Running";
    if (bootModeEnabled) status += " - Boot: " + currentBootScriptFile;
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
    ledEnabled = !ledEnabled;
    preferences.putBool("led_enabled", ledEnabled);
    
    if (ledEnabled) {
      setLEDMode(0);
    } else {
      pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      pixels.show();
    }
    
    DynamicJsonDocument doc(128);
    doc["ledEnabled"] = ledEnabled;
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
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
    
    String type = doc["type"].as<String>();
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
    
    String ssid = doc["ssid"].as<String>();
    String password = doc["password"].as<String>();
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
    
    String filename = doc["filename"].as<String>();
    
    if (filename == "") {
      bootModeEnabled = false;
      currentBootScriptFile = "";
      bootScript = "";
      preferences.remove("boot_script");
      server.send(200, "text/plain; charset=utf-8", "Boot script disabled");
    } else if (SD.exists("/scripts/" + filename)) {
      currentBootScriptFile = filename;
      bootScript = loadScript(filename);
      bootModeEnabled = true;
      preferences.putString("boot_script", filename);
      server.send(200, "text/plain; charset=utf-8", "Boot script set to: " + filename);
    } else {
      server.send(404, "text/plain; charset=utf-8", "Script file not found");
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
    
    if (SD.exists("/scripts/" + filename)) {
      String script = loadScript(filename);
      executeScript(script);
      server.send(200, "text/plain; charset=utf-8", "Testing boot script: " + filename);
    } else {
      server.send(404, "text/plain; charset=utf-8", "Script file not found");
    }
  });
  
  server.on("/api/stats", []() {
    DynamicJsonDocument doc(1024);
    doc["errorCount"] = errorCount;
    doc["scriptCount"] = availableScripts.size();
    doc["languageCount"] = availableLanguages.size();
    doc["clientCount"] = WiFi.softAPgetStationNum();
    doc["lastError"] = lastError;
    doc["sdCardPresent"] = sdCardPresent;
    doc["uptime"] = millis() / 1000;
    doc["freeMemory"] = ESP.getFreeHeap();
    doc["totalScripts"] = totalScriptsExecuted;
    doc["totalCommands"] = totalCommandsExecuted;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  server.on("/api/history", []() {
    String json = "[";
    for (int i = 0; i < commandHistory.size(); i++) {
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
    errorCount = 0;
    lastError = "";
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
    int errorCount = 0;
    String errors = "";
    
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
    
    // Basic validation - check for common syntax errors
    for (String line : lines) {
      if (line.startsWith("STRING ") && line.length() == 7) {
        errors += "Empty STRING command\\n";
        errorCount++;
      }
      if (line.startsWith("DELAY ") && line.substring(6).toInt() <= 0) {
        errors += "Invalid DELAY value\\n";
        errorCount++;
      }
      if (line.startsWith("REPEAT ") && line.substring(7).toInt() <= 0) {
        errors += "Invalid REPEAT count\\n";
        errorCount++;
      }
    }
    
    if (errorCount == 0) {
      server.send(200, "text/plain; charset=utf-8", "Script validation passed! " + String(lineCount) + " commands found.");
    } else {
      server.send(200, "text/plain; charset=utf-8", "Script validation found " + String(errorCount) + " issues:\\n" + errors);
    }
  });
  
  server.begin();
  Serial.println("Web server started");
}

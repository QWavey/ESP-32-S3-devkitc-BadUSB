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

// File upload variables
File uploadFile;
String uploadFilename = "";

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
      if (kc.modifier & 0x40) { keyboard.press(KEY_RIGHT_ALT); delay(5); keyboard.release(KEY_RIGHT_ALT); }
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

// Improved File upload handler
void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    uploadFilename = upload.filename;
    Serial.println("File upload start: " + uploadFilename);
    
    // Determine upload directory based on file type
    String uploadPath;
    if (uploadFilename.endsWith(".txt")) {
      uploadPath = "/scripts/" + uploadFilename;
    } else if (uploadFilename.endsWith(".json")) {
      uploadPath = "/languages/" + uploadFilename;
    } else {
      uploadPath = "/uploads/" + uploadFilename;
    }
    
    // Close any previously open file
    if (uploadFile) {
      uploadFile.close();
    }
    
    // Delete existing file if it exists
    if (SD.exists(uploadPath)) {
      SD.remove(uploadPath);
    }
    
    // Open new file for writing
    uploadFile = SD.open(uploadPath, FILE_WRITE);
    if (!uploadFile) {
      Serial.println("Failed to create file: " + uploadPath);
      return;
    }
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      // Write received data to file
      size_t bytesWritten = uploadFile.write(upload.buf, upload.currentSize);
      if (bytesWritten != upload.currentSize) {
        Serial.println("File write error: " + String(bytesWritten) + " vs " + String(upload.currentSize));
      }
    }
    
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.println("File upload complete: " + uploadFilename + " size: " + String(upload.totalSize));
      
      // Refresh appropriate file lists
      if (uploadFilename.endsWith(".txt")) {
        loadAvailableScripts();
      } else if (uploadFilename.endsWith(".json")) {
        loadAvailableLanguages();
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Serial.println("File upload aborted: " + uploadFilename);
    if (uploadFile) {
      uploadFile.close();
      // Delete partially uploaded file
      String uploadPath = "/uploads/" + uploadFilename;
      if (SD.exists(uploadPath)) {
        SD.remove(uploadPath);
      }
    }
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
        server.sendHeader("Content-Type", "application/octet-stream");
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

// Recursive function to delete a directory and all its contents
bool deleteDirectory(String path) {
  File dir = SD.open(path);
  if (!dir) {
    return false;
  }

  if (!dir.isDirectory()) {
    dir.close();
    return SD.remove(path);
  }

  dir.rewindDirectory();
  File file = dir.openNextFile();
  while (file) {
    String filepath = path + "/" + String(file.name());
    if (file.isDirectory()) {
      if (!deleteDirectory(filepath)) {
        dir.close();
        return false;
      }
    } else {
      if (!SD.remove(filepath)) {
        dir.close();
        return false;
      }
    }
    file = dir.openNextFile();
  }
  dir.close();

  return SD.rmdir(path);
}

// List all files on SD card with improved error handling
void handleListFiles() {
  String path = "/";
  if (server.hasArg("path")) {
    path = server.arg("path");
    // Basic path sanitization
    if (path.indexOf("..") >= 0) {
      server.send(400, "text/plain", "Invalid path");
      return;
    }
  }

  File root = SD.open(path);
  if (!root) {
    server.send(500, "text/plain", "Failed to open directory: " + path);
    return;
  }

  if (!root.isDirectory()) {
    server.send(400, "text/plain", "Not a directory: " + path);
    root.close();
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

// Delete file or directory handler with improved error handling
void handleDeleteFile() {
  if (server.hasArg("file")) {
    String filename = server.arg("file");
    
    // Basic security check
    if (filename.indexOf("..") >= 0 || filename.length() == 0) {
      server.send(400, "text/plain", "Invalid filename");
      return;
    }

    String filepath = filename;
    if (!filepath.startsWith("/")) {
      filepath = "/" + filepath;
    }

    if (SD.exists(filepath)) {
      bool success = false;
      File file = SD.open(filepath);
      if (file) {
        if (file.isDirectory()) {
          file.close();
          success = deleteDirectory(filepath);
        } else {
          file.close();
          success = SD.remove(filepath);
        }
      }

      if (success) {
        server.send(200, "text/plain", "Deleted: " + filename);
        Serial.println("Deleted: " + filename);

        // Refresh appropriate file lists
        if (filename.endsWith(".txt")) {
          loadAvailableScripts();
        } else if (filename.endsWith(".json")) {
          loadAvailableLanguages();
        }
      } else {
        server.send(500, "text/plain", "Failed to delete: " + filename);
      }
    } else {
      server.send(404, "text/plain", "File not found: " + filename);
    }
  } else {
    server.send(400, "text/plain", "No file specified");
  }
}

// Create directory handler
void handleCreateDirectory() {
  if (server.hasArg("path")) {
    String path = server.arg("path");
    
    if (!path.startsWith("/")) {
      path = "/" + path;
    }
    
    if (SD.mkdir(path)) {
      server.send(200, "text/plain", "Directory created: " + path);
      Serial.println("Directory created: " + path);
    } else {
      server.send(500, "text/plain", "Failed to create directory");
    }
  } else {
    server.send(400, "text/plain", "No path specified");
  }
}

// File info handler
void handleFileInfo() {
  if (server.hasArg("file")) {
    String filename = server.arg("file");
    String filepath = filename;
    
    if (!filepath.startsWith("/")) {
      filepath = "/" + filepath;
    }
    
    if (SD.exists(filepath)) {
      File file = SD.open(filepath);
      if (file) {
        DynamicJsonDocument doc(512);
        doc["name"] = filename;
        doc["size"] = file.size();
        doc["isDirectory"] = file.isDirectory();
        file.close();
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
      } else {
        server.send(500, "text/plain", "Failed to open file");
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

  // File upload endpoint - improved
  server.on("/api/upload", HTTP_POST, []() {
    server.send(200, "text/plain", "Upload complete: " + uploadFilename);
  }, handleFileUpload);

  // File download endpoint
  server.on("/api/download", handleFileDownload);

  // List files endpoint
  server.on("/api/list-files", handleListFiles);

  // Delete file endpoint
  server.on("/api/delete-file", HTTP_DELETE, handleDeleteFile);

  // Create directory endpoint
  server.on("/api/create-directory", HTTP_POST, handleCreateDirectory);

  // File info endpoint
  server.on("/api/file-info", handleFileInfo);

  server.on("/", []() {
    String html = R"=====(
<!DOCTYPE html><html><head><title>ESP32-S3 BadUSB</title>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<style>
body{font-family:Arial,sans-serif;margin:20px;background:#1a1a1a;color:#fff;}
.container{max-width:1200px;margin:0 auto;background:#2d2d2d;padding:20px;border-radius:10px;}
h1{color:#4CAF50;text-align:center;margin-bottom:30px;}
.section{margin:20px 0;padding:15px;background:#3d3d3d;border-radius:5px;}
textarea{width:100%;height:300px;background:#1a1a1a;color:#fff;border:1px solid #555;padding:10px;font-family:monospace;font-size:14px;resize:vertical;}
button{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;margin:5px;}
button:hover{background:#45a049;}
button.danger{background:#f44336;}
button.danger:hover{background:#da190b;}
select{background:#1a1a1a;color:#fff;border:1px solid #555;padding:8px;margin:5px;border-radius:3px;}
input{background:#1a1a1a;color:#fff;border:1px solid #555;padding:8px;margin:5px;border-radius:3px;}
.status{padding:10px;margin:10px 0;border-radius:5px;background:#333;}
.examples{background:#2a2a2a;padding:15px;border-radius:5px;margin:10px 0;}
pre{background:#1a1a1a;padding:10px;border-radius:3px;overflow-x:auto;font-size:12px;}
.tab{overflow:hidden;background-color:#2d2d2d;display:flex;flex-wrap:wrap;}
.tab button{background-color:#2d2d2d;border:none;outline:none;cursor:pointer;padding:12px 16px;transition:0.3s;flex:1;min-width:100px;color:#fff;}
.tab button:hover{background-color:#3d3d3d;}
.tab button.active{background-color:#4CAF50;}
@media (max-width: 768px) {.tab button{font-size:12px;padding:10px 8px;}}
.tabcontent{display:none;padding:20px 12px;border:1px solid #555;border-top:none;animation:fadeEffect 0.5s;}
@keyframes fadeEffect{from{opacity:0;} to{opacity:1;}}
.file-list{max-height:200px;overflow-y:auto;border:1px solid #555;padding:10px;background:#1a1a1a;border-radius:5px;}
.file-item{padding:8px;cursor:pointer;border-bottom:1px solid #333;display:flex;justify-content:space-between;align-items:center;}
.file-item:hover{background:#2a2a2a;}
.file-buttons{display:flex;gap:5px;}
.file-buttons button{padding:4px 8px;font-size:11px;margin:0;}
.control-panel{display:flex;gap:10px;flex-wrap:wrap;margin:10px 0;}
.control-btn{flex:1;min-width:120px;}
@media (max-width: 768px) {.control-btn{min-width:100px;font-size:12px;}}
.setting-group{margin:15px 0;padding:10px;background:#2a2a2a;border-radius:5px;}
.setting-row{display:flex;align-items:center;margin:10px 0;gap:10px;flex-wrap:wrap;}
.setting-label{min-width:120px;font-weight:bold;}
@media (max-width: 768px) {.setting-row{flex-direction:column;align-items:flex-start;}}
.modal{display:none;position:fixed;z-index:1000;left:0;top:0;width:100%;height:100%;overflow:auto;background-color:rgba(0,0,0,0.4);}
.modal-content{background-color:#2d2d2d;margin:15% auto;padding:20px;border:1px solid #888;width:300px;max-width:90%;border-radius:10px;}
.close{color:#aaa;float:right;font-size:28px;font-weight:bold;cursor:pointer;}
.close:hover{color:#fff;}
.led-control{display:flex;align-items:center;gap:10px;margin:10px 0;}
.led-status{padding:8px 12px;border-radius:5px;font-weight:bold;}
.led-on{background:#4CAF50;color:white;}
.led-off{background:#666;color:#ccc;}
.switch{position:relative;display:inline-block;width:60px;height:34px;}
.switch input{opacity:0;width:0;height:0;}
.slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#ccc;transition:.4s;border-radius:34px;}
.slider:before{position:absolute;content:'';height:26px;width:26px;left:4px;bottom:4px;background-color:white;transition:.4s;border-radius:50%;}
input:checked + .slider{background-color:#4CAF50;}
input:checked + .slider:before{transform:translateX(26px);}
.switch-container{display:flex;align-items:center;gap:10px;margin:10px 0;}
.stats-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:10px;margin:10px 0;}
.stat-card{background:#2a2a2a;padding:15px;border-radius:5px;text-align:center;}
.stat-value{font-size:24px;font-weight:bold;color:#4CAF50;}
.stat-label{font-size:12px;color:#ccc;}
.history-list{max-height:300px;overflow-y:auto;border:1px solid #555;padding:10px;background:#1a1a1a;border-radius:5px;}
.history-item{padding:5px;border-bottom:1px solid #333;font-family:monospace;font-size:12px;}
.progress-bar{width:100%;height:20px;background:#1a1a1a;border-radius:10px;overflow:hidden;margin:10px 0;}
.progress-fill{height:100%;background:#4CAF50;transition:width 0.3s;}
.command-help{background:#2a2a2a;padding:10px;border-radius:5px;margin:5px 0;cursor:pointer;}
.command-help:hover{background:#3a3a3a;}
.command-details{display:none;padding:10px;background:#1a1a1a;border-radius:5px;margin-top:5px;}
.file-manager{background:#2a2a2a;padding:15px;border-radius:5px;margin:10px 0;}
.upload-area{border:2px dashed #555;padding:20px;text-align:center;border-radius:5px;margin:10px 0;cursor:pointer;}
.upload-area:hover{border-color:#4CAF50;}
.file-browser{max-height:400px;overflow-y:auto;border:1px solid #555;padding:10px;background:#1a1a1a;border-radius:5px;}
.file-browser-item{padding:8px;border-bottom:1px solid #333;display:flex;justify-content:space-between;align-items:center;}
.file-browser-item:hover{background:#2a2a2a;}
.file-size{color:#888;font-size:12px;}
.file-actions{display:flex;gap:5px;}
.directory{color:#4CAF50;font-weight:bold;}
.file{color:#fff;}
.upload-status{padding:10px;margin:10px 0;border-radius:5px;}
.upload-success{background:#4CAF50;color:white;}
.upload-error{background:#f44336;color:white;}
.upload-progress-container{margin:10px 0;padding:10px;background:#2a2a2a;border-radius:5px;}
.upload-progress-text{display:flex;justify-content:space-between;margin-bottom:5px;}
.upload-progress-bar{width:100%;height:20px;background:#1a1a1a;border-radius:10px;overflow:hidden;}
.upload-progress-fill{height:100%;background:#4CAF50;transition:width 0.3s;width:0%;}
.loading-spinner{display:inline-block;width:20px;height:20px;border:3px solid #f3f3f3;border-top:3px solid #4CAF50;border-radius:50%;animation:spin 1s linear infinite;margin-right:10px;}
@keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}
*{-webkit-tap-highlight-color:transparent;-webkit-touch-callout:none;}

.upload-area {
  transition: background-color 0.2s, border-color 0.2s;
}
.upload-area:hover {
  background-color: #333;
  border-color: #4CAF50;
}
.upload-area:active {
  background-color: #2a2a2a;
  border-color: #45a049;
}

</style></head><body>

<div class='container'>
<h1>ESP32-S3 BadUSB</h1>

<div class='tab'>
<button class='tablinks active' onclick="openTab(event, 'ScriptTab')">Script</button>
<button class='tablinks' onclick="openTab(event, 'FilesTab')">Files</button>
<button class='tablinks' onclick="openTab(event, 'FileManagerTab')">File Manager</button>
<button class='tablinks' onclick="openTab(event, 'BootTab')">Boot</button>
<button class='tablinks' onclick="openTab(event, 'SettingsTab')">Settings</button>
<button class='tablinks' onclick="openTab(event, 'StatsTab')">Statistics</button>
<button class='tablinks' onclick="openTab(event, 'HelpTab')">Help</button>
</div>

<div id='ScriptTab' class='tabcontent' style='display:block'>
<h3>Script Execution</h3>
<textarea id='scriptArea' placeholder='Enter your BadUSB script here...&#10;&#10;Example:&#10;DELAY 1000&#10;GUI r&#10;DELAY 500&#10;STRING notepad&#10;ENTER&#10;DELAY 1000&#10;STRING Hello World!'></textarea>
<br>
<div class='control-panel'>
<button class='control-btn' onclick='executeScript()'>Execute Script</button>
<button class='control-btn' onclick='stopScript()'>Stop Script</button>
<button class='control-btn' onclick='clearScript()'>Clear</button>
<button class='control-btn' onclick='saveScriptPrompt()'>Save Script</button>
<button class='control-btn' onclick='loadFromHistory()'>Load from History</button>
<button class='control-btn' onclick='validateScript()'>Validate Script</button>
</div>
<div class='progress-bar' id='progressBar' style='display:none'>
<div class='progress-fill' id='progressFill' style='width:0%'></div>
</div>
</div>

<div id='FilesTab' class='tabcontent'>
<h3>Script Files</h3>
<div class='file-list' id='fileList'>Loading...</div>
<br>
<div class='setting-row'>
<input type='text' id='newFilename' placeholder='New script name (without .txt)' style='flex:1'>



</div>
</div>

<div id='FileManagerTab' class='tabcontent'>
<h3>File Manager - Copy Files to SD Card</h3>

<div class='file-manager'>
<h4>Upload Files to SD Card</h4>
<div class='upload-area' id='uploadArea'>
<p>Drag & drop files here or click to browse</p>
<input type='file' id='fileInput' multiple style='display:none'>

</div>

<div class='upload-progress-container' id='uploadProgress' style='display:none'>
<div class='upload-progress-text'>
<span>Uploading: <span id='uploadFileName'></span></span>
<span id='uploadPercentage'>0%</span>
</div>
<div class='upload-progress-bar'>
<div class='upload-progress-fill' id='uploadProgressFill'></div>
</div>
</div>
</div>

<div class='file-manager'>
<h4>SD Card File Browser</h4>
<div class='setting-row'>
<input type='text' id='currentPath' value='/' readonly style='flex:1'>



</div>
<div class='file-browser' id='fileBrowser'>
<div class='file-browser-item'>
<span><span class='loading-spinner'></span>Loading files...</span>
</div>
</div>
</div>
</div>

<div id='BootTab' class='tabcontent'>
<h3>Boot Script Configuration</h3>
<div class='setting-group'>
<div class='setting-row'>
<span class='setting-label'>Current Boot Script:</span>
<select id='bootScriptSelect' style='flex:1'>
<option value=''>None</option>
</select>

</div>
</div>
<div class='section'>
<h4>Boot Script Preview</h4>
<textarea id='bootScriptPreview' readonly style='height:200px' placeholder='Select a boot script to preview...'></textarea>


</div>
</div>

<div id='SettingsTab' class='tabcontent'>
<h3>Settings</h3>

<div class='setting-group'>
<h4>LED Control</h4>
<div class='led-control'>
<span class='setting-label'>RGB LED Status:</span>
<span id='ledStatus' class='led-status led-on'>ON</span>
<button id='ledToggleBtn' onclick='toggleLED()'>Turn Off</button>
</div>
<p style='font-size:12px;color:#ccc'>LED States: Solid Green = Idle, Fast Blinking Green = Running, Blinking Red = Error/SD Card Removed, Orange Blink = Completion</p>
<p style='font-size:12px;color:#ccc'>Use BLINKING ON/OFF and RGB ON/OFF commands in scripts to control LED</p>
</div>

<div class='setting-group'>
<h4>Logging</h4>
<div class='switch-container'>
<span class='setting-label'>Log Files:</span>
<label class='switch'>
<input type='checkbox' id='loggingToggle' checked onchange='toggleLogging()'>
<span class='slider'></span>
</label>
<span id='loggingStatus'>ON</span>
</div>
<p style='font-size:12px;color:#ccc'>When enabled, all script commands will be logged to /logs/log.txt</p>
</div>

<div class='setting-group'>
<h4>Language Configuration</h4>
<div class='setting-row'>
<span class='setting-label'>Current Language:</span>
<span id='currentLang'>us</span>
</div>
<div class='setting-row'>
<span class='setting-label'>Select Language:</span>
<select id='languageSelect' style='flex:1'>
</select>

</div>

</div>

<div class='setting-group'>
<h4>WiFi Configuration</h4>
<div class='setting-row'>
<span class='setting-label'>Network Name (SSID):</span>
<input type='text' id='wifiSSID' value='ESP32-BadUSB' style='flex:1'>
</div>
<div class='setting-row'>
<span class='setting-label'>Password:</span>
<input type='password' id='wifiPassword' value='badusb123' style='flex:1'>
</div>
<div class='setting-row'>
<span class='setting-label'>WiFi Scan Time (ms):</span>
<input type='number' id='wifiScanTime' value='5000' min='1000' max='30000' step='1000' style='flex:1'>
<span style='font-size:12px;color:#ccc'>1000-30000 ms</span>
</div>

<p style='font-size:12px;color:#ccc'>WiFi changes require device reboot to take effect</p>
</div>

<div class='setting-group'>
<h4>Quick Actions</h4>
<div class='control-panel'>
<button class='control-btn' onclick='quickAction("GUI r")'>Win+R</button>
<button class='control-btn' onclick='quickAction("CTRL ALT DEL")'>Ctrl+Alt+Del</button>
<button class='control-btn' onclick='quickAction("ALT F4")'>Alt+F4</button>
<button class='control-btn' onclick='quickAction("CTRL SHIFT ESC")'>Task Manager</button>
<button class='control-btn' onclick='typeText()'>Type Text</button>
<button class='control-btn' onclick='quickAction("REPEAT 5 STRING Repeated text")'>Repeat Example</button>
</div>
</div>

<div class='setting-group'>
<h4>Danger Zone</h4>
<button class='danger' onclick='selfDestructPrompt()'>Self Destruct</button>
<button class='danger' onclick='clearErrorLog()'>Clear Error Log</button>
<button class='danger' onclick='factoryReset()'>Factory Reset</button>
<p style='font-size:12px;color:#ccc'>Self Destruct: Deletes all scripts and reboots | Clear Errors: Resets error counter | Factory Reset: Restores all defaults</p>
</div>
</div>

<div id='StatsTab' class='tabcontent'>
<h3>Device Statistics & History</h3>

<div class='stats-grid'>
<div class='stat-card'>
<div class='stat-value' id='errorCount'>0</div>
<div class='stat-label'>Total Errors</div>
</div>
<div class='stat-card'>
<div class='stat-value' id='scriptCount'>0</div>
<div class='stat-label'>Available Scripts</div>
</div>
<div class='stat-card'>
<div class='stat-value' id='languageCount'>0</div>
<div class='stat-label'>Languages</div>
</div>
<div class='stat-card'>
<div class='stat-value' id='clientCount'>0</div>
<div class='stat-label'>Connected Clients</div>
</div>
<div class='stat-card'>
<div class='stat-value' id='totalScripts'>0</div>
<div class='stat-label'>Scripts Executed</div>
</div>
<div class='stat-card'>
<div class='stat-value' id='totalCommands'>0</div>
<div class='stat-label'>Commands Executed</div>
</div>
</div>

<div class='setting-group'>
<h4>Command History</h4>
<div class='history-list' id='historyList'>Loading...</div>


</div>

<div class='setting-group'>
<h4>System Information</h4>
<div class='setting-row'>
<span class='setting-label'>Last Error:</span>
<span id='lastError'>None</span>
</div>
<div class='setting-row'>
<span class='setting-label'>SD Card Status:</span>
<span id='sdStatus'>OK</span>
</div>
<div class='setting-row'>
<span class='setting-label'>Uptime:</span>
<span id='uptime'>0s</span>
</div>
<div class='setting-row'>
<span class='setting-label'>Free Memory:</span>
<span id='freeMemory'>0</span>
</div>
</div>
</div>

<div id='HelpTab' class='tabcontent'>
<h3>Command Reference & Help</h3>

<div class='command-help' onclick='toggleCommandDetails("basic")'>Basic Commands</div>
<div id='basic-details' class='command-details'>
<pre>STRING text          - Type text
DELAY ms            - Delay in milliseconds
GUI key             - Windows key
ENTER               - Enter key
TAB                 - Tab key
ESC                 - Escape key
UP / DOWN / LEFT / RIGHT - Arrow keys</pre>
</div>

<div class='command-help' onclick='toggleCommandDetails("modifiers")'>Modifier Keys</div>
<div id='modifiers-details' class='command-details'>
<pre>CTRL key            - Control key combination
SHIFT key           - Shift key combination
ALT key             - Alt key combination
CTRL ALT key        - Control+Alt combination
CTRL SHIFT key      - Control+Shift combination
ALT SHIFT key       - Alt+Shift combination
CTRL ALT SHIFT key  - Control+Alt+Shift combination</pre>
</div>

<div class='command-help' onclick='toggleCommandDetails("advanced")'>Advanced Commands</div>
<div id='advanced-details' class='command-details'>
<pre>VAR name=value      - Set variable
DEFAULTDELAY ms     - Set default delay between commands
DELAY_BETWEEN_KEYS ms - Set delay between keystrokes
LOCALE language     - Change keyboard layout
REPEAT n command    - Repeat command n times
WAIT_FOR_SD         - Wait for SD card to be present
IF_PRESENT SSID="name" - Conditional execution
IF_NOTPRESENT SSID="name" - Conditional execution
ENDIF               - End conditional block</pre>
</div>

<div class='command-help' onclick='toggleCommandDetails("led")'>LED Control Commands</div>
<div id='led-details' class='command-details'>
<pre>LED r g b          - Set LED color (0-255)
BLINKING ON         - Enable LED blinking
BLINKING OFF        - Disable LED blinking
RGB ON              - Enable RGB LED
RGB OFF             - Disable RGB LED</pre>
</div>

<div class='command-help' onclick='toggleCommandDetails("wifi")'>WiFi Commands</div>
<div id='wifi-details' class='command-details'>
<pre>WIFI_SCAN_TIME ms   - Set WiFi scan time (1000-30000ms)
IF_PRESENT SSID="name" SCAN_TIME=ms - Conditional with custom scan
IF_NOTPRESENT SSID="name" SCAN_TIME=ms - Conditional with custom scan</pre>
</div>

<div class='examples'>
<h4>Quick Examples</h4>




</div>
</div>

<div class='section'>
<h3>Device Status</h3>
<div id='status' class='status'>Ready</div>
</div>

<div id='fileOverrideModal' class='modal'>
<div class='modal-content'>
<span class='close' onclick='closeModal()'>&times;</span>
<h3>File Already Exists</h3>
<p id='modalMessage'></p>



</div>
</div>

<div id='historyModal' class='modal'>
<div class='modal-content'>
<span class='close' onclick='closeHistoryModal()'>&times;</span>
<h3>Command History</h3>
<div class='history-list' id='modalHistoryList'></div>

</div>
</div>

<div id='folderModal' class='modal'>
<div class='modal-content'>
<span class='close' onclick='closeFolderModal()'>&times;</span>
<h3>Create New Folder</h3>
<input type='text' id='newFolderName' placeholder='Folder name' style='width:100%;margin:10px 0;'>


</div>
</div>

</div>

<script>
let pendingFilename = '';
let pendingContent = '';
let scriptProgress = 0;
let progressInterval;
let currentBrowserPath = '/';

function openTab(evt, tabName) {
  var i, tabcontent, tablinks;
  tabcontent = document.getElementsByClassName('tabcontent');
  for (i = 0; i < tabcontent.length; i++) {
    tabcontent[i].style.display = 'none';
  }
  tablinks = document.getElementsByClassName('tablinks');
  for (i = 0; i < tablinks.length; i++) {
    tablinks[i].className = tablinks[i].className.replace(' active', '');
  }
  document.getElementById(tabName).style.display = 'block';
  evt.currentTarget.className += ' active';
  if (tabName === 'FilesTab') { refreshFiles(); }
  if (tabName === 'BootTab') { refreshBootScripts(); }
  if (tabName === 'StatsTab') { updateStats(); }
  if (tabName === 'FileManagerTab') { refreshFileBrowser(); }
}

function toggleCommandDetails(id) {
  const details = document.getElementById(id + '-details');
  if (details.style.display === 'block') {
    details.style.display = 'none';
  } else {
    details.style.display = 'block';
  }
}

function executeScript() {
  const script = document.getElementById('scriptArea').value;
  if (!script.trim()) { alert('Please enter a script'); return; }
  document.getElementById('status').innerHTML = 'Executing script...';
  document.getElementById('progressBar').style.display = 'block';
  scriptProgress = 0;
  updateProgress(0);
  
  progressInterval = setInterval(() => {
    scriptProgress += Math.random() * 10;
    if (scriptProgress > 90) scriptProgress = 90;
    updateProgress(scriptProgress);
  }, 500);
  
  fetch('/execute', { method: 'POST', body: script })
  .then(response => response.text())
  .then(data => { 
    clearInterval(progressInterval);
    updateProgress(100);
    setTimeout(() => {
      document.getElementById('progressBar').style.display = 'none';
    }, 1000);
    document.getElementById('status').innerHTML = 'Script executed';
  })
  .catch(err => { 
    clearInterval(progressInterval);
    document.getElementById('progressBar').style.display = 'none';
    document.getElementById('status').innerHTML = 'Error: ' + err;
  });
}

function updateProgress(percent) {
  document.getElementById('progressFill').style.width = percent + '%';
}

function stopScript() {
  clearInterval(progressInterval);
  document.getElementById('progressBar').style.display = 'none';
  fetch('/stop', { method: 'POST' })
  .then(response => response.text())
  .then(data => { 
    document.getElementById('status').innerHTML = data;
    setTimeout(() => { document.getElementById('status').innerHTML = 'Script stopped'; }, 1000);
  });
}

function validateScript() {
  const script = document.getElementById('scriptArea').value;
  if (!script.trim()) { alert('Please enter a script'); return; }
  fetch('/api/validate-script', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ script: script })
  })
  .then(response => response.text())
  .then(result => {
    document.getElementById('status').innerHTML = result;
  });
}

function changeLanguage() {
  const lang = document.getElementById('languageSelect').value;
  fetch('/language?lang=' + lang)
  .then(response => response.text())
  .then(data => { 
    document.getElementById('currentLang').innerHTML = lang;
    document.getElementById('status').innerHTML = 'Language changed to: ' + lang;
  });
}

function saveLanguageSettings() {
  fetch('/api/save-settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ type: 'language' })
  })
  .then(response => response.text())
  .then(data => { document.getElementById('status').innerHTML = 'Language settings saved'; });
}

function saveWiFiSettings() {
  const ssid = document.getElementById('wifiSSID').value;
  const password = document.getElementById('wifiPassword').value;
  const scanTime = document.getElementById('wifiScanTime').value;
  if (!ssid || !password) { alert('Please enter both SSID and password'); return; }
  fetch('/api/save-wifi', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid: ssid, password: password, scanTime: scanTime })
  })
  .then(response => response.text())
  .then(data => { 
    document.getElementById('status').innerHTML = 'WiFi settings saved. Reboot required.';
    setTimeout(() => { window.location.reload(); }, 2000);
  });
}

function quickAction(action) {
  document.getElementById('status').innerHTML = 'Executing: ' + action;
  fetch('/execute', { method: 'POST', body: action })
  .then(response => response.text())
  .then(data => { document.getElementById('status').innerHTML = 'Executed: ' + action; });
}

function typeText() {
  const text = prompt('Enter text to type:');
  if (text) { quickAction('STRING ' + text); }
}

function clearScript() {
  document.getElementById('scriptArea').value = '';
}

function loadExample(num) {
  const examples = {
    1: 'REM Basic script example\\nDELAY 1000\\nGUI r\\nDELAY 500\\nSTRING notepad\\nENTER\\nDELAY 1000\\nSTRING Hello World!',
    2: 'REM WiFi detection example\\nIF_PRESENT SSID=\\\"MyWiFi\\\"\\nSTRING WiFi Found!\\nENTER\\nELSE\\nSTRING WiFi Not Found!\\nENTER\\nENDIF',
    3: 'REM LED control example\\nLED 255 0 0\\nDELAY 1000\\nLED 0 255 0\\nDELAY 1000\\nLED 0 0 255\\nDELAY 1000\\nRGB OFF',
    4: 'REM Repeat command example\\nREPEAT 5 STRING Hello World!\\nENTER\\nREPEAT 10 DELAY 100 STRING Test'
  };
  document.getElementById('scriptArea').value = examples[num];
  openTab(event, 'ScriptTab');
}

function refreshFiles() {
  fetch('/api/scripts')
  .then(response => response.json())
  .then(files => {
    const fileList = document.getElementById('fileList');
    fileList.innerHTML = '';
    files.forEach(file => {
      const fileItem = document.createElement('div');
      fileItem.className = 'file-item';
      const fileName = document.createElement('span');
      fileName.textContent = file;
      const buttons = document.createElement('div');
      buttons.className = 'file-buttons';
      
      const loadBtn = document.createElement('button');
      loadBtn.innerHTML = 'Load';
      loadBtn.onclick = () => loadFile(file);
      
      const deleteBtn = document.createElement('button');
      deleteBtn.innerHTML = 'Delete';
      deleteBtn.onclick = () => deleteFile(file);
      deleteBtn.className = 'danger';
      
      buttons.appendChild(loadBtn);
      buttons.appendChild(deleteBtn);
      fileItem.appendChild(fileName);
      fileItem.appendChild(buttons);
      fileList.appendChild(fileItem);
    });
    if (files.length === 0) {
      fileList.innerHTML = '<div style=\"text-align:center;color:#666\">No script files found</div>';
    }
  });
}

function refreshBootScripts() {
  fetch('/api/scripts')
  .then(response => response.json())
  .then(files => {
    const select = document.getElementById('bootScriptSelect');
    const currentValue = select.value;
    select.innerHTML = '<option value=\"\">None</option>';
    files.forEach(file => {
      const option = document.createElement('option');
      option.value = file;
      option.textContent = file;
      if (file === currentValue) option.selected = true;
      select.appendChild(option);
    });
  });
}

function previewBootScript() {
  const filename = document.getElementById('bootScriptSelect').value;
  const preview = document.getElementById('bootScriptPreview');
  if (!filename) {
    preview.value = 'Select a boot script to preview...';
    return;
  }
  fetch('/api/load?file=' + encodeURIComponent(filename))
  .then(response => response.text())
  .then(content => {
    preview.value = content;
  })
  .catch(err => {
    preview.value = 'Error loading script: ' + err;
  });
}

function setBootScript() {
  const filename = document.getElementById('bootScriptSelect').value;
  fetch('/api/set-boot-script', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ filename: filename })
  })
  .then(response => response.text())
  .then(result => {
    document.getElementById('status').innerHTML = result;
    previewBootScript();
  });
}

function loadBootScriptToEditor() {
  const content = document.getElementById('bootScriptPreview').value;
  if (content && content !== 'Select a boot script to preview...') {
    document.getElementById('scriptArea').value = content;
    openTab(event, 'ScriptTab');
    document.getElementById('status').innerHTML = 'Boot script loaded to editor';
  }
}

function testBootScript() {
  const filename = document.getElementById('bootScriptSelect').value;
  if (!filename) { alert('Select a boot script first'); return; }
  fetch('/api/test-boot-script', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ filename: filename })
  })
  .then(response => response.text())
  .then(result => {
    document.getElementById('status').innerHTML = result;
  });
}

function loadFile(filename) {
  fetch('/api/load?file=' + encodeURIComponent(filename))
  .then(response => response.text())
  .then(content => {
    document.getElementById('scriptArea').value = content;
    document.getElementById('status').innerHTML = 'Loaded: ' + filename;
    openTab(event, 'ScriptTab');
  });
}

function deleteFile(filename) {
  if (confirm('Delete ' + filename + '?')) {
    fetch('/api/delete?file=' + encodeURIComponent(filename), { method: 'DELETE' })
    .then(response => response.text())
    .then(result => {
      document.getElementById('status').innerHTML = result;
      refreshFiles();
      refreshBootScripts();
    });
  }
}

function saveScriptPrompt() {
  const filename = prompt('Enter filename (without .txt):');
  if (filename) { saveScriptAs(filename); }
}

function checkFileExists(filename) {
  return fetch('/api/check-file?file=' + encodeURIComponent(filename))
  .then(response => response.json())
  .then(data => data.exists);
}

function saveScriptAs(filename) {
  const script = document.getElementById('scriptArea').value;
  if (!script.trim()) { alert('Script is empty'); return; }
  
  if (!filename.endsWith('.txt')) filename += '.txt';
  
  checkFileExists(filename).then(exists => {
    if (exists) {
      pendingFilename = filename;
      pendingContent = script;
      document.getElementById('modalMessage').textContent = 'File \"' + filename + '\" already exists. What would you like to do?';
      document.getElementById('fileOverrideModal').style.display = 'block';
    } else {
      doSaveScript(filename, script);
    }
  });
}

function doSaveScript(filename, content) {
  fetch('/api/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ filename: filename, content: content })
  })
  .then(response => response.text())
  .then(result => {
    document.getElementById('status').innerHTML = result;
    refreshFiles();
    refreshBootScripts();
  });
}

function overrideFile() {
  doSaveScript(pendingFilename, pendingContent);
  closeModal();
}

function autoRename() {
  let baseName = pendingFilename.replace('.txt', '');
  let counter = 1;
  let newName = baseName + counter + '.txt';
  
  checkFileExists(newName).then(exists => {
    while (exists && counter < 100) {
      counter++;
      newName = baseName + counter + '.txt';
    }
    doSaveScript(newName, pendingContent);
  });
  closeModal();
}

function closeModal() {
  document.getElementById('fileOverrideModal').style.display = 'none';
  pendingFilename = '';
  pendingContent = '';
}

function saveScript() {
  const filename = document.getElementById('newFilename').value;
  if (!filename) { alert('Enter filename first'); return; }
  saveScriptAs(filename);
}

function toggleLED() {
  fetch('/api/toggle-led', { method: 'POST' })
  .then(response => response.json())
  .then(data => {
    const ledStatus = document.getElementById('ledStatus');
    const ledToggleBtn = document.getElementById('ledToggleBtn');
    if (data.ledEnabled) {
      ledStatus.textContent = 'ON';
      ledStatus.className = 'led-status led-on';
      ledToggleBtn.textContent = 'Turn Off';
    } else {
      ledStatus.textContent = 'OFF';
      ledStatus.className = 'led-status led-off';
      ledToggleBtn.textContent = 'Turn On';
    }
    document.getElementById('status').innerHTML = 'LED ' + (data.ledEnabled ? 'enabled' : 'disabled');
  });
}

function toggleLogging() {
  const isEnabled = document.getElementById('loggingToggle').checked;
  fetch('/api/toggle-logging', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ enabled: isEnabled })
  })
  .then(response => response.text())
  .then(result => {
    document.getElementById('loggingStatus').textContent = isEnabled ? 'ON' : 'OFF';
    document.getElementById('status').innerHTML = 'Logging ' + (isEnabled ? 'enabled' : 'disabled');
  });
}

function selfDestructPrompt() {
  const password = prompt('Enter AP password to confirm self-destruct:');
  if (password) {
    fetch('/selfdestruct', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ password: password })
    })
    .then(response => response.text())
    .then(result => {
      document.getElementById('status').innerHTML = result;
    });
  }
}

function clearErrorLog() {
  if (confirm('Clear error log and reset error counter?')) {
    fetch('/api/clear-errors', { method: 'POST' })
    .then(response => response.text())
    .then(result => {
      document.getElementById('status').innerHTML = result;
      updateStats();
    });
  }
}

function factoryReset() {
  if (confirm('Factory reset will restore all defaults. Continue?')) {
    fetch('/api/factory-reset', { method: 'POST' })
    .then(response => response.text())
    .then(result => {
      document.getElementById('status').innerHTML = result;
      setTimeout(() => { window.location.reload(); }, 2000);
    });
  }
}

function updateStats() {
  fetch('/api/stats')
  .then(response => response.json())
  .then(data => {
    document.getElementById('errorCount').textContent = data.errorCount;
    document.getElementById('scriptCount').textContent = data.scriptCount;
    document.getElementById('languageCount').textContent = data.languageCount;
    document.getElementById('clientCount').textContent = data.clientCount;
    document.getElementById('totalScripts').textContent = data.totalScripts;
    document.getElementById('totalCommands').textContent = data.totalCommands;
    document.getElementById('lastError').textContent = data.lastError || 'None';
    document.getElementById('sdStatus').textContent = data.sdCardPresent ? 'OK' : 'REMOVED';
    document.getElementById('uptime').textContent = data.uptime + 's';
    document.getElementById('freeMemory').textContent = data.freeMemory;
  });
  
  fetch('/api/history')
  .then(response => response.json())
  .then(history => {
    const historyList = document.getElementById('historyList');
    historyList.innerHTML = '';
    history.forEach(item => {
      const historyItem = document.createElement('div');
      historyItem.className = 'history-item';
      historyItem.textContent = item;
      historyList.appendChild(historyItem);
    });
    if (history.length === 0) {
      historyList.innerHTML = '<div style=\"text-align:center;color:#666\">No history available</div>';
    }
  });
}

function loadFromHistory() {
  fetch('/api/history')
  .then(response => response.json())
  .then(history => {
    const modalList = document.getElementById('modalHistoryList');
    modalList.innerHTML = '';
    history.forEach(item => {
      const historyItem = document.createElement('div');
      historyItem.className = 'history-item';
      historyItem.textContent = item;
      historyItem.style.cursor = 'pointer';
      historyItem.onclick = () => {
        document.getElementById('scriptArea').value = item;
        closeHistoryModal();
        openTab(event, 'ScriptTab');
      };
      modalList.appendChild(historyItem);
    });
    document.getElementById('historyModal').style.display = 'block';
  });
}

function closeHistoryModal() {
  document.getElementById('historyModal').style.display = 'none';
}

function clearHistory() {
  if (confirm('Clear command history?')) {
    fetch('/api/clear-history', { method: 'POST' })
    .then(response => response.text())
    .then(result => {
      document.getElementById('status').innerHTML = result;
      updateStats();
    });
  }
}

function exportHistory() {
  fetch('/api/export-history')
  .then(response => response.blob())
  .then(blob => {
    const url = window.URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.style.display = 'none';
    a.href = url;
    a.download = 'command_history.txt';
    document.body.appendChild(a);
    a.click();
    window.URL.revokeObjectURL(url);
  });
}

function uploadScriptFile() {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.txt';
  input.onchange = e => {
    const file = e.target.files[0];
    const reader = new FileReader();
    reader.onload = event => {
      const content = event.target.result;
      const filename = file.name;
      doSaveScript(filename, content);
    };
    reader.readAsText(file);
  };
  input.click();
}

// File Manager Functions
function refreshFileBrowser() {
  fetch('/api/list-files?path=' + encodeURIComponent(currentBrowserPath))
  .then(response => response.json())
  .then(files => {
    const fileBrowser = document.getElementById('fileBrowser');
    fileBrowser.innerHTML = '';
    document.getElementById('currentPath').value = currentBrowserPath;
    
    files.forEach(file => {
      const fileItem = document.createElement('div');
      fileItem.className = 'file-browser-item ' + (file.isDirectory ? 'directory' : 'file');
      
      const fileInfo = document.createElement('div');
      fileInfo.style.flex = '1';
      
      const fileName = document.createElement('div');
      fileName.textContent = file.name;
      fileName.style.cursor = file.isDirectory ? 'pointer' : 'default';
      if (file.isDirectory) {
        fileName.onclick = () => {
          currentBrowserPath = currentBrowserPath + (currentBrowserPath.endsWith('/') ? '' : '/') + file.name;
          refreshFileBrowser();
        };
      }
      
      const fileSize = document.createElement('div');
      fileSize.className = 'file-size';
      fileSize.textContent = file.isDirectory ? 'Directory' : formatFileSize(file.size);
      
      fileInfo.appendChild(fileName);
      fileInfo.appendChild(fileSize);
      
      const fileActions = document.createElement('div');
      fileActions.className = 'file-actions';
      
      if (file.isDirectory) {
        const deleteBtn = document.createElement('button');
        deleteBtn.innerHTML = 'Delete';
        deleteBtn.className = 'danger';
        deleteBtn.onclick = () => deleteBrowserFile(currentBrowserPath + (currentBrowserPath.endsWith('/') ? '' : '/') + file.name);
        fileActions.appendChild(deleteBtn);
      } else {
        const downloadBtn = document.createElement('button');
        downloadBtn.innerHTML = 'Download';
        downloadBtn.onclick = () => downloadFile(currentBrowserPath + (currentBrowserPath.endsWith('/') ? '' : '/') + file.name);
        
        const deleteBtn = document.createElement('button');
        deleteBtn.innerHTML = 'Delete';
        deleteBtn.className = 'danger';
        deleteBtn.onclick = () => deleteBrowserFile(currentBrowserPath + (currentBrowserPath.endsWith('/') ? '' : '/') + file.name);
        
        fileActions.appendChild(downloadBtn);
        fileActions.appendChild(deleteBtn);
      }
      
      fileItem.appendChild(fileInfo);
      fileItem.appendChild(fileActions);
      fileBrowser.appendChild(fileItem);
    });
    
    if (files.length === 0) {
      fileBrowser.innerHTML = '<div style=\"text-align:center;color:#666\">Directory is empty</div>';
    }
  })
  .catch(err => {
    document.getElementById('fileBrowser').innerHTML = '<div style=\"text-align:center;color:#f44336\">Error loading files: ' + err + '</div>';
  });
}

function goToParent() {
  if (currentBrowserPath !== '/') {
    const lastSlash = currentBrowserPath.lastIndexOf('/');
    currentBrowserPath = currentBrowserPath.substring(0, lastSlash);
    if (currentBrowserPath === '') currentBrowserPath = '/';
    refreshFileBrowser();
  }
}

function formatFileSize(bytes) {
  if (bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

function downloadFile(filepath) {
  window.open('/api/download?file=' + encodeURIComponent(filepath), '_blank');
}

function deleteBrowserFile(filepath) {
  if (confirm('Delete ' + filepath + '?')) {
    fetch('/api/delete-file?file=' + encodeURIComponent(filepath), { method: 'DELETE' })
    .then(response => response.text())
    .then(result => {
      document.getElementById('status').innerHTML = result;
      refreshFileBrowser();
    });
  }
}

function createNewFolder() {
  document.getElementById('folderModal').style.display = 'block';
  document.getElementById('newFolderName').value = '';
}

function closeFolderModal() {
  document.getElementById('folderModal').style.display = 'none';
}

function createFolder() {
  const folderName = document.getElementById('newFolderName').value;
  if (!folderName) {
    alert('Please enter a folder name');
    return;
  }
  
  const folderPath = currentBrowserPath + (currentBrowserPath.endsWith('/') ? '' : '/') + folderName;
  
  fetch('/api/create-directory', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ path: folderPath })
  })
  .then(response => response.text())
  .then(result => {
    document.getElementById('status').innerHTML = result;
    closeFolderModal();
    refreshFileBrowser();
  })
  .catch(err => {
    document.getElementById('status').innerHTML = 'Error creating folder: ' + err;
  });
}

// File Upload Handling
document.getElementById('fileInput').addEventListener('change', function(e) {
  const files = e.target.files;
  for (let i = 0; i < files.length; i++) {
    uploadFile(files[i]);
  }
});

document.getElementById('uploadArea').addEventListener('dragover', function(e) {
  e.preventDefault();
  e.currentTarget.style.borderColor = '#4CAF50';
});

document.getElementById('uploadArea').addEventListener('dragleave', function(e) {
  e.preventDefault();
  e.currentTarget.style.borderColor = '#555';
});

document.getElementById('uploadArea').addEventListener('drop', function(e) {
  e.preventDefault();
  e.currentTarget.style.borderColor = '#555';
  const files = e.dataTransfer.files;
  for (let i = 0; i < files.length; i++) {
    uploadFile(files[i]);
  }
});

function uploadFile(file) {
  const formData = new FormData();
  const filepath = currentBrowserPath + (currentBrowserPath.endsWith('/') ? '' : '/') + file.name;
  formData.append('file', file, filepath);
  
  document.getElementById('uploadProgress').style.display = 'block';
  document.getElementById('uploadFileName').textContent = file.name;
  document.getElementById('uploadPercentage').textContent = '0%';
  document.getElementById('uploadProgressFill').style.width = '0%';
  
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/upload', true);
  
  xhr.upload.onprogress = function(e) {
    if (e.lengthComputable) {
      const percentComplete = (e.loaded / e.total) * 100;
      document.getElementById('uploadProgressFill').style.width = percentComplete + '%';
      document.getElementById('uploadPercentage').textContent = Math.round(percentComplete) + '%';
    }
  };
  
  xhr.onload = function() {
    if (xhr.status === 200) {
      document.getElementById('status').innerHTML = 'File uploaded successfully: ' + file.name;
      setTimeout(() => {
        document.getElementById('uploadProgress').style.display = 'none';
        document.getElementById('uploadProgressFill').style.width = '0%';
        document.getElementById('uploadPercentage').textContent = '0%';
      }, 2000);
      refreshFileBrowser();
    } else {
      document.getElementById('status').innerHTML = 'Upload failed: ' + xhr.responseText;
    }
  };
  
  xhr.onerror = function() {
    document.getElementById('status').innerHTML = 'Upload failed: Network error';
    document.getElementById('uploadProgress').style.display = 'none';
  };
  
  xhr.send(formData);
}

document.getElementById('bootScriptSelect').addEventListener('change', previewBootScript);

setInterval(() => {
  fetch('/status').then(r => r.text()).then(data => {
    const statusEl = document.getElementById('status');
    if (!data.includes('Script Running') && !statusEl.innerHTML.includes('Executing')) {
      statusEl.innerHTML = data;
    }
  });
}, 2000);

setInterval(() => {
  if (document.getElementById('StatsTab').style.display === 'block') {
    updateStats();
  }
}, 5000);

window.onload = function() {
  refreshFiles();
  refreshBootScripts();
  previewBootScript();
  updateStats();
  refreshFileBrowser();
  
  // Load available languages
  fetch('/api/languages')
  .then(response => response.json())
  .then(languages => {
    const select = document.getElementById('languageSelect');
    languages.forEach(lang => {
      const option = document.createElement('option');
      option.value = lang;
      option.textContent = lang;
      select.appendChild(option);
    });
  });
};

window.onclick = function(event) {
  if (event.target == document.getElementById('fileOverrideModal')) {
    closeModal();
  }
  if (event.target == document.getElementById('historyModal')) {
    closeHistoryModal();
  }
  if (event.target == document.getElementById('folderModal')) {
    closeFolderModal();
  }
}

// Click anywhere on upload area to open file picker
document.getElementById('uploadArea').addEventListener('click', function(e) {
  if (e.target.id !== 'fileInput') {
    document.getElementById('fileInput').click();
  }
});

</script>
</body></html>
)=====";

    server.send(200, "text/html; charset=utf-8", html);
  });

  // Add languages endpoint
  server.on("/api/languages", []() {
    String json = "[";
    for (int i = 0; i < availableLanguages.size(); i++) {
      if (i > 0) json += ",";
      json += "\"" + availableLanguages[i] + "\"";
    }
    json += "]";
    server.send(200, "application/json; charset=utf-8", json);
  });

  // Existing endpoints...
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

  // Add all other existing endpoints...
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

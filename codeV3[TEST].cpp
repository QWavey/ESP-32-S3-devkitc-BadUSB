// THE EDIT COMMANDS ETC ARE NOT WORKING AND I AM STILL TRYING FIXING THEM AND IMPLEMENTING SOMETHING IN THEM

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

// NEW: File operations
String currentDirectory = "/";
String copiedFilePath = "";
String cutFilePath = "";
bool fileCopied = false;
bool fileCut = false;
std::vector<String> selectedFiles;

// NEW: OS detection
String detectedOS = "Unknown";

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

// NEW: Change directory function
void changeDirectory(String path) {
  if (!sdCardPresent) {
    Serial.println("SD card not present");
    return;
  }

  // Handle relative paths
  if (path.startsWith("./")) {
    path = currentDirectory + path.substring(2);
  } else if (path == "..") {
    // Go up one directory
    if (currentDirectory != "/") {
      int lastSlash = currentDirectory.lastIndexOf('/');
      if (lastSlash == 0) {
        currentDirectory = "/";
      } else {
        currentDirectory = currentDirectory.substring(0, lastSlash);
      }
    }
    return;
  } else if (!path.startsWith("/")) {
    // Relative path from current directory
    if (currentDirectory.endsWith("/")) {
      path = currentDirectory + path;
    } else {
      path = currentDirectory + "/" + path;
    }
  }

  // Clean up path
  path.replace("//", "/");
  
  // Check if directory exists
  if (SD.exists(path)) {
    File dir = SD.open(path);
    if (dir && dir.isDirectory()) {
      currentDirectory = path;
      if (!currentDirectory.endsWith("/") && currentDirectory != "/") {
        currentDirectory += "/";
      }
      Serial.println("Changed directory to: " + currentDirectory);
    } else {
      Serial.println("Not a directory: " + path);
    }
    dir.close();
  } else {
    Serial.println("Directory not found: " + path);
  }
}

// NEW: OS Detection function
void detectOS() {
  Serial.println("Starting OS detection...");
  
  // Reset detected OS
  detectedOS = "Unknown";
  
  // Method 1: Try Windows key combinations
  keyboard.press(KEY_LEFT_GUI);
  keyboard.press('r');
  delay(100);
  keyboard.releaseAll();
  delay(1000);
  
  // Type various OS-specific commands
  fastTypeString("cmd");
  delay(500);
  fastPressKey("ENTER");
  delay(1000);
  
  // Try to get system info
  fastTypeString("ver");
  fastPressKey("ENTER");
  delay(500);
  
  // Method 2: Try Linux/Mac terminal
  fastPressKey("CTRL");
  fastPressKey("ALT");
  fastPressKey("t");
  delay(100);
  keyboard.releaseAll();
  delay(1000);
  
  // Method 3: Try Android back button
  fastPressKey("ESC");
  delay(500);
  
  // Method 4: Try iOS home button simulation
  fastPressKey("HOME");
  delay(500);
  
  // Based on typical responses, set detected OS
  // This is a simplified detection - in practice you'd need more sophisticated methods
  detectedOS = "Windows"; // Default assumption
  
  Serial.println("OS detection completed. Detected OS: " + detectedOS);
  variables["DETECTED_OS"] = detectedOS;
}

// NEW: Use file function
void useFile(String filePath) {
  if (!sdCardPresent) {
    Serial.println("SD card not present");
    return;
  }

  // Handle relative paths
  if (!filePath.startsWith("/")) {
    filePath = currentDirectory + filePath;
  }

  if (SD.exists(filePath)) {
    selectedFiles.clear();
    selectedFiles.push_back(filePath);
    Serial.println("File ready to use: " + filePath);
    variables["SELECTED_FILE"] = filePath;
  } else {
    Serial.println("File not found: " + filePath);
  }
}

// NEW: Use multiple files function
void useFiles(std::vector<String> filePaths) {
  if (!sdCardPresent) {
    Serial.println("SD card not present");
    return;
  }

  selectedFiles.clear();
  for (String filePath : filePaths) {
    // Handle relative paths
    if (!filePath.startsWith("/")) {
      filePath = currentDirectory + filePath;
    }

    if (SD.exists(filePath)) {
      selectedFiles.push_back(filePath);
      Serial.println("File ready to use: " + filePath);
    } else {
      Serial.println("File not found: " + filePath);
    }
  }
  
  if (!selectedFiles.empty()) {
    variables["SELECTED_FILES"] = String(selectedFiles.size());
  }
}

// NEW: Copy file function
void copyFile(String sourcePath, String destPath) {
  if (!sdCardPresent) {
    Serial.println("SD card not present");
    return;
  }

  // If no source path provided, use selected files
  if (sourcePath == "" && !selectedFiles.empty()) {
    sourcePath = selectedFiles[0];
  }

  // Handle relative paths
  if (!sourcePath.startsWith("/")) {
    sourcePath = currentDirectory + sourcePath;
  }
  if (destPath != "" && !destPath.startsWith("/")) {
    destPath = currentDirectory + destPath;
  }

  if (destPath == "") {
    // Just mark for copy-paste operation
    copiedFilePath = sourcePath;
    fileCopied = true;
    fileCut = false;
    Serial.println("File marked for copy: " + sourcePath);
  } else {
    // Direct copy operation
    if (copySDFile(sourcePath, destPath)) {
      Serial.println("File copied: " + sourcePath + " -> " + destPath);
    } else {
      Serial.println("Failed to copy file: " + sourcePath);
    }
  }
}

// NEW: Cut file function
void cutFile(String sourcePath, String destPath) {
  if (!sdCardPresent) {
    Serial.println("SD card not present");
    return;
  }

  // If no source path provided, use selected files
  if (sourcePath == "" && !selectedFiles.empty()) {
    sourcePath = selectedFiles[0];
  }

  // Handle relative paths
  if (!sourcePath.startsWith("/")) {
    sourcePath = currentDirectory + sourcePath;
  }
  if (destPath != "" && !destPath.startsWith("/")) {
    destPath = currentDirectory + destPath;
  }

  if (destPath == "") {
    // Just mark for cut-paste operation
    cutFilePath = sourcePath;
    fileCut = true;
    fileCopied = false;
    Serial.println("File marked for cut: " + sourcePath);
  } else {
    // Direct move operation
    if (moveSDFile(sourcePath, destPath)) {
      Serial.println("File moved: " + sourcePath + " -> " + destPath);
    } else {
      Serial.println("Failed to move file: " + sourcePath);
    }
  }
}

// NEW: Paste file function
void pasteFile(String destPath) {
  if (!sdCardPresent) {
    Serial.println("SD card not present");
    return;
  }

  // Handle relative paths
  if (destPath != "" && !destPath.startsWith("/")) {
    destPath = currentDirectory + destPath;
  }

  if (destPath == "") {
    destPath = currentDirectory;
  }

  if (fileCopied && copiedFilePath != "") {
    // Copy operation
    String fileName = getFileNameFromPath(copiedFilePath);
    String destFile = destPath;
    if (!destPath.endsWith("/")) {
      destFile += "/";
    }
    destFile += fileName;

    if (copySDFile(copiedFilePath, destFile)) {
      Serial.println("File pasted: " + copiedFilePath + " -> " + destFile);
    } else {
      Serial.println("Failed to paste file: " + copiedFilePath);
    }
  } else if (fileCut && cutFilePath != "") {
    // Move operation
    String fileName = getFileNameFromPath(cutFilePath);
    String destFile = destPath;
    if (!destPath.endsWith("/")) {
      destFile += "/";
    }
    destFile += fileName;

    if (moveSDFile(cutFilePath, destFile)) {
      Serial.println("File moved: " + cutFilePath + " -> " + destFile);
      cutFilePath = "";
      fileCut = false;
    } else {
      Serial.println("Failed to move file: " + cutFilePath);
    }
  } else {
    Serial.println("No file to paste");
  }
}

// NEW: Helper function to get filename from path
String getFileNameFromPath(String path) {
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash != -1) {
    return path.substring(lastSlash + 1);
  }
  return path;
}

// NEW: Copy file on SD card
bool copySDFile(String sourcePath, String destPath) {
  File sourceFile = SD.open(sourcePath, FILE_READ);
  if (!sourceFile) {
    Serial.println("Failed to open source file: " + sourcePath);
    return false;
  }

  // If destination is a directory, append filename
  File destTest = SD.open(destPath);
  if (destTest && destTest.isDirectory()) {
    if (!destPath.endsWith("/")) {
      destPath += "/";
    }
    destPath += getFileNameFromPath(sourcePath);
  }
  destTest.close();

  // Delete destination file if it exists
  if (SD.exists(destPath)) {
    SD.remove(destPath);
  }

  File destFile = SD.open(destPath, FILE_WRITE);
  if (!destFile) {
    Serial.println("Failed to create destination file: " + destPath);
    sourceFile.close();
    return false;
  }

  // Copy data
  uint8_t buffer[512];
  size_t bytesRead;
  while ((bytesRead = sourceFile.read(buffer, sizeof(buffer))) > 0) {
    destFile.write(buffer, bytesRead);
  }

  sourceFile.close();
  destFile.close();

  return true;
}

// NEW: Move file on SD card
bool moveSDFile(String sourcePath, String destPath) {
  if (copySDFile(sourcePath, destPath)) {
    if (SD.remove(sourcePath)) {
      return true;
    } else {
      Serial.println("Failed to remove source file after copy: " + sourcePath);
      return false;
    }
  }
  return false;
}

// NEW: Join WiFi network
void joinWiFi(String ssid, String password) {
  Serial.println("Attempting to connect to WiFi: " + ssid);
  
  WiFi.mode(WIFI_AP_STA); // Both AP and station mode
  
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(1000);
    attempts++;
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.println("IP address: " + WiFi.localIP().toString());
    variables["WIFI_CONNECTED"] = "true";
    variables["WIFI_SSID"] = ssid;
  } else {
    Serial.println("\nFailed to connect to WiFi");
    variables["WIFI_CONNECTED"] = "false";
  }
}

// NEW: Leave WiFi network
void leaveWiFi() {
  WiFi.disconnect();
  delay(1000);
  WiFi.mode(WIFI_AP);
  Serial.println("Disconnected from WiFi");
  variables["WIFI_CONNECTED"] = "false";
  variables["WIFI_SSID"] = "";
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

  // NEW: CD command for changing directories
  if (line.startsWith("CD ")) {
    String path = line.substring(3);
    path.trim();
    changeDirectory(path);
    return;
  }

  // NEW: DETECT_OS command
  if (line == "DETECT_OS") {
    detectOS();
    return;
  }

  // NEW: USE_FILE command
  if (line.startsWith("USE_FILE ")) {
    String filePath = line.substring(9);
    filePath.trim();
    useFile(filePath);
    return;
  }

  // NEW: USE_FILES command
  if (line.startsWith("USE_FILES ")) {
    String filesStr = line.substring(10);
    filesStr.trim();
    std::vector<String> filePaths;
    
    int startIdx = 0;
    int spaceIdx = filesStr.indexOf(' ');
    while (spaceIdx != -1) {
      filePaths.push_back(filesStr.substring(startIdx, spaceIdx));
      startIdx = spaceIdx + 1;
      spaceIdx = filesStr.indexOf(' ', startIdx);
    }
    if (startIdx < filesStr.length()) {
      filePaths.push_back(filesStr.substring(startIdx));
    }
    
    useFiles(filePaths);
    return;
  }

  // NEW: COPY_FILE command
  if (line.startsWith("COPY_FILE ")) {
    String params = line.substring(10);
    params.trim();
    
    if (params.length() == 0) {
      // Copy without path (use selected file)
      copyFile("", "");
    } else {
      // Copy with path
      int spaceIdx = params.indexOf(' ');
      if (spaceIdx == -1) {
        // Only source path provided
        copyFile(params, "");
      } else {
        // Both source and destination provided
        String sourcePath = params.substring(0, spaceIdx);
        String destPath = params.substring(spaceIdx + 1);
        copyFile(sourcePath, destPath);
      }
    }
    return;
  }

  // NEW: CUT_FILE command
  if (line.startsWith("CUT_FILE ")) {
    String params = line.substring(9);
    params.trim();
    
    if (params.length() == 0) {
      // Cut without path (use selected file)
      cutFile("", "");
    } else {
      // Cut with path
      int spaceIdx = params.indexOf(' ');
      if (spaceIdx == -1) {
        // Only source path provided
        cutFile(params, "");
      } else {
        // Both source and destination provided
        String sourcePath = params.substring(0, spaceIdx);
        String destPath = params.substring(spaceIdx + 1);
        cutFile(sourcePath, destPath);
      }
    }
    return;
  }

  // NEW: PASTE_FILE command
  if (line.startsWith("PASTE_FILE")) {
    String destPath = line.substring(10);
    destPath.trim();
    pasteFile(destPath);
    return;
  }

  // NEW: JOIN_INTERNET command
  if (line.startsWith("JOIN_INTERNET")) {
    String params = line.substring(13);
    params.trim();
    
    // Parse SSID and password
    String ssid = "";
    String password = "";
    
    int ssidStart = params.indexOf("SSID=\"");
    if (ssidStart != -1) {
      int ssidEnd = params.indexOf("\"", ssidStart + 6);
      if (ssidEnd != -1) {
        ssid = params.substring(ssidStart + 6, ssidEnd);
      }
    }
    
    int passStart = params.indexOf("PASSWORD=\"");
    if (passStart != -1) {
      int passEnd = params.indexOf("\"", passStart + 10);
      if (passEnd != -1) {
        password = params.substring(passStart + 10, passEnd);
      }
    }
    
    if (ssid.length() > 0) {
      joinWiFi(ssid, password);
    } else {
      Serial.println("Invalid JOIN_INTERNET command. Usage: JOIN_INTERNET SSID=\"network\" PASSWORD=\"password\"");
    }
    return;
  }

  // NEW: LEAVE_INTERNET command
  if (line == "LEAVE_INTERNET") {
    leaveWiFi();
    return;
  }

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

// NEW: Change directory handler for web interface
void handleChangeDirectory() {
  if (server.hasArg("path")) {
    String path = server.arg("path");
    changeDirectory(path);
    server.send(200, "application/json", "{\"success\":true,\"currentDirectory\":\"" + currentDirectory + "\"}");
  } else {
    server.send(400, "text/plain", "No path specified");
  }
}

// NEW: Get current directory handler
void handleGetCurrentDirectory() {
  server.send(200, "application/json", "{\"currentDirectory\":\"" + currentDirectory + "\"}");
}

// NEW: OS detection handler
void handleDetectOS() {
  detectOS();
  server.send(200, "application/json", "{\"detectedOS\":\"" + detectedOS + "\"}");
}

// NEW: File operations handlers
void handleUseFile() {
  if (server.hasArg("file")) {
    String filePath = server.arg("file");
    useFile(filePath);
    server.send(200, "application/json", "{\"success\":true,\"message\":\"File ready to use: " + filePath + "\"}");
  } else {
    server.send(400, "text/plain", "No file specified");
  }
}

void handleCopyFile() {
  if (server.hasArg("source")) {
    String sourcePath = server.arg("source");
    String destPath = server.hasArg("destination") ? server.arg("destination") : "";
    copyFile(sourcePath, destPath);
    server.send(200, "application/json", "{\"success\":true,\"message\":\"File copied: " + sourcePath + "\"}");
  } else {
    server.send(400, "text/plain", "No source file specified");
  }
}

void handleCutFile() {
  if (server.hasArg("source")) {
    String sourcePath = server.arg("source");
    String destPath = server.hasArg("destination") ? server.arg("destination") : "";
    cutFile(sourcePath, destPath);
    server.send(200, "application/json", "{\"success\":true,\"message\":\"File cut: " + sourcePath + "\"}");
  } else {
    server.send(400, "text/plain", "No source file specified");
  }
}

void handlePasteFile() {
  String destPath = server.hasArg("destination") ? server.arg("destination") : "";
  pasteFile(destPath);
  server.send(200, "application/json", "{\"success\":true,\"message\":\"File pasted\"}");
}

// NEW: WiFi connection handlers
void handleJoinInternet() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    joinWiFi(ssid, password);
    
    if (WiFi.status() == WL_CONNECTED) {
      server.send(200, "application/json", "{\"success\":true,\"message\":\"Connected to WiFi: " + ssid + "\",\"ip\":\"" + WiFi.localIP().toString() + "\"}");
    } else {
      server.send(500, "application/json", "{\"success\":false,\"message\":\"Failed to connect to WiFi: " + ssid + "\"}");
    }
  } else {
    server.send(400, "text/plain", "Missing SSID or password");
  }
}

void handleLeaveInternet() {
  leaveWiFi();
  server.send(200, "application/json", "{\"success\":true,\"message\":\"Disconnected from WiFi\"}");
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
  String path = currentDirectory; // Use current directory instead of root
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

  // Add parent directory entry if not in root
  if (path != "/") {
    json += "{";
    json += "\"name\":\"..\",";
    json += "\"size\":0,";
    json += "\"isDirectory\":true,";
    json += "\"path\":\"" + getParentDirectory(path) + "\"";
    json += "}";
    first = false;
  }

  File file = root.openNextFile();
  while (file) {
    if (!first) {
      json += ",";
    }
    first = false;

    json += "{";
    json += "\"name\":\"" + String(file.name()) + "\",";
    json += "\"size\":" + String(file.size()) + ",";
    json += "\"isDirectory\":" + String(file.isDirectory() ? "true" : "false") + ",";
    json += "\"path\":\"" + path + (path.endsWith("/") ? "" : "/") + String(file.name()) + "\"";
    json += "}";

    file = root.openNextFile();
  }
  json += "]";

  root.close();
  server.send(200, "application/json", json);
}

// NEW: Helper function to get parent directory
String getParentDirectory(String path) {
  if (path == "/") return "/";
  
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash == 0) return "/";
  
  return path.substring(0, lastSlash);
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
      path = currentDirectory + (currentDirectory.endsWith("/") ? "" : "/") + path;
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

  // NEW: Add new API endpoints
  server.on("/api/change-directory", HTTP_POST, handleChangeDirectory);
  server.on("/api/current-directory", handleGetCurrentDirectory);
  server.on("/api/detect-os", HTTP_POST, handleDetectOS);
  server.on("/api/use-file", HTTP_POST, handleUseFile);
  server.on("/api/copy-file", HTTP_POST, handleCopyFile);
  server.on("/api/cut-file", HTTP_POST, handleCutFile);
  server.on("/api/paste-file", HTTP_POST, handlePasteFile);
  server.on("/api/join-internet", HTTP_POST, handleJoinInternet);
  server.on("/api/leave-internet", HTTP_POST, handleLeaveInternet);

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
/* OPTIMIZATION 1: Compressed CSS with reduced redundancy */
body{font-family:Arial,sans-serif;margin:20px;background:#1a1a1a;color:#fff;}
.container{max-width:1200px;margin:0 auto;background:#2d2d2d;padding:20px;border-radius:10px;}
h1{color:#4CAF50;text-align:center;margin-bottom:30px;}
.section,.setting-group{margin:20px 0;padding:15px;background:#3d3d3d;border-radius:5px;}
textarea{width:100%;height:300px;background:#1a1a1a;color:#fff;border:1px solid #555;padding:10px;font-family:monospace;font-size:14px;resize:vertical;}
button{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;margin:5px;transition:all 0.3s ease;position:relative;overflow:hidden;}
button:hover{background:#45a049;transform:translateY(-2px);box-shadow:0 5px 15px rgba(0,0,0,0.3);}
button.danger{background:#f44336;}
button.danger:hover{background:#da190b;}
select,input{background:#1a1a1a;color:#fff;border:1px solid #555;padding:8px;margin:5px;border-radius:3px;}
.status{padding:10px;margin:10px 0;border-radius:5px;background:#333;}
.examples{background:#2a2a2a;padding:15px;border-radius:5px;margin:10px 0;}
pre{background:#1a1a1a;padding:10px;border-radius:3px;overflow-x:auto;font-size:12px;}
/* OPTIMIZATION 2: Optimized tab system with flexbox */
.tab{display:flex;flex-wrap:wrap;background:#2d2d2d;}
.tab button{flex:1;min-width:100px;background:#2d2d2d;border:none;padding:12px 16px;cursor:pointer;transition:0.3s;color:#fff;}
.tab button:hover{background:#3d3d3d;transform:none;box-shadow:none;}
.tab button.active{background:#4CAF50;}
.tabcontent{display:none;padding:20px 12px;border:1px solid #555;border-top:none;animation:fadeEffect 0.3s;}
@keyframes fadeEffect{from{opacity:0;} to{opacity:1;}}
/* OPTIMIZATION 3: Enhanced file browser with better scrolling */
.file-list,.history-list,.file-browser{max-height:300px;overflow-y:auto;border:1px solid #555;padding:10px;background:#1a1a1a;border-radius:5px;}
.file-browser{max-height:400px;}
.file-item,.history-item,.file-browser-item{padding:8px;border-bottom:1px solid #333;display:flex;justify-content:space-between;align-items:center;cursor:pointer;transition:all 0.3s ease;}
.file-item:hover,.history-item:hover,.file-browser-item:hover{background:#2a2a2a;transform:translateX(5px);}
.file-buttons,.file-actions{display:flex;gap:5px;}
.file-buttons button,.file-actions button{padding:4px 8px;font-size:11px;margin:0;}
/* OPTIMIZATION 4: Improved responsive controls */
.control-panel{display:flex;gap:10px;flex-wrap:wrap;margin:10px 0;}
.control-btn{flex:1;min-width:120px;}
.setting-row{display:flex;align-items:center;margin:10px 0;gap:10px;flex-wrap:wrap;}
.setting-label{min-width:120px;font-weight:bold;}
/* OPTIMIZATION 5: Optimized modal system */
.modal{display:none;position:fixed;z-index:1000;left:0;top:0;width:100%;height:100%;overflow:auto;background-color:rgba(0,0,0,0.4);animation:fadeIn 0.3s;}
.modal-content{background-color:#2d2d2d;margin:10% auto;padding:20px;border:1px solid #888;width:300px;max-width:90%;border-radius:10px;animation:slideDown 0.3s;}
@keyframes fadeIn{from{opacity:0;} to{opacity:1;}}
@keyframes slideDown{from{transform:translateY(-50px);opacity:0;} to{transform:translateY(0);opacity:1;}}
.close{color:#aaa;float:right;font-size:28px;font-weight:bold;cursor:pointer;transition:color 0.3s;}
.close:hover{color:#fff;transform:scale(1.1);}
/* OPTIMIZATION 6: Enhanced LED and status indicators */
.led-control{display:flex;align-items:center;gap:10px;margin:10px 0;}
.led-status{padding:8px 12px;border-radius:5px;font-weight:bold;transition:all 0.3s ease;}
.led-on{background:#4CAF50;color:white;animation:pulse 2s infinite;}
.led-off{background:#666;color:#ccc;}
.switch{position:relative;display:inline-block;width:60px;height:34px;}
.switch input{opacity:0;width:0;height:0;}
.slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#ccc;transition:.4s;border-radius:34px;}
.slider:before{position:absolute;content:'';height:26px;width:26px;left:4px;bottom:4px;background-color:white;transition:.4s;border-radius:50%;}
input:checked + .slider{background-color:#4CAF50;}
input:checked + .slider:before{transform:translateX(26px);}
.switch-container{display:flex;align-items:center;gap:10px;margin:10px 0;}
/* OPTIMIZATION 7: Optimized grid layouts */
.stats-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px;margin:10px 0;}
.stat-card{background:#2a2a2a;padding:15px;border-radius:5px;text-align:center;transition:all 0.3s ease;}
.stat-card:hover{transform:translateY(-3px);box-shadow:0 5px 15px rgba(0,0,0,0.2);}
.stat-value{font-size:24px;font-weight:bold;color:#4CAF50;}
.stat-label{font-size:12px;color:#ccc;}
/* OPTIMIZATION 8: Enhanced progress and loading */
.progress-bar{width:100%;height:20px;background:#1a1a1a;border-radius:10px;overflow:hidden;margin:10px 0;}
.progress-fill{height:100%;background:#4CAF50;transition:width 0.3s;}
.loading-spinner{display:inline-block;width:20px;height:20px;border:3px solid #f3f3f3;border-top:3px solid #4CAF50;border-radius:50%;animation:spin 1s linear infinite;margin-right:10px;}
@keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}
/* OPTIMIZATION 9: Improved file manager */
.file-manager{background:#2a2a2a;padding:15px;border-radius:5px;margin:10px 0;}
.upload-area{border:2px dashed #555;padding:20px;text-align:center;border-radius:5px;margin:10px 0;cursor:pointer;transition:all 0.3s ease;position:relative;overflow:hidden;}
.upload-area:hover{background:#333;border-color:#4CAF50;transform:scale(1.02);}
.upload-area:hover .folder-icon{animation:bounce 1s infinite;}
.file-size{color:#888;font-size:12px;}
.directory{color:#4CAF50;font-weight:bold;}
.file{color:#fff;}
/* OPTIMIZATION 10: Enhanced upload system */
.upload-progress-container{margin:10px 0;padding:10px;background:#2a2a2a;border-radius:5px;}
.upload-progress-text{display:flex;justify-content:space-between;margin-bottom:5px;}
.upload-progress-bar{width:100%;height:20px;background:#1a1a1a;border-radius:10px;overflow:hidden;}
.upload-progress-fill{height:100%;background:#4CAF50;transition:width 0.3s;width:0%;}
/* Special elements */
.command-help{background:#2a2a2a;padding:10px;border-radius:5px;margin:5px 0;cursor:pointer;transition:all 0.3s ease;}
.command-help:hover{background:#3a3a3a;transform:translateX(5px);}
.command-details{display:none;padding:10px;background:#1a1a1a;border-radius:5px;margin-top:5px;animation:slideDown 0.3s;}
.run-bunny{font-size:18px;margin-right:8px;transition:transform 0.3s;}
button:hover .run-bunny{animation:bounce 0.5s;}
.new-command{background:#2a7a2a !important;border-left:4px solid #4CAF50;}
/* Ripple effect */
.ripple{position:absolute;border-radius:50%;background:rgba(255,255,255,0.7);transform:scale(0);animation:ripple 0.6s linear;pointer-events:none;}
@keyframes ripple{to{transform:scale(4);opacity:0;}}
/* Animations */
@keyframes bounce{0%,20%,53%,80%,100%{transform:translateY(0);}40%,43%{transform:translateY(-8px);}70%{transform:translateY(-4px);}90%{transform:translateY(-2px);}}
@keyframes pulse{0%{box-shadow:0 0 0 0 rgba(76,175,80,0.4);}70%{box-shadow:0 0 0 10px rgba(76,175,80,0);}100%{box-shadow:0 0 0 0 rgba(76,175,80,0);}}
@keyframes float{0%,100%{transform:translateY(0);}50%{transform:translateY(-10px);}}
@keyframes shake{0%,100%{transform:translateX(0);}10%,30%,50%,70%,90%{transform:translateX(-5px);}20%,40%,60%,80%{transform:translateX(5px);}}
/* Folder icon animation */
.folder-icon{font-size:48px;margin-bottom:10px;display:block;transition:all 0.3s ease;}
.upload-area:hover .folder-icon{transform:scale(1.2);}
/* Design customization elements */
.design-selector{background:#2a2a2a;padding:15px;border-radius:5px;margin:10px 0;}
.design-item{padding:10px;border:1px solid #555;margin:5px 0;border-radius:5px;cursor:pointer;transition:all 0.3s;}
.design-item:hover{background:#3a3a3a;border-color:#4CAF50;}
.design-item.active{background:#4CAF50;border-color:#4CAF50;}
/* Mobile optimizations */
@media (max-width: 768px) {
  .tab button{font-size:12px;padding:10px 8px;min-width:80px;}
  .control-btn{min-width:100px;font-size:12px;}
  .setting-row{flex-direction:column;align-items:flex-start;}
  .container{padding:10px;margin:10px;}
  body{margin:10px;}
  .file-buttons,.file-actions{flex-direction:column;gap:2px;}
  .file-buttons button,.file-actions button{font-size:10px;padding:3px 6px;}
}
/* Performance optimizations */
*{-webkit-tap-highlight-color:transparent;-webkit-touch-callout:none;}
.file-browser-item>*{pointer-events:none;} /* Fix for click events */
.file-browser-item{position:relative;}
.file-browser-item .file-actions{pointer-events:auto;} /* Allow button clicks */
</style></head><body>
<div class='container'>
<h1>ESP32-S3 BadUSB</h1>
<div class='tab'>
<button class='tablinks active' onclick="openTab(event, 'ScriptTab')">Script</button>
<button class='tablinks' onclick="openTab(event, 'FilesTab')">Files</button>
<button class='tablinks' onclick="openTab(event, 'FileManagerTab')">File Manager</button>
<button class='tablinks' onclick="openTab(event, 'DesignTab')">Design</button>
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
<button class='control-btn' onclick='executeScript()'><span class='run-bunny'>🐇</span> Run Script</button>
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
<div class='file-list' id='fileList'><div class='file-item'><span><span class='loading-spinner'></span>Loading scripts...</span></div></div>
<br>
<div class='setting-row'>
<input type='text' id='newFilename' placeholder='New script name (without .txt)' style='flex:1'>
<button onclick='saveScript()'>Save Current Script</button>
</div>
</div>
<div id='FileManagerTab' class='tabcontent'>
<h3>File Manager</h3>
<div class='file-manager'>
<h4>Current Directory: <span id='currentDirDisplay'>/</span></h4>
<div class='control-panel'>
<button class='control-btn' onclick='goToParent()'>⬆️ Parent Directory</button>
<button class='control-btn' onclick='refreshFileBrowser()'>🔄 Refresh</button>
<button class='control-btn' onclick='createNewFolder()'>📁 New Folder</button>
<button class='control-btn' onclick='detectOS()'>🔍 Detect OS</button>
</div>
<div class='file-operations'>
<h4>File Operations</h4>
<div class='control-panel'>
<button class='control-btn' onclick='useSelectedFile()'>📄 Use File</button>
<button class='control-btn' onclick='copySelectedFile()'>📋 Copy</button>
<button class='control-btn' onclick='cutSelectedFile()'>✂️ Cut</button>
<button class='control-btn' onclick='pasteFile()'>📁 Paste</button>
</div>
</div>
<div class='upload-area' id='uploadArea'>
<span class='folder-icon'>📁</span>
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
<div class='file-browser' id='fileBrowser'>
<div class='file-browser-item'>
<span><span class='loading-spinner'></span>Loading files...</span>
</div>
</div>
</div>
</div>
<div id='DesignTab' class='tabcontent'>
<h3>Custom Design</h3>
<div class='design-selector'>
<h4>Available Designs</h4>
<div id='designList' class='file-list'>
<div class='file-item'><span><span class='loading-spinner'></span>Loading designs...</span></div>
</div>
<div class='control-panel'>
<button class='control-btn' onclick='refreshDesigns()'>🔄 Refresh Designs</button>
<button class='control-btn' onclick='applySelectedDesign()'>🎨 Apply Design</button>
<button class='control-btn' onclick='resetDesign()'>⚡ Reset to Default</button>
</div>
</div>
<div class='setting-group'>
<h4>Design Information</h4>
<div class='setting-row'>
<span class='setting-label'>Current Design:</span>
<span id='currentDesign'>Default</span>
</div>
<div class='setting-row'>
<span class='setting-label'>Design Status:</span>
<span id='designStatus'>No custom design applied</span>
</div>
</div>
<div class='setting-group'>
<h4>Custom Design Instructions</h4>
<p style='font-size:14px;color:#ccc;line-height:1.5;'>
To use custom designs, create a folder structure on your SD card:<br>
<strong>Designs/</strong> (main folder)<br>
&nbsp;&nbsp;├── <strong>CSS/</strong> (for custom stylesheets)<br>
&nbsp;&nbsp;├── <strong>JS/</strong> (for custom JavaScript)<br>
&nbsp;&nbsp;└── <strong>FONTS/</strong> (for custom fonts)<br><br>
The system will automatically detect and load compatible design files.
</p>
</div>
</div>
<div id='BootTab' class='tabcontent'>
<h3>Boot Script Configuration</h3>
<div class='setting-group'>
<div class='setting-row'>
<span class='setting-label'>Current Boot Script:</span>
<select id='bootScriptSelect' style='flex:1' onchange='previewBootScript()'>
<option value=''>None</option>
</select>
<button onclick='setBootScript()'>Set as Boot</button>
</div>
</div>
<div class='section'>
<h4>Boot Script Preview</h4>
<textarea id='bootScriptPreview' readonly style='height:200px' placeholder='Select a boot script to preview...'></textarea>
<div class='control-panel'>
<button class='control-btn' onclick='loadBootScriptToEditor()'>Load to Editor</button>
<button class='control-btn' onclick='testBootScript()'>Test Script</button>
</div>
</div>
</div>
<div id='SettingsTab' class='tabcontent'>
<h3>Settings</h3>
<div class='setting-group'>
<h4>WiFi Connection</h4>
<div class='setting-row'>
<span class='setting-label'>Join WiFi:</span>
<input type='text' id='wifiSSIDJoin' placeholder='SSID' style='flex:1'>
<input type='password' id='wifiPasswordJoin' placeholder='Password' style='flex:1'>
<button onclick='joinInternet()'>Join</button>
</div>
<div class='setting-row'>
<span class='setting-label'>Current Status:</span>
<span id='wifiStatus'>Not Connected</span>
<button onclick='leaveInternet()'>Leave</button>
</div>
</div>
<div class='setting-group'>
<h4>LED Control</h4>
<div class='led-control'>
<span class='setting-label'>RGB LED Status:</span>
<span id='ledStatus' class='led-status led-on'>ON</span>
<button id='ledToggleBtn' onclick='toggleLED()'>Turn Off</button>
</div>
<p style='font-size:12px;color:#ccc'>LED States: Solid Green = Idle, Fast Blinking Green = Running, Blinking Red = Error/SD Card Removed, Orange Blink = Completion</p>
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
<select id='languageSelect' style='flex:1' onchange='changeLanguage()'>
</select>
<button onclick='saveLanguageSettings()'>Save Language</button>
</div>
</div>
<div class='setting-group'>
<h4>WiFi AP Configuration</h4>
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
<button onclick='saveWiFiSettings()'>Save WiFi Settings</button>
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
<div class='stat-card'>
<div class='stat-value' id='detectedOS'>Unknown</div>
<div class='stat-label'>Detected OS</div>
</div>
</div>
<div class='setting-group'>
<h4>Command History</h4>
<div class='history-list' id='historyList'><div class='history-item'><span class='loading-spinner'></span>Loading history...</div></div>
<div class='control-panel'>
<button class='control-btn' onclick='clearHistory()'>Clear History</button>
<button class='control-btn' onclick='exportHistory()'>Export History</button>
</div>
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
<div class='command-help new-command' onclick='toggleCommandDetails("new")'>NEW Commands</div>
<div id='new-details' class='command-details'>
<pre>CD path             - Change directory
DETECT_OS           - Detect connected OS
USE_FILE path       - Select file for operations
USE_FILES path1 path2 - Select multiple files
COPY_FILE [src] [dest] - Copy file(s)
CUT_FILE [src] [dest]  - Cut file(s)  
PASTE_FILE [dest]   - Paste copied/cut files
JOIN_INTERNET SSID="name" PASSWORD="pass" - Connect to WiFi
LEAVE_INTERNET      - Disconnect from WiFi</pre>
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
<div class='examples'>
<h4>Quick Examples</h4>
<button class='control-btn' onclick='loadExample(1)'>Basic Example</button>
<button class='control-btn' onclick='loadExample(2)'>WiFi Detection</button>
<button class='control-btn' onclick='loadExample(3)'>LED Control</button>
<button class='control-btn' onclick='loadExample(4)'>Repeat Example</button>
<button class='control-btn' onclick='loadExample(5)'>NEW Commands</button>
</div>
</div>
<div class='section'>
<h3>Device Status</h3>
<div id='status' class='status'>Ready - Loading device status...</div>
</div>
<div id='fileOverrideModal' class='modal'>
<div class='modal-content'>
<span class='close' onclick='closeModal()'>&times;</span>
<h3>File Already Exists</h3>
<p id='modalMessage'></p>
<button onclick='overrideFile()'>Overwrite</button>
<button onclick='autoRename()'>Auto Rename</button>
<button onclick='closeModal()'>Cancel</button>
</div>
</div>
<div id='historyModal' class='modal'>
<div class='modal-content'>
<span class='close' onclick='closeHistoryModal()'>&times;</span>
<h3>Command History</h3>
<div class='history-list' id='modalHistoryList'></div>
<button onclick='closeHistoryModal()'>Close</button>
</div>
</div>
<div id='folderModal' class='modal'>
<div class='modal-content'>
<span class='close' onclick='closeFolderModal()'>&times;</span>
<h3>Create New Folder</h3>
<input type='text' id='newFolderName' placeholder='Folder name' style='width:100%;margin:10px 0;'>
<button onclick='createFolder()'>Create Folder</button>
<button onclick='closeFolderModal()'>Cancel</button>
</div>
</div>
<div id='designModal' class='modal'>
<div class='modal-content'>
<span class='close' onclick='closeDesignModal()'>&times;</span>
<h3>Apply Custom Design</h3>
<p id='designModalMessage'>Are you sure you want to apply this design?</p>
<button onclick='confirmDesignApply()'>Apply Design</button>
<button onclick='closeDesignModal()'>Cancel</button>
</div>
</div>
</div>
<script>
// OPTIMIZATION 1: Enhanced performance with variable caching
let pendingFilename = '', pendingContent = '', scriptProgress = 0, progressInterval;
let currentBrowserPath = '/', selectedFile = null, currentStats = null;
let selectedDesign = null, currentDesign = 'Default';

// OPTIMIZATION 2: Ripple effect implementation
function createRipple(event) {
  const button = event.currentTarget;
  const circle = document.createElement('span');
  const diameter = Math.max(button.clientWidth, button.clientHeight);
  const radius = diameter / 2;
  
  circle.style.width = circle.style.height = `${diameter}px`;
  circle.style.left = `${event.clientX - button.getBoundingClientRect().left - radius}px`;
  circle.style.top = `${event.clientY - button.getBoundingClientRect().top - radius}px`;
  circle.classList.add('ripple');
  
  const ripple = button.getElementsByClassName('ripple')[0];
  if (ripple) {
    ripple.remove();
  }
  
  button.appendChild(circle);
  
  // Remove ripple after animation completes
  setTimeout(() => {
    if (circle.parentNode === button) {
      button.removeChild(circle);
    }
  }, 600);
}

// Add ripple effect to all buttons
document.addEventListener('DOMContentLoaded', function() {
  const buttons = document.querySelectorAll('button');
  buttons.forEach(button => {
    button.addEventListener('click', createRipple);
  });
});

// OPTIMIZATION 3: Debounced functions for better performance
function debounce(func, wait) {
  let timeout;
  return function executedFunction(...args) {
    const later = () => { clearTimeout(timeout); func(...args); };
    clearTimeout(timeout);
    timeout = setTimeout(later, wait);
  };
}

// OPTIMIZATION 4: Enhanced tab management with state persistence
function openTab(evt, tabName) {
  const tabcontent = document.getElementsByClassName('tabcontent');
  const tablinks = document.getElementsByClassName('tablinks');
  
  for (let i = 0; i < tabcontent.length; i++) {
    tabcontent[i].style.display = 'none';
  }
  for (let i = 0; i < tablinks.length; i++) {
    tablinks[i].className = tablinks[i].className.replace(' active', '');
  }
  
  document.getElementById(tabName).style.display = 'block';
  evt.currentTarget.className += ' active';
  
  // Load content only when tab is opened
  if (tabName === 'FilesTab') refreshFiles();
  if (tabName === 'BootTab') refreshBootScripts();
  if (tabName === 'StatsTab') updateStats();
  if (tabName === 'FileManagerTab') refreshFileBrowser();
  if (tabName === 'DesignTab') refreshDesigns();
}

// OPTIMIZATION 5: Improved file browser with better directory handling
function refreshFileBrowser() {
  const fileBrowser = document.getElementById('fileBrowser');
  fileBrowser.innerHTML = '<div class="file-browser-item"><span><span class="loading-spinner"></span>Loading files...</span></div>';
  
  fetch('/api/list-files?path=' + encodeURIComponent(currentBrowserPath))
    .then(response => response.json())
    .then(files => {
      fileBrowser.innerHTML = '';
      updateCurrentDirectoryDisplay();
      
      if (!files || files.length === 0) {
        fileBrowser.innerHTML = '<div class="file-browser-item"><span style="color:#666">Directory is empty</span></div>';
        return;
      }
      
      files.forEach(file => {
        const fileItem = createFileBrowserItem(file);
        fileBrowser.appendChild(fileItem);
      });
    })
    .catch(err => {
      fileBrowser.innerHTML = '<div class="file-browser-item"><span style="color:#f44336">Error loading files</span></div>';
      console.error('File browser error:', err);
    });
}

// OPTIMIZATION 6: Fixed directory navigation with proper event handling
function createFileBrowserItem(file) {
  const fileItem = document.createElement('div');
  fileItem.className = `file-browser-item ${file.isDirectory ? 'directory' : 'file'}`;
  
  const fileInfo = document.createElement('div');
  fileInfo.style.flex = '1';
  fileInfo.style.display = 'flex';
  fileInfo.style.flexDirection = 'column';
  
  const fileName = document.createElement('div');
  fileName.textContent = file.name + (file.isDirectory ? '/' : '');
  fileName.style.cursor = 'pointer';
  fileName.style.fontWeight = file.isDirectory ? 'bold' : 'normal';
  fileName.style.color = file.isDirectory ? '#4CAF50' : '#fff';
  
  const fileSize = document.createElement('div');
  fileSize.className = 'file-size';
  fileSize.textContent = file.isDirectory ? 'Directory' : formatFileSize(file.size);
  
  fileInfo.appendChild(fileName);
  fileInfo.appendChild(fileSize);
  
  const fileActions = document.createElement('div');
  fileActions.className = 'file-actions';
  
  // FIX: Proper event handling for directory navigation
  if (file.isDirectory) {
    fileName.onclick = (e) => {
      e.stopPropagation();
      navigateToDirectory(file.path);
    };
    
    const openBtn = document.createElement('button');
    openBtn.textContent = 'Open';
    openBtn.onclick = (e) => {
      e.stopPropagation();
      navigateToDirectory(file.path);
    };
    
    const deleteBtn = document.createElement('button');
    deleteBtn.textContent = 'Delete';
    deleteBtn.className = 'danger';
    deleteBtn.onclick = (e) => {
      e.stopPropagation();
      deleteBrowserFile(file.path);
    };
    
    fileActions.appendChild(openBtn);
    fileActions.appendChild(deleteBtn);
  } else {
    fileName.onclick = (e) => {
      e.stopPropagation();
      selectFileInBrowser(file);
    };
    
    const useBtn = document.createElement('button');
    useBtn.textContent = 'Use';
    useBtn.onclick = (e) => {
      e.stopPropagation();
      useFile(file.path);
    };
    
    const downloadBtn = document.createElement('button');
    downloadBtn.textContent = 'Download';
    downloadBtn.onclick = (e) => {
      e.stopPropagation();
      downloadFile(file.path);
    };
    
    const deleteBtn = document.createElement('button');
    deleteBtn.textContent = 'Delete';
    deleteBtn.className = 'danger';
    deleteBtn.onclick = (e) => {
      e.stopPropagation();
      deleteBrowserFile(file.path);
    };
    
    fileActions.appendChild(useBtn);
    fileActions.appendChild(downloadBtn);
    fileActions.appendChild(deleteBtn);
  }
  
  // File item selection
  fileItem.onclick = (e) => {
    if (!e.target.matches('button')) {
      selectFileInBrowser(file);
    }
  };
  
  fileItem.appendChild(fileInfo);
  fileItem.appendChild(fileActions);
  return fileItem;
}

// OPTIMIZATION 7: Enhanced directory navigation
function navigateToDirectory(path) {
  currentBrowserPath = path;
  refreshFileBrowser();
}

function goToParent() {
  if (currentBrowserPath === '/') return;
  
  const pathParts = currentBrowserPath.split('/').filter(part => part);
  pathParts.pop();
  const parentPath = pathParts.length > 0 ? '/' + pathParts.join('/') : '/';
  
  currentBrowserPath = parentPath;
  refreshFileBrowser();
}

function updateCurrentDirectoryDisplay() {
  document.getElementById('currentDirDisplay').textContent = currentBrowserPath;
}

// OPTIMIZATION 8: Improved file operations with better feedback
function selectFileInBrowser(file) {
  // Remove previous selection
  document.querySelectorAll('.file-browser-item').forEach(item => {
    item.style.background = '';
  });
  
  // Highlight selected file
  event.currentTarget.style.background = '#2a2a2a';
  selectedFile = file;
  
  // Update status
  document.getElementById('status').textContent = `Selected: ${file.name}`;
}

function useFile(filePath) {
  fetch('/api/use-file', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ file: filePath })
  })
  .then(response => response.json())
  .then(data => {
    document.getElementById('status').textContent = data.message;
  })
  .catch(err => {
    document.getElementById('status').textContent = 'Error using file: ' + err;
  });
}

// OPTIMIZATION 9: Enhanced utility functions
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
      document.getElementById('status').textContent = result;
      refreshFileBrowser();
    })
    .catch(err => {
      document.getElementById('status').textContent = 'Delete failed: ' + err;
    });
  }
}

// OPTIMIZATION 10: Enhanced Design System
function refreshDesigns() {
  const designList = document.getElementById('designList');
  designList.innerHTML = '<div class="file-item"><span><span class="loading-spinner"></span>Loading designs...</span></div>';
  
  fetch('/api/list-designs')
    .then(response => response.json())
    .then(designs => {
      designList.innerHTML = '';
      
      if (!designs || designs.length === 0) {
        designList.innerHTML = '<div class="file-item"><span style="color:#666">No custom designs found</span></div>';
        return;
      }
      
      designs.forEach(design => {
        const designItem = document.createElement('div');
        designItem.className = 'design-item';
        if (design.name === currentDesign) {
          designItem.classList.add('active');
        }
        
        designItem.innerHTML = `
          <div style="display:flex; justify-content:space-between; align-items:center;">
            <div>
              <strong>${design.name}</strong>
              <div style="font-size:12px; color:#ccc;">${design.type} • ${design.files} files</div>
            </div>
            <div style="font-size:12px; color:#4CAF50;">${design.status}</div>
          </div>
        `;
        
        designItem.onclick = () => {
          document.querySelectorAll('.design-item').forEach(item => {
            item.classList.remove('active');
          });
          designItem.classList.add('active');
          selectedDesign = design.name;
        };
        
        designList.appendChild(designItem);
      });
    })
    .catch(err => {
      designList.innerHTML = '<div class="file-item"><span style="color:#f44336">Error loading designs</span></div>';
      console.error('Design loading error:', err);
    });
}

function applySelectedDesign() {
  if (!selectedDesign) {
    alert('Please select a design first');
    return;
  }
  
  document.getElementById('designModalMessage').textContent = `Are you sure you want to apply the "${selectedDesign}" design?`;
  document.getElementById('designModal').style.display = 'block';
}

function confirmDesignApply() {
  fetch('/api/apply-design', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ design: selectedDesign })
  })
  .then(response => response.json())
  .then(data => {
    document.getElementById('status').textContent = data.message;
    document.getElementById('currentDesign').textContent = selectedDesign;
    document.getElementById('designStatus').textContent = 'Custom design applied';
    document.getElementById('designStatus').style.color = '#4CAF50';
    currentDesign = selectedDesign;
    closeDesignModal();
    
    // Reload the page to apply the design
    setTimeout(() => {
      window.location.reload();
    }, 2000);
  })
  .catch(err => {
    document.getElementById('status').textContent = 'Design application failed: ' + err;
    closeDesignModal();
  });
}

function resetDesign() {
  if (confirm('Reset to default design?')) {
    fetch('/api/reset-design', { method: 'POST' })
    .then(response => response.json())
    .then(data => {
      document.getElementById('status').textContent = data.message;
      document.getElementById('currentDesign').textContent = 'Default';
      document.getElementById('designStatus').textContent = 'Default design active';
      currentDesign = 'Default';
      
      // Reload the page to apply default design
      setTimeout(() => {
        window.location.reload();
      }, 2000);
    })
    .catch(err => {
      document.getElementById('status').textContent = 'Design reset failed: ' + err;
    });
  }
}

function closeDesignModal() {
  document.getElementById('designModal').style.display = 'none';
}

// OPTIMIZATION 11: Improved script execution with better progress tracking
function executeScript() {
  const script = document.getElementById('scriptArea').value.trim();
  if (!script) { 
    alert('Please enter a script'); 
    return; 
  }
  
  document.getElementById('status').textContent = 'Executing script...';
  const progressBar = document.getElementById('progressBar');
  const progressFill = document.getElementById('progressFill');
  
  progressBar.style.display = 'block';
  scriptProgress = 0;
  updateProgress(0);
  
  progressInterval = setInterval(() => {
    scriptProgress = Math.min(scriptProgress + Math.random() * 5, 90);
    updateProgress(scriptProgress);
  }, 200);
  
  fetch('/execute', { method: 'POST', body: script })
    .then(response => {
      if (!response.ok) throw new Error('Network error');
      return response.text();
    })
    .then(data => { 
      clearInterval(progressInterval);
      updateProgress(100);
      setTimeout(() => {
        progressBar.style.display = 'none';
        document.getElementById('status').textContent = 'Script executed successfully';
      }, 1000);
    })
    .catch(err => { 
      clearInterval(progressInterval);
      progressBar.style.display = 'none';
      document.getElementById('status').textContent = 'Script execution failed: ' + err;
    });
}

function updateProgress(percent) {
  document.getElementById('progressFill').style.width = percent + '%';
}

// OPTIMIZATION 12: Enhanced status updates with caching
function updateStats() {
  if (currentStats && Date.now() - (currentStats.timestamp || 0) < 2000) {
    displayStats(currentStats);
    return;
  }
  
  fetch('/api/stats')
    .then(response => response.json())
    .then(data => {
      currentStats = { ...data, timestamp: Date.now() };
      displayStats(data);
    })
    .catch(err => {
      console.error('Stats update failed:', err);
    });
}

function displayStats(data) {
  if (!data) return;
  
  const elements = {
    errorCount: data.errorCount,
    scriptCount: data.scriptCount,
    languageCount: data.languageCount,
    clientCount: data.clientCount,
    totalScripts: data.totalScripts,
    totalCommands: data.totalCommands,
    detectedOS: data.detectedOS || 'Unknown',
    lastError: data.lastError || 'None',
    sdStatus: data.sdCardPresent ? 'OK' : 'REMOVED',
    uptime: data.uptime + 's',
    freeMemory: data.freeMemory
  };
  
  Object.keys(elements).forEach(key => {
    const element = document.getElementById(key);
    if (element) element.textContent = elements[key];
  });
}

// File operations for selected file
function useSelectedFile() {
  if (!selectedFile) {
    alert('Please select a file first');
    return;
  }
  useFile(selectedFile.path);
}

function copySelectedFile() {
  if (!selectedFile) {
    alert('Please select a file first');
    return;
  }
  
  fetch('/api/copy-file', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ source: selectedFile.path })
  })
  .then(response => response.json())
  .then(data => {
    document.getElementById('status').textContent = data.message;
  })
  .catch(err => {
    document.getElementById('status').textContent = 'Copy failed: ' + err;
  });
}

function cutSelectedFile() {
  if (!selectedFile) {
    alert('Please select a file first');
    return;
  }
  
  fetch('/api/cut-file', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ source: selectedFile.path })
  })
  .then(response => response.json())
  .then(data => {
    document.getElementById('status').textContent = data.message;
  })
  .catch(err => {
    document.getElementById('status').textContent = 'Cut failed: ' + err;
  });
}

function pasteFile() {
  fetch('/api/paste-file', { method: 'POST' })
  .then(response => response.json())
  .then(data => {
    document.getElementById('status').textContent = data.message;
    refreshFileBrowser();
  })
  .catch(err => {
    document.getElementById('status').textContent = 'Paste failed: ' + err;
  });
}

// Folder operations
function createNewFolder() {
  document.getElementById('folderModal').style.display = 'block';
  document.getElementById('newFolderName').value = '';
  document.getElementById('newFolderName').focus();
}

function closeFolderModal() {
  document.getElementById('folderModal').style.display = 'none';
}

function createFolder() {
  const folderName = document.getElementById('newFolderName').value.trim();
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
    document.getElementById('status').textContent = result;
    closeFolderModal();
    refreshFileBrowser();
  })
  .catch(err => {
    document.getElementById('status').textContent = 'Error creating folder: ' + err;
  });
}

// OS Detection
function detectOS() {
  document.getElementById('status').textContent = 'Detecting OS...';
  
  fetch('/api/detect-os', { method: 'POST' })
  .then(response => response.json())
  .then(data => {
    document.getElementById('status').textContent = 'OS Detected: ' + data.detectedOS;
    updateStats();
  })
  .catch(err => {
    document.getElementById('status').textContent = 'OS detection failed: ' + err;
  });
}

// WiFi Connection
function joinInternet() {
  const ssid = document.getElementById('wifiSSIDJoin').value.trim();
  const password = document.getElementById('wifiPasswordJoin').value;
  
  if (!ssid) {
    alert('Please enter SSID');
    return;
  }
  
  document.getElementById('status').textContent = 'Connecting to WiFi...';
  
  fetch('/api/join-internet', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid: ssid, password: password })
  })
  .then(response => response.json())
  .then(data => {
    document.getElementById('status').textContent = data.message;
    if (data.success) {
      document.getElementById('wifiStatus').textContent = 'Connected to ' + ssid;
      document.getElementById('wifiStatus').style.color = '#4CAF50';
    }
  })
  .catch(err => {
    document.getElementById('status').textContent = 'WiFi connection failed: ' + err;
  });
}

function leaveInternet() {
  document.getElementById('status').textContent = 'Disconnecting from WiFi...';
  
  fetch('/api/leave-internet', { method: 'POST' })
  .then(response => response.json())
  .then(data => {
    document.getElementById('status').textContent = data.message;
    document.getElementById('wifiStatus').textContent = 'Not Connected';
    document.getElementById('wifiStatus').style.color = '#f44336';
  })
  .catch(err => {
    document.getElementById('status').textContent = 'Disconnect failed: ' + err;
  });
}

// File Upload Handling with improved UX
document.getElementById('fileInput').addEventListener('change', function(e) {
  Array.from(e.target.files).forEach(uploadFile);
});

document.getElementById('uploadArea').addEventListener('click', function() {
  document.getElementById('fileInput').click();
});

document.getElementById('uploadArea').addEventListener('dragover', function(e) {
  e.preventDefault();
  this.style.borderColor = '#4CAF50';
  this.style.background = '#333';
});

document.getElementById('uploadArea').addEventListener('dragleave', function(e) {
  e.preventDefault();
  this.style.borderColor = '#555';
  this.style.background = '';
});

document.getElementById('uploadArea').addEventListener('drop', function(e) {
  e.preventDefault();
  this.style.borderColor = '#555';
  this.style.background = '';
  Array.from(e.dataTransfer.files).forEach(uploadFile);
});

function uploadFile(file) {
  const formData = new FormData();
  const uploadPath = currentBrowserPath + (currentBrowserPath.endsWith('/') ? '' : '/') + file.name;
  formData.append('file', file, uploadPath);
  
  const progress = document.getElementById('uploadProgress');
  const fileName = document.getElementById('uploadFileName');
  const percentage = document.getElementById('uploadPercentage');
  const progressFill = document.getElementById('uploadProgressFill');
  
  progress.style.display = 'block';
  fileName.textContent = file.name;
  percentage.textContent = '0%';
  progressFill.style.width = '0%';
  
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/upload', true);
  
  xhr.upload.onprogress = function(e) {
    if (e.lengthComputable) {
      const percentComplete = (e.loaded / e.total) * 100;
      progressFill.style.width = percentComplete + '%';
      percentage.textContent = Math.round(percentComplete) + '%';
    }
  };
  
  xhr.onload = function() {
    if (xhr.status === 200) {
      document.getElementById('status').textContent = 'Uploaded: ' + file.name;
      setTimeout(() => {
        progress.style.display = 'none';
        progressFill.style.width = '0%';
        percentage.textContent = '0%';
      }, 2000);
      refreshFileBrowser();
    } else {
      document.getElementById('status').textContent = 'Upload failed: ' + xhr.responseText;
    }
  };
  
  xhr.onerror = function() {
    document.getElementById('status').textContent = 'Upload failed: Network error';
    progress.style.display = 'none';
  };
  
  xhr.send(formData);
}

// Initialize on load
window.addEventListener('load', function() {
  refreshFiles();
  refreshBootScripts();
  updateStats();
  refreshFileBrowser();
  refreshDesigns();
  
  // Load available languages
  fetch('/api/languages')
    .then(response => response.json())
    .then(languages => {
      const select = document.getElementById('languageSelect');
      select.innerHTML = '';
      languages.forEach(lang => {
        const option = document.createElement('option');
        option.value = lang;
        option.textContent = lang;
        select.appendChild(option);
      });
    })
    .catch(err => console.error('Failed to load languages:', err));
  
  // Regular status updates
  setInterval(updateStats, 5000);
  setInterval(() => {
    fetch('/status')
      .then(r => r.text())
      .then(data => {
        const statusEl = document.getElementById('status');
        if (!statusEl.textContent.includes('Executing') && !data.includes('Script Running')) {
          statusEl.textContent = data;
        }
      })
      .catch(err => console.error('Status update failed:', err));
  }, 3000);
});

// Modal handlers
window.onclick = function(event) {
  const modals = ['fileOverrideModal', 'historyModal', 'folderModal', 'designModal'];
  modals.forEach(modalId => {
    const modal = document.getElementById(modalId);
    if (event.target == modal) {
      modal.style.display = 'none';
    }
  });
}

// Include all other existing functions (toggleCommandDetails, stopScript, validateScript, etc.)
function toggleCommandDetails(id) {
  const details = document.getElementById(id + '-details');
  details.style.display = details.style.display === 'block' ? 'none' : 'block';
}

function stopScript() {
  if (progressInterval) clearInterval(progressInterval);
  document.getElementById('progressBar').style.display = 'none';
  fetch('/stop', { method: 'POST' })
    .then(response => response.text())
    .then(data => { 
      document.getElementById('status').textContent = 'Script stopped';
    })
    .catch(err => {
      document.getElementById('status').textContent = 'Stop failed: ' + err;
    });
}

function validateScript() {
  const script = document.getElementById('scriptArea').value;
  if (!script.trim()) { 
    alert('Please enter a script'); 
    return; 
  }
  
  fetch('/api/validate-script', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ script: script })
  })
  .then(response => response.text())
  .then(result => {
    document.getElementById('status').textContent = result;
  })
  .catch(err => {
    document.getElementById('status').textContent = 'Validation failed: ' + err;
  });
}

function changeLanguage() {
  const lang = document.getElementById('languageSelect').value;
  fetch('/language?lang=' + lang)
    .then(response => response.text())
    .then(data => { 
      document.getElementById('currentLang').textContent = lang;
      document.getElementById('status').textContent = 'Language changed to: ' + lang;
    })
    .catch(err => {
      document.getElementById('status').textContent = 'Language change failed: ' + err;
    });
}

function saveLanguageSettings() {
  fetch('/api/save-settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ type: 'language' })
  })
  .then(response => response.text())
  .then(data => { 
    document.getElementById('status').textContent = 'Language settings saved'; 
  })
  .catch(err => {
    document.getElementById('status').textContent = 'Save failed: ' + err;
  });
}

function saveWiFiSettings() {
  const ssid = document.getElementById('wifiSSID').value;
  const password = document.getElementById('wifiPassword').value;
  const scanTime = document.getElementById('wifiScanTime').value;
  
  if (!ssid || !password) { 
    alert('Please enter both SSID and password'); 
    return; 
  }
  
  fetch('/api/save-wifi', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid: ssid, password: password, scanTime: scanTime })
  })
  .then(response => response.text())
  .then(data => { 
    document.getElementById('status').textContent = 'WiFi settings saved. Rebooting...';
    setTimeout(() => { window.location.reload(); }, 2000);
  })
  .catch(err => {
    document.getElementById('status').textContent = 'Save failed: ' + err;
  });
}

function quickAction(action) {
  document.getElementById('status').textContent = 'Executing: ' + action;
  fetch('/execute', { method: 'POST', body: action })
    .then(response => response.text())
    .then(data => { 
      document.getElementById('status').textContent = 'Executed: ' + action; 
    })
    .catch(err => {
      document.getElementById('status').textContent = 'Action failed: ' + err;
    });
}

function typeText() {
  const text = prompt('Enter text to type:');
  if (text) { 
    quickAction('STRING ' + text); 
  }
}

function clearScript() {
  document.getElementById('scriptArea').value = '';
}

function loadExample(num) {
  const examples = {
    1: 'REM Basic script example\nDELAY 1000\nGUI r\nDELAY 500\nSTRING notepad\nENTER\nDELAY 1000\nSTRING Hello World!',
    2: 'REM WiFi detection example\nIF_PRESENT SSID="MyWiFi"\nSTRING WiFi Found!\nENTER\nELSE\nSTRING WiFi Not Found!\nENTER\nENDIF',
    3: 'REM LED control example\nLED 255 0 0\nDELAY 1000\nLED 0 255 0\nDELAY 1000\nLED 0 0 255\nDELAY 1000\nRGB OFF',
    4: 'REM Repeat command example\nREPEAT 5 STRING Hello World!\nENTER\nREPEAT 10 DELAY 100 STRING Test',
    5: 'REM NEW: File operations example\nCD /scripts\nUSE_FILE script.txt\nCOPY_FILE script.txt /backups/\nDETECT_OS\nJOIN_INTERNET SSID="MyWiFi" PASSWORD="mypassword"\nDELAY 5000\nLEAVE_INTERNET'
  };
  document.getElementById('scriptArea').value = examples[num] || '';
  openTab(event, 'ScriptTab');
}

function refreshFiles() {
  fetch('/api/scripts')
    .then(response => response.json())
    .then(files => {
      const fileList = document.getElementById('fileList');
      fileList.innerHTML = '';
      
      if (!files || files.length === 0) {
        fileList.innerHTML = '<div class="file-item"><span style="color:#666">No script files found</span></div>';
        return;
      }
      
      files.forEach(file => {
        const fileItem = document.createElement('div');
        fileItem.className = 'file-item';
        
        const fileName = document.createElement('span');
        fileName.textContent = file;
        
        const buttons = document.createElement('div');
        buttons.className = 'file-buttons';
        
        const loadBtn = document.createElement('button');
        loadBtn.textContent = 'Load';
        loadBtn.onclick = () => loadFile(file);
        
        const deleteBtn = document.createElement('button');
        deleteBtn.textContent = 'Delete';
        deleteBtn.className = 'danger';
        deleteBtn.onclick = () => deleteFile(file);
        
        buttons.appendChild(loadBtn);
        buttons.appendChild(deleteBtn);
        fileItem.appendChild(fileName);
        fileItem.appendChild(buttons);
        fileList.appendChild(fileItem);
      });
    })
    .catch(err => {
      document.getElementById('fileList').innerHTML = '<div class="file-item"><span style="color:#f44336">Error loading files</span></div>';
    });
}

function refreshBootScripts() {
  fetch('/api/scripts')
    .then(response => response.json())
    .then(files => {
      const select = document.getElementById('bootScriptSelect');
      const currentValue = select.value;
      select.innerHTML = '<option value="">None</option>';
      
      if (files && files.length > 0) {
        files.forEach(file => {
          const option = document.createElement('option');
          option.value = file;
          option.textContent = file;
          if (file === currentValue) option.selected = true;
          select.appendChild(option);
        });
      }
    })
    .catch(err => {
      console.error('Failed to load boot scripts:', err);
    });
}

function previewBootScript() {
  const filename = document.getElementById('bootScriptSelect').value;
  const preview = document.getElementById('bootScriptPreview');
  
  if (!filename) {
    preview.value = '';
    preview.placeholder = 'Select a boot script to preview...';
    return;
  }
  
  fetch('/api/load?file=' + encodeURIComponent(filename))
    .then(response => response.text())
    .then(content => {
      preview.value = content;
    })
    .catch(err => {
      preview.value = '';
      preview.placeholder = 'Error loading script: ' + err;
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
    document.getElementById('status').textContent = result;
    previewBootScript();
  })
  .catch(err => {
    document.getElementById('status').textContent = 'Set boot script failed: ' + err;
  });
}

function loadBootScriptToEditor() {
  const content = document.getElementById('bootScriptPreview').value;
  if (content) {
    document.getElementById('scriptArea').value = content;
    openTab(event, 'ScriptTab');
    document.getElementById('status').textContent = 'Boot script loaded to editor';
  }
}

function testBootScript() {
  const filename = document.getElementById('bootScriptSelect').value;
  if (!filename) { 
    alert('Select a boot script first'); 
    return; 
  }
  
  fetch('/api/test-boot-script', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ filename: filename })
  })
  .then(response => response.text())
  .then(result => {
    document.getElementById('status').textContent = result;
  })
  .catch(err => {
    document.getElementById('status').textContent = 'Test failed: ' + err;
  });
}

function loadFile(filename) {
  fetch('/api/load?file=' + encodeURIComponent(filename))
    .then(response => response.text())
    .then(content => {
      document.getElementById('scriptArea').value = content;
      document.getElementById('status').textContent = 'Loaded: ' + filename;
      openTab(event, 'ScriptTab');
    })
    .catch(err => {
      document.getElementById('status').textContent = 'Load failed: ' + err;
    });
}

function deleteFile(filename) {
  if (confirm('Delete ' + filename + '?')) {
    fetch('/api/delete?file=' + encodeURIComponent(filename), { method: 'DELETE' })
      .then(response => response.text())
      .then(result => {
        document.getElementById('status').textContent = result;
        refreshFiles();
        refreshBootScripts();
      })
      .catch(err => {
        document.getElementById('status').textContent = 'Delete failed: ' + err;
      });
  }
}

function saveScriptPrompt() {
  const filename = prompt('Enter filename (without .txt):');
  if (filename) { 
    saveScriptAs(filename); 
  }
}

function saveScriptAs(filename) {
  const script = document.getElementById('scriptArea').value.trim();
  if (!script) { 
    alert('Script is empty'); 
    return; 
  }
  
  if (!filename.endsWith('.txt')) {
    filename += '.txt';
  }
  
  checkFileExists(filename).then(exists => {
    if (exists) {
      pendingFilename = filename;
      pendingContent = script;
      document.getElementById('modalMessage').textContent = 'File "' + filename + '" already exists. What would you like to do?';
      document.getElementById('fileOverrideModal').style.display = 'block';
    } else {
      doSaveScript(filename, script);
    }
  }).catch(err => {
    document.getElementById('status').textContent = 'Save failed: ' + err;
  });
}

function checkFileExists(filename) {
  return fetch('/api/check-file?file=' + encodeURIComponent(filename))
    .then(response => response.json())
    .then(data => data.exists);
}

function doSaveScript(filename, content) {
  fetch('/api/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ filename: filename, content: content })
  })
  .then(response => response.text())
  .then(result => {
    document.getElementById('status').textContent = result;
    refreshFiles();
    refreshBootScripts();
  })
  .catch(err => {
    document.getElementById('status').textContent = 'Save failed: ' + err;
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
  
  const trySave = () => {
    checkFileExists(newName).then(exists => {
      if (exists && counter < 100) {
        counter++;
        newName = baseName + counter + '.txt';
        trySave();
      } else {
        doSaveScript(newName, pendingContent);
      }
    });
  };
  
  trySave();
  closeModal();
}

function closeModal() {
  document.getElementById('fileOverrideModal').style.display = 'none';
  pendingFilename = '';
  pendingContent = '';
}

function saveScript() {
  const filename = document.getElementById('newFilename').value.trim();
  if (!filename) { 
    alert('Enter filename first'); 
    return; 
  }
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
      document.getElementById('status').textContent = 'LED ' + (data.ledEnabled ? 'enabled' : 'disabled');
    })
    .catch(err => {
      document.getElementById('status').textContent = 'LED toggle failed: ' + err;
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
    document.getElementById('status').textContent = 'Logging ' + (isEnabled ? 'enabled' : 'disabled');
  })
  .catch(err => {
    document.getElementById('status').textContent = 'Logging toggle failed: ' + err;
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
      document.getElementById('status').textContent = result;
    })
    .catch(err => {
      document.getElementById('status').textContent = 'Self-destruct failed: ' + err;
    });
  }
}

function clearErrorLog() {
  if (confirm('Clear error log and reset error counter?')) {
    fetch('/api/clear-errors', { method: 'POST' })
      .then(response => response.text())
      .then(result => {
        document.getElementById('status').textContent = result;
        updateStats();
      })
      .catch(err => {
        document.getElementById('status').textContent = 'Clear errors failed: ' + err;
      });
  }
}

function factoryReset() {
  if (confirm('Factory reset will restore all defaults. Continue?')) {
    fetch('/api/factory-reset', { method: 'POST' })
      .then(response => response.text())
      .then(result => {
        document.getElementById('status').textContent = result;
        setTimeout(() => { window.location.reload(); }, 2000);
      })
      .catch(err => {
        document.getElementById('status').textContent = 'Factory reset failed: ' + err;
      });
  }
}

function loadFromHistory() {
  fetch('/api/history')
    .then(response => response.json())
    .then(history => {
      const modalList = document.getElementById('modalHistoryList');
      modalList.innerHTML = '';
      
      if (history && history.length > 0) {
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
      } else {
        modalList.innerHTML = '<div class="history-item"><span style="color:#666">No history available</span></div>';
      }
      
      document.getElementById('historyModal').style.display = 'block';
    })
    .catch(err => {
      console.error('Failed to load history:', err);
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
        document.getElementById('status').textContent = result;
        updateStats();
      })
      .catch(err => {
        document.getElementById('status').textContent = 'Clear history failed: ' + err;
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
    })
    .catch(err => {
      document.getElementById('status').textContent = 'Export failed: ' + err;
    });
}
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
    status += " - Detected OS: " + detectedOS;
    status += " - Current Dir: " + currentDirectory;
    if (scriptRunning) status += " - Script Running";
    if (bootModeEnabled) status += " - Boot: " + currentBootScriptFile;
    if (WiFi.status() == WL_CONNECTED) status += " - WiFi: " + WiFi.SSID();
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
    doc["detectedOS"] = detectedOS;

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

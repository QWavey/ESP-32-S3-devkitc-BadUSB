// THE EDIT COMMANDS ETC ARE NOT WORKING AND I AM STILL TRYING FIXING THEM AND IMPLEMENTING SOMETHING IN THEM
// This code includes new animations for the Web Interface and a couple of optimizations for the WEB GUI!
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
#define USE_NEOPIXEL 0

#if USE_NEOPIXEL
#include <Adafruit_NeoPixel.h>
#else
// Dummy constants
#define NEO_GRB 0
#define NEO_KHZ800 0

// Dummy class to replace Adafruit_NeoPixel
class Adafruit_NeoPixel {
public:
    Adafruit_NeoPixel(int n, int pin, int type) {}
    void begin() {}
    void show() {}
    void setPixelColor(int n, int r, int g, int b) {}
    void setPixelColor(int n, uint32_t color) {}
    void setBrightness(uint8_t brightness) {}
    uint32_t Color(uint8_t r, uint8_t g, uint8_t b) { return 0; }
};
#endif

// SD Card pin configuration
#define SD_CS_PIN 17
#define SD_MOSI_PIN 1
#define SD_MISO_PIN 3
#define SD_SCK_PIN 2

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
    if (!sdCardPresent) {
        server.send(500, "text/plain", "SD Card not present");
        return;
    }
    
    String filePath = "/index.html";
    if (!SD.exists(filePath)) {
        server.send(404, "text/plain", "index.html not found on SD card");
        return;
    }
    
    File file = SD.open(filePath);
    if (!file) {
        server.send(500, "text/plain", "Failed to open index.html");
        return;
    }
    
    server.streamFile(file, "text/html");
    file.close();
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

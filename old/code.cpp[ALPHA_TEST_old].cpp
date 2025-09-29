#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <vector>
#include <map>

// SD Card pin configuration
#define SD_CS_PIN 10
#define SD_MOSI_PIN 11
#define SD_MISO_PIN 13
#define SD_SCK_PIN 12

// WiFi AP configuration
const char* ap_ssid = "EspGuard";
const char* ap_password = "987654321";

// Web server
WebServer server(80);

// USB HID Keyboard
USBHIDKeyboard keyboard;

// Global variables
std::vector<String> availableLanguages;
std::vector<String> availableScripts;
std::map<String, String> currentKeymap;
String currentLanguage = "us";
int defaultDelay = 0;
std::map<String, String> variables;
String lastCommand = "";
int repeatCount = 0;

// LED control (built-in RGB LED)
#define LED_PIN 48

struct KeyCode {
  uint8_t modifier;
  uint8_t key;
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize built-in LED
  pinMode(LED_PIN, OUTPUT);
  setLED(0, 255, 0); // Green - starting up
  
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
  if (!loadLanguage("us")) {
    Serial.println("Failed to load default language");
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
  WiFi.softAP(ap_ssid, ap_password);
  
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

void pressKey(String key) {
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
    
    delay(50);
    keyboard.releaseAll();
  }
}

void pressKeyCombination(std::vector<String> keys) {
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
  
  delay(50);
  keyboard.releaseAll();
}

void pressKeySequentially(std::vector<String> keys) {
  for (String key : keys) {
    pressKey(key);
    if (defaultDelay > 0) {
      delay(defaultDelay);
    } else {
      delay(50);
    }
  }
}

void typeString(String text) {
  for (int i = 0; i < text.length(); i++) {
    String ch = String(text.charAt(i));
    
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
      
      delay(20);
      keyboard.releaseAll();
      delay(20);
    }
  }
}

void executeScript(String script) {
  std::vector<String> lines;
  int startIndex = 0;
  int endIndex = script.indexOf('\n');
  
  // Split script into lines
  while (endIndex != -1) {
    lines.push_back(script.substring(startIndex, endIndex));
    startIndex = endIndex + 1;
    endIndex = script.indexOf('\n', startIndex);
  }
  if (startIndex < script.length()) {
    lines.push_back(script.substring(startIndex));
  }
  
  // Execute each line
  for (String line : lines) {
    line.trim();
    if (line.length() == 0 || line.startsWith("REM")) {
      continue;
    }
    
    Serial.println("Executing: " + line);
    
    // Handle REPEAT/REPLAY
    if (repeatCount > 0) {
      executeCommand(lastCommand);
      repeatCount--;
      continue;
    }
    
    executeCommand(line);
  }
}

void executeCommand(String line) {
  lastCommand = line; // Store for REPEAT function
  
  if (line.startsWith("DEFAULTDELAY ") || line.startsWith("DEFAULT_DELAY ")) {
    int spaceIndex = line.indexOf(' ');
    defaultDelay = line.substring(spaceIndex + 1).toInt();
  }
  else if (line.startsWith("DELAY ")) {
    int delayTime = line.substring(6).toInt();
    delay(delayTime);
  }
  else if (line.startsWith("STRING ")) {
    String text = line.substring(7);
    // Replace variables in string
    for (auto& pair : variables) {
      text.replace(pair.first, pair.second);
    }
    typeString(text);
  }
  else if (line.startsWith("REPEAT ") || line.startsWith("REPLAY ")) {
    int spaceIndex = line.indexOf(' ');
    repeatCount = line.substring(spaceIndex + 1).toInt() - 1; // -1 because we already executed once
  }
  else if (line.startsWith("LOCALE ")) {
    String locale = line.substring(7);
    loadLanguage(locale);
  }
  else if (line.startsWith("LED ")) {
    // Parse RGB values
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
    // Handle raw keycode
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
      // Handle modifier byte
      uint8_t modifier = keycodes[0];
      uint8_t keyCode = keycodes[1];
      
      // Press modifier keys
      if (modifier & 0x01) keyboard.press(KEY_LEFT_CTRL);
      if (modifier & 0x02) keyboard.press(KEY_LEFT_SHIFT);
      if (modifier & 0x04) keyboard.press(KEY_LEFT_ALT);
      if (modifier & 0x08) keyboard.press(KEY_LEFT_GUI);
      if (modifier & 0x10) keyboard.press(KEY_RIGHT_CTRL);
      if (modifier & 0x20) keyboard.press(KEY_RIGHT_SHIFT);
      if (modifier & 0x40) keyboard.press(KEY_RIGHT_ALT);
      if (modifier & 0x80) keyboard.press(KEY_RIGHT_GUI);
      
      // Press main key
      if (keyCode > 0) {
        keyboard.pressRaw(keyCode);
      }
      
      delay(50);
      keyboard.releaseAll();
    }
  }
  else if (line.startsWith("VAR ")) {
    // Variable declaration VAR VARNAME = VALUE
    String varLine = line.substring(4);
    int equalIndex = varLine.indexOf('=');
    if (equalIndex > 0) {
      String varName = varLine.substring(0, equalIndex);
      String varValue = varLine.substring(equalIndex + 1);
      varName.trim();
      varValue.trim();
      variables[varName] = varValue;
    }
  }
  else if (line.indexOf('=') > 0 && !line.startsWith("STRING")) {
    // Variable assignment or math operation
    if (isMathExpression(line)) {
      processMathOperation(line);
    } else {
      // Simple variable assignment
      int equalIndex = line.indexOf('=');
      String varName = line.substring(0, equalIndex);
      String varValue = line.substring(equalIndex + 1);
      varName.trim();
      varValue.trim();
      
      // Replace variables in value
      for (auto& pair : variables) {
        varValue.replace(pair.first, pair.second);
      }
      
      variables[varName] = varValue;
    }
  }
  else {
    // Handle key combinations and sequences
    handleKeyInput(line);
  }
}

bool isMathExpression(String line) {
  return (line.indexOf('+') > 0 || line.indexOf('-') > 0 || 
          line.indexOf('*') > 0 || line.indexOf('/') > 0 ||
          line.indexOf('×') > 0 || line.indexOf('÷') > 0) &&
         line.indexOf('=') > 0;
}

void handleKeyInput(String line) {
  std::vector<String> keys;
  int startIdx = 0;
  int spaceIdx = line.indexOf(' ');
  
  // Parse keys
  while (spaceIdx != -1) {
    keys.push_back(line.substring(startIdx, spaceIdx));
    startIdx = spaceIdx + 1;
    spaceIdx = line.indexOf(' ', startIdx);
  }
  if (startIdx < line.length()) {
    keys.push_back(line.substring(startIdx));
  }
  
  // Check if it's a sequence (separate lines) or combination (same line)
  if (keys.size() == 1) {
    // Single key
    pressKey(keys[0]);
  } else {
    // Multiple keys - check if they should be pressed together or sequentially
    // In BadUSB, keys on same line = simultaneous, different lines = sequential
    pressKeyCombination(keys);
  }
}

void processMathOperation(String line) {
  int equalIndex = line.indexOf('=');
  if (equalIndex == -1) return;
  
  String leftSide = line.substring(0, equalIndex);
  String rightSide = line.substring(equalIndex + 1);
  leftSide.trim();
  rightSide.trim();
  
  // Replace variables with their values in the expression
  for (auto& pair : variables) {
    rightSide.replace(pair.first, pair.second);
  }
  
  // Parse and evaluate the expression
  float result = evaluateExpression(rightSide);
  
  // Store result as variable
  variables[leftSide] = String(result, 0); // Store as integer string
  
  Serial.println("Math result: " + leftSide + " = " + String(result, 0));
}

float evaluateExpression(String expr) {
  expr.trim();
  
  // Handle addition
  int plusIndex = expr.lastIndexOf('+');
  if (plusIndex > 0) {
    float left = evaluateExpression(expr.substring(0, plusIndex));
    float right = evaluateExpression(expr.substring(plusIndex + 1));
    return left + right;
  }
  
  // Handle subtraction
  int minusIndex = expr.lastIndexOf('-');
  if (minusIndex > 0) {
    float left = evaluateExpression(expr.substring(0, minusIndex));
    float right = evaluateExpression(expr.substring(minusIndex + 1));
    return left - right;
  }
  
  // Handle multiplication (both * and ×)
  int multIndex = expr.lastIndexOf('*');
  if (multIndex == -1) multIndex = expr.lastIndexOf('×');
  if (multIndex > 0) {
    float left = evaluateExpression(expr.substring(0, multIndex));
    float right = evaluateExpression(expr.substring(multIndex + 1));
    return left * right;
  }
  
  // Handle division (both / and ÷)
  int divIndex = expr.lastIndexOf('/');
  if (divIndex == -1) divIndex = expr.lastIndexOf('÷');
  if (divIndex > 0) {
    float left = evaluateExpression(expr.substring(0, divIndex));
    float right = evaluateExpression(expr.substring(divIndex + 1));
    if (right != 0) {
      return left / right;
    }
    return 0;
  }
  
  // Handle parentheses
  int openParen = expr.indexOf('(');
  int closeParen = expr.lastIndexOf(')');
  if (openParen >= 0 && closeParen > openParen) {
    String beforeParen = expr.substring(0, openParen);
    String insideParen = expr.substring(openParen + 1, closeParen);
    String afterParen = expr.substring(closeParen + 1);
    
    float parenResult = evaluateExpression(insideParen);
    String newExpr = beforeParen + String(parenResult, 0) + afterParen;
    return evaluateExpression(newExpr);
  }
  
  // Replace variables with their values
  for (auto& pair : variables) {
    if (expr.equals(pair.first)) {
      return pair.second.toFloat();
    }
  }
  
  // Return as number
  return expr.toFloat();
}

void setLED(int r, int g, int b) {
  // This is a simplified LED control - actual implementation depends on your LED setup
  analogWrite(LED_PIN, (r + g + b) / 3);
}

void setupWebServer() {
  // Set UTF-8 encoding for all responses
  server.enableCORS(true);
  
  // Serve main page
  server.on("/", []() {
    String html = "<!DOCTYPE html><html><head><title>ESP32-S3 BadUSB</title>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body{font-family:Arial,sans-serif;margin:20px;background:#1a1a1a;color:#fff}";
    html += ".container{max-width:1000px;margin:0 auto;background:#2d2d2d;padding:20px;border-radius:10px}";
    html += "h1{color:#4CAF50;text-align:center;margin-bottom:30px}";
    html += ".section{margin:20px 0;padding:15px;background:#3d3d3d;border-radius:5px}";
    html += "textarea{width:100%;height:200px;background:#1a1a1a;color:#fff;border:1px solid #555;padding:10px;font-family:monospace}";
    html += "button{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;margin:5px}";
    html += "button:hover{background:#45a049}";
    html += "select{background:#1a1a1a;color:#fff;border:1px solid #555;padding:5px;margin:5px}";
    html += "input{background:#1a1a1a;color:#fff;border:1px solid #555;padding:5px;margin:5px}";
    html += ".status{padding:10px;margin:10px 0;border-radius:5px;background:#333}";
    html += ".examples{background:#2a2a2a;padding:15px;border-radius:5px;margin:10px 0}";
    html += "pre{background:#1a1a1a;padding:10px;border-radius:3px;overflow-x:auto}";
    html += ".tab{overflow:hidden;border:1px solid #555;background-color:#2d2d2d}";
    html += ".tab button{background-color:inherit;float:left;border:none;outline:none;cursor:pointer;padding:14px 16px;transition:0.3s}";
    html += ".tab button:hover{background-color:#3d3d3d}";
    html += ".tab button.active{background-color:#4CAF50}";
    html += ".tabcontent{display:none;padding:6px 12px;border:1px solid #555;border-top:none;animation:fadeEffect 1s}";
    html += "@keyframes fadeEffect{from{opacity:0;} to{opacity:1;}}";
    html += ".file-list{max-height:200px;overflow-y:auto;border:1px solid #555;padding:10px;background:#1a1a1a}";
    html += ".file-item{padding:5px;cursor:pointer;border-bottom:1px solid #333}";
    html += ".file-item:hover{background:#2a2a2a}";
    html += "</style></head><body>";
    
    html += "<div class='container'>";
    html += "<h1>🔌 ESP32-S3 BadUSB Control Panel</h1>";
    
    // Tab navigation
    html += "<div class='tab'>";
    html += "<button class='tablinks active' onclick=\"openTab(event, 'ScriptTab')\">💻 Script</button>";
    html += "<button class='tablinks' onclick=\"openTab(event, 'FilesTab')\">📁 Files</button>";
    html += "<button class='tablinks' onclick=\"openTab(event, 'SettingsTab')\">⚙️ Settings</button>";
    html += "<button class='tablinks' onclick=\"openTab(event, 'ExamplesTab')\">📚 Examples</button>";
    html += "</div>";
    
    // Script Tab
    html += "<div id='ScriptTab' class='tabcontent' style='display:block'>";
    html += "<h3>💻 Script Execution</h3>";
    html += "<textarea id='scriptArea' placeholder='Enter your BadUSB script here...&#10;&#10;Example:&#10;DELAY 1000&#10;GUI r&#10;DELAY 500&#10;STRING notepad&#10;ENTER&#10;DELAY 1000&#10;STRING Hello World!'></textarea>";
    html += "<br>";
    html += "<button onclick='executeScript()'>Execute Script</button>";
    html += "<button onclick='clearScript()'>Clear</button>";
    html += "<button onclick='saveScriptPrompt()'>Save Script</button>";
    html += "</div>";
    
    // Files Tab
    html += "<div id='FilesTab' class='tabcontent'>";
    html += "<h3>📁 Script Files</h3>";
    html += "<div class='file-list' id='fileList'>Loading...</div>";
    html += "<br>";
    html += "<input type='text' id='newFilename' placeholder='New script name (without .txt)'>";
    html += "<button onclick='saveScript()'>Save Current Script</button>";
    html += "<button onclick='refreshFiles()'>Refresh List</button>";
    html += "</div>";
    
    // Settings Tab
    html += "<div id='SettingsTab' class='tabcontent'>";
    html += "<h3>⚙️ Settings</h3>";
    
    // Language selection
    html += "<div class='section'>";
    html += "<h4>🌍 Language Selection</h4>";
    html += "Current: <span id='currentLang'>" + currentLanguage + "</span>";
    html += "<select id='languageSelect' onchange='changeLanguage()'>";
    for (String lang : availableLanguages) {
      html += "<option value='" + lang + "'";
      if (lang == currentLanguage) html += " selected";
      html += ">" + lang + "</option>";
    }
    html += "</select>";
    html += "</div>";
    
    // Quick actions
    html += "<div class='section'>";
    html += "<h4>⚡ Quick Actions</h4>";
    html += "<button onclick='quickAction(\"GUI r\")'>Win+R</button>";
    html += "<button onclick='quickAction(\"CTRL ALT DEL\")'>Ctrl+Alt+Del</button>";
    html += "<button onclick='quickAction(\"ALT F4\")'>Alt+F4</button>";
    html += "<button onclick='quickAction(\"CTRL SHIFT ESC\")'>Task Manager</button>";
    html += "<button onclick='typeText()'>Type Custom Text</button>";
    html += "</div>";
    html += "</div>";
    
    // Examples Tab
    html += "<div id='ExamplesTab' class='tabcontent'>";
    html += "<h3>📚 Script Examples</h3>";
    html += "<div class='examples'>";
    
    html += "<h4>Windows Run Dialog (Sequential):</h4>";
    html += "<pre>REM Press Windows key, then R key\nWINDOWS\nr</pre>";
    html += "<button onclick='loadExample(1)'>Load Example</button>";
    
    html += "<h4>Windows Run Dialog (Simultaneous):</h4>";
    html += "<pre>REM Press Windows+R together\nWINDOWS r</pre>";
    html += "<button onclick='loadExample(2)'>Load Example</button>";
    
    html += "<h4>GUI Alternative:</h4>";
    html += "<pre>GUI r\nDELAY 500\nSTRING notepad\nENTER</pre>";
    html += "<button onclick='loadExample(3)'>Load Example</button>";
    
    html += "<h4>Math Variables:</h4>";
    html += "<pre>VAR1 = 10\nVAR2 = 5\nRESULT = VAR1 + VAR2\nSTRING The result is: \nSTRING RESULT</pre>";
    html += "<button onclick='loadExample(4)'>Load Example</button>";
    
    html += "<h4>Repeat Command:</h4>";
    html += "<pre>STRING Hello World!\nENTER\nREPEAT 3</pre>";
    html += "<button onclick='loadExample(5)'>Load Example</button>";
    
    html += "<h4>Numpad Keys:</h4>";
    html += "<pre>NUMLOCK\nNUM_1 NUM_2 NUM_3\nNUM_PLUS\nNUM_4 NUM_5 NUM_6\nNUM_ENTER</pre>";
    html += "<button onclick='loadExample(6)'>Load Example</button>";
    html += "</div>";
    html += "</div>";
    
    // Status
    html += "<div class='section'>";
    html += "<h3>📊 Status</h3>";
    html += "<div id='status' class='status'>Ready</div>";
    html += "</div>";
    
    html += "</div>";
    
    // JavaScript
    html += "<script>";
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
    html += "}";
    
    html += "function executeScript() {";
    html += "  const script = document.getElementById('scriptArea').value;";
    html += "  if (!script.trim()) { alert('Please enter a script'); return; }";
    html += "  document.getElementById('status').innerHTML = 'Executing script...';";
    html += "  fetch('/execute', { method: 'POST', body: script })";
    html += "  .then(response => response.text())";
    html += "  .then(data => { document.getElementById('status').innerHTML = 'Script executed: ' + data; })";
    html += "  .catch(err => { document.getElementById('status').innerHTML = 'Error: ' + err; });";
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
    html += "    1: 'REM Press Windows key, then R key\\nWINDOWS\\nr',";
    html += "    2: 'REM Press Windows+R together\\nWINDOWS r',";
    html += "    3: 'GUI r\\nDELAY 500\\nSTRING notepad\\nENTER',";
    html += "    4: 'VAR1 = 10\\nVAR2 = 5\\nRESULT = VAR1 + VAR2\\nSTRING The result is: \\nSTRING RESULT',";
    html += "    5: 'STRING Hello World!\\nENTER\\nREPEAT 3',";
    html += "    6: 'NUMLOCK\\nNUM_1 NUM_2 NUM_3\\nNUM_PLUS\\nNUM_4 NUM_5 NUM_6\\nNUM_ENTER'";
    html += "  };";
    html += "  document.getElementById('scriptArea').value = examples[num];";
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
    html += "      fileItem.innerHTML = file + ' ';";
    html += "      ";
    html += "      const loadBtn = document.createElement('button');";
    html += "      loadBtn.innerHTML = 'Load';";
    html += "      loadBtn.onclick = () => loadFile(file);";
    html += "      loadBtn.style.padding = '2px 5px';";
    html += "      loadBtn.style.fontSize = '12px';";
    html += "      ";
    html += "      const deleteBtn = document.createElement('button');";
    html += "      deleteBtn.innerHTML = 'Delete';";
    html += "      deleteBtn.onclick = () => deleteFile(file);";
    html += "      deleteBtn.style.padding = '2px 5px';";
    html += "      deleteBtn.style.fontSize = '12px';";
    html += "      deleteBtn.style.background = '#f44336';";
    html += "      ";
    html += "      fileItem.appendChild(loadBtn);";
    html += "      fileItem.appendChild(deleteBtn);";
    html += "      fileList.appendChild(fileItem);";
    html += "    });";
    html += "    if (files.length === 0) {";
    html += "      fileList.innerHTML = 'No script files found';";
    html += "    }";
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
    html += "    });";
    html += "  }";
    html += "}";
    
    html += "function saveScriptPrompt() {";
    html += "  const filename = prompt('Enter filename (without .txt):');";
    html += "  if (filename) { saveScriptAs(filename); }";
    html += "}";
    
    html += "function saveScriptAs(filename) {";
    html += "  const script = document.getElementById('scriptArea').value;";
    html += "  if (!script.trim()) { alert('Script is empty'); return; }";
    html += "  ";
    html += "  fetch('/api/save', {";
    html += "    method: 'POST',";
    html += "    headers: { 'Content-Type': 'application/json' },";
    html += "    body: JSON.stringify({ filename: filename, content: script })";
    html += "  })";
    html += "  .then(response => response.text())";
    html += "  .then(result => {";
    html += "    document.getElementById('status').innerHTML = result;";
    html += "    refreshFiles();";
    html += "  });";
    html += "}";
    
    html += "function saveScript() {";
    html += "  const filename = document.getElementById('newFilename').value;";
    html += "  if (!filename) { alert('Enter filename first'); return; }";
    html += "  saveScriptAs(filename);";
    html += "}";
    
    html += "setInterval(() => {";
    html += "  fetch('/status').then(r => r.text()).then(data => {";
    html += "    if (data !== document.getElementById('status').innerHTML) {";
    html += "      document.getElementById('status').innerHTML = data;";
    html += "    }";
    html += "  });";
    html += "}, 2000);";
    
    html += "// Load files on page load";
    html += "window.onload = function() {";
    html += "  refreshFiles();";
    html += "};";
    
    html += "</script>";
    html += "</body></html>";
    
    server.send(200, "text/html; charset=utf-8", html);
  });
  
  // Execute script endpoint
  server.on("/execute", HTTP_POST, []() {
    String script = server.arg("plain");
    executeScript(script);
    server.send(200, "text/plain; charset=utf-8", "OK");
  });
  
  // Change language endpoint
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
  
  // Status endpoint
  server.on("/status", []() {
    String status = "Ready - Language: " + currentLanguage + " - Scripts: " + String(availableScripts.size());
    server.send(200, "text/plain; charset=utf-8", status);
  });
  
  // API endpoint for available languages
  server.on("/api/languages", []() {
    String json = "[";
    for (int i = 0; i < availableLanguages.size(); i++) {
      if (i > 0) json += ",";
      json += "\"" + availableLanguages[i] + "\"";
    }
    json += "]";
    server.send(200, "application/json; charset=utf-8", json);
  });
  
  // API endpoint for available scripts
  server.on("/api/scripts", []() {
    String json = "[";
    for (int i = 0; i < availableScripts.size(); i++) {
      if (i > 0) json += ",";
      json += "\"" + availableScripts[i] + "\"";
    }
    json += "]";
    server.send(200, "application/json; charset=utf-8", json);
  });
  
  // API endpoint to load a script
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
  
  // API endpoint to save a script
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
      server.send(200, "text/plain; charset=utf-8", "Script saved: " + filename);
    } else {
      server.send(500, "text/plain; charset=utf-8", "Failed to save script");
    }
  });
  
  // API endpoint to delete a script
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
  
  server.begin();
  Serial.println("Web server started");
}

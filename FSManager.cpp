#include "FSManager.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include "LEDManager.h"
#include "LogManager.h"
#include <ArduinoJson.h>

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
  if (!SD.exists(DIR_LANGUAGES)) {
    if (!SD.mkdir(DIR_LANGUAGES)) {
      Serial.println("Failed to create languages directory");
      return false;
    }
  }
  if (!SD.exists(DIR_SCRIPTS)) {
    if (!SD.mkdir(DIR_SCRIPTS)) {
      Serial.println("Failed to create scripts directory");
      return false;
    }
  }
  if (!SD.exists(DIR_LOGS)) {
    if (!SD.mkdir(DIR_LOGS)) {
      Serial.println("Failed to create logs directory");
    }
  }
  if (!SD.exists(DIR_UPLOADS)) {
    if (!SD.mkdir(DIR_UPLOADS)) {
      Serial.println("Failed to create uploads directory");
    }
  }

  Serial.println("SD Card initialized successfully");
  return true;
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

void loadAvailableLanguages() {
  if (!sdCardPresent) {
    Serial.println("[FS] Language discovery skipped: SD not present");
    return;
  }

  Serial.println("[FS] Scanning for languages in: " + String(DIR_LANGUAGES));
  File root = SD.open(DIR_LANGUAGES);
  if (!root) {
    Serial.println("[FS] FAILED to open languages directory: " + String(DIR_LANGUAGES));
    logDebug("Discovery: Failed to open " + String(DIR_LANGUAGES));
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
        Serial.println("[FS] Discovered language: " + langName);
      } else {
        Serial.println("[FS] Ignoring non-JSON file: " + fileName);
      }
    }
    file = root.openNextFile();
  }
  root.close();
  Serial.println("[FS] Discovery complete. Total languages found: " + String(availableLanguages.size()));
}

void loadAvailableScripts() {
  if (!sdCardPresent) return;

  File root = SD.open(DIR_SCRIPTS);
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

  String filePath = String(DIR_LANGUAGES) + "/" + language + ".json";
  File file = SD.open(filePath);

  if (!file) {
    Serial.println("Failed to open language file: " + filePath);
    lastError = "Language file not found: " + language;
    errorCount++;
    return false;
  }

  DynamicJsonDocument doc(16384);
  DeserializationError error = deserializeJson(doc, file);
  file.close();

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

  String filePath = String(DIR_SCRIPTS) + "/" + filename;
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

  String filePath = String(DIR_SCRIPTS) + "/" + filename;

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

  String filePath = String(DIR_SCRIPTS) + "/" + filename;

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

void changeDirectory(String path) {
  if (!sdCardPresent) {
    Serial.println("SD card not present");
    return;
  }

  if (path.startsWith("./")) {
    path = currentDirectory + path.substring(2);
  } else if (path == "..") {
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
    if (currentDirectory.endsWith("/")) {
      path = currentDirectory + path;
    } else {
      path = currentDirectory + "/" + path;
    }
  }

  path.replace("//", "/");
  
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

void useFile(String filePath) {
  if (!sdCardPresent) {
    Serial.println("SD card not present");
    return;
  }

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

void useFiles(std::vector<String> filePaths) {
  if (!sdCardPresent) {
    Serial.println("SD card not present");
    return;
  }

  selectedFiles.clear();
  for (String filePath : filePaths) {
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

void copyFile(String sourcePath, String destPath) {
  if (!sdCardPresent) {
    Serial.println("SD card not present");
    return;
  }

  if (sourcePath == "" && !selectedFiles.empty()) {
    sourcePath = selectedFiles[0];
  }

  if (!sourcePath.startsWith("/")) {
    sourcePath = currentDirectory + sourcePath;
  }
  if (destPath != "" && !destPath.startsWith("/")) {
    destPath = currentDirectory + destPath;
  }

  if (destPath == "") {
    copiedFilePath = sourcePath;
    fileCopied = true;
    fileCut = false;
    Serial.println("File marked for copy: " + sourcePath);
  } else {
    if (copySDFile(sourcePath, destPath)) {
      Serial.println("File copied: " + sourcePath + " -> " + destPath);
    } else {
      Serial.println("Failed to copy file: " + sourcePath);
    }
  }
}

void cutFile(String sourcePath, String destPath) {
  if (!sdCardPresent) {
    Serial.println("SD card not present");
    return;
  }

  if (sourcePath == "" && !selectedFiles.empty()) {
    sourcePath = selectedFiles[0];
  }

  if (!sourcePath.startsWith("/")) {
    sourcePath = currentDirectory + sourcePath;
  }
  if (destPath != "" && !destPath.startsWith("/")) {
    destPath = currentDirectory + destPath;
  }

  if (destPath == "") {
    cutFilePath = sourcePath;
    fileCut = true;
    fileCopied = false;
    Serial.println("File marked for cut: " + sourcePath);
  } else {
    if (moveSDFile(sourcePath, destPath)) {
      Serial.println("File moved: " + sourcePath + " -> " + destPath);
    } else {
      Serial.println("Failed to move file: " + sourcePath);
    }
  }
}

void pasteFile(String destPath) {
  if (!sdCardPresent) {
    Serial.println("SD card not present");
    return;
  }

  if (destPath != "" && !destPath.startsWith("/")) {
    destPath = currentDirectory + destPath;
  }

  if (destPath == "") {
    destPath = currentDirectory;
  }

  if (fileCopied && copiedFilePath != "") {
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

String getFileNameFromPath(String path) {
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash != -1) {
    return path.substring(lastSlash + 1);
  }
  return path;
}

String getParentDirectory(String path) {
  if (path == "/") return "/";
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash == 0) return "/";
  return path.substring(0, lastSlash);
}

bool copySDFile(String sourcePath, String destPath) {
  File sourceFile = SD.open(sourcePath, FILE_READ);
  if (!sourceFile) {
    Serial.println("Failed to open source file: " + sourcePath);
    return false;
  }

  File destTest = SD.open(destPath);
  if (destTest && destTest.isDirectory()) {
    if (!destPath.endsWith("/")) {
      destPath += "/";
    }
    destPath += getFileNameFromPath(sourcePath);
  }
  destTest.close();

  if (SD.exists(destPath)) {
    SD.remove(destPath);
  }

  File destFile = SD.open(destPath, FILE_WRITE);
  if (!destFile) {
    Serial.println("Failed to create destination file: " + destPath);
    sourceFile.close();
    return false;
  }

  uint8_t buffer[512];
  size_t bytesRead;
  while ((bytesRead = sourceFile.read(buffer, sizeof(buffer))) > 0) {
    destFile.write(buffer, bytesRead);
  }

  sourceFile.close();
  destFile.close();

  return true;
}

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

bool downloadFileFromURL(String url, String path) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected - cannot download file");
    return false;
  }

  // Ensure parent directory exists
  String parentDir = getParentDirectory(path);
  if (parentDir != "" && parentDir != "/") {
    ensureDirectoryExists(parentDir);
  }

  HTTPClient http;
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(10000); // 10s timeout
  
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND) {
    int len = http.getSize();
    WiFiClient * stream = http.getStreamPtr();

    File file = SD.open(path, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file for writing: " + path);
      http.end();
      return false;
    }

    uint8_t buff[512] = { 0 };
    while (http.connected() && (len > 0 || len == -1)) {
      size_t size = stream->available();
      if (size) {
        int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
        file.write(buff, c);
        if (len > 0) len -= c;
      }
      delay(1);
    }

    file.close();
    http.end();
    Serial.println("File downloaded successfully: " + path);
    return true;
  } else {
    Serial.printf("[HTTP] GET... failed, error: %s (code %d)\n", http.errorToString(httpCode).c_str(), httpCode);
    http.end();
    return false;
  }
}

bool ensureDirectoryExists(String path) {
  if (SD.exists(path)) return true;
  
  // Recursively create parent if needed
  String parent = getParentDirectory(path);
  if (parent != "" && parent != "/" && !SD.exists(parent)) {
    ensureDirectoryExists(parent);
  }
  
  Serial.println("Creating directory: " + path);
  return SD.mkdir(path);
}
bool uploadFileToServer(String localPath, String remoteUrl) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[FS] WiFi not connected - cannot upload file");
    return false;
  }

  if (!SD.exists(localPath)) {
    Serial.println("[FS] Local file not found: " + localPath);
    return false;
  }

  File file = SD.open(localPath, FILE_READ);
  if (!file) {
    Serial.println("[FS] Failed to open local file for reading: " + localPath);
    return false;
  }

  HTTPClient http;
  http.begin(remoteUrl);
  
  // Use a simple POST with the file stream
  int httpCode = http.sendRequest("POST", &file, file.size());

  if (httpCode > 0) {
    Serial.printf("[HTTP] POST... code: %d\n", httpCode);
    String payload = http.getString();
    Serial.println("[HTTP] Response: " + payload);
    file.close();
    http.end();
    return (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED);
  } else {
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
    file.close();
    http.end();
    return false;
  }
}

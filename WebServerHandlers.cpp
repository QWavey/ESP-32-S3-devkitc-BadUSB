#include "WebServerHandlers.h"
#include "FSManager.h"
#include "DuckyInterpreter.h"
#include "WiFiManager.h"
#include "LogManager.h"
#include "LEDManager.h"
#include <ArduinoJson.h>

void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    uploadFilename = upload.filename;
    Serial.println("File upload start: " + uploadFilename);
    
    String uploadPath;
    if (uploadFilename.endsWith(".txt")) {
      uploadPath = String(DIR_SCRIPTS) + "/" + uploadFilename;
    } else if (uploadFilename.endsWith(".json")) {
      uploadPath = String(DIR_LANGUAGES) + "/" + uploadFilename;
    } else {
      uploadPath = String(DIR_UPLOADS) + "/" + uploadFilename;
    }
    
    if (uploadFile) {
      uploadFile.close();
    }
    
    if (SD.exists(uploadPath)) {
      SD.remove(uploadPath);
    }
    
    uploadFile = SD.open(uploadPath, FILE_WRITE);
    if (!uploadFile) {
      Serial.println("Failed to create file: " + uploadPath);
      return;
    }
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      size_t bytesWritten = uploadFile.write(upload.buf, upload.currentSize);
      if (bytesWritten != upload.currentSize) {
        Serial.println("File write error: " + String(bytesWritten) + " vs " + String(upload.currentSize));
      }
    }
    
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.flush();
      uploadFile.close();
      Serial.println("File upload complete: " + uploadFilename + " size: " + String(upload.totalSize));
      
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
      String uploadPath = String(DIR_UPLOADS) + "/" + uploadFilename;
      if (SD.exists(uploadPath)) {
        SD.remove(uploadPath);
      }
    }
  }
}

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

void handleChangeDirectory() {
  if (server.hasArg("path")) {
    String path = server.arg("path");
    changeDirectory(path);
    server.send(200, "application/json", "{\"success\":true,\"currentDirectory\":\"" + currentDirectory + "\"}");
  } else {
    server.send(400, "text/plain", "No path specified");
  }
}

void handleGetCurrentDirectory() {
  server.send(200, "application/json", "{\"currentDirectory\":\"" + currentDirectory + "\"}");
}

void handleDetectOS() {
  detectOS();
  server.send(200, "application/json", "{\"detectedOS\":\"" + detectedOS + "\"}");
}

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

void handleListFiles() {
  String path = currentDirectory;
  if (server.hasArg("path")) {
    path = server.arg("path");
    if (path.indexOf("..") >= 0) {
      server.send(400, "text/plain", "Invalid path");
      return;
    }
  }
  
  if (!path.startsWith("/")) path = "/" + path;

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
    if (!first) json += ",";
    first = false;

    String name = String(file.name());
    if (name.lastIndexOf('/') >= 0) {
      name = name.substring(name.lastIndexOf('/') + 1);
    }

    json += "{";
    json += "\"name\":\"" + name + "\",";
    json += "\"size\":" + String(file.size()) + ",";
    json += "\"isDirectory\":" + String(file.isDirectory() ? "true" : "false") + ",";
    json += "\"path\":\"" + path + (path.endsWith("/") ? "" : "/") + name + "\"";
    json += "}";

    file = root.openNextFile();
  }
  json += "]";

  root.close();
  server.send(200, "application/json", json);
}

void handleDeleteFile() {
  if (server.hasArg("file")) {
    String filename = server.arg("file");
    
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

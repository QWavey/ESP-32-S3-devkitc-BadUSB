#include "LogManager.h"

void openLogFile() {
  if (!sdCardPresent || !loggingEnabled) return;

  if (logFileOpen) {
    logFile.close();
  }

  logFile = SD.open(FILE_LOG, FILE_APPEND);
  if (logFile) {
    logFileOpen = true;
    logFile.println(LOG_SESSION_START_MARKER);
    logFile.println("Timestamp: " + String(millis()));
    logFile.println("Device: ESP32-S3 BadUSB");
    logFile.flush();
  }
}

void closeLogFile() {
  if (logFileOpen) {
    logFile.println(LOG_SESSION_END_MARKER);
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

// Always-on verbose debug log — written to /logs/debug.txt regardless of loggingEnabled
// Rotates at 64KB to avoid filling SD card
void logDebug(String message) {
  if (!sdCardPresent) return;

  // Rotate if too large
  File check = SD.open(FILE_DEBUG);
  if (check && check.size() > 65536) {
    check.close();
    SD.remove(FILE_DEBUG);
    File init = SD.open(FILE_DEBUG, FILE_WRITE);
    if (init) { init.println("[DEBUG LOG ROTATED]"); init.close(); }
  } else {
    if (check) check.close();
  }

  File f = SD.open(FILE_DEBUG, FILE_APPEND);
  if (f) {
    f.println("[" + String(millis()) + "] " + message);
    f.flush();
    f.close();
  }
}

void loadCommandHistory() {
  if (!sdCardPresent) return;

  if (SD.exists(FILE_HISTORY)) {
    File file = SD.open(FILE_HISTORY);
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

      if (commandHistory.size() > MAX_HISTORY_SIZE) {
        commandHistory.erase(commandHistory.begin(), commandHistory.begin() + (commandHistory.size() - MAX_HISTORY_SIZE));
      }
    }
  }
}

void saveCommandHistory() {
  if (!sdCardPresent) return;

  if (SD.exists(FILE_HISTORY)) {
    SD.remove(FILE_HISTORY);
  }

  File file = SD.open(FILE_HISTORY, FILE_WRITE);
  if (file) {
    for (String cmd : commandHistory) {
      file.println(cmd);
    }
    file.close();
  }
}

void addToHistory(String command) {
  commandHistory.push_back(command);

  if (commandHistory.size() > MAX_HISTORY_SIZE) {
    commandHistory.erase(commandHistory.begin());
  }

  saveCommandHistory();
}

void clearErrors() {
  errorCount = 0;
  lastError = "No Errors";
  totalCommandsExecuted = 0; // Optional: Reset command counter too if desired, user said "Clear Error Log"
  Serial.println("[Log] Errors cleared");
  logDebug("Errors cleared by user");
}

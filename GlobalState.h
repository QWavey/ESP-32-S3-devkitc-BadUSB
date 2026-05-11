#ifndef GLOBAL_STATE_H
#define GLOBAL_STATE_H

#include <Arduino.h>
#include <WebServer.h>
#include <USBHIDKeyboard.h>
#include <Preferences.h>
#include <SD.h>
#include <vector>
#include <map>
#include <Adafruit_NeoPixel.h>
#include <USB.h>
#include "Config.h"

// Hardware instances
extern Adafruit_NeoPixel pixels;
extern WebServer server;
extern USBHIDKeyboard keyboard;
extern Preferences preferences;
extern ESPUSB USB;

// WiFi & AP
extern String ap_ssid;
extern String ap_password;
extern std::vector<String> availableSSIDs;
extern int wifiScanTime;
extern bool autoConnectEnabled;
extern bool saveOnConnectEnabled;
extern int buttonPin;
extern bool buttonState;

// Scripting & Execution
extern std::vector<String> availableLanguages;
extern std::vector<String> availableScripts;
extern std::map<String, String> currentKeymap;
extern String currentLanguage;
extern int defaultDelay;
extern int delayBetweenKeys;
extern std::map<String, String> variables;
extern String lastCommand;
extern bool scriptRunning;
extern bool stopRequested;
extern bool bootModeEnabled;
extern String bootScript;
extern std::vector<String> currentBootScriptFiles;
extern unsigned long lastExecutionTime;
extern int executionDelay;
extern unsigned long scriptStartTime;
extern int currentLineNum;
extern bool holdTillStringActive;

// LED State
extern bool ledEnabled;
extern bool blinkingEnabled;
extern unsigned long lastBlinkTime;
extern bool blinkState;
extern int blinkInterval;
extern int ledMode;
extern uint8_t currentR, currentG, currentB;
extern int completionBlinkCount;
extern unsigned long lastCompletionBlinkTime;
extern bool completionBlinkState;

// Conditional Execution
extern bool skipConditionalBlock;
extern String skipUntilSSID;

// File System
extern bool sdCardPresent;
extern unsigned long lastSDCheck;
extern String currentDirectory;
extern String copiedFilePath;
extern String cutFilePath;
extern bool fileCopied;
extern bool fileCut;
extern std::vector<String> selectedFiles;

// Logging & History
extern bool loggingEnabled;
extern bool logFileOpen;
extern File logFile;
extern std::vector<String> commandHistory;

// Statistics & Monitoring
extern unsigned long lastStatusUpdate;
extern int errorCount;
extern String lastError;
extern unsigned long totalScriptsExecuted;
extern unsigned long totalCommandsExecuted;
extern String detectedOS;

// File Upload
extern File uploadFile;
extern String uploadFilename;

struct RowerState {
  std::vector<String> payloads;
  int currentPayloadIdx;
  bool active;
};

extern RowerState rower;
extern std::map<String, String> automationRules;

struct BackgroundTask {
  int id;
  String description;
  String type;
  String condition;
  String payload;
  bool active;
};

extern std::vector<BackgroundTask> activeTasks;
extern int nextTaskId;

extern bool wifiToggleEnabled;
extern bool bluetoothToggleEnabled;
extern bool btDiscoveryEnabled;
extern String bluetoothName;
extern bool wifiJoining;
extern unsigned long wifiJoinStartTime;
extern String current_sta_ssid;
extern String current_sta_password;

struct USBConfig {
  String vid;
  String pid;
  String mfr;
  String prod;
  bool rndVid;
  bool rndPid;
};

extern USBConfig currentUSBConfig;

// Delay Progress Tracking
extern unsigned long currentDelayTotal;
extern unsigned long currentDelayStart;

struct KeyCode {
  uint8_t modifier;
  uint8_t key;
};

#endif // GLOBAL_STATE_H

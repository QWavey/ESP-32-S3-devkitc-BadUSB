#include <Arduino.h>
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

#include "Config.h"
#include "GlobalState.h"
#include "LEDManager.h"
#include "FSManager.h"
#include "LogManager.h"
#include "USBManager.h"
#include "WiFiManager.h"
#include "DuckyInterpreter.h"
#include "WebServerManager.h"
#include "BTManager.h"

void setup() {
  Serial.begin(115200);
  delay(1000);

  pixels.begin();
  pixels.setBrightness(50);
  setLED(0, 255, 0);

  preferences.begin("badusb", false);

  ap_ssid = preferences.getString("ap_ssid", DEFAULT_AP_SSID);
  ap_password = preferences.getString("ap_password", DEFAULT_AP_PASSWORD);
  currentLanguage = preferences.getString("language", "us");
  wifiScanTime = preferences.getInt("wifi_scan_time", WIFI_SCAN_TIMEOUT);
  ledEnabled = preferences.getBool("led_enabled", true);
  loggingEnabled = preferences.getBool("logging_enabled", false);
  autoConnectEnabled = preferences.getBool("autoconnect", false);
  saveOnConnectEnabled = preferences.getBool("save_creds", false);
  btDiscoveryEnabled = preferences.getBool("bt_discovery", false);


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

  logDebug("=== BOOT START ===");
  logDebug("AP SSID: " + ap_ssid);
  logDebug("Language pref: " + currentLanguage);

  loadAvailableLanguages();
  logDebug("Languages loaded: " + String(availableLanguages.size()));
  for (auto& l : availableLanguages) logDebug("  lang: " + l);

  loadAvailableScripts();
  logDebug("Scripts loaded: " + String(availableScripts.size()));

  if (!loadLanguage(currentLanguage)) {
    Serial.println("Failed to load default language, trying 'us'");
    logDebug("Failed to load language: " + currentLanguage + ", trying 'us'");
    if (!loadLanguage("us")) {
      Serial.println("Failed to load 'us' language");
      logDebug("CRITICAL: Failed to load 'us' language!");
      setLEDMode(2);
    }
  }
  logDebug("Active language: " + currentLanguage);
  logDebug("Keymap entries: " + String(currentKeymap.size()));

  String bootPref = preferences.getString("boot_script", "");
  currentBootScriptFiles.clear();
  bootScript = "";
  
  if (bootPref.length() > 0) {
    int start = 0;
    int end = bootPref.indexOf(',');
    while (end != -1) {
      String file = bootPref.substring(start, end);
      if (SD.exists(String(DIR_SCRIPTS) + "/" + file)) {
        currentBootScriptFiles.push_back(file);
        bootScript += loadScript(file) + "\n";
      }
      start = end + 1;
      end = bootPref.indexOf(',', start);
    }
    String lastFile = bootPref.substring(start);
    if (SD.exists(String(DIR_SCRIPTS) + "/" + lastFile)) {
      currentBootScriptFiles.push_back(lastFile);
      bootScript += loadScript(lastFile) + "\n";
    }

    if (currentBootScriptFiles.size() > 0) {
      bootModeEnabled = true;
      Serial.println("Boot scripts loaded: " + bootPref);
    }
  } else if (SD.exists(String(DIR_SCRIPTS) + "/boot.txt")) {
    bootScript = loadScript("boot.txt");
    bootModeEnabled = true;
    currentBootScriptFiles.push_back("boot.txt");
    Serial.println("Default boot.txt found");
  }

  currentUSBConfig.vid = preferences.getString("usb_vid", "0x303a");
  currentUSBConfig.pid = preferences.getString("usb_pid", "0x0002");
  currentUSBConfig.rndVid = preferences.getBool("usb_rndVid", false);
  currentUSBConfig.rndPid = preferences.getBool("usb_rndPid", false);
  currentUSBConfig.mfr = preferences.getString("usb_mfr", "Espressif");
  currentUSBConfig.prod = preferences.getString("usb_prod", "ESP32-S3");

  if (currentUSBConfig.rndVid) {
    char buf[7]; sprintf(buf, "0x%04x", (uint16_t)(esp_random() & 0xFFFF));
    currentUSBConfig.vid = String(buf);
  }
  if (currentUSBConfig.rndPid) {
    char buf[7]; sprintf(buf, "0x%04x", (uint16_t)(esp_random() & 0xFFFF));
    currentUSBConfig.pid = String(buf);
  }

  USB.VID((uint16_t)strtol(currentUSBConfig.vid.c_str(), NULL, 16));
  USB.PID((uint16_t)strtol(currentUSBConfig.pid.c_str(), NULL, 16));
  USB.manufacturerName(currentUSBConfig.mfr.c_str());
  USB.productName(currentUSBConfig.prod.c_str());

  keyboard.begin();
  USB.begin();
  delay(1000);

  setupAP();
  bluetoothName = preferences.getString("bt_name", "ESP32-S3");
  setupBT();
  setupWebServer();

  setLED(0, 255, 0);

  // Check for reboot-once script (from RUN_ON_REBOOT block)
  if (SD.exists("/reboot_script.txt")) {
    Serial.println("Reboot script found - executing once");
    String content = loadScript("/reboot_script.txt");
    SD.remove("/reboot_script.txt");
    if (content.length() > 0) {
      delay(2000);
      executeScript(content);
    }
  }

  // Check for USB identity change resume script (from RANDOM_VID/PID/MAN/PRODUCT)
  if (SD.exists("/temp_resume.txt")) {
    Serial.println("Resume script found after USB identity change - executing");
    String content = loadScript("/temp_resume.txt");
    SD.remove("/temp_resume.txt");
    if (content.length() > 0) {
      delay(2000);
      executeScript(content);
    }
  }

  Serial.println("ESP32-S3 BadUSB Ready!");
  Serial.print("Connect to WiFi: ");
  Serial.println(ap_ssid);
  Serial.print("Password: ");
  Serial.println(ap_password);
  Serial.println("Open browser and go to: 192.168.4.1");

  loadCommandHistory();

  // Check for reboot payload
  if (sdCardPresent && SD.exists("/reboot_script.txt")) {
    Serial.println("Reboot payload found! Executing...");
    File f = SD.open("/reboot_script.txt", FILE_READ);
    if (f) {
      String payload = f.readString();
      f.close();
      SD.remove("/reboot_script.txt");
      executeScript(payload);
    }
  }
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
    if (millis() - lastButtonPress > 500) {
      lastButtonPress = millis();
      if (scriptRunning) {
        stopRequested = true;
        Serial.println("Stop requested via reset button");
        setLEDMode(4); // Warning mode
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
  
  // Background processing for Rower and Automation
  processRower();
  processAutomation();
  processAutoConnect();
  processBackgroundTasks();
  
  if (bluetoothToggleEnabled) {
    loopBT();
  }
  
  // Background Bluetooth tasks (periodic scan for automation)
  static unsigned long lastBTScan = 0;
  if ((btDiscoveryEnabled || millis() - lastBTScan > 60000) && !scriptRunning) {
    if (millis() - lastBTScan > 15000) { // Scan every 15s if enabled, or 60s otherwise
      lastBTScan = millis();
      scanBT();
    }
  }


  // Poll async WiFi scan without blocking
  pollWiFiScan();

  delay(1);
}

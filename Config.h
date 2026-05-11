#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

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

// Default WiFi AP configuration
#define DEFAULT_AP_SSID "ESP32-BadUSB"
#define DEFAULT_AP_PASSWORD "badusb123"

// Intervals and limits
#define SD_CHECK_INTERVAL 1000
#define STATUS_UPDATE_INTERVAL 5000
#define MAX_HISTORY_SIZE 50
#define WIFI_SCAN_TIMEOUT 5000
#define LOG_SESSION_START_MARKER "=== Log Session Started ==="
#define LOG_SESSION_END_MARKER "=== Log Session Ended ==="

// File system paths
#define DIR_LANGUAGES "/languages"
#define DIR_SCRIPTS "/scripts"
#define DIR_LOGS "/logs"
#define DIR_UPLOADS "/uploads"
#define FILE_HISTORY "/logs/history.txt"
#define FILE_LOG "/logs/log.txt"
#define FILE_DEBUG "/logs/debug.txt"
#define FILE_INDEX "/index.html"

#endif // CONFIG_H

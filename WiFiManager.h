#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "GlobalState.h"
#include <WiFi.h>

void setupAP();
void stopAP();
void scanWiFi();
void startWiFiScan();
void pollWiFiScan();
bool wifiScanComplete();
bool isSSIDPresent(String ssid);
void joinWiFi(String ssid, String password);
void stopJoiningWiFi();
void leaveWiFi();
String getTime(String region);
String getDay(String region);
String makeHttpRequest(String url);
void saveWiFiCredentials(String ssid, String pass);
void processAutoConnect();
String getSavedWiFiCredentials();
void deleteWiFiCredential(String ssid);

#endif // WIFI_MANAGER_H

#ifndef BT_MANAGER_H
#define BT_MANAGER_H

#include "GlobalState.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

void setupBT();
void loopBT();
void stopBT();
void scanBT();
bool isBTDevicePresent(String name);
int getBTClientCount();

#endif // BT_MANAGER_H

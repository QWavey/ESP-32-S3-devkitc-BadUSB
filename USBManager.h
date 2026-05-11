#ifndef USB_MANAGER_H
#define USB_MANAGER_H

#include "GlobalState.h"

KeyCode parseKeyCode(String keyCodeStr);
void fastPressKey(String key);
void fastPressKeyCombination(std::vector<String> keys);
void fastTypeString(String text);
void handleKeyInput(String line);
void pressKeyOnly(String key);
void releaseAllKeys();

#endif // USB_MANAGER_H

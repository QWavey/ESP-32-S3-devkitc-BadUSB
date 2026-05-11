#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include "GlobalState.h"

void handleLED();
void setLED(int r, int g, int b);
void setLEDMode(int mode);
void showCompletionBlink();
void showWarningBlink();

#endif // LED_MANAGER_H

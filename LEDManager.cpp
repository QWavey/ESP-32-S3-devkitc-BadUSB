#include "LEDManager.h"

void handleLED() {
  if (!ledEnabled) {
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show();
    return;
  }

  unsigned long currentTime = millis();

  if (ledMode == 3) { // Completion blink (orange)
    if (currentTime - lastCompletionBlinkTime >= 200) {
      lastCompletionBlinkTime = currentTime;
      completionBlinkState = !completionBlinkState;

      if (completionBlinkState) {
        pixels.setPixelColor(0, pixels.Color(255, 165, 0));
      } else {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      }

      pixels.show();

      if (!completionBlinkState) {
        completionBlinkCount++;
        if (completionBlinkCount >= 6) {
          ledMode = 0;
          completionBlinkCount = 0;
          setLED(0, 255, 0);
        }
      }
    }
    return;
  }

  if (ledMode == 4) { // Warning mode (orange blinking)
    if (currentTime - lastBlinkTime >= 300) {
      lastBlinkTime = currentTime;
      blinkState = !blinkState;
      if (blinkState) {
        pixels.setPixelColor(0, pixels.Color(255, 165, 0));
      } else {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      }
      pixels.show();
    }
    return;
  }

  if (blinkingEnabled || ledMode == 1 || ledMode == 2) {
    if (currentTime - lastBlinkTime >= blinkInterval) {
      lastBlinkTime = currentTime;
      blinkState = !blinkState;
      if (blinkState) {
        pixels.setPixelColor(0, pixels.Color(currentR, currentG, currentB));
      } else {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      }
      pixels.show();
    }
  } else {
    pixels.setPixelColor(0, pixels.Color(currentR, currentG, currentB));
    pixels.show();
  }
}

void setLED(int r, int g, int b) {
  if (!ledEnabled) return;
  currentR = r;
  currentG = g;
  currentB = b;
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
  ledMode = 0;
  blinkingEnabled = false;
  completionBlinkCount = 0;
}

void setLEDMode(int mode) {
  ledMode = mode;
  lastBlinkTime = millis();
  blinkState = false;
  completionBlinkCount = 0;

  if (mode == 0) {
    currentR = 0;
    currentG = 255;
    currentB = 0;
    blinkInterval = 500;
    blinkingEnabled = false;
  } else if (mode == 1) {
    currentR = 0;
    currentG = 255;
    currentB = 0;
    blinkInterval = 50;
    blinkingEnabled = true;
  } else if (mode == 2) {
    currentR = 255;
    currentG = 0;
    currentB = 0;
    blinkInterval = 500;
    blinkingEnabled = true;
  } else if (mode == 3) {
    lastCompletionBlinkTime = millis();
    completionBlinkState = true;
    completionBlinkCount = 0;
  } else if (mode == 4) {
    currentR = 255;
    currentG = 165;
    currentB = 0;
    blinkInterval = 300;
    blinkingEnabled = true;
  } else if (mode == 5) { // Amber static
    currentR = 255;
    currentG = 127;
    currentB = 0;
    blinkInterval = 500;
    blinkingEnabled = false;
  }
}

void showCompletionBlink() {
  setLEDMode(3);
}

void showWarningBlink() {
  setLEDMode(4);
}

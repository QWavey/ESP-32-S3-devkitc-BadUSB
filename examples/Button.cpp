#include "USB.h"
#include "USBHIDKeyboard.h"

USBHIDKeyboard Keyboard;

const int BTN = 0;
bool last = HIGH;
unsigned long pressT = 0;
int cnt = 0;
unsigned long lastClickT = 0;
bool longFired = false;
bool longReleaseFired = false;

void onSingleClick() {
  Keyboard.print("hello");
}

void onLongPress() {
  Keyboard.print("HOLDING!");
}

void onLongPressRelease() {
  Keyboard.print("RELEASED AFTER HOLD");
}

void setup() {
  pinMode(BTN, INPUT_PULLUP);
  Keyboard.begin();
  USB.begin();
  delay(1000);
}

void loop() {
  bool cur = digitalRead(BTN);
  unsigned long t = millis();
  
  // Button pressed
  if (last == HIGH && cur == LOW) {
    pressT = t;
    longFired = false;
    longReleaseFired = false;
    if (t - lastClickT < 400) cnt++;
    else cnt = 1;
    lastClickT = t;
  }
  
  // Check for long press while holding
  if (cur == LOW && !longFired && (t - pressT >= 1000)) {
    onLongPress();
    longFired = true;
    cnt = 0;
  }
  
  // Button released
  if (last == LOW && cur == HIGH) {
    unsigned long dur = t - pressT;
    
    if (longFired && !longReleaseFired) {
      onLongPressRelease();
      longReleaseFired = true;
    } else if (dur < 1000) {
      onSingleClick();
    }
    cnt = 0;
  }
  
  last = cur;
  delay(10);
}

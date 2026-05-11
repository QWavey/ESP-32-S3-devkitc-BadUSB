#include "DuckyInterpreter.h"
#include "LEDManager.h"
#include "LogManager.h"
#include "USBManager.h"
#include "FSManager.h"
#include "WiFiManager.h"
#include "BTManager.h"
#include <USB.h>

struct LoopState {
  int startLine;
  int currentIteration;
  int totalIterations;
  String varName;
  int step;
};

bool evalCondition(String condition) {
  condition.trim();
  condition = processVariables(condition);

  if (condition == "true" || condition == "1") return true;
  if (condition == "false" || condition == "0") return false;

  // New Connection Condition Support
  if (condition == "IF_CLIENT_CONNECTED_BLUETOOTH") return (getBTClientCount() > 0);
  if (condition == "IF_CLIENT_CONNECTED_WIFI") return (WiFi.softAPgetStationNum() > 0);
  if (condition == "IF_CLIENT_DISCONNECTED_WIFI") return (WiFi.softAPgetStationNum() == 0);
  if (condition == "IF_CLIENT_DISCONNECTED_BLUETOOTH") return (getBTClientCount() == 0);
  if (condition == "IF_CLIENT_CONNECTED") return (WiFi.softAPgetStationNum() > 0 || getBTClientCount() > 0);
  if (condition == "IF_CLIENT_DISCONNECTED") return (WiFi.softAPgetStationNum() == 0 && getBTClientCount() == 0);
  if (condition == "IF_CLIENT_CONNECTED_DISCONNECTED") return true; // Catch-all trigger
  if (condition == "IF_CLIENT_CONNECTED_DISCONNECTED_BLUETOOTH") return true;
  if (condition == "IF_CLIENT_CONNECTED_DISCONNECTED_WIFI") return true;


  String ops[] = {"==", "!=", ">=", "<=", ">", "<"};
  for (String op : ops) {
    int opIdx = condition.indexOf(op);
    if (opIdx != -1) {
      String left = condition.substring(0, opIdx);
      String right = condition.substring(opIdx + op.length());
      left.trim();
      right.trim();

      if (left.length() > 0 && right.length() > 0) {
        float lVal = left.toFloat();
        float rVal = right.toFloat();
        
        if (op == "==") return lVal == rVal || left == right;
        if (op == "!=") return lVal != rVal || left != right;
        if (op == ">=") return lVal >= rVal;
        if (op == "<=") return lVal <= rVal;
        if (op == ">") return lVal > rVal;
        if (op == "<") return lVal < rVal;
      }
      break;
    }
  }

  return condition.length() > 0;
}

void executeScript(const String& script) {
  if (scriptRunning) {
    Serial.println("Script already running");
    return;
  }

  scriptRunning = true;
  stopRequested = false;
  scriptStartTime = millis();
  setLEDMode(1);

  if (loggingEnabled) {
    openLogFile();
    logCommand("SCRIPT_START", "Script execution started");
  }

  addToHistory("Script executed at " + String(millis()));
  totalScriptsExecuted++;

  std::vector<String> lines;
  int startIndex = 0;
  int endIndex = script.indexOf('\n');

  while (endIndex != -1) {
    String line = script.substring(startIndex, endIndex);
    line.trim();
    if (line.length() > 0) lines.push_back(line);
    startIndex = endIndex + 1;
    endIndex = script.indexOf('\n', startIndex);
  }
  if (startIndex < script.length()) {
    String line = script.substring(startIndex);
    line.trim();
    if (line.length() > 0) lines.push_back(line);
  }

  int totalLines = lines.size();
  std::vector<LoopState> loopStack;
  int i = 0;
  int skipDepth = 0;
  bool skipActive = false;
  std::vector<int> callStack;
  std::vector<bool> ifHandledStack;
  std::map<String, int> functionTable;

  // Pre-scan for functions
  for (int j = 0; j < lines.size(); j++) {
    String fLine = lines[j];
    fLine.trim();
    if (fLine.startsWith("FUNCTION ")) {
      String funcName = fLine.substring(9);
      funcName.trim();
      if (funcName.endsWith("()")) funcName = funcName.substring(0, funcName.length() - 2);
      functionTable[funcName] = j;
    }
  }

  // Handle BEGIN_ROWER block
  bool inRowerBlock = false;
  std::vector<String> rowerPayloads;

  while (i < lines.size() && !stopRequested) {
    currentLineNum = i + 1;
    String line = lines[i];
    line.trim();

    if (line.length() == 0 || line.startsWith("REM") || line.startsWith("//")) {
      i++;
      continue;
    }

    if (skipActive) {
      if (line.startsWith("IF") || line.startsWith("FOR") || line.startsWith("FUNCTION ")) {
        skipDepth++;
      } else if (line.startsWith("ENDIF") || line.startsWith("END_IF") || line.startsWith("ENDFOR") || line.startsWith("END_FOR") || line == "END_FUNCTION") {
        if (skipDepth == 0) skipActive = false;
        else skipDepth--;
      }
      i++;
      continue;
    }

    if (line.startsWith("BEGIN_ROWER")) {
      inRowerBlock = true;
      i++;
      continue;
    }

    if (line == "END_ROWER") {
      inRowerBlock = false;
      rower.payloads = rowerPayloads;
      rower.currentPayloadIdx = 0;
      rower.active = true;
      i++;
      continue;
    }

    if (inRowerBlock) {
      rowerPayloads.push_back(line);
      i++;
      continue;
    }

    if (line.startsWith("RUN_ON_REBOOT")) {
      i++;
      String payload = "";
      int depth = 1;
      while (i < lines.size() && depth > 0) {
        String subLine = lines[i];
        subLine.trim();
        if (subLine.startsWith("IF") || subLine.startsWith("FOR") || subLine.startsWith("WHILE")) depth++;
        else if (subLine.startsWith("ENDIF") || subLine.startsWith("END_IF") || subLine.startsWith("ENDFOR") || subLine.startsWith("END_FOR") || subLine.startsWith("END_WHILE")) depth--;
        
        if (depth > 0) {
          payload += lines[i] + "\n";
          i++;
        }
      }
      if (payload.length() > 0) {
        File f = SD.open("/reboot_script.txt", FILE_WRITE);
        if (f) {
          f.print(payload);
          f.close();
          Serial.println("Reboot payload saved to SD");
        }
      }
      if (i < lines.size()) {
          String endLine = lines[i];
          endLine.trim();
          if (endLine.startsWith("END_RUN_ON_REBOOT")) i++;
          else if (endLine.startsWith("ENDIF") || endLine.startsWith("END_IF")) i++;
      }
      continue;
    }

    // RANDOM USB Identity commands — apply change, save remaining script, and reboot
    auto isRandomUSBCmd = [](const String& l) {
      return l == "RANDOM_VID" || l == "RANDOM_PID" || l == "RANDOM_MAN" || l == "RANDOM_PRODUCT" ||
             l.startsWith("RANDOM_VID ") || l.startsWith("RANDOM_PID ") ||
             l.startsWith("RANDOM_MAN ") || l.startsWith("RANDOM_PRODUCT ");
    };

    if (isRandomUSBCmd(line)) {
      // Apply the randomization to the matching field
      String cmd = line.substring(0, line.indexOf(' ') == -1 ? line.length() : line.indexOf(' '));
      cmd.trim();
      if (cmd == "RANDOM_VID") {
        char buf[7]; sprintf(buf, "0x%04x", (uint16_t)(esp_random() & 0xFFFF));
        preferences.putString("usb_vid", String(buf));
        Serial.println("RANDOM_VID: " + String(buf));
      } else if (cmd == "RANDOM_PID") {
        char buf[7]; sprintf(buf, "0x%04x", (uint16_t)(esp_random() & 0xFFFF));
        preferences.putString("usb_pid", String(buf));
        Serial.println("RANDOM_PID: " + String(buf));
      } else if (cmd == "RANDOM_MAN") {
        const char* mfrs[] = {"Microsoft", "Logitech", "Dell", "Apple", "HP", "Lenovo", "Asus", "Samsung"};
        String mfr = mfrs[esp_random() % 8];
        preferences.putString("usb_mfr", mfr);
        Serial.println("RANDOM_MAN: " + mfr);
      } else if (cmd == "RANDOM_PRODUCT") {
        const char* prods[] = {"USB Keyboard", "HID Device", "Wireless Dongle", "USB Hub", "Flash Drive"};
        String prod = prods[esp_random() % 5];
        preferences.putString("usb_prod", prod);
        Serial.println("RANDOM_PRODUCT: " + prod);
      }
      // Save remaining script lines to /temp_resume.txt
      i++;
      String remaining = "";
      while (i < lines.size()) {
        String remLine = lines[i];
        remLine.trim();
        if (remLine.length() > 0) remaining += remLine + "\n";
        i++;
      }
      if (remaining.length() > 0 && sdCardPresent) {
        File f = SD.open("/temp_resume.txt", FILE_WRITE);
        if (f) { f.print(remaining); f.close(); }
        Serial.println("Resume script saved. Rebooting for USB identity change...");
      }
      delay(500);
      ESP.restart();
      return; // Never reached
    }

    if (line.startsWith("IF_NOT_PRESENT ")) {
      String target = line.substring(15);
      target.trim();
      bool isPresent = false;
      
      if (target == "SD") isPresent = sdCardPresent;
      else if (target.startsWith("SSID=\"")) {
        int q1 = target.indexOf('"') + 1;
        int q2 = target.indexOf('"', q1);
        if (q2 > q1) {
          String ssid = target.substring(q1, q2);
          scanWiFi();
          isPresent = isSSIDPresent(ssid);
        }
      } else if (target == "WIFI") isPresent = (WiFi.status() == WL_CONNECTED);
      else if (target == "BT" || target == "BLUETOOTH") isPresent = (getBTClientCount() > 0);

      if (isPresent) {
        // Condition NOT met (we want NOT present), so skip the block
        ifHandledStack.push_back(false);
        skipActive = true;
        skipDepth = 0;
      } else {
        // Condition met (it is NOT present)
        ifHandledStack.push_back(true);
      }
      i++;
      continue;
    }

    if (line.startsWith("FUNCTION ") || line.startsWith("DEF_")) {
      skipActive = true;
      skipDepth = 0;
      i++;
      continue;
    }

    if (line == "END_FUNCTION" || line == "RETURN") {
      if (!callStack.empty()) {
        i = callStack.back() + 1;
        callStack.pop_back();
        continue;
      }
      i++;
      continue;
    }

    if (line.startsWith("FOR ")) {
      String forParams = line.substring(4);
      forParams.trim();
      int fromIdx = forParams.indexOf("FROM ");
      int toIdx = forParams.indexOf("TO ");
      int stepIdx = forParams.indexOf("STEP ");
      
      if (fromIdx != -1 && toIdx != -1) {
        String varName = forParams.substring(0, fromIdx);
        varName.trim();
        int startVal = forParams.substring(fromIdx + 5, toIdx).toInt();
        int endVal;
        int stepVal = 1;
        if (stepIdx != -1) {
          endVal = forParams.substring(toIdx + 3, stepIdx).toInt();
          stepVal = forParams.substring(stepIdx + 5).toInt();
        } else {
          endVal = forParams.substring(toIdx + 3).toInt();
        }
        LoopState loop = {i, startVal, endVal, varName, stepVal};
        loopStack.push_back(loop);
        variables[varName] = String(startVal);
      }
      i++;
      continue;
    }

    if (line.startsWith("ENDFOR") || line.startsWith("END_FOR")) {
      if (!loopStack.empty()) {
        LoopState& loop = loopStack.back();
        loop.currentIteration += loop.step;
        if (loop.currentIteration <= loop.totalIterations) {
          variables[loop.varName] = String(loop.currentIteration);
          i = loop.startLine + 1;
          continue;
        } else {
          loopStack.pop_back();
        }
      }
      i++;
      continue;
    }

    bool isIf = false;
    bool conditionMet = false;

    if (line.startsWith("IF_PRESENT SSID=\"")) {
      isIf = true;
      int quoteStart = line.indexOf('"') + 1;
      int quoteEnd = line.indexOf('"', quoteStart);
      if (quoteEnd > quoteStart) {
        String ssid = line.substring(quoteStart, quoteEnd);
        scanWiFi();
        conditionMet = isSSIDPresent(ssid);
      }
    } else if (line.startsWith("IF_NOTPRESENT SSID=\"")) {
      isIf = true;
      int quoteStart = line.indexOf('"') + 1;
      int quoteEnd = line.indexOf('"', quoteStart);
      if (quoteEnd > quoteStart) {
        String ssid = line.substring(quoteStart, quoteEnd);
        scanWiFi();
        conditionMet = !isSSIDPresent(ssid);
      }
    } else if (line.startsWith("IF_BT_PRESENT \"")) {
      isIf = true;
      int quoteStart = line.indexOf('"') + 1;
      int quoteEnd = line.indexOf('"', quoteStart);
      if (quoteEnd > quoteStart) {
        String name = line.substring(quoteStart, quoteEnd);
        scanBT();
        conditionMet = isBTDevicePresent(name);
      }
    } else if (line.startsWith("IF_CLIENT_CONNECTED_BLUETOOTH")) {
      isIf = true;
      conditionMet = (getBTClientCount() > 0);
    } else if (line.startsWith("IF_CLIENT_CONNECTED_WIFI")) {
      isIf = true;
      conditionMet = (WiFi.softAPgetStationNum() > 0);
    } else if (line.startsWith("IF_CLIENT_DISCONNECTED_WIFI")) {
      isIf = true;
      conditionMet = (WiFi.softAPgetStationNum() == 0);
    } else if (line.startsWith("IF_ONLINE")) {
      isIf = true;
      conditionMet = (WiFi.status() == WL_CONNECTED);
    } else if (line.startsWith("IF_OFFLINE")) {
      isIf = true;
      conditionMet = (WiFi.status() != WL_CONNECTED);
    } else if (line.startsWith("IF_OS ")) {
      isIf = true;
      String targetOS = line.substring(6);
      targetOS.trim();
      conditionMet = (detectedOS.equalsIgnoreCase(targetOS));
    } else if (line.startsWith("IF_DETECT_OS_INCLUDES = \"")) {
      isIf = true;
      int q1 = line.indexOf('"') + 1;
      int q2 = line.indexOf('"', q1);
      if (q2 > q1) {
        String target = line.substring(q1, q2);
        conditionMet = (detectedOS.indexOf(target) != -1);
      }
    } else if (line.startsWith("IF_CLIENT_CONNECTED")) {
      isIf = true;
      conditionMet = (WiFi.softAPgetStationNum() > 0 || getBTClientCount() > 0);
    } else if (line.startsWith("IF_CLIENT_DISCONNECTED_BLUETOOTH")) {
      isIf = true;
      conditionMet = (getBTClientCount() == 0);
    } else if (line.startsWith("IF_CLIENT_DISCONNECTED")) {
      isIf = true;
      conditionMet = (WiFi.softAPgetStationNum() == 0 && getBTClientCount() == 0);
    } else if (line.startsWith("IF_CONNECTED_TO_WIFI")) {
      isIf = true;
      conditionMet = (WiFi.status() == WL_CONNECTED);
    } else if (line.startsWith("IF ")) {
      isIf = true;
      conditionMet = evalCondition(line.substring(3));
    } else if (line.startsWith("IF_CLIENT_CONNECTED_DISCONNECTED_BLUETOOTH")) {
      isIf = true;
      conditionMet = true; // Triggered if reached
    } else if (line.startsWith("IF_CLIENT_CONNECTED_DISCONNECTED_WIFI")) {
      isIf = true;
      conditionMet = true;
    } else if (line.startsWith("IF_CLIENT_CONNECTED_DISCONNECTED")) {
      isIf = true;
      conditionMet = true;
    } else if (line.startsWith("IF_")) {
      isIf = true;
      conditionMet = evalCondition(line);
    }


    if (isIf) {
      ifHandledStack.push_back(conditionMet);
      if (!conditionMet) {
        skipActive = true;
        skipDepth = 0;
      }
      i++;
      continue;
    }

    if (line.startsWith("ELIF ")) {
      if (!skipActive) {
        skipActive = true;
        skipDepth = 0;
      } else if (skipDepth == 0) {
        if (!ifHandledStack.empty() && !ifHandledStack.back()) {
          conditionMet = evalCondition(line.substring(5));
          if (conditionMet) {
            skipActive = false;
            ifHandledStack.back() = true;
          }
        }
      }
      i++;
      continue;
    }
    
    if (line.startsWith("ELIF_")) {
      if (!skipActive) {
        skipActive = true;
        skipDepth = 0;
      } else if (skipDepth == 0) {
        if (!ifHandledStack.empty() && !ifHandledStack.back()) {
          conditionMet = evalCondition(line.substring(5));
          if (conditionMet) {
            skipActive = false;
            ifHandledStack.back() = true;
          }
        }
      }
      i++;
      continue;
    }

    if (line == "ELSE" || line == "ELSE:") {
      if (!skipActive) {
        skipActive = true;
        skipDepth = 0;
      } else if (skipDepth == 0) {
        if (!ifHandledStack.empty() && !ifHandledStack.back()) {
          skipActive = false;
        }
      }
      i++;
      continue;
    }

    if (line.startsWith("ENDIF") || line.startsWith("END_IF")) {
      if (!ifHandledStack.empty() && skipDepth == 0) ifHandledStack.pop_back();
      if (skipDepth > 0) skipDepth--;
      else skipActive = false;
      i++;
      continue;
    }

    String potentialFunc = line;
    if (potentialFunc.endsWith("()")) potentialFunc = potentialFunc.substring(0, potentialFunc.length() - 2);
    if (functionTable.find(potentialFunc) != functionTable.end()) {
      callStack.push_back(i);
      i = functionTable[potentialFunc] + 1;
      continue;
    }

    executeCommand(line);
    totalCommandsExecuted++;
    if (stopRequested) break;
    if (defaultDelay > 0) {
      unsigned long delayStart = millis();
      while (millis() - delayStart < (unsigned long)defaultDelay && !stopRequested) delay(10);
    }
    i++;
  }

  scriptRunning = false;
  if (loggingEnabled) {
    if (stopRequested) logCommand("SCRIPT_STOP", "Stopped at line " + String(currentLineNum));
    else logCommand("SCRIPT_END", "Completed successfully");
    closeLogFile();
  }

  if (stopRequested) {
    stopRequested = false;
    setLEDMode(0);
  } else {
    showCompletionBlink();
  }
}

void executeCommand(String line) {
  if (stopRequested) return;
  
  if (!line.startsWith("REPEAT ")) {
    lastCommand = line;
  }
  
  addToHistory(line);

  if (line.startsWith("STRING ")) {
    fastTypeString(processVariables(line.substring(7)));
    if (holdTillStringActive) {
      releaseAllKeys();
      holdTillStringActive = false;
    }
    return;
  }

  if (line.startsWith("STRINGLN ")) {
    fastTypeString(processVariables(line.substring(9)));
    fastPressKey("ENTER");
    if (holdTillStringActive) {
      releaseAllKeys();
      holdTillStringActive = false;
    }
    return;
  }

  if (line == "HOLD_TILL_STRING") {
    holdTillStringActive = true;
    return;
  }

  if (line.startsWith("DEFAULTDELAY ") || line.startsWith("DEFAULT_DELAY ")) {
    defaultDelay = line.substring(line.indexOf(' ') + 1).toInt();
    return;
  }

  if (line.startsWith("LOCALE ")) {
    loadLanguage(line.substring(7));
    return;
  }

  if (line.startsWith("LOCALE_")) {
    String lang = line.substring(7);
    lang.toLowerCase();
    loadLanguage(lang + ".json");
    return;
  }

  if (line.startsWith("DELAY ")) {
    String delayStr = line.substring(6);
    delayStr.trim();
    if (delayStr.endsWith("ms")) {
      delayStr = delayStr.substring(0, delayStr.length() - 2);
      delayStr.trim();
    }
    int delayTime = delayStr.toInt();
    currentDelayTotal = delayTime;
    currentDelayStart = millis();
    unsigned long startTime = millis();
    while (millis() - startTime < (unsigned long)delayTime && !stopRequested) delay(10);
    currentDelayTotal = 0;
    currentDelayStart = 0;
    return;
  }


  if (line.startsWith("VAR ")) {
    String varLine = line.substring(4);
    int eqIdx = varLine.indexOf('=');
    if (eqIdx > 0) {
      String name = varLine.substring(0, eqIdx);
      String val = varLine.substring(eqIdx + 1);
      name.trim(); val.trim();
      val = processVariables(val);
      if (val.indexOf('+') != -1 || val.indexOf('-') != -1 || val.indexOf('*') != -1 || val.indexOf('/') != -1) {
        char ops[] = {'+', '-', '*', '/'};
        for (char op : ops) {
          int opIdx = val.indexOf(op);
          if (opIdx != -1) {
            float v1 = val.substring(0, opIdx).toFloat();
            float v2 = val.substring(opIdx + 1).toFloat();
            if (op == '+') val = String(v1 + v2);
            else if (op == '-') val = String(v1 - v2);
            else if (op == '*') val = String(v1 * v2);
            else if (op == '/') val = (v2 != 0) ? String(v1 / v2) : "0";
            break;
          }
        }
      }
      variables[name] = val;
    }
    return;
  }

  if (line.startsWith("BEGIN_ROWER")) {
    // Already handled in pre-scan or skip
    return;
  }

  if (line == "END_ROWER") {
    return;
  }

  if (line.startsWith("WIFI_OFF_WHEN_WIFI=") || line.startsWith("WIFI_ON_WHEN_WIFI=") || 
      line.startsWith("BLUETOOTH_OFF_WHEN_WIFI=") || line.startsWith("BLUETOOTH_ON_WHEN_WIFI=") ||
      line.startsWith("RUN_WHEN_BLUETOOTH_FOUND=") || line.startsWith("RUN_WHEN_BT_FOUND=") || line.startsWith("BT_FOUND=")) {
    int eqIdx = line.indexOf('=');
    String cmd = line.substring(0, eqIdx);
    String val = line.substring(eqIdx + 1);
    variables[cmd] = val;
    Serial.println("Background automation set: " + cmd + " = " + val);
    return;
  }

  if (line.startsWith("BLUETOOTH_DISCOVERY ")) {
    String state = line.substring(20);
    state.trim();
    if (state == "ON") btDiscoveryEnabled = true;
    else if (state == "OFF") btDiscoveryEnabled = false;
    Serial.println("Bluetooth discovery: " + String(btDiscoveryEnabled ? "ON" : "OFF"));
    return;
  }

  if (line.startsWith("SET_BOOT_SCRIPT ")) {
    String scriptName = line.substring(16);
    scriptName.trim();
    if (!scriptName.endsWith(".txt")) scriptName += ".txt";
    String fullPath = String(DIR_SCRIPTS) + "/" + scriptName;
    if (SD.exists(fullPath)) {
      preferences.putString("boot_script", scriptName);
      currentBootScriptFiles.clear();
      currentBootScriptFiles.push_back(scriptName);
      bootScript = loadScript(scriptName);
      bootModeEnabled = true;
      Serial.println("Boot script set to: " + scriptName);
    } else {
      Serial.println("SET_BOOT_SCRIPT: File not found: " + scriptName);
    }
    return;
  }

  if (line.startsWith("LED ")) {
    String rgb = line.substring(4);
    int r, g, b, s1 = rgb.indexOf(' '), s2 = rgb.indexOf(' ', s1 + 1);
    if (s1 > 0 && s2 > s1) {
      r = rgb.substring(0, s1).toInt();
      g = rgb.substring(s1 + 1, s2).toInt();
      b = rgb.substring(s2 + 1).toInt();
      setLED(r, g, b);
    }
    return;
  }

  if (line.startsWith("LED_")) {
    if (line == "LED_R") setLED(255, 0, 0);
    else if (line == "LED_G") setLED(0, 255, 0);
    else if (line == "LED_B") setLED(0, 0, 255);
    else if (line == "LED_Y") setLED(255, 255, 0);
    else if (line == "LED_W") setLED(255, 255, 255);
    else if (line == "LED_O") setLED(255, 165, 0);
    else if (line == "LED_P") setLED(128, 0, 128);
    else if (line == "LED_C") setLED(0, 255, 255);
    else if (line == "LED_M") setLED(255, 0, 255);
    else if (line == "LED_IR") Serial.println("IR LED Not Hardware Supported (Stub)");
    else if (line == "LED_UV") Serial.println("UV LED Not Hardware Supported (Stub)");
    else if (line == "LED_A") setLED(255, 127, 0); // Amber
    else if (line == "LED_V") setLED(148, 0, 211); // Violet
    else if (line == "LED_OFF") {
      setLED(0, 0, 0);
      blinkingEnabled = false;
    }
    else if (line == "LED_BLINK") {
      blinkingEnabled = true;
      if (blinkInterval <= 0) blinkInterval = 500;
    }
    return;
  }

  if (line == "BLINK_STOP") {
    blinkingEnabled = false;
    return;
  }

  if (line.startsWith("BLINK_LED_")) {
    String color = line.substring(10);
    int interval = 500;
    int sIdx = color.indexOf(' ');
    if (sIdx != -1) {
      interval = color.substring(sIdx + 1).toInt();
      color = color.substring(0, sIdx);
    }
    blinkingEnabled = true;
    blinkInterval = interval;
    if (color == "R") setLED(255, 0, 0);
    else if (color == "G") setLED(0, 255, 0);
    else if (color == "B") setLED(0, 0, 255);
    else if (color == "Y") setLED(255, 255, 0);
    else if (color == "W") setLED(255, 255, 255);
    else if (color == "O") setLED(255, 165, 0);
    else if (color == "P") setLED(128, 0, 128);
    else if (color == "C") setLED(0, 255, 255);
    else if (color == "M") setLED(255, 0, 255);
    else if (color == "V") setLED(148, 0, 211);
    else if (color == "A") setLED(255, 127, 0); // Amber
    return;
  }

  if (line == "LED_STOP") {
    blinkingEnabled = false;
    return;
  }

  if (line.startsWith("HOLD ")) {
    String params = line.substring(5);
    int lastSpace = params.lastIndexOf(' ');
    int dur = -1;
    String keysPart = params;
    
    // Check if the last part is a duration (integer)
    if (lastSpace != -1) {
      String lastPart = params.substring(lastSpace + 1);
      bool isNum = true;
      for (int k = 0; k < lastPart.length(); k++) if (!isdigit(lastPart[k])) { isNum = false; break; }
      if (isNum) {
        dur = lastPart.toInt();
        keysPart = params.substring(0, lastSpace);
      }
    }

    std::vector<String> keys;
    int s1 = 0, s2 = keysPart.indexOf(' ');
    while (s2 != -1) {
      keys.push_back(keysPart.substring(s1, s2));
      s1 = s2 + 1;
      s2 = keysPart.indexOf(' ', s1);
    }
    keys.push_back(keysPart.substring(s1));

    for (String k : keys) pressKeyOnly(k);
    
    if (dur > 0) {
      delay(dur * 1000);
      releaseAllKeys();
    }
    return;
  }

  if (line.startsWith("HOLD ")) {
    String key = line.substring(5);
    key.trim();
    pressKeyOnly(key); // Press without release
    return;
  }

  if (line == "STOPHOLD") {
    keyboard.releaseAll();
    return;
  }

  if (line.startsWith("HOLD_TILL_")) {
    String event = line.substring(10);
    event.trim();
    if (event.startsWith("STRING ")) {
        String target = event.substring(7);
        target.trim();
        if (target.startsWith("\"")) target = target.substring(1, target.length()-1);
        while (true) {
            if (Serial.available()) {
                String input = Serial.readStringUntil('\n');
                if (input.indexOf(target) != -1) break;
            }
            delay(10);
        }
    } else if (event == "ESC") {
        while (true) {
            if (Serial.available() && Serial.read() == 0x1B) break;
            delay(10);
        }
    } else if (event == "ENTER") {
        while (true) {
            if (Serial.available() && Serial.read() == 0x0D) break;
            delay(10);
        }
    }
    return;
  }

  if (line.startsWith("KEYCODE ")) {
    String hex = line.substring(8);
    std::vector<uint8_t> codes;
    int s1 = 0, s2 = hex.indexOf(' ');
    while (s2 != -1) {
      String h = hex.substring(s1, s2);
      if (h.startsWith("0x")) h = h.substring(2);
      codes.push_back(strtol(h.c_str(), NULL, 16));
      s1 = s2 + 1;
      s2 = hex.indexOf(' ', s1);
    }
    if (s1 < hex.length()) {
      String h = hex.substring(s1);
      if (h.startsWith("0x")) h = h.substring(2);
      codes.push_back(strtol(h.c_str(), NULL, 16));
    }
    if (codes.size() >= 2) {
      uint8_t mod = codes[0], key = codes[1];
      if (mod & 0x01) keyboard.press(KEY_LEFT_CTRL);
      if (mod & 0x02) keyboard.press(KEY_LEFT_SHIFT);
      if (mod & 0x04) keyboard.press(KEY_LEFT_ALT);
      if (mod & 0x08) keyboard.press(KEY_LEFT_GUI);
      if (key > 0) keyboard.pressRaw(key);
      delay(5);
      keyboard.releaseAll();
    }
    return;
  }

  // Consolidated Modifier and Special Key Logic
  static const struct { const char* name; const char* key; } keyMap[] = {
    {"CTRL", "CTRL"}, {"CONTROL", "CTRL"}, {"SHIFT", "SHIFT"}, {"ALT", "ALT"},
    {"WINDOWS", "GUI"}, {"GUI", "GUI"}, {"ENTER", "ENTER"}, {"TAB", "TAB"},
    {"ESC", "ESC"}, {"ESCAPE", "ESC"}, {"DELETE", "DELETE"}, {"DEL", "DELETE"},
    {"BACKSPACE", "BACKSPACE"}, {"HOME", "HOME"}, {"END", "END"},
    {"PAGEUP", "PAGEUP"}, {"PAGEDOWN", "PAGEDOWN"}, {"INSERT", "INSERT"},
    {"UP", "UP"}, {"UPARROW", "UP"}, {"DOWN", "DOWN"}, {"DOWNARROW", "DOWN"},
    {"LEFT", "LEFT"}, {"LEFTARROW", "LEFT"}, {"RIGHT", "RIGHT"}, {"RIGHTARROW", "RIGHT"},
    {"CAPSLOCK", "CAPSLOCK"}, {"NUMLOCK", "NUMLOCK"}, {"SCROLLLOCK", "SCROLLLOCK"},
    {"PRINTSCREEN", "PRINTSCREEN"}, {"PAUSE", "PAUSE"}, {"BREAK", "PAUSE"},
    {"MENU", "APP"}, {"APP", "APP"}
  };

  for (auto const& km : keyMap) {
    if (line == km.name) {
      fastPressKey(km.key);
      return;
    }
  }

  // System & Hardware Commands
  if (line == "WIFI_ON") { setupAP(); return; }
  if (line == "WIFI_OFF") { stopAP(); return; }
  if (line == "BLUETOOTH_ON") { setupBT(); return; }
  if (line == "BLUETOOTH_OFF") { stopBT(); return; }

  if (line.startsWith("UPLOAD_FILE ")) {
    String params = line.substring(12);
    int sIdx = params.indexOf(' ');
    if (sIdx != -1) {
      String local = params.substring(0, sIdx);
      String remote = params.substring(sIdx + 1);
      local.trim(); remote.trim();
      uploadFileToServer(local, remote);
    }
    return;
  }

  if (line.startsWith("HTTP_REQUEST = ") || line.startsWith("HTTPS_REQUEST = ")) {
    String url = line.substring(15);
    url.trim();
    if (url.startsWith("\"")) url = url.substring(1, url.length() - 1);
    variables["HTTP_RESPONSE"] = makeHttpRequest(url);
    return;
  }

  if (line == "GET_TIME") {
    variables["TIME"] = getTime("");
    return;
  }

  if (line == "GET_DAY") {
    // NTP doesn't directly give day of week easily in one call here, 
    // but we stub it for regional support as requested.
    variables["DAY"] = "Monday"; // Stub
    return;
  }

  if (line.startsWith("RUN_AT_DAY = ")) {
     String targetDay = line.substring(13); targetDay.trim();
     // While loop blocking as requested in RUN_AT_TIME style
     while (variables["DAY"] != targetDay && !stopRequested) delay(5000);
     return;
  }

  if (line.startsWith("VID_")) {
    currentUSBConfig.vid = line.substring(4);
    currentUSBConfig.rndVid = false;
    USB.VID((uint16_t)strtol(currentUSBConfig.vid.c_str(), NULL, 16));
    saveSettings();
    return;
  }

  if (line.startsWith("PID_")) {
    currentUSBConfig.pid = line.substring(4);
    currentUSBConfig.rndPid = false;
    USB.PID((uint16_t)strtol(currentUSBConfig.pid.c_str(), NULL, 16));
    saveSettings();
    return;
  }

  if (line.startsWith("MAN_")) {
    currentUSBConfig.mfr = line.substring(4);
    USB.manufacturerName(currentUSBConfig.mfr.c_str());
    saveSettings();
    return;
  }

  if (line.startsWith("PRODUCT_")) {
    currentUSBConfig.prod = line.substring(8);
    USB.productName(currentUSBConfig.prod.c_str());
    saveSettings();
    return;
  }

  if (line == "REBOOT") {
    Serial.println("Rebooting device...");
    delay(500);
    ESP.restart();
    return;
  }

  if (line.startsWith("DOWNLOAD_FILE ")) {
    String params = line.substring(14);
    int sIdx = params.indexOf(' ');
    if (sIdx != -1) {
      String url = params.substring(0, sIdx);
      String dest = params.substring(sIdx + 1);
      url.trim(); dest.trim();
      downloadFileFromURL(url, dest);
    }
    return;
  }

  if (line.startsWith("JOIN_INTERNET")) {
    String params = line.substring(13);
    params.trim();
    String ssid = "", password = "";
    int ssidStart = params.indexOf("SSID=\"");
    if (ssidStart != -1) {
      int ssidEnd = params.indexOf("\"", ssidStart + 6);
      if (ssidEnd != -1) ssid = params.substring(ssidStart + 6, ssidEnd);
    }
    int passStart = params.indexOf("PASSWORD=\"");
    if (passStart != -1) {
      int passEnd = params.indexOf("\"", passStart + 10);
      if (passEnd != -1) password = params.substring(passStart + 10, passEnd);
    }
    if (ssid.length() > 0) joinWiFi(ssid, password);
    return;
  }

  if (line.startsWith("RANDOM_")) {
    int spaceIdx = line.indexOf(' ');
    String typeStr = (spaceIdx != -1) ? line.substring(7, spaceIdx) : line.substring(7);
    int count = (spaceIdx != -1) ? line.substring(spaceIdx + 1).toInt() : 1;
    if (count < 1) count = 1;
    
    bool useChar = typeStr.indexOf("CHAR") != -1;
    bool useNum = typeStr.indexOf("NUMBER") != -1;
    bool useSpec = typeStr.indexOf("SPECIAL") != -1;
    
    String chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    String nums = "0123456789";
    String specs = "!@#$%^&*()_+-=[]{}|;:,.<>?";
    String pool = "";
    if (useChar) pool += chars;
    if (useNum) pool += nums;
    if (useSpec) pool += specs;
    
    if (pool.length() > 0) {
      String out = "";
      for (int k = 0; k < count; k++) out += pool.charAt(esp_random() % pool.length());
      fastTypeString(out);
    }
    return;
  }

  if (line == "LEAVE_INTERNET") {
    leaveWiFi();
    return;
  }

  if (line.startsWith("WAIT_FOR_SD")) {
    unsigned long waitStart = millis();
    while (!sdCardPresent && (millis() - waitStart < 30000) && !stopRequested) {
      delay(500);
      checkSDCard();
    }
    return;
  }

  if (line.startsWith("WAIT_FOR_EVENT = ")) {
    String event = line.substring(17); event.trim();
    if (event == "USB_CONNECTED") {
      while (!USB && !stopRequested) delay(500);
    } else if (event == "USB_DISCONNECTED") {
      while (USB && !stopRequested) delay(500);
    }
    return;
  }

  if (line.startsWith("HTTP_REQUEST = \"") || line.startsWith("HTTPS_REQUEST = \"")) {
    int q1 = line.indexOf('"') + 1;
    int q2 = line.indexOf('"', q1);
    if (q2 > q1) {
      String url = line.substring(q1, q2);
      variables["LAST_HTTP_RESPONSE"] = makeHttpRequest(url);
    }
    return;
  }

  if (line.startsWith("GET_TIME")) {
    String region = "us";
    if (line.length() > 9) region = line.substring(9);
    region.trim();
    variables["CURRENT_TIME"] = getTime(region);
    return;
  }

  if (line.startsWith("GET_DAY")) {
    String region = "us";
    if (line.length() > 8) region = line.substring(8);
    region.trim();
    variables["CURRENT_DAY"] = getDay(region);
    return;
  }

  if (line.startsWith("RUN_AT_TIME = ")) {
    String target = line.substring(14);
    target.trim();
    BackgroundTask task;
    task.id = nextTaskId++;
    task.description = "Run at time: " + target;
    task.type = "TIME_TRIGGER";
    task.payload = target;
    task.active = true;
    activeTasks.push_back(task);
    return;
  }

  if (line.startsWith("RUN_AT_DAY = ")) {
    String target = line.substring(13);
    target.trim();
    BackgroundTask task;
    task.id = nextTaskId++;
    task.description = "Run at day: " + target;
    task.type = "DAY_TRIGGER";
    task.payload = target;
    task.active = true;
    activeTasks.push_back(task);
    return;
  }

  if (line.startsWith("RUN_WHEN_WIFI = \"")) {
    int q1 = line.indexOf('"') + 1;
    int q2 = line.indexOf('"', q1);
    if (q2 > q1) {
      String ssid = line.substring(q1, q2);
      BackgroundTask task;
      task.id = nextTaskId++;
      task.description = "Run when WiFi seen: " + ssid;
      task.type = "WIFI_TRIGGER";
      task.payload = ssid;
      task.active = true;
      activeTasks.push_back(task);
    }
    return;
  }

  if (line == "HOLD_TILL_STRING") {
     holdTillStringActive = true;
     return;
  }

  if (line.startsWith("WAIT_FOR_EVENT = ")) {
    String event = line.substring(17);
    event.trim();
    // Blocking wait for event
    unsigned long start = millis();
    while (millis() - start < 30000) { // 30s timeout
      if (event == "USB_CONNECTED") break; // Stub for now
      if (event == "WIFI_CONNECTED" && WiFi.status() == WL_CONNECTED) break;
      delay(100);
    }
    return;
  }

  if (line.startsWith("PING ")) {
    variables["LAST_PING_SUCCESS"] = (WiFi.status() == WL_CONNECTED) ? "true" : "false";
    return;
  }

  if (line.startsWith("USE_FILE ")) {
    String arg = line.substring(9); arg.trim();
    useFile(arg);
    return;
  }

  if (line.startsWith("COPY_FILE ")) {
    String params = line.substring(10); params.trim();
    int sIdx = params.indexOf(' ');
    if (sIdx != -1) copyFile(params.substring(0, sIdx), params.substring(sIdx + 1));
    else copyFile(params, "");
    return;
  }

  if (line.startsWith("CUT_FILE ")) {
    String params = line.substring(9); params.trim();
    int sIdx = params.indexOf(' ');
    if (sIdx != -1) cutFile(params.substring(0, sIdx), params.substring(sIdx + 1));
    else cutFile(params, "");
    return;
  }

  if (line.startsWith("PASTE_FILE")) {
    String arg = line.substring(10); arg.trim();
    pasteFile(arg);
    return;
  }

  if (line.startsWith("RUN_AT_TIME = ")) {
    String target = line.substring(14); target.trim();
    while (getTime("") != target && !stopRequested) delay(1000);
    return;
  }

  if (line.startsWith("RUN_WHEN_WIFI = \"")) {
    int q1 = line.indexOf('"') + 1, q2 = line.indexOf('"', q1);
    if (q2 > q1) {
      String ssid = line.substring(q1, q2);
      bool online = (line.indexOf("IS_ONLINE") != -1);
      while (!stopRequested) {
        scanWiFi();
        if (isSSIDPresent(ssid) == online) break;
        delay(5000);
      }
    }
    return;
  }

  if (line.startsWith("REPEAT ")) {
    int count = line.substring(7).toInt();
    String cmdToRepeat = lastCommand; // This will be the command BEFORE the current REPEAT
    for (int j = 0; j < count && !stopRequested; j++) {
      executeCommand(cmdToRepeat);
    }
    return;
  }

  if (line == "SHUTDOWN") { ESP.deepSleep(0); return; }
  if (line == "REBOOT") { ESP.restart(); return; }
  if (line == "DETECT_OS") { detectOS(); return; }
  if (line == "SELFDESTRUCT" || line.startsWith("SELFDESTRUCT ")) {
    selfDestruct();
    return;
  }
  if (line.startsWith("CD ")) { 
    String arg = line.substring(3); arg.trim();
    changeDirectory(arg); 
    return; 
  }
  if (line.startsWith("SET_BUTTON_PIN ")) {
    buttonPin = line.substring(15).toInt();
    if (buttonPin > 0) pinMode(buttonPin, INPUT_PULLUP);
    return;
  }
  if (line.startsWith("RUN_PAYLOAD ")) {
    String f = line.substring(12); f.trim();
    if (!f.startsWith("/")) f = "/scripts/" + f;
    File file = SD.open(f);
    if (file) { String s = file.readString(); file.close(); executeScript(s); }
    return;
  }

  if (line.startsWith("IF_CLIENT_CONNECTED_DISCONNECTED_WIFI")) {
    int startNum = WiFi.softAPgetStationNum();
    while (WiFi.softAPgetStationNum() == startNum && !stopRequested) delay(500);
    return;
  }
  if (line.startsWith("IF_CLIENT_CONNECTED_DISCONNECTED_BLUETOOTH")) {
    bool startState = getBTClientCount() > 0;
    while ((getBTClientCount() > 0) == startState && !stopRequested) delay(500);
    return;
  }
  if (line.startsWith("IF_CLIENT_CONNECTED_DISCONNECTED")) {
    int startWifi = WiFi.softAPgetStationNum();
    bool startBT = getBTClientCount() > 0;
    while (WiFi.softAPgetStationNum() == startWifi && (getBTClientCount() > 0) == startBT && !stopRequested) delay(500);
    return;
  }
  if (line == "IF_CLIENT_CONNECTED_WIFI") {
    while (WiFi.softAPgetStationNum() == 0 && !stopRequested) delay(500);
    return;
  }
  if (line == "IF_CLIENT_CONNECTED_BLUETOOTH") {
    while (getBTClientCount() == 0 && !stopRequested) delay(500);
    return;
  }
  if (line == "IF_CLIENT_CONNECTED") {
    while (WiFi.softAPgetStationNum() == 0 && getBTClientCount() == 0 && !stopRequested) delay(500);
    return;
  }
  if (line == "IF_CLIENT_DISCONNECTED_WIFI") {
    while (WiFi.softAPgetStationNum() > 0 && !stopRequested) delay(500);
    return;
  }
  if (line == "IF_CLIENT_DISCONNECTED_BLUETOOTH") {
    while (getBTClientCount() > 0 && !stopRequested) delay(500);
    return;
  }
  if (line == "IF_CLIENT_DISCONNECTED") {
    while ((WiFi.softAPgetStationNum() > 0 || getBTClientCount() > 0) && !stopRequested) delay(500);
    return;
  }

  if (line.indexOf('=') != -1) {
    int eqIdx = line.indexOf('=');
    String varName = line.substring(0, eqIdx);
    String varVal = line.substring(eqIdx + 1);
    varName.trim();
    varVal.trim();
    
    if (varName == "VAR" || varName.startsWith("VAR_") || varName.startsWith("VARIABLE_")) {
      variables[varName] = processVariables(varVal);
      return;
    }
  }

  if (line.startsWith("LED ") || line.startsWith("RGB ")) {
    String params = line.substring(4);
    params.trim();
    if (params == "OFF") {
      pixels.setPixelColor(0, pixels.Color(0, 0, 0));
      pixels.show();
    } else {
      int r = 0, g = 0, b = 0;
      int s1 = params.indexOf(' ');
      if (s1 != -1) {
        r = params.substring(0, s1).toInt();
        int s2 = params.indexOf(' ', s1 + 1);
        if (s2 != -1) {
          g = params.substring(s1 + 1, s2).toInt();
          b = params.substring(s2 + 1).toInt();
        } else {
          g = params.substring(s1 + 1).toInt();
        }
      } else {
        r = params.toInt();
      }
      setLED(r, g, b);
    }
    return;
  }

  if (line == "SAVE_CREDENTIALS") {
    if (WiFi.status() == WL_CONNECTED) {
      saveWiFiCredentials(current_sta_ssid, current_sta_password);
    } else {
      Serial.println("[Interpreter] Cannot save credentials: Not connected to a WiFi");
    }
    return;
  }

  if (line == "IF_CONNECTED_TO_WIFI") {
    while (WiFi.status() != WL_CONNECTED && !stopRequested) delay(500);
    return;
  }

  handleKeyInput(line);
}

String processVariables(String text) {
  String result = text;
  
  // Handle methods like .toString() and .padStart()
  // Very basic regex-like replacement for ${i.toString().padStart(4,"0")}
  if (result.indexOf(".toString()") != -1) {
    for (auto const& [key, val] : variables) {
      String search = "${" + key + ".toString().padStart(";
      int startIdx = result.indexOf(search);
      if (startIdx != -1) {
         int endIdx = result.indexOf(")}", startIdx);
         if (endIdx != -1) {
           String params = result.substring(startIdx + search.length(), endIdx);
           int commaIdx = params.indexOf(',');
           int padLen = params.substring(0, commaIdx).toInt();
           String padChar = params.substring(commaIdx + 1);
           if (padChar.startsWith("\"")) padChar = padChar.substring(1, padChar.length() - 1);
           
           String paddedVal = val;
           while (paddedVal.length() < padLen) paddedVal = padChar + paddedVal;
           result.replace(result.substring(startIdx, endIdx + 2), paddedVal);
         }
      }
    }
  }

  // Sort keys by length descending to prevent partial replacements
  std::vector<String> keys;
  for (auto const& [key, val] : variables) keys.push_back(key);
  std::sort(keys.begin(), keys.end(), [](const String& a, const String& b) {
    return a.length() > b.length();
  });

  for (String const& key : keys) {
    String val = variables[key];
    result.replace("${" + key + "}", val);
    result.replace("$" + key, val);
    
    int idx = 0;
    while ((idx = result.indexOf(key, idx)) != -1) {
      bool startOk = (idx == 0 || (!isalnum(result.charAt(idx - 1)) && result.charAt(idx - 1) != '_'));
      bool endOk = (idx + key.length() >= result.length() || (!isalnum(result.charAt(idx + key.length())) && result.charAt(idx + key.length()) != '_'));
      if (startOk && endOk) {
        result = result.substring(0, idx) + val + result.substring(idx + key.length());
        idx += val.length();
      } else {
        idx += key.length();
      }
    }
  }
  return result;
}

void detectOS() {
  Serial.println("Starting OS detection...");
  detectedOS = "Unknown";
  
  keyboard.press(KEY_LEFT_GUI);
  keyboard.press('r');
  delay(100);
  keyboard.releaseAll();
  delay(1000);
  
  fastTypeString("cmd");
  delay(500);
  fastPressKey("ENTER");
  delay(1000);
  
  fastTypeString("ver");
  fastPressKey("ENTER");
  delay(500);
  
  fastPressKey("CTRL");
  fastPressKey("ALT");
  fastPressKey("t");
  delay(100);
  keyboard.releaseAll();
  delay(1000);
  
  fastPressKey("ESC");
  delay(500);
  
  fastPressKey("HOME");
  delay(500);
  
  detectedOS = "Windows"; // Default assumption for badusb
  Serial.println("OS detection completed. Detected OS: " + detectedOS);
  variables["DETECTED_OS"] = detectedOS;
}

void selfDestruct() {
  Serial.println("SELF DESTRUCT INITIATED!");
  setLEDMode(2);

  if (sdCardPresent) {
    auto deleteFilesInDir = [](String path) {
      File root = SD.open(path);
      if (root) {
        File file = root.openNextFile();
        while (file) {
          if (!file.isDirectory()) {
            String fileName = file.name();
            SD.remove(path + "/" + fileName);
            Serial.println("Deleted: " + fileName);
          }
          file = root.openNextFile();
        }
        root.close();
      }
    };

    deleteFilesInDir(DIR_SCRIPTS);
    deleteFilesInDir(DIR_LANGUAGES);
    deleteFilesInDir(DIR_LOGS);
    deleteFilesInDir(DIR_UPLOADS);
  }

  delay(2000);
  ESP.restart();
}

#include <WiFi.h>
#include <vector>

extern std::vector<String> foundBTDevices;
extern bool deviceConnected;

void processRower() {
  if (rower.active && !scriptRunning) {
    if (rower.currentPayloadIdx < rower.payloads.size()) {
      String nextPayload = rower.payloads[rower.currentPayloadIdx];
      rower.currentPayloadIdx++;
      Serial.println("Rower executing next: " + nextPayload);
      
      // Load and execute
      String content = loadScript(nextPayload);
      if (content.length() > 0) {
        executeScript(content);
      }
    } else {
      rower.active = false;
      rower.payloads.clear();
      Serial.println("Rower completed.");
    }
  }
}

void processAutomation() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 2000 || scriptRunning) return; 
  lastCheck = millis();

  // 1. WiFi Connection Triggers
  int currentWiFiClients = WiFi.softAPgetStationNum();
  static int lastWiFiClients = 0;
  if (currentWiFiClients != lastWiFiClients) {
    if (variables.count("IF_CLIENT_CONNECTED_DISCONNECTED_WIFI") || variables.count("IF_CLIENT_CONNECTED_DISCONNECTED")) {
       String content = loadScript("/scripts/wifi_change.txt");
       if (content.length() > 0) executeScript(content);
    }
    if (currentWiFiClients > lastWiFiClients) {
      if (variables.count("IF_CLIENT_CONNECTED_WIFI") || variables.count("IF_CLIENT_CONNECTED")) {
         String content = loadScript("/scripts/wifi_connect.txt");
         if (content.length() > 0) executeScript(content);
      }
    } else if (currentWiFiClients < lastWiFiClients) {
      if (variables.count("IF_CLIENT_DISCONNECTED_WIFI") || variables.count("IF_CLIENT_DISCONNECTED")) {
         String content = loadScript("/scripts/wifi_disconnect.txt");
         if (content.length() > 0) executeScript(content);
      }
    }
  }
  lastWiFiClients = currentWiFiClients;

  // 2. Bluetooth Connection Triggers
  static bool lastBTConnected = false;
  if (deviceConnected != lastBTConnected) {
    if (variables.count("IF_CLIENT_CONNECTED_DISCONNECTED_BLUETOOTH") || variables.count("IF_CLIENT_CONNECTED_DISCONNECTED")) {
       String content = loadScript("/scripts/bt_change.txt");
       if (content.length() > 0) executeScript(content);
    }
    if (deviceConnected && !lastBTConnected) {
      if (variables.count("IF_CLIENT_CONNECTED_BLUETOOTH") || variables.count("IF_CLIENT_CONNECTED")) {
         String content = loadScript("/scripts/bt_connect.txt");
         if (content.length() > 0) executeScript(content);
      }
    } else if (!deviceConnected && lastBTConnected) {
      if (variables.count("IF_CLIENT_DISCONNECTED_BLUETOOTH") || variables.count("IF_CLIENT_DISCONNECTED")) {
         String content = loadScript("/scripts/bt_disconnect.txt");
         if (content.length() > 0) executeScript(content);
      }
    }
  }
  lastBTConnected = deviceConnected;

  // 3. Bluetooth Discovery Triggers
  if (btDiscoveryEnabled && !foundBTDevices.empty()) {
    String triggerName = "";
    if (variables.count("RUN_WHEN_BLUETOOTH_FOUND")) triggerName = variables["RUN_WHEN_BLUETOOTH_FOUND"];
    else if (variables.count("RUN_WHEN_BT_FOUND")) triggerName = variables["RUN_WHEN_BT_FOUND"];
    else if (variables.count("BT_FOUND")) triggerName = variables["BT_FOUND"];

    if (triggerName.length() > 0) {
      triggerName.replace("\"", ""); // Strip quotes
      for (String& device : foundBTDevices) {
        if (device.indexOf(triggerName) != -1) {
          Serial.println("Bluetooth automation trigger: Found " + device);
          String content = loadScript("/scripts/bt_found.txt");
          if (content.length() > 0) executeScript(content);
          foundBTDevices.clear(); // Prevent re-triggering immediately
          break;
        }
      }
    }
  }

  // 4. Legacy WiFi Status Automation
  for (auto const& [key, val] : variables) {
    if (key.indexOf("_WHEN_WIFI=") != -1) {
      int q1 = val.indexOf('"') + 1, q2 = val.indexOf('"', q1);
      if (q2 > q1) {
        String ssid = val.substring(q1, q2);
        bool online = (val.indexOf("IS_ONLINE") != -1);
        scanWiFi();
        bool present = isSSIDPresent(ssid);
        
        if (present == online) {
          if (key.startsWith("WIFI_OFF")) WiFi.mode(WIFI_OFF);
          else if (key.startsWith("WIFI_ON")) setupAP();
          else if (key.startsWith("BLUETOOTH_OFF")) stopBT();
          else if (key.startsWith("BLUETOOTH_ON")) setupBT();
        }
      }
    }
  }
}

void saveSettings() {
  preferences.putString("ap_ssid", ap_ssid);
  preferences.putString("ap_password", ap_password);
  preferences.putString("language", currentLanguage);
  String bootFiles = "";
  for (size_t i=0; i<currentBootScriptFiles.size(); i++) {
    if (i > 0) bootFiles += ",";
    bootFiles += currentBootScriptFiles[i];
  }
  preferences.putString("boot_script", bootFiles);
  preferences.putInt("wifi_scan_time", wifiScanTime);
  preferences.putBool("led_enabled", ledEnabled);
  preferences.putBool("logging_enabled", loggingEnabled);
  preferences.putBool("autoconnect", autoConnectEnabled);
  preferences.putBool("save_creds", saveOnConnectEnabled);
  preferences.putBool("bt_discovery", btDiscoveryEnabled);
  preferences.putString("usb_vid", currentUSBConfig.vid);
  preferences.putString("usb_pid", currentUSBConfig.pid);
  preferences.putBool("usb_rndVid", currentUSBConfig.rndVid);
  preferences.putBool("usb_rndPid", currentUSBConfig.rndPid);
  preferences.putString("usb_mfr", currentUSBConfig.mfr);
  preferences.putString("usb_prod", currentUSBConfig.prod);
  Serial.println("Settings saved");
}

// ============================================================
// Background Task Processing
// ============================================================
void processBackgroundTasks() {
  if (activeTasks.empty()) return;

  String curTime = getTime("us");
  String curDay = getDay("us");

  for (auto it = activeTasks.begin(); it != activeTasks.end(); ) {
    bool completed = false;

    if (it->type == "WIFI_JOINING") {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[Task] WiFi connected successfully");
        variables["WIFI_CONNECTED"] = "true";
        variables["WIFI_SSID"] = it->payload;
        completed = true;
        wifiJoining = false;
      } else if (millis() - wifiJoinStartTime > 30000) {
        Serial.println("[Task] WiFi connection timeout");
        lastError = "WiFi join timeout";
        errorCount++;
        completed = true;
        wifiJoining = false;
        WiFi.disconnect();
      }
    } 
    else if (it->type == "TIME_TRIGGER") {
      if (curTime.startsWith(it->payload)) {
        Serial.println("[Task] Time trigger hit: " + it->payload);
        completed = true;
      }
    }
    else if (it->type == "DAY_TRIGGER") {
      if (curDay.equalsIgnoreCase(it->payload)) {
        Serial.println("[Task] Day trigger hit: " + it->payload);
        completed = true;
      }
    }
    else if (it->type == "WIFI_TRIGGER") {
      if (isSSIDPresent(it->payload)) {
        Serial.println("[Task] WiFi trigger hit: " + it->payload);
        completed = true;
      }
    }
    else if (it->type == "SD_REMOVAL_TRIGGER") {
      if (!sdCardPresent) {
        Serial.println("[Task] SD Removal trigger hit. Executing stored payload.");
        // We need a way to execute the block. Since executeScript is not recursive easily,
        // we process line by line.
        String p = it->payload;
        int s = 0;
        int e = p.indexOf('\n');
        while (e != -1) {
          String l = p.substring(s, e);
          l.trim();
          if (l.length() > 0) executeCommand(l);
          s = e + 1;
          e = p.indexOf('\n', s);
        }
        completed = true;
      }
    }

    if (completed) {
      it = activeTasks.erase(it);
    } else {
      ++it;
    }
  }
}

#ifndef DUCKY_INTERPRETER_H
#define DUCKY_INTERPRETER_H

#include "GlobalState.h"

void executeScript(const String& script);
void executeCommand(String line);
String processVariables(String text);
bool evalCondition(String condition);
void detectOS();
void selfDestruct();
void saveSettings();
void processRower();
void processAutomation();
void processBackgroundTasks();

#endif // DUCKY_INTERPRETER_H

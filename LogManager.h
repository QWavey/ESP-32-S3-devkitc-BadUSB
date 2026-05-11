#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include "GlobalState.h"

void openLogFile();
void closeLogFile();
void logCommand(String type, String command);
void logDebug(String message);  // Always-on verbose debug log to /logs/debug.txt
void loadCommandHistory();
void saveCommandHistory();
void addToHistory(String command);
void clearErrors();

#endif // LOG_MANAGER_H

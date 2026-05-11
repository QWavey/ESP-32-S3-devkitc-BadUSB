#ifndef FS_MANAGER_H
#define FS_MANAGER_H

#include "GlobalState.h"
#include <SPI.h>

bool initSDCard();
void checkSDCard();
void loadAvailableLanguages();
void loadAvailableScripts();
bool loadLanguage(String language);
String loadScript(String filename);
bool saveScript(String filename, String content);
bool deleteScript(String filename);

// File Operations
void changeDirectory(String path);
bool downloadFileFromURL(String url, String path);
bool uploadFileToServer(String localPath, String remoteUrl);
void useFile(String filePath);
void useFiles(std::vector<String> filePaths);
void copyFile(String sourcePath, String destPath);
void cutFile(String sourcePath, String destPath);
void pasteFile(String destPath);

// Helpers
String getFileNameFromPath(String path);
String getParentDirectory(String path);
bool copySDFile(String sourcePath, String destPath);
bool moveSDFile(String sourcePath, String destPath);
bool deleteDirectory(String path);
bool ensureDirectoryExists(String path);

#endif // FS_MANAGER_H

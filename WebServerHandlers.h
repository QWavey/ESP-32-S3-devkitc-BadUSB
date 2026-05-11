#ifndef WEB_SERVER_HANDLERS_H
#define WEB_SERVER_HANDLERS_H

#include "GlobalState.h"

// API Handlers
void handleFileUpload();
void handleFileDownload();
void handleChangeDirectory();
void handleGetCurrentDirectory();
void handleDetectOS();
void handleUseFile();
void handleCopyFile();
void handleCutFile();
void handlePasteFile();
void handleJoinInternet();
void handleLeaveInternet();
void handleListFiles();
void handleDeleteFile();
void handleCreateDirectory();
void handleFileInfo();

#endif // WEB_SERVER_HANDLERS_H

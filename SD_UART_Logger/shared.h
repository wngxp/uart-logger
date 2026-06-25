/* ============================================================================
 *  shared.h  —  common includes, global state (extern) and prototypes.
 *  Every .cpp in this sketch includes this; the .ino defines the globals.
 * ==========================================================================*/
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <SD_MMC.h>
#include <vector>
#include "config.h"

#if ENABLE_LCD
  #include <Arduino_GFX_Library.h>
  extern Arduino_GFX *gfx;
#endif

// ------------------------------------------------- global state -----------
extern WebServer        server;
extern volatile Mode    mode;

extern volatile bool    recording;         // logging UART1 -> SD?
extern volatile bool    startReq;          // button asked to start
extern volatile bool    stopReq;           // button asked to stop
extern bool             sdOk;

extern String           currentFile;       // path being written
extern char             g_curFile[24];     // fixed snapshot for the LCD task
extern File             logFile;
extern uint8_t          logBuf[LOG_BUF_SIZE];
extern size_t           logIdx;
extern SemaphoreHandle_t sdMutex;           // guards all SD access

extern uint64_t         g_totalBytes, g_freeBytes;
extern float            g_battV;

// Suppress debug prints while an RFC2217 client is attached is NOT needed
// (RFC2217 runs over WiFi, not the USB console) — so debug is always allowed.
#define DBG(...) do { Serial.printf(__VA_ARGS__); } while (0)

// ------------------------------------------------- storage.cpp ------------
bool   mountSD();
bool   formatSD();
void   refreshStorage();
String getNewFileName();
String humanSize(uint64_t b);
bool   safeName(const String &n);

// ------------------------------------------------- display.cpp ------------
void   displayInit();
void   displayUpdate();
float  readBattery();
int    batteryPct(float v);
void   setRGB(uint8_t r, uint8_t g, uint8_t b);

// ------------------------------------------------- logger.cpp -------------
void   uartTask(void *pv);                 // owns UART1 (logger + rfc2217)
void   pollButton();
void   stopRecordingAndWait();             // signal+wait (call from loop/web)
void   closeLogNow();                      // direct close (call from uartTask)
void   targetIdle();
void   targetEnterBootloader();            // reset target into download mode

// ------------------------------------------------- rfc2217.cpp ------------
void   rfc2217Begin();
bool   rfc2217Service();                   // returns true while a client owns UART1

// ------------------------------------------------- flasher.cpp ------------
bool   flashTargetFromFile(const char *path, uint32_t offset, String &msg);

// ------------------------------------------------- web.cpp ----------------
void   webBegin();
String statusBarHtml();

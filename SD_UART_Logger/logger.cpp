/* ============================================================================
 *  logger.cpp  —  UART1 owner task (logs to SD, or yields to the RFC2217
 *  WiFi server) + the BOOT button + target reset helpers.
 * ==========================================================================*/
#include "shared.h"

// ---- target EN/IO0 are driven open-drain: we only ever pull LOW; the target
//      pull-ups keep them HIGH so its own buttons keep working.
void targetIdle() {
  digitalWrite(TARGET_EN_PIN,  HIGH);
  digitalWrite(TARGET_IO0_PIN, HIGH);
}

// Classic esptool reset sequence: leave the target in serial download mode.
void targetEnterBootloader() {
  digitalWrite(TARGET_IO0_PIN, LOW);    // hold BOOT low
  digitalWrite(TARGET_EN_PIN,  LOW);    // assert reset
  delay(100);
  digitalWrite(TARGET_EN_PIN,  HIGH);   // release reset -> samples IO0 low
  delay(50);
  digitalWrite(TARGET_IO0_PIN, HIGH);   // release BOOT
}

// Signal the worker task to stop and wait for it. Call ONLY from another task
// (e.g. loop()/web handlers) — never from uartTask itself, or it deadlocks.
void stopRecordingAndWait() {
  if (!recording) return;
  stopReq = true;
  for (int i = 0; i < 400 && recording; i++) vTaskDelay(pdMS_TO_TICKS(5)); // <=2 s
}

// Flush+close the log immediately. Call ONLY from uartTask (it owns logFile).
void closeLogNow() {
  if (!recording) return;
  xSemaphoreTake(sdMutex, portMAX_DELAY);
  if (logIdx > 0) { logFile.write(logBuf, logIdx); logIdx = 0; }
  logFile.flush();
  logFile.close();
  xSemaphoreGive(sdMutex);
  recording = false;
  startReq = false;
  stopReq = false;
  g_curFile[0] = 0;
  currentFile = "";
  refreshStorage();
}

// --------------------------------------------------- graph parser ----------
#if LOG_PARSE_ENABLE
static char g_parseBuf[128];
static int  g_parsePos = 0;

static void parseForGraph(const uint8_t *data, int len) {
  for (int i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '\n' || c == '\r') {
      if (g_parsePos > 0) {
        g_parseBuf[g_parsePos] = '\0';
        float a, v, t;
        if (sscanf(g_parseBuf, LOG_PARSE_FMT, &a, &v, &t) == 3) {
          int idx       = g_dataTail;
          g_dataBuf[idx] = { (uint32_t)millis(), a, v, t };
          g_dataTail    = (idx + 1) % LOG_GRAPH_BUF;
          if (g_dataCount < LOG_GRAPH_BUF) g_dataCount++;
        }
        g_parsePos = 0;
      }
    } else if (g_parsePos < (int)sizeof(g_parseBuf) - 1) {
      g_parseBuf[g_parsePos++] = c;
    }
  }
}
#endif

// --------------------------------------------------- logging step ---------
static void loggerStep() {
  if (startReq) {
    startReq = false;
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    currentFile = getNewFileName();
    logFile = SD_MMC.open(currentFile, FILE_WRITE);
    xSemaphoreGive(sdMutex);

    if (!logFile) {
      DBG("[REC] open failed\n");
      currentFile = "";
    } else {
      while (Serial1.available()) Serial1.read();   // drop stale bytes
      logIdx = 0;
#if LOG_PARSE_ENABLE
      g_parsePos = 0; g_dataCount = 0; g_dataTail = 0; // reset graph on new recording
#endif
      snprintf(g_curFile, sizeof(g_curFile), "%s", currentFile.c_str());
      recording = true;
      DBG("[REC] -> %s\n", currentFile.c_str());
    }
  }

  // Always drain Serial1 — web monitor works even when not recording
  int avail = Serial1.available();
  if (avail > 0) {
    if (recording) {
      size_t space = LOG_BUF_SIZE - logIdx;
      size_t want  = ((size_t)avail < space) ? (size_t)avail : space;
      int got = Serial1.readBytes(logBuf + logIdx, want);
      if (got > 0) {
        feedMonitor(logBuf + logIdx, got);
#if LOG_PARSE_ENABLE
        parseForGraph(logBuf + logIdx, got);
#endif
        logIdx += got;
      }
      if (logIdx >= LOG_FLUSH_AT) {
        xSemaphoreTake(sdMutex, portMAX_DELAY);
        logFile.write(logBuf, logIdx);
        xSemaphoreGive(sdMutex);
        logIdx = 0;
      }
    } else {
      // Not recording — use logBuf as scratch and feed monitor only
      size_t want = ((size_t)avail < (size_t)LOG_BUF_SIZE) ? (size_t)avail : (size_t)LOG_BUF_SIZE;
      int got = Serial1.readBytes(logBuf, want);
      if (got > 0) feedMonitor(logBuf, got);
    }
  } else {
    vTaskDelay(1);
  }

  if (stopReq) {
    stopReq = false;
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    if (logIdx > 0) { logFile.write(logBuf, logIdx); logIdx = 0; }
    logFile.flush();
    logFile.close();
    xSemaphoreGive(sdMutex);
    recording = false;
    g_curFile[0] = 0;
    DBG("[REC] closed %s\n", currentFile.c_str());
    currentFile = "";
    refreshStorage();
  }
}

// One task owns UART1. Priority over logging: flashing > WiFi client > logger.
void uartTask(void *pv) {
  for (;;) {
    if (mode == MODE_FLASHING) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    if (rfc2217Service())      { continue; }   // a WiFi client is attached
    loggerStep();
  }
}

// --------------------------------------------------- BOOT button ----------
// Short press toggles recording (only meaningful in logger mode).
void pollButton() {
  static bool     stable    = HIGH;
  static bool     lastRead  = HIGH;
  static uint32_t lastChange = 0;

  bool r = digitalRead(BTN_PIN);
  if (r != lastRead) { lastRead = r; lastChange = millis(); }

  if (millis() - lastChange > 40) {            // debounce
    if (r != stable) {
      stable = r;
      if (r == LOW && mode != MODE_FLASHING) {   // falling edge — allowed in logger + RFC2217 modes
        if (!recording && !startReq) startReq = true;
        else if (recording && !stopReq) stopReq = true;
      }
    }
  }
}

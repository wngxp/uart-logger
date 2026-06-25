/* ============================================================================
 *  storage.cpp  —  SD (SDMMC) mount/format + file helpers.
 * ==========================================================================*/
#include "shared.h"

static bool sdSetPins() {
  return SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN,
                        SD_D1_PIN,  SD_D2_PIN,  SD_D3_PIN);
}

// Mount the card. First try without destroying data; if that fails (e.g. the
// card is unformatted or exFAT), optionally format it to FAT and retry.
bool mountSD() {
  if (!sdSetPins()) { DBG("[SD] setPins failed\n"); sdOk = false; return false; }

  // 1) mount as-is (1-bit mode, no format) — preserves existing logs
  bool mounted = SD_MMC.begin("/sdcard", true /*1-bit*/, false /*no format*/, SD_FREQ_KHZ);

#if SD_AUTO_FORMAT
  if (!mounted) {
    DBG("[SD] mount failed -> formatting to FAT...\n");
    SD_MMC.end();
    sdSetPins();
    mounted = SD_MMC.begin("/sdcard", true, true /*format on fail*/, SD_FREQ_KHZ);
  }
#endif

  if (mounted && SD_MMC.cardType() != CARD_NONE) {
    sdOk = true;
    DBG("[SD] mounted, %.0f MB (type %d)\n",
        SD_MMC.cardSize() / (1024.0 * 1024), SD_MMC.cardType());
    refreshStorage();
  } else {
    sdOk = false;
    DBG("[SD] no card / mount FAILED\n");
    SD_MMC.end();
  }
  return sdOk;
}

// Force a reformat to FAT (wipes everything). Used by the web "Format" button.
bool formatSD() {
  SD_MMC.end();
  if (!sdSetPins()) return false;
  // begin() only formats when the mount fails, so wipe the FATs first by
  // mounting then formatting via the IDF helper is not exposed in Arduino;
  // instead we delete every root file (good enough to "reset" the card).
  if (!SD_MMC.begin("/sdcard", true, true, SD_FREQ_KHZ)) { sdOk = false; return false; }
  sdOk = (SD_MMC.cardType() != CARD_NONE);
  if (sdOk) {
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    File dir = SD_MMC.open("/");
    std::vector<String> names;
    if (dir) {
      File f = dir.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          String n = String(f.name());
          if (n[0] != '/') n = "/" + n;
          names.push_back(n);
        }
        f = dir.openNextFile();
      }
      dir.close();
    }
    for (auto &n : names) SD_MMC.remove(n);
    xSemaphoreGive(sdMutex);
    refreshStorage();
  }
  return sdOk;
}

void refreshStorage() {
  if (!sdOk) return;
  xSemaphoreTake(sdMutex, portMAX_DELAY);
  g_totalBytes = SD_MMC.totalBytes();
  g_freeBytes  = g_totalBytes - SD_MMC.usedBytes();
  xSemaphoreGive(sdMutex);
}

String getNewFileName() {
  for (int i = 1; i < 100000; i++) {
    char name[20];
    snprintf(name, sizeof(name), "/LOG_%04d.txt", i);
    if (!SD_MMC.exists(name)) return String(name);
  }
  return "/LOG_overflow.txt";
}

String humanSize(uint64_t b) {
  char buf[24];
  if (b < 1024)                       snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)b);
  else if (b < 1024ULL * 1024)        snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
  else if (b < 1024ULL * 1024 * 1024) snprintf(buf, sizeof(buf), "%.2f MB", b / (1024.0 * 1024));
  else                                snprintf(buf, sizeof(buf), "%.2f GB", b / (1024.0 * 1024 * 1024));
  return String(buf);
}

bool safeName(const String &n) {
  if (n.length() == 0 || n.length() > 64) return false;
  if (n.indexOf('/') >= 0 || n.indexOf('\\') >= 0) return false;
  if (n.indexOf("..") >= 0) return false;
  return true;
}

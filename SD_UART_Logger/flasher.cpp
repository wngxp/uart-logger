/* ============================================================================
 *  flasher.cpp  —  on-device target flashing from an uploaded .bin (feature 1).
 *
 *  SELF-CONTAINED: speaks the ESP serial ROM-bootloader protocol directly
 *  (SLIP framing -> SYNC -> SPI_ATTACH -> FLASH_BEGIN/DATA/END), the same
 *  protocol esptool uses. No external library needed.
 *
 *  ROM loader only (no flasher stub): slower than esptool's stub mode and no
 *  compression, but dependency-free and fine for typical app images. Works for
 *  ESP32 / S2 / S3 / C3 targets (chip is auto-detected via the magic register).
 *  Uses the Arduino Serial1 we already own (uartTask is idle in MODE_FLASHING).
 * ==========================================================================*/
#include "shared.h"

#if ENABLE_WEB_FLASH

#define SLIP_END     0xC0
#define SLIP_ESC     0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

#define CMD_FLASH_BEGIN 0x02
#define CMD_FLASH_DATA  0x03
#define CMD_FLASH_END   0x04
#define CMD_SYNC        0x08
#define CMD_READ_REG    0x0A
#define CMD_CHANGE_BAUD 0x0F
#define CMD_SPI_ATTACH  0x0D

#define CHIP_MAGIC_REG  0x40001000UL
#define ESP32_MAGIC     0x00f01d83UL
#define ROM_BLOCK       1024            // FLASH_DATA block size for the ROM loader

static uint32_t g_chipMagic  = 0;
static String  *g_flashLog   = nullptr;
static bool     g_flashStream = false;
static int      g_flashPct    = -1;     // set before a flog() call to update progress bar

// Write a line to Serial AND to the web result log simultaneously.
static void flog(const char *fmt, ...) {
  char buf[220];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
  Serial.printf("[FLASH] %s\n", buf);
  if (g_flashLog) { *g_flashLog += buf; *g_flashLog += '\n'; }
  if (g_flashStream) {
    int pct = g_flashPct; g_flashPct = -1;
    // escape single quotes and backslashes so the string is safe inside JS ''
    char esc[240]; int ei = 0;
    for (const char *s = buf; *s && ei < (int)sizeof(esc) - 3; s++) {
      if      (*s == '\'') { esc[ei++] = '\\'; esc[ei++] = '\''; }
      else if (*s == '\\') { esc[ei++] = '\\'; esc[ei++] = '\\'; }
      else                   esc[ei++] = *s;
    }
    esc[ei] = '\0';
    char chunk[300];
    snprintf(chunk, sizeof(chunk), "<script>U(%d,'%s')</script>", pct, esc);
    server.sendContent(chunk);
  }
}

void flashEnableStream()  { g_flashStream = true;  g_flashPct = -1; }
void flashDisableStream() { g_flashStream = false; }

#if DEBUG_FLASH
// Hex dump up to 64 bytes of a buffer, 16 per line.
static void flogHex(const char *label, const uint8_t *d, int n) {
  char line[64]; int li = 0;
  for (int i = 0; i < n && i < 64; i++) {
    li += snprintf(line + li, sizeof(line) - li, "%02X ", d[i]);
    if ((i & 15) == 15 || i == n - 1) { flog("  %s +%02d: %s", label, i & ~15, line); li = 0; }
  }
  if (n > 64) flog("  %s ... (%d B total, showing first 64)", label, n);
}
#endif

// ------------------------------------------------- low-level SLIP ---------
static void slipByte(uint8_t b) {
  if      (b == SLIP_END) { Serial1.write(SLIP_ESC); Serial1.write((uint8_t)SLIP_ESC_END); }
  else if (b == SLIP_ESC) { Serial1.write(SLIP_ESC); Serial1.write((uint8_t)SLIP_ESC_ESC); }
  else                      Serial1.write(b);
}

static void put32(uint8_t *b, int &p, uint32_t v) {
  b[p++] = v; b[p++] = v >> 8; b[p++] = v >> 16; b[p++] = v >> 24;
}

static void sendCommand(uint8_t cmd, const uint8_t *data, uint16_t len, uint32_t checksum) {
  uint8_t h[8];
  h[0] = 0x00; h[1] = cmd; h[2] = len & 0xFF; h[3] = len >> 8;
  h[4] = checksum; h[5] = checksum >> 8; h[6] = checksum >> 16; h[7] = checksum >> 24;
  Serial1.write((uint8_t)SLIP_END);
  for (int i = 0; i < 8; i++) slipByte(h[i]);
  for (uint16_t i = 0; i < len; i++) slipByte(data[i]);
  Serial1.write((uint8_t)SLIP_END);
}

// Read one SLIP frame into buf. Returns payload length, or -1 on timeout.
static int readPacket(uint8_t *buf, int maxLen, uint32_t timeoutMs) {
  uint32_t start = millis();
  bool inFrame = false, esc = false;
  int idx = 0;
  while (millis() - start < timeoutMs) {
    if (!Serial1.available()) { vTaskDelay(1); continue; }
    uint8_t b = Serial1.read();
    if (!inFrame) { if (b == SLIP_END) inFrame = true; continue; }
    if (b == SLIP_END) { if (idx == 0) continue; return idx; }
    if (esc) { b = (b == SLIP_ESC_END) ? SLIP_END : (b == SLIP_ESC_ESC) ? SLIP_ESC : b; esc = false; }
    else if (b == SLIP_ESC) { esc = true; continue; }
    if (idx < maxLen) buf[idx++] = b;
  }
  return -1;
}

// Send a command and wait for its response. Returns true if status byte == 0.
static bool doCommand(uint8_t cmd, const uint8_t *data, uint16_t len,
                      uint32_t checksum, uint32_t timeoutMs, uint32_t *outValue = nullptr) {
  sendCommand(cmd, data, len, checksum);
  uint8_t resp[64];
  uint32_t start = millis();
  bool gotAny = false;
  while (millis() - start < timeoutMs) {
    int n = readPacket(resp, sizeof(resp), timeoutMs);
    if (n < 0) {
#if DEBUG_FLASH
      flog("  cmd 0x%02X: no SLIP frame in %u ms (gotAny=%d)", cmd, timeoutMs, gotAny);
#endif
      return false;
    }
    gotAny = true;
#if DEBUG_FLASH
    flogHex("rx", resp, n);
#endif
    if (n >= 8 && resp[0] == 0x01 && resp[1] == cmd) {
      uint16_t size = resp[2] | (resp[3] << 8);
      if (outValue) *outValue = resp[4] | (resp[5] << 8) | (resp[6] << 16) | ((uint32_t)resp[7] << 24);
      if (size >= 2 && 8 + size <= n) {
        bool ok = resp[8 + size - 2] == 0;
        if (!ok) flog("  cmd 0x%02X: ROM error byte 0x%02X", cmd, resp[8 + size - 1]);
        return ok;
      }
      return true;
    }
#if DEBUG_FLASH
    if (n >= 2) flog("  cmd 0x%02X: stale pkt dir=0x%02X op=0x%02X len=%d", cmd, resp[0], resp[1], n);
#endif
  }
#if DEBUG_FLASH
  flog("  cmd 0x%02X: outer loop timed out", cmd);
#endif
  return false;
}

// ------------------------------------------------- protocol steps ---------
static bool romSync() {
  uint8_t d[36]; d[0] = 0x07; d[1] = 0x07; d[2] = 0x12; d[3] = 0x20;
  for (int i = 4; i < 36; i++) d[i] = 0x55;
  for (int a = 0; a < 16; a++) {          // 16 attempts — ROM can be slow to answer
    flog("  sync attempt %d/16 ...", a + 1);
    if (doCommand(CMD_SYNC, d, sizeof(d), 0, 150)) {
      delay(60); while (Serial1.available()) Serial1.read();
      flog("  sync OK on attempt %d", a + 1);
      return true;
    }
  }
  flog("  sync FAILED after 16 attempts");
  // Drain whatever the target is actually sending — distinguishes "dead silent"
  // (wiring/power problem) from "target is responding but not a ROM bootloader".
  uint32_t t0 = millis(); int nr = 0;
  char raw[97]; int ri = 0;
  while (millis() - t0 < 300 && nr < 32) {
    if (!Serial1.available()) { vTaskDelay(1); continue; }
    uint8_t b = Serial1.read(); nr++;
    ri += snprintf(raw + ri, sizeof(raw) - ri, "%02X ", b);
  }
  if (nr) flog("  raw bytes from target after failure: %s(%d B) — target may be running app, not ROM bootloader", raw, nr);
  else    flog("  no bytes from target — check EN/IO0 wiring and target power");
  return false;
}

static uint32_t checksumXor(const uint8_t *d, int len) {
  uint32_t c = 0xEF;
  for (int i = 0; i < len; i++) c ^= d[i];
  return c;
}

bool flashTargetFromFile(const char *path, uint32_t offset, String &msg) {
  g_flashLog = &msg;
  msg = "";

  xSemaphoreTake(sdMutex, portMAX_DELAY);
  File fw = SD_MMC.open(path, FILE_READ);
  uint32_t size = fw ? fw.size() : 0;
  xSemaphoreGive(sdMutex);

  if (!fw)       { flog("ERROR: temp file not found on SD"); g_flashLog = nullptr; return false; }
  if (size == 0) { fw.close(); flog("ERROR: uploaded file is empty"); g_flashLog = nullptr; return false; }
  flog("File: %s  size: %u bytes  offset: 0x%X", path, size, offset);

  Serial1.updateBaudRate(115200);
  flog("UART set to 115200 baud");
  targetEnterBootloader();
  flog("Target reset into bootloader mode (EN + IO0 pulsed)");
  delay(100);
  while (Serial1.available()) Serial1.read();

  bool ok = false;
  do {
    flog("--- SYNC ---");
    if (!romSync()) {
      flog("FAILED: no SYNC — check EN/IO0 wiring and target power");
      break;
    }

    flog("--- CHIP ID ---");
    uint8_t rr[4]; int p = 0; put32(rr, p, CHIP_MAGIC_REG);
    doCommand(CMD_READ_REG, rr, 4, 0, 1000, &g_chipMagic);
    bool isEsp32 = (g_chipMagic == ESP32_MAGIC);
    flog("  chip magic: 0x%08X  (%s)", g_chipMagic,
         isEsp32        ? "ESP32" :
         g_chipMagic==0 ? "unreadable (treating as S2/S3/C3)" :
                          "ESP32-S2/S3/C3 or newer");

    flog("--- SPI ATTACH ---");
    uint8_t at[8] = {0};
    bool spiOk = doCommand(CMD_SPI_ATTACH, at, 8, 0, 3000);
    flog("  SPI attach: %s", spiOk ? "OK" : "no ACK (may still work)");

  #if WEB_FLASH_HIGHER_BAUD
    flog("--- BAUD CHANGE -> %u ---", (uint32_t)WEB_FLASH_HIGHER_BAUD);
    { uint8_t cb[8]; int q = 0; put32(cb, q, WEB_FLASH_HIGHER_BAUD); put32(cb, q, 0);
      if (doCommand(CMD_CHANGE_BAUD, cb, 8, 0, 2000)) {
        delay(20); Serial1.updateBaudRate(WEB_FLASH_HIGHER_BAUD); delay(20);
        while (Serial1.available()) Serial1.read();
        flog("  baud changed to %u", (uint32_t)WEB_FLASH_HIGHER_BAUD);
      } else {
        flog("  baud change failed — staying at 115200");
      }
    }
  #endif

    uint32_t blocks = (size + ROM_BLOCK - 1) / ROM_BLOCK;
    flog("--- FLASH_BEGIN: %u bytes, %u blocks of %u, erase may take ~10-30 s ---",
         size, blocks, ROM_BLOCK);
    uint8_t fb[20]; int fp = 0;
    put32(fb, fp, size); put32(fb, fp, blocks); put32(fb, fp, ROM_BLOCK); put32(fb, fp, offset);
    if (!isEsp32) put32(fb, fp, 0);               // encrypted-flag word for S2/S3/C3
    if (!doCommand(CMD_FLASH_BEGIN, fb, fp, 0, 30000)) {
      flog("FAILED: FLASH_BEGIN — erase timed out (check flash size vs offset)");
      break;
    }
    flog("  FLASH_BEGIN OK (erase done)");

    flog("--- FLASH_DATA ---");
    static uint8_t pkt[16 + ROM_BLOCK];
    uint32_t seq = 0, remaining = size;
    bool werr = false;
    uint32_t t0 = millis(), nextLogPct = 10;
    while (remaining > 0) {
      memset(pkt + 16, 0xFF, ROM_BLOCK);
      uint32_t chunk = remaining < ROM_BLOCK ? remaining : ROM_BLOCK;
      xSemaphoreTake(sdMutex, portMAX_DELAY);
      int r = fw.read(pkt + 16, chunk);
      xSemaphoreGive(sdMutex);
      if (r <= 0) { flog("FAILED: SD read error at block %u", seq); werr = true; break; }

      int hp = 0; put32(pkt, hp, ROM_BLOCK); put32(pkt, hp, seq); put32(pkt, hp, 0); put32(pkt, hp, 0);
      uint32_t cs = checksumXor(pkt + 16, ROM_BLOCK);
      if (!doCommand(CMD_FLASH_DATA, pkt, 16 + ROM_BLOCK, cs, 5000)) {
        flog("FAILED: FLASH_DATA at block %u/%u", seq, blocks);
        werr = true; break;
      }
      seq++; remaining -= (uint32_t)r;

      uint32_t pct = size ? (uint32_t)(((uint64_t)(size - remaining) * 100) / size) : 100;
      if (pct >= nextLogPct || remaining == 0) {
        uint32_t elapsed = millis() - t0;
        uint32_t kbps = elapsed ? (uint32_t)(((uint64_t)(size - remaining)) * 1000 / elapsed / 1024) : 0;
        g_flashPct = (int)pct;
        flog("  %3u%%  block %u/%u  %u KB/s", pct, seq, blocks, kbps);
        nextLogPct = ((pct / 10) + 1) * 10;
      }
    }
    if (werr) break;

    flog("--- FLASH_END ---");
    uint8_t fe[4]; int ep = 0; put32(fe, ep, 1);  // 1 = don't auto-reboot from ROM
    doCommand(CMD_FLASH_END, fe, 4, 0, 2000);

    uint32_t elapsed = millis() - t0;
    ok = true;
    flog("Flash complete: %u bytes @ 0x%X in %u ms", size, offset, elapsed);
  } while (0);

  fw.close();
  Serial1.updateBaudRate(UART_BAUD);
  flog("UART restored to %u baud", (uint32_t)UART_BAUD);
  targetIdle();
  digitalWrite(TARGET_EN_PIN, LOW); delay(50); digitalWrite(TARGET_EN_PIN, HIGH);
  flog("Target rebooted");

  g_flashLog = nullptr;
  return ok;
}

#else  // !ENABLE_WEB_FLASH

bool flashTargetFromFile(const char *path, uint32_t offset, String &msg) {
  (void)path; (void)offset;
  msg = "Web flashing disabled (ENABLE_WEB_FLASH = 0)";
  return false;
}

#endif

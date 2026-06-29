/* ============================================================================
 *  display.cpp  —  battery read, RGB status LED and the 1.47" LCD dashboard.
 * ==========================================================================*/
#include "shared.h"

// ------------------------------------------------------ battery -----------
float readBattery() {
  uint32_t acc = 0;
  for (int i = 0; i < 16; i++) acc += analogReadMilliVolts(BAT_ADC_PIN);
  float mv = acc / 16.0f;
  return (mv * BAT_DIV / 1000.0f) / BAT_CAL;   // board's calibration factor
}

int batteryPct(float v) {
  float p = (v - BAT_EMPTY_V) / (BAT_FULL_V - BAT_EMPTY_V) * 100.0f;
  if (p < 0)   p = 0;
  if (p > 100) p = 100;
  return (int)(p + 0.5f);
}

// ------------------------------------------------------ RGB ---------------
void setRGB(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(RGB_PIN, r, g, b); }

// pick an at-a-glance colour for the current state
static void rgbForState(bool wifi) {
  uint8_t L = RGB_LEVEL;
  if (!sdOk)                    setRGB(L, 0, L);     // magenta: SD problem
  else if (mode == MODE_FLASHING) setRGB(L, L/2, 0); // orange: flashing
  else if (mode == MODE_RFC2217)  setRGB(0, L, L);   // cyan: WiFi serial active
  else if (recording)           setRGB(L, 0, 0);     // red: recording
  else if (wifi)                setRGB(0, L, 0);     // green: ready
  else                          setRGB(0, 0, L);     // blue: connecting
}

#if ENABLE_LCD
// RGB565 color constants — defined explicitly so they compile on any
// Arduino_GFX_Library version (some releases dropped these macros).
#ifndef BLACK
# define BLACK    0x0000
# define WHITE    0xFFFF
# define RED      0xF800
# define GREEN    0x07E0
# define BLUE     0x001F
# define CYAN     0x07FF
# define MAGENTA  0xF81F
# define YELLOW   0xFFE0
# define ORANGE   0xFD20
# define DARKGREY 0x7BEF
#endif

Arduino_GFX *gfx = nullptr;
static Arduino_DataBus *bus = nullptr;

#define ROW_MODE 40
#define ROW_WIFI 72
#define ROW_BAT  104
#define ROW_FREE 136
#define ROW_FILE 168
#define ROW_STAT 200

static void lcdRow(int y, const String &s, uint16_t color) {
  gfx->fillRect(0, y, LCD_W, 24, BLACK);
  gfx->setTextColor(color);
  gfx->setTextSize(2);
  gfx->setCursor(4, y + 3);
  gfx->print(s);
}

static String shortBytes(uint64_t b) {
  char buf[16];
  if (b < 1024ULL * 1024)             snprintf(buf, sizeof(buf), "%.0fK", b / 1024.0);
  else if (b < 1024ULL * 1024 * 1024) snprintf(buf, sizeof(buf), "%.0fM", b / (1024.0 * 1024));
  else                                snprintf(buf, sizeof(buf), "%.1fG", b / (1024.0 * 1024 * 1024));
  return String(buf);
}
#endif // ENABLE_LCD

void displayInit() {
  analogReadResolution(12);
  setRGB(0, 0, RGB_LEVEL);                 // blue while booting
#if ENABLE_LCD
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);              // backlight on
  bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, LCD_MISO);
  // IPS=true -> the panel uses INVON (0x21), matching the board's own driver.
  gfx = new Arduino_ST7789(bus, LCD_RST, 0 /*rot*/, true /*IPS*/,
                           LCD_W, LCD_H, LCD_OFFX, LCD_OFFY, LCD_OFFX, LCD_OFFY);
  gfx->begin();
  gfx->fillScreen(BLACK);
  gfx->setTextWrap(false);
  gfx->setTextSize(2);
  gfx->setTextColor(WHITE);
  gfx->setCursor(4, 6);  gfx->print("SD UART");
  gfx->setCursor(4, 22); gfx->print("Logger");
  gfx->drawFastHLine(0, 38, LCD_W, DARKGREY);
#endif
}

void displayUpdate() {
  static uint32_t last = 0;
  if (millis() - last < 400) return;        // ~2.5 Hz
  last = millis();

  bool wifi = (WiFi.status() == WL_CONNECTED);
  rgbForState(wifi);

#if ENABLE_LCD
  static String pMode, pWifi, pBat, pFree, pFile, pStat;

  String sMode = (mode == MODE_RFC2217)  ? "Mode: WiFi"
               : (mode == MODE_FLASHING) ? "Mode: FLASH"
                                         : "Mode: LOGGER";
  uint16_t cMode = (mode == MODE_RFC2217) ? CYAN : (mode == MODE_FLASHING) ? ORANGE : WHITE;
  if (sMode != pMode) { lcdRow(ROW_MODE, sMode, cMode); pMode = sMode; }

  String sWifi = wifi ? WiFi.localIP().toString() : "WiFi: down";
  if (sWifi != pWifi) { lcdRow(ROW_WIFI, sWifi, wifi ? GREEN : BLUE); pWifi = sWifi; }

  char b[24];
  snprintf(b, sizeof(b), "Bat %.2fV %d%%", g_battV, batteryPct(g_battV));
  String sBat = b;
  if (sBat != pBat) { lcdRow(ROW_BAT, sBat, g_battV < BAT_EMPTY_V + 0.1f ? RED : WHITE); pBat = sBat; }

  String sFree = sdOk ? ("Free " + shortBytes(g_freeBytes)) : "SD: none";
  if (sFree != pFree) { lcdRow(ROW_FREE, sFree, sdOk ? WHITE : MAGENTA); pFree = sFree; }

  const char *cf = (g_curFile[0] == '/') ? g_curFile + 1 : g_curFile;
  String sFile = recording ? ("F:" + String(cf)) : "File: -";
  if (sFile != pFile) { lcdRow(ROW_FILE, sFile, recording ? YELLOW : DARKGREY); pFile = sFile; }

  String sStat; uint16_t cStat;
  if (!sdOk)                     { sStat = "SD ERROR";   cStat = MAGENTA; }
  else if (mode == MODE_FLASHING){ sStat = "Flashing";  cStat = ORANGE;  }
  else if (mode == MODE_RFC2217) { sStat = "WiFi serial";cStat = CYAN;   }
  else if (recording)            { sStat = "RECORDING";  cStat = RED;    }
  else if (wifi)                 { sStat = "Ready";      cStat = GREEN;  }
  else                           { sStat = "Connecting"; cStat = BLUE;   }
  if (sStat != pStat) { lcdRow(ROW_STAT, sStat, cStat); pStat = sStat; }
#endif
}

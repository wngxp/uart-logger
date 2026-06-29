/* ============================================================================
 *  SD_UART_Logger  —  Waveshare ESP32-S3-Touch-LCD-1.47
 *
 *  A "programmer/logger" board you wire to a TARGET esp32. It can:
 *    1) LOG the target's UART to the microSD card (BOOT button start/stop) and
 *       serve the files over Wi-Fi (download one / zip-all / delete).
 *    2) FLASH the target from an uploaded .bin via the web page (feature behind
 *       ENABLE_WEB_FLASH — needs the esp-serial-flasher library).
 *    3) Expose the target's UART over Wi-Fi as an RFC2217 port so a PC can
 *       FLASH and MONITOR it from esptool / VS Code ESP-IDF over the network:
 *         idf.py -p rfc2217://<device-ip>:3333 flash monitor
 *
 *  Status is shown on the 1.47" LCD and the RGB LED.
 *
 *  Files: config.h (pins/flags), shared.h (state), storage/display/logger/
 *  rfc2217/flasher/web .cpp modules.
 *
 *  Build: ESP32S3 Dev Module, PSRAM "OPI PSRAM".
 *    esp32 Arduino core 3.3.5 (3.3.6+ breaks GFX Library for Arduino).
 *    Library: "GFX Library for Arduino" (moononournation), if ENABLE_LCD.
 * ==========================================================================*/
#include "shared.h"

// ----------------------------- global definitions -------------------------
WebServer        server(80);
volatile Mode    mode = MODE_LOGGER;

volatile bool    recording = false;
volatile bool    startReq  = false;
volatile bool    stopReq   = false;
bool             sdOk      = false;

String           currentFile = "";
char             g_curFile[24] = {0};
File             logFile;
uint8_t          logBuf[LOG_BUF_SIZE];
size_t           logIdx = 0;
SemaphoreHandle_t sdMutex = nullptr;

uint64_t         g_totalBytes = 0, g_freeBytes = 0;
float            g_battV = 0;

#if LOG_PARSE_ENABLE
DataSample       g_dataBuf[LOG_GRAPH_BUF];
volatile int     g_dataTail = 0, g_dataCount = 0;
#endif

// =============================== SETUP ===================================
void setup() {
 
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(TARGET_EN_PIN,  OUTPUT_OPEN_DRAIN);   // only ever pulled LOW
  pinMode(TARGET_IO0_PIN, OUTPUT_OPEN_DRAIN);

   Serial.begin(115200);
  delay(3000);
  Serial.println("\n[BOOT] SD UART Logger / WiFi programmer");

  targetIdle();

  sdMutex = xSemaphoreCreateMutex();

  displayInit();                                // LCD + RGB + ADC
  g_battV = readBattery();

  // UART1 to the target (kept running; bytes stored only while recording)
  Serial1.setRxBufferSize(UART_RX_BUF);
  Serial1.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial1.setTimeout(0);

  mountSD();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WiFi] connecting to \"%s\"...\n", WIFI_SSID);

  webBegin();
  rfc2217Begin();

  // UART1 owner task on core 1 (Wi-Fi stack lives on core 0)
  xTaskCreatePinnedToCore(uartTask, "uart", 8192, NULL, 3, NULL, 1);
}

// =============================== LOOP ====================================
void loop() {
  static bool     announced = false;
  static uint32_t lastBat = 0, lastStor = 0;

  if (WiFi.status() == WL_CONNECTED && !announced) {
    announced = true;
    Serial.print("[WiFi] connected, IP: ");
    Serial.println(WiFi.localIP());
    if (MDNS.begin(MDNS_NAME))
      Serial.printf("[mDNS] flashed  http://%s.local/  | rfc2217://%s:%d\n",
                    MDNS_NAME, WiFi.localIP().toString().c_str(), RFC2217_PORT);
  }
  if (WiFi.status() != WL_CONNECTED) announced = false;

  server.handleClient();
  pollButton();

  if (millis() - lastBat > 1000) { lastBat = millis(); g_battV = readBattery(); }
  if (mode == MODE_LOGGER && !recording && millis() - lastStor > 3000) {
    lastStor = millis(); refreshStorage();
  }
  displayUpdate();
}

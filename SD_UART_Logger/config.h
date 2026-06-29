/* ============================================================================
 *  config.h  —  all board pins, feature flags and tunables in one place.
 *
 *  Board: Waveshare ESP32-S3-Touch-LCD-1.47 (ESP32-S3R8, octal PSRAM).
 *  Pins below are taken from the board's OWN example drivers (examples/),
 *  not from generic web sources.
 * ==========================================================================*/
#pragma once

// ---------------------------------------------------------------- Wi-Fi ----
// #define WIFI_SSID   "W-NET"
// #define WIFI_PASS   ""
#define WIFI_SSID   "AI_team"
#define WIFI_PASS   "1234@aaa"
#define MDNS_NAME   "sdlogger"             // -> http://sdlogger.local/

// ------------------------------------------- microSD (SDMMC, 1-bit) --------
//   From examples/.../SD_Card.h. Mounted 1-bit (D0 only) with auto-format.
#define SD_CLK_PIN  14
#define SD_CMD_PIN  15
#define SD_D0_PIN   16
#define SD_D1_PIN   18
#define SD_D2_PIN   17
#define SD_D3_PIN   21
#define SD_FREQ_KHZ 20000                  // 20 MHz; lower = more tolerant
#define SD_AUTO_FORMAT 1                   // format to FAT if mount fails

// ---------------------------------------- LCD ST7789 (FSPI bus) ------------
//   From examples/.../Display_ST7789.h. Panel is 172x320, color inverted (IPS).
#define LCD_SCK   40
#define LCD_MOSI  45
#define LCD_MISO  -1
#define LCD_CS    42
#define LCD_DC    41
#define LCD_RST   39
#define LCD_BL    46                       // backlight, active HIGH
#define LCD_W     172
#define LCD_H     320
#define LCD_OFFX  34
#define LCD_OFFY  0

// --------------------------------------------------- Battery sense ---------
//   From examples/.../BAT_Driver: GPIO1, x3 divider, calibration 0.992857.
#define BAT_ADC_PIN 1
#define BAT_DIV     3.0f
#define BAT_CAL     0.992857f
#define BAT_FULL_V  4.20f
#define BAT_EMPTY_V 3.30f

// ----------------------------------------------- RGB + Button --------------
#define RGB_PIN   38                       // on-board WS2812
#define RGB_LEVEL 30                        // brightness 0..255
#define BTN_PIN   0                         // BOOT button (active LOW)

// ======================== TARGET ESP32 CONNECTION =========================
//  *** YOU wire these four lines (plus GND) to the target esp32. ***
//  Defaults use the broken-out UART pads (43/44) and two spare GPIOs.
//  Verify they are accessible on your board and change if needed.
#define UART_TX_PIN    10                  // bridge TX  -> target RX (U0RXD)
#define UART_RX_PIN    11                  // bridge RX  <- target TX (U0TXD)
#define UART_BAUD      115200UL            // default logging/monitor baud
#define UART_RX_BUF    16384               // big driver RX buffer for bursts
#define TARGET_EN_PIN  8                   // -> target EN / CHIP_PU (reset)
#define TARGET_IO0_PIN 9                   // -> target GPIO0 / BOOT (strap)

// ----------------------------------- Logging staging buffer ----------------
#define LOG_BUF_SIZE   8192
#define LOG_FLUSH_AT   4096

// ----------------------------------- WiFi serial (RFC2217) -----------------
//  esptool / VS Code ESP-IDF can target  rfc2217://<device-ip>:RFC2217_PORT
//  to FLASH and MONITOR the target over WiFi (no USB cable to the target).
#define RFC2217_PORT   3333

// ---------------------------------------------- Feature flags --------------
#define ENABLE_LCD        1                // 0 = build without the LCD/GFX lib
#define ENABLE_WEB_FLASH  1                // 1 = on-device .bin flasher (needs
                                           //     vendored esp-serial-flasher)
#define WEB_FLASH_HIGHER_BAUD 460800UL     // speed-up after sync (0 = stay 115200)
#define DEBUG_FLASH           0            // 1 = verbose SLIP trace + hex dumps in flasher

// ----------------------------------------------- Real-time graph -----------
#define LOG_PARSE_ENABLE  1              // 1 = parse UART lines for live /graph page
#define LOG_PARSE_FMT     "%f,%f,%f"     // sscanf pattern: angle, ang-velocity, torque
#define LOG_GRAPH_BUF     300            // ring-buffer depth (300 @ 10 Hz = 30 s)

// ----------------------------------------------- Operating mode ------------
//  All three contend for UART1, so only one is active at a time.
enum Mode { MODE_LOGGER, MODE_RFC2217, MODE_FLASHING };

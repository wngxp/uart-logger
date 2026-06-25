/* ============================================================================
 *  rfc2217.cpp  —  RFC2217 (Telnet COM-port) server over WiFi.
 *
 *  Lets a PC reach the TARGET esp32's UART over the network. Point esptool or
 *  the VS Code ESP-IDF extension at:   rfc2217://<device-ip>:3333
 *  and Flash / Monitor work as if the target were on a local serial port:
 *    - serial data is tunnelled over TCP both ways (with IAC 0xFF escaping)
 *    - COM-PORT SET-BAUDRATE  -> Serial1.updateBaudRate()  (high-speed flashing)
 *    - COM-PORT SET-CONTROL DTR/RTS -> target IO0/EN (esptool auto-reset)
 *
 *  While a client is attached, SD logging is paused (UART1 is exclusive).
 * ==========================================================================*/
#include "shared.h"

// ---- Telnet ----
#define T_SE   240
#define T_SB   250
#define T_WILL 251
#define T_WONT 252
#define T_DO   253
#define T_DONT 254
#define T_IAC  255
// ---- options ----
#define OPT_BINARY  0
#define OPT_SGA     3
#define OPT_COMPORT 44
// ---- com-port client sub-commands (server reply = +100) ----
#define CP_SET_BAUDRATE 1
#define CP_SET_CONTROL  5
// ---- SET-CONTROL values (pyserial / esptool) ----
#define CTL_DTR_ON  8
#define CTL_DTR_OFF 9
#define CTL_RTS_ON  11
#define CTL_RTS_OFF 12

static WiFiServer rfcServer(RFC2217_PORT);
static WiFiClient client;
static bool       started = false;

// telnet parser
enum { ST_DATA, ST_IAC, ST_OPT, ST_SB, ST_SB_IAC };
static uint8_t  parseState = ST_DATA;
static uint8_t  iacCmd = 0;
static uint8_t  sbBuf[24];
static uint8_t  sbLen = 0;

// negotiated option state (Q-method, prevents negotiation loops)
static bool localWill[256];
static bool localDo[256];

// modem control mirrored to the target
static bool g_dtr = false, g_rts = false;

static bool optSupported(uint8_t o) {
  return o == OPT_BINARY || o == OPT_SGA || o == OPT_COMPORT;
}

static void sendWill(uint8_t o) { uint8_t b[3] = {T_IAC, T_WILL, o}; client.write(b, 3); localWill[o] = true;  }
static void sendWont(uint8_t o) { uint8_t b[3] = {T_IAC, T_WONT, o}; client.write(b, 3); localWill[o] = false; }
static void sendDo  (uint8_t o) { uint8_t b[3] = {T_IAC, T_DO,   o}; client.write(b, 3); localDo[o]   = true;  }
static void sendDont(uint8_t o) { uint8_t b[3] = {T_IAC, T_DONT, o}; client.write(b, 3); localDo[o]   = false; }

static void applyControlLines() {
  // Same truth table as a CP2102/CH340 auto-reset circuit:
  digitalWrite(TARGET_IO0_PIN, (g_dtr && !g_rts) ? LOW : HIGH);
  digitalWrite(TARGET_EN_PIN,  (g_rts && !g_dtr) ? LOW : HIGH);
}

static void comResp(uint8_t cmd, const uint8_t *data, uint8_t len) {
  uint8_t b[16]; uint8_t i = 0;
  b[i++] = T_IAC; b[i++] = T_SB; b[i++] = OPT_COMPORT; b[i++] = cmd + 100;
  for (uint8_t k = 0; k < len && i < sizeof(b) - 2; k++) b[i++] = data[k];
  b[i++] = T_IAC; b[i++] = T_SE;
  client.write(b, i);
}

static void handleOption(uint8_t cmd, uint8_t opt) {
  switch (cmd) {
    case T_DO:                                       // remote asks us to enable
      if (optSupported(opt)) { if (!localWill[opt]) sendWill(opt); }
      else                     sendWont(opt);        // refuse unsupported
      break;
    case T_DONT:
      if (localWill[opt]) sendWont(opt);
      break;
    case T_WILL:                                     // remote will enable its side
      if (optSupported(opt)) { if (!localDo[opt]) sendDo(opt); }
      else                   sendDont(opt);
      break;
    case T_WONT:
      if (localDo[opt]) sendDont(opt);
      break;
  }
}

static void handleSubneg() {
  if (sbLen < 2 || sbBuf[0] != OPT_COMPORT) return;
  uint8_t cmd = sbBuf[1];
  if (cmd == CP_SET_BAUDRATE && sbLen >= 6) {
    uint32_t baud = ((uint32_t)sbBuf[2] << 24) | ((uint32_t)sbBuf[3] << 16) |
                    ((uint32_t)sbBuf[4] << 8)  |  (uint32_t)sbBuf[5];
    if (baud > 0) Serial1.updateBaudRate(baud);
    comResp(CP_SET_BAUDRATE, &sbBuf[2], 4);          // echo back the applied baud
  } else if (cmd == CP_SET_CONTROL && sbLen >= 3) {
    uint8_t v = sbBuf[2];
    switch (v) {
      case CTL_DTR_ON:  g_dtr = true;  applyControlLines(); break;
      case CTL_DTR_OFF: g_dtr = false; applyControlLines(); break;
      case CTL_RTS_ON:  g_rts = true;  applyControlLines(); break;
      case CTL_RTS_OFF: g_rts = false; applyControlLines(); break;
      default: break;
    }
    comResp(CP_SET_CONTROL, &v, 1);
  }
  // other com-port sub-commands (datasize/parity/stopsize) are accepted silently
}

static void feed(uint8_t c) {
  switch (parseState) {
    case ST_DATA:
      if (c == T_IAC) parseState = ST_IAC;
      else            Serial1.write(c);
      break;
    case ST_IAC:
      if (c == T_IAC)      { Serial1.write((uint8_t)0xFF); parseState = ST_DATA; }
      else if (c == T_SB)  { sbLen = 0; parseState = ST_SB; }
      else if (c == T_WILL || c == T_WONT || c == T_DO || c == T_DONT) {
        iacCmd = c; parseState = ST_OPT;
      } else parseState = ST_DATA;                    // NOP / other -> ignore
      break;
    case ST_OPT:
      handleOption(iacCmd, c); parseState = ST_DATA;
      break;
    case ST_SB:
      if (c == T_IAC) parseState = ST_SB_IAC;
      else if (sbLen < sizeof(sbBuf)) sbBuf[sbLen++] = c;
      break;
    case ST_SB_IAC:
      if (c == T_SE)        { handleSubneg(); parseState = ST_DATA; }
      else if (c == T_IAC)  { if (sbLen < sizeof(sbBuf)) sbBuf[sbLen++] = 0xFF; parseState = ST_SB; }
      else                  parseState = ST_SB;
      break;
  }
}

static void onConnect() {
  closeLogNow();                                   // we run inside uartTask
  mode = MODE_RFC2217;
  parseState = ST_DATA; sbLen = 0;
  g_dtr = g_rts = false;
  memset(localWill, 0, sizeof(localWill));
  memset(localDo,   0, sizeof(localDo));
  Serial1.updateBaudRate(UART_BAUD);
  targetIdle();
  // offer binary + suppress-go-ahead + com-port
  sendWill(OPT_BINARY); sendDo(OPT_BINARY);
  sendWill(OPT_SGA);    sendDo(OPT_SGA);
  sendDo(OPT_COMPORT);
  DBG("[RFC2217] client %s connected\n", client.remoteIP().toString().c_str());
}

static void onDisconnect() {
  client.stop();
  mode = MODE_LOGGER;
  Serial1.updateBaudRate(UART_BAUD);
  targetIdle();
  DBG("[RFC2217] client disconnected\n");
}

static void pump() {
  bool moved = false;

  // PC -> target (run the telnet parser)
  int guard = 2048;
  while (client.available() && guard-- > 0) {
    int c = client.read();
    if (c < 0) break;
    feed((uint8_t)c);
    moved = true;
  }

  // target -> PC (escape 0xFF as 0xFF 0xFF)
  int n = Serial1.available();
  if (n > 0) {
    uint8_t in[256], out[512];
    if (n > (int)sizeof(in)) n = sizeof(in);
    int r = Serial1.readBytes(in, n);
    int oi = 0;
    for (int i = 0; i < r; i++) { out[oi++] = in[i]; if (in[i] == 0xFF) out[oi++] = 0xFF; }
    if (oi > 0) { client.write(out, oi); moved = true; }
  }

  if (!moved) vTaskDelay(1);
}

void rfc2217Begin() {
  rfcServer.begin();
  rfcServer.setNoDelay(true);
  started = true;
  DBG("[RFC2217] listening on port %d\n", RFC2217_PORT);
}

// Called every uartTask iteration. Returns true while a client owns UART1.
bool rfc2217Service() {
  if (!started) return false;

  if (!client || !client.connected()) {
    if (mode == MODE_RFC2217) onDisconnect();        // client just left
    if (rfcServer.hasClient()) {
      client = rfcServer.accept();
      client.setNoDelay(true);
      onConnect();
      return true;
    }
    return false;
  }
  pump();
  return true;
}

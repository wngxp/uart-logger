/* ============================================================================
 *  web.cpp  —  Wi-Fi UI: download/delete logs, zip-all, format card,
 *  upload a .bin to flash the target, and show the RFC2217 WiFi-serial URL.
 * ==========================================================================*/
#include "shared.h"

#define FLASH_TMP_PATH "/__upload.bin"

static String htmlEscape(const String &s) {
  String o; o.reserve(s.length() + 8);
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;";  break;
      case '<': o += "&lt;";   break;
      case '>': o += "&gt;";   break;
      case '"': o += "&quot;"; break;
      default:  o += c;
    }
  }
  return o;
}

String statusBarHtml() {
  String s = "<div class='bar2'>";
  s += "<b>Mode:</b> ";
  s += (mode == MODE_RFC2217) ? "WiFi-serial" : (mode == MODE_FLASHING) ? "FLASHING" : "LOGGER";
  s += " &nbsp; <b>Wi-Fi:</b> ";
  s += (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("...");
  char b[40]; snprintf(b, sizeof(b), "%.2fV (%d%%)", g_battV, batteryPct(g_battV));
  s += " &nbsp; <b>Battery:</b> "; s += b;
  s += " &nbsp; <b>Free:</b> "; s += sdOk ? humanSize(g_freeBytes) : String("no SD");
  s += "</div>";
  return s;
}

// ----------------------------------------------------- root ---------------
static void handleRoot() {
  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("<ip>");
  String page; page.reserve(6144);
  page += F("<!doctype html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>SD UART Logger</title><style>"
            "body{font-family:system-ui,Arial;margin:1.2rem;background:#111;color:#eee}"
            "h1{font-size:1.3rem}table{border-collapse:collapse;width:100%;max-width:760px}"
            "td,th{padding:.45rem .6rem;border-bottom:1px solid #333;text-align:left}"
            "a.btn,button{background:#2563eb;color:#fff;border:0;border-radius:6px;"
            "padding:.4rem .7rem;margin:.15rem;cursor:pointer;text-decoration:none;"
            "display:inline-block;font-size:.9rem}"
            "a.del,button.del{background:#dc2626}a.alt{background:#059669}.bar{margin:.8rem 0}"
            ".bar2{margin:.6rem 0;padding:.5rem;background:#1f2937;border-radius:8px;font-size:.9rem}"
            ".note{margin:.6rem 0;padding:.6rem;background:#0e7490;border-radius:8px;font-size:.9rem}"
            "code{background:#000;padding:.1rem .3rem;border-radius:4px}"
            ".rec{background:#7f1d1d;padding:.6rem;border-radius:8px;margin-bottom:1rem}"
            ".rfc{margin:.6rem 0;padding:.8rem;background:#1e3a5f;border-radius:8px;font-size:.9rem}"
            ".step{font-weight:600;margin:.7rem 0 .25rem;color:#93c5fd}"
            ".cmdrow{display:flex;align-items:center;gap:.5rem;margin:.15rem 0}"
            ".cmdrow code{flex:1;padding:.35rem .5rem;word-break:break-all}"
            ".cpbtn{background:#374151!important;font-size:.78rem!important;"
            "padding:.2rem .45rem!important;margin:0!important;flex-shrink:0}"
            ".dim{color:#9ca3af}"
            "</style></head><body><h1>SD UART Logger</h1>");
  page += statusBarHtml();

  // RFC2217 instruction block with copy buttons
  String rfc     = "rfc2217://" + ip + ":" + String(RFC2217_PORT);
  String rfcMdns = String("rfc2217://") + MDNS_NAME + ".local:" + String(RFC2217_PORT);
  page += "<div class='rfc'>"
          "<b>Wireless Flash &amp; Monitor &mdash; RFC2217 port " + String(RFC2217_PORT) + "</b>"
          "<p class='dim' style='margin:.3rem 0 .4rem'>"
          "Point idf.py or esptool at the logger. "
          "Auto-reset (EN&nbsp;+&nbsp;IO0) and baud negotiation are handled automatically &mdash; "
          "works exactly like a USB cable.</p>";

  page += "<div class='step'>1 &mdash; Terminal: flash &amp; open serial monitor</div>"
          "<div class='cmdrow'>"
          "<code id='c1'>idf.py -p " + rfc + " flash monitor</code>"
          "<button class='btn cpbtn' onclick='cp(\"c1\")'>Copy</button></div>";

  page += "<div class='step'>2 &mdash; Terminal: flash only (esptool)</div>"
          "<div class='cmdrow'>"
          "<code id='c2'>esptool.py --port " + rfc + " write_flash 0x10000 app.bin</code>"
          "<button class='btn cpbtn' onclick='cp(\"c2\")'>Copy</button></div>";

  page += "<div class='step'>3 &mdash; VS Code ESP-IDF: add to <code>.vscode/settings.json</code></div>"
          "<div class='cmdrow'>"
          "<code id='c3'>&quot;idf.port&quot;: &quot;" + rfc + "&quot;</code>"
          "<button class='btn cpbtn' onclick='cp(\"c3\")'>Copy</button></div>";

  page += "<p class='dim' style='margin:.6rem 0 0'>"
          "mDNS alternative: <code>" + rfcMdns + "</code>"
          " &mdash; macOS/Linux native; Windows needs Bonjour.<br>"
          "SD logging pauses while a client is connected and resumes automatically on disconnect.</p>"
          "</div>"
          "<script>function cp(id){"
          "navigator.clipboard.writeText(document.getElementById(id).innerText)"
          ".then(function(){var b=event.target;b.textContent='Copied!';setTimeout(function(){b.textContent='Copy'},1500)})"
          "}</script>";

  page += "<div class='bar'><a class='btn' href='/flash'>Upload &amp; flash .bin</a> "
          "<a class='btn del' href='/format' onclick=\"return confirm('Format the SD card? This erases all logs.')\">Format SD</a></div>";

  if (mode == MODE_RFC2217) {
    page += F("<div class='rec'>A WiFi-serial client is attached to the target — "
              "logging is paused until it disconnects.</div></body></html>");
    server.send(200, "text/html", page); return;
  }
  if (recording) {
    page += F("<div class='rec'><b>● Recording…</b> file ops paused. Press BOOT to stop.<br>Writing: ");
    page += htmlEscape(currentFile);
    page += F("</div></body></html>");
    server.send(200, "text/html", page); return;
  }
  if (!sdOk) {
    page += F("<p style='color:#f87171'>SD card not available.</p></body></html>");
    server.send(200, "text/html", page); return;
  }

  uint64_t total = 0; int count = 0;
  String rows;
  xSemaphoreTake(sdMutex, portMAX_DELAY);
  File dir = SD_MMC.open("/");
  if (dir) {
    File f = dir.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        String name = String(f.name());
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        uint64_t sz = f.size();
        total += sz; count++;
        String nameLower = name; nameLower.toLowerCase();
        rows += "<tr><td>" + htmlEscape(name) + "</td><td>" + humanSize(sz) +
                "</td><td><a class='btn' href='/download?name=" + name + "'>Download</a>";
        if (nameLower.endsWith(".bin"))
          rows += " <a class='btn alt' href='/flashfile?name=" + name + "'>Flash</a>";
        rows += " <a class='btn del' href='/delete?name=" + name +
                "' onclick=\"return confirm('Delete " + name + "?')\">Delete</a></td></tr>";
      }
      f = dir.openNextFile();
    }
    dir.close();
  }
  xSemaphoreGive(sdMutex);

  page += "<div class='bar'><a class='btn' href='/zip'>⬇ Download All (zip)</a> "
          "<a class='btn del' href='/deleteall' onclick=\"return confirm('Delete ALL files?')\">🗑 Delete All</a></div>";
  page += "<table><tr><th>File</th><th>Size</th><th>Action</th></tr>";
  page += (count == 0) ? "<tr><td colspan='3'>No files.</td></tr>" : rows;
  page += "</table><p>" + String(count) + " file(s), " + humanSize(total) + " total.</p>";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

static void handleDownload() {
  if (recording || mode != MODE_LOGGER) { server.send(409, "text/plain", "Busy"); return; }
  String name = server.arg("name");
  if (!safeName(name)) { server.send(400, "text/plain", "Bad name"); return; }
  xSemaphoreTake(sdMutex, portMAX_DELAY);
  File f = SD_MMC.open("/" + name, FILE_READ);
  if (!f) { xSemaphoreGive(sdMutex); server.send(404, "text/plain", "Not found"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  server.streamFile(f, "application/octet-stream");
  f.close();
  xSemaphoreGive(sdMutex);
}

static void handleDelete() {
  if (recording || mode != MODE_LOGGER) { server.send(409, "text/plain", "Busy"); return; }
  String name = server.arg("name");
  if (!safeName(name)) { server.send(400, "text/plain", "Bad name"); return; }
  xSemaphoreTake(sdMutex, portMAX_DELAY);
  SD_MMC.remove("/" + name);
  xSemaphoreGive(sdMutex);
  refreshStorage();
  server.sendHeader("Location", "/"); server.send(303);
}

static void handleDeleteAll() {
  if (recording || mode != MODE_LOGGER) { server.send(409, "text/plain", "Busy"); return; }
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
  server.sendHeader("Location", "/"); server.send(303);
}

static void handleFormat() {
  if (recording || mode != MODE_LOGGER) { server.send(409, "text/plain", "Busy"); return; }
  formatSD();
  server.sendHeader("Location", "/"); server.send(303);
}

// ----------------------------------------------- ZIP (store, streamed) ----
static inline void le16(uint8_t *p, uint16_t v) { p[0]=v; p[1]=v>>8; }
static inline void le32(uint8_t *p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static uint32_t crcRun(uint32_t crc, const uint8_t *d, size_t n) {
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88420u & (uint32_t)(-(int32_t)(crc & 1)));
  }
  return crc;
}
struct CdEntry { String name; uint32_t crc, size, offset; };

static void handleZip() {
  if (recording || mode != MODE_LOGGER) { server.send(409, "text/plain", "Busy"); return; }
  if (!sdOk) { server.send(503, "text/plain", "No SD"); return; }

  std::vector<String> files;
  xSemaphoreTake(sdMutex, portMAX_DELAY);
  File dir = SD_MMC.open("/");
  if (dir) {
    File f = dir.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        String n = String(f.name());
        int slash = n.lastIndexOf('/');
        if (slash >= 0) n = n.substring(slash + 1);
        files.push_back(n);
      }
      f = dir.openNextFile();
    }
    dir.close();
  }
  xSemaphoreGive(sdMutex);

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=\"logs.zip\"");
  server.send(200, "application/zip", "");

  std::vector<CdEntry> cd;
  uint32_t offset = 0;
  uint8_t buf[2048];

  for (auto &name : files) {
    uint32_t localOff = offset;
    uint8_t lh[30];
    le32(lh+0, 0x04034b50); le16(lh+4, 20); le16(lh+6, 0x0008); le16(lh+8, 0);
    le16(lh+10, 0); le16(lh+12, 0); le32(lh+14, 0); le32(lh+18, 0); le32(lh+22, 0);
    le16(lh+26, name.length()); le16(lh+28, 0);
    server.sendContent((const char *)lh, 30);
    server.sendContent(name.c_str(), name.length());
    offset += 30 + name.length();

    uint32_t crc = 0xFFFFFFFF, sz = 0;
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    File f = SD_MMC.open("/" + name, FILE_READ);
    if (f) {
      int n;
      while ((n = f.read(buf, sizeof(buf))) > 0) {
        crc = crcRun(crc, buf, n);
        server.sendContent((const char *)buf, n);
        offset += n; sz += n;
      }
      f.close();
    }
    xSemaphoreGive(sdMutex);
    crc ^= 0xFFFFFFFF;

    uint8_t dd[16];
    le32(dd+0, 0x08074b50); le32(dd+4, crc); le32(dd+8, sz); le32(dd+12, sz);
    server.sendContent((const char *)dd, 16);
    offset += 16;
    cd.push_back({name, crc, sz, localOff});
  }

  uint32_t cdStart = offset;
  for (auto &e : cd) {
    uint8_t ch[46];
    le32(ch+0, 0x02014b50); le16(ch+4, 20); le16(ch+6, 20); le16(ch+8, 0x0008); le16(ch+10, 0);
    le16(ch+12, 0); le16(ch+14, 0); le32(ch+16, e.crc); le32(ch+20, e.size); le32(ch+24, e.size);
    le16(ch+28, e.name.length()); le16(ch+30, 0); le16(ch+32, 0); le16(ch+34, 0); le16(ch+36, 0);
    le32(ch+38, 0); le32(ch+42, e.offset);
    server.sendContent((const char *)ch, 46);
    server.sendContent(e.name.c_str(), e.name.length());
    offset += 46 + e.name.length();
  }
  uint32_t cdSize = offset - cdStart;

  uint8_t eo[22];
  le32(eo+0, 0x06054b50); le16(eo+4, 0); le16(eo+6, 0);
  le16(eo+8, cd.size()); le16(eo+10, cd.size()); le32(eo+12, cdSize); le32(eo+16, cdStart); le16(eo+20, 0);
  server.sendContent((const char *)eo, 22);
  server.sendContent("");
}

// ----------------------------------------------- flash a .bin -------------
static void handleFlashPage() {
  String p; p.reserve(2048);
  p += F("<!doctype html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>Flash target</title><style>"
         "body{font-family:system-ui,Arial;margin:1.2rem;background:#111;color:#eee}"
         "input,button{font-size:1rem;padding:.4rem;margin:.2rem 0}"
         "button{background:#2563eb;color:#fff;border:0;border-radius:6px;cursor:pointer}"
         "a{color:#60a5fa}code{background:#000;padding:.1rem .3rem;border-radius:4px}"
         "</style></head><body><h1>Flash target ESP32</h1>");
  p += statusBarHtml();
#if ENABLE_WEB_FLASH
  p += F("<form method='POST' action='/flash' enctype='multipart/form-data'>"
         "<p>Offset (<code>0x10000</code>=app, <code>0x0</code>=merged image):<br>"
         "<input name='offset' value='0x10000'></p>"
         "<p>Firmware .bin:<br><input type='file' name='firmware' accept='.bin' required></p>"
         "<button type='submit'>⚡ Flash</button></form>"
         "<p>The board resets the target into download mode automatically "
         "(needs EN/IO0 wired) and is offline while flashing.</p>");
#else
  p += F("<p style='color:#fbbf24'><b>On-device flashing is disabled in this build.</b></p>"
         "<p>Either set <code>ENABLE_WEB_FLASH 1</code> (and vendor esp-serial-flasher), "
         "or just flash over WiFi from your PC using "
         "<code>rfc2217://&lt;ip&gt;:" "3333" "</code> — see the home page.</p>");
#endif
  p += "<p><a href='/'>&larr; back</a></p></body></html>";
  server.send(200, "text/html", p);
}

static void handleFlashUpload() {
  HTTPUpload &up = server.upload();
  static File     f;
  static uint32_t uploadedBytes;

  if (up.status == UPLOAD_FILE_START) {
    DBG("[WEB] Flash upload started: %s\n", up.filename.c_str());
    if (mode == MODE_RFC2217) {
      DBG("[WEB] Upload rejected — RFC2217 client is active\n");
      return;
    }
    stopRecordingAndWait();
    mode = MODE_FLASHING;
    uploadedBytes = 0;
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    SD_MMC.remove(FLASH_TMP_PATH);
    f = SD_MMC.open(FLASH_TMP_PATH, FILE_WRITE);
    xSemaphoreGive(sdMutex);
    if (!f) DBG("[WEB] ERROR: could not open temp file on SD\n");
    else    DBG("[WEB] Temp file opened OK\n");

  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (f) {
      xSemaphoreTake(sdMutex, portMAX_DELAY);
      f.write(up.buf, up.currentSize);
      xSemaphoreGive(sdMutex);
      uint32_t prev = uploadedBytes;
      uploadedBytes += up.currentSize;
      if ((uploadedBytes / (64 * 1024)) != (prev / (64 * 1024)))
        DBG("[WEB] Upload: %u KB received...\n", uploadedBytes / 1024);
    }

  } else if (up.status == UPLOAD_FILE_END) {
    if (f) {
      xSemaphoreTake(sdMutex, portMAX_DELAY);
      f.flush(); f.close();
      xSemaphoreGive(sdMutex);
    }
    DBG("[WEB] Upload complete: %u bytes saved to SD\n", uploadedBytes);

  } else if (up.status == UPLOAD_FILE_ABORTED) {
    DBG("[WEB] Upload aborted by client\n");
    if (f) {
      xSemaphoreTake(sdMutex, portMAX_DELAY);
      f.close();
      xSemaphoreGive(sdMutex);
    }
    mode = MODE_LOGGER;
  }
}

static void handleFlashResult() {
  uint32_t offset = strtoul(server.arg("offset").c_str(), nullptr, 0);
  DBG("[WEB] Flash handler called: offset=0x%X\n", offset);

  String log;
  bool ok = flashTargetFromFile(FLASH_TMP_PATH, offset, log);

  xSemaphoreTake(sdMutex, portMAX_DELAY);
  SD_MMC.remove(FLASH_TMP_PATH);
  xSemaphoreGive(sdMutex);
  mode = MODE_LOGGER;
  refreshStorage();
  DBG("[WEB] Flash result: %s\n", ok ? "OK" : "FAILED");

  String p = F("<!doctype html><html><head><meta charset='utf-8'>"
               "<title>Flash result</title><style>"
               "body{font-family:system-ui,Arial;margin:1.2rem;background:#111;color:#eee}"
               "a{color:#60a5fa}"
               "pre{background:#000;padding:.8rem 1rem;border-radius:8px;overflow:auto;"
               "white-space:pre-wrap;font-size:.82rem;line-height:1.55;color:#86efac;"
               "max-height:70vh}"
               "</style></head><body><h1>");
  p += ok ? "&#x2705; Flash OK" : "&#x274C; Flash failed";
  p += F("</h1><pre>");
  p += htmlEscape(log);
  p += F("</pre><p>"
         "<a class='btn' href='/flash'>&larr; Flash another</a> &nbsp; "
         "<a class='btn' href='/'>Home</a></p>"
         "</body></html>");
  server.send(200, "text/html", p);
}

// ----------------------------------------------- flash an existing SD .bin --
static void handleFlashFileGet() {
  if (recording || mode != MODE_LOGGER) { server.send(409, "text/plain", "Busy"); return; }
  String name = server.arg("name");
  if (!safeName(name)) { server.send(400, "text/plain", "Bad name"); return; }
  String lower = name; lower.toLowerCase();
  if (!lower.endsWith(".bin")) { server.send(400, "text/plain", "Not a .bin"); return; }

  String p; p.reserve(1536);
  p += F("<!doctype html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>Flash from SD</title><style>"
         "body{font-family:system-ui,Arial;margin:1.2rem;background:#111;color:#eee}"
         "input,button{font-size:1rem;padding:.4rem;margin:.2rem 0}"
         "button{background:#2563eb;color:#fff;border:0;border-radius:6px;cursor:pointer}"
         "a{color:#60a5fa}code{background:#000;padding:.1rem .3rem;border-radius:4px}"
         "small{color:#9ca3af}"
         "</style></head><body><h1>Flash from SD card</h1>");
  p += statusBarHtml();
  p += "<p>File: <b>" + htmlEscape(name) + "</b><br>"
       "The target will be reset into bootloader mode automatically (EN + IO0 required).</p>";
  p += "<form method='POST' action='/flashfile'>"
       "<input type='hidden' name='name' value='" + htmlEscape(name) + "'>"
       "<p>Flash offset:<br>"
       "<input name='offset' value='0x10000'><br>"
       "<small><code>0x10000</code>&nbsp;= app partition &nbsp;"
       "<code>0x0</code>&nbsp;= merged/bootloader image</small></p>"
       "<button type='submit'>&#x26A1; Flash now</button></form>";
  p += "<p><a href='/'>&#x2190; back</a></p></body></html>";
  server.send(200, "text/html", p);
}

static void handleFlashFileDo() {
  String name = server.arg("name");
  uint32_t offset = strtoul(server.arg("offset").c_str(), nullptr, 0);
  if (!safeName(name)) { server.send(400, "text/plain", "Bad name"); return; }
  String lower = name; lower.toLowerCase();
  if (!lower.endsWith(".bin")) { server.send(400, "text/plain", "Not a .bin"); return; }
  if (mode == MODE_RFC2217) { server.send(409, "text/plain", "RFC2217 client active"); return; }
  stopRecordingAndWait();
  mode = MODE_FLASHING;

  String log;
  bool ok = flashTargetFromFile(("/" + name).c_str(), offset, log);
  mode = MODE_LOGGER;
  refreshStorage();
  DBG("[WEB] Flash-from-SD %s: %s\n", name.c_str(), ok ? "OK" : "FAILED");

  String p = F("<!doctype html><html><head><meta charset='utf-8'>"
               "<title>Flash result</title><style>"
               "body{font-family:system-ui,Arial;margin:1.2rem;background:#111;color:#eee}"
               "a.btn{background:#2563eb;color:#fff;border:0;border-radius:6px;"
               "padding:.4rem .7rem;margin:.15rem;cursor:pointer;"
               "text-decoration:none;display:inline-block;font-size:.9rem}"
               "pre{background:#000;padding:.8rem 1rem;border-radius:8px;overflow:auto;"
               "white-space:pre-wrap;font-size:.82rem;line-height:1.55;color:#86efac;"
               "max-height:70vh}"
               "</style></head><body><h1>");
  p += ok ? "&#x2705; Flash OK" : "&#x274C; Flash failed";
  p += F("</h1><pre>");
  p += htmlEscape(log);
  p += "</pre><p>"
       "<a class='btn' href='/flashfile?name=" + htmlEscape(name) + "'>&#x26A1; Flash again</a>"
       " &nbsp; <a class='btn' href='/'>Home</a></p></body></html>";
  server.send(200, "text/html", p);
}

void webBegin() {
  server.on("/",          handleRoot);
  server.on("/download",  handleDownload);
  server.on("/delete",    handleDelete);
  server.on("/deleteall", handleDeleteAll);
  server.on("/format",    handleFormat);
  server.on("/zip",       handleZip);
  server.on("/flash",     HTTP_GET,  handleFlashPage);
  server.on("/flash",     HTTP_POST, handleFlashResult, handleFlashUpload);
  server.on("/flashfile", HTTP_GET,  handleFlashFileGet);
  server.on("/flashfile", HTTP_POST, handleFlashFileDo);
  server.begin();
}

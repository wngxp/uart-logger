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
  String s = "<div class='bar2' id='sbar'>";
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
          "<b>RFC2217 wireless port &mdash; port " + String(RFC2217_PORT) + "</b>"
          "<p class='dim' style='margin:.3rem 0 .4rem'>"
          "For flashing from your PC terminal. "
          "To just view serial output, use the <a href='/monitor' style='color:#93c5fd'>web monitor</a> instead &mdash; "
          "no terminal needed, and SD logging works in parallel.</p>";

  page += "<div class='step'>1 &mdash; Monitor only (no flash)</div>"
          "<div class='cmdrow'>"
          "<code id='c1'>idf.py -p " + rfc + " monitor</code>"
          "<button class='btn cpbtn' onclick='cp(\"c1\")'>Copy</button></div>";

  page += "<div class='step'>2 &mdash; Flash from PC build dir, then monitor</div>"
          "<div class='cmdrow'>"
          "<code id='c2'>idf.py -p " + rfc + " flash monitor</code>"
          "<button class='btn cpbtn' onclick='cp(\"c2\")'>Copy</button></div>";

  page += "<div class='step'>3 &mdash; Flash a specific .bin (esptool)</div>"
          "<div class='cmdrow'>"
          "<code id='c3'>esptool.py --port " + rfc + " write_flash 0x10000 yourfile.bin</code>"
          "<button class='btn cpbtn' onclick='cp(\"c3\")'>Copy</button></div>";

  page += "<div class='step'>4 &mdash; VS Code ESP-IDF: set port in <code>.vscode/settings.json</code></div>"
          "<div class='cmdrow'>"
          "<code id='c4'>&quot;idf.port&quot;: &quot;" + rfc + "&quot;</code>"
          "<button class='btn cpbtn' onclick='cp(\"c4\")'>Copy</button></div>";

  page += "<p class='dim' style='margin:.6rem 0 0'>"
          "mDNS alternative: <code>" + rfcMdns + "</code>"
          " &mdash; macOS/Linux native; Windows needs Bonjour.<br>"
          "SD logging via BOOT button works during an RFC2217 session.</p>"
          "</div>"
          "<script>"
          "function cp(id){"
            "var b=event.target,txt=document.getElementById(id).innerText;"
            "var done=function(){b.textContent='Copied!';setTimeout(function(){b.textContent='Copy';},1500);};"
            "if(navigator.clipboard){"
              "navigator.clipboard.writeText(txt).then(done).catch(function(){fbCopy(txt,b,done);});"
            "}else{fbCopy(txt,b,done);}"
          "}"
          "function fbCopy(txt,b,done){"
            "var t=document.createElement('textarea');"
            "t.value=txt;t.style.cssText='position:fixed;opacity:0;top:0;left:0';"
            "document.body.appendChild(t);t.focus();t.select();"
            "try{document.execCommand('copy');done();}catch(e){b.textContent='Failed';}"
            "document.body.removeChild(t);"
          "}"
          "</script>";

  page += "<div class='bar'>"
          "<a class='btn' href='/flash'>Upload &amp; flash .bin</a> "
          "<a class='btn alt' href='/graph'>Live Graph</a> "
          "<a class='btn' style='background:#7c3aed' href='/monitor'>&#9654; Monitor</a> "
          "<a class='btn del' href='/format' onclick=\"return confirm('Format the SD card? This erases all logs.')\">Format SD</a>"
          "</div>"
          // status bar auto-refresh every 2 s
          "<script>"
          "function refreshStat(){"
            "fetch('/api/statusbar').then(function(r){return r.text();}).then(function(h){"
              "var w=document.getElementById('sbar');if(w)w.innerHTML=h;"
            "}).catch(function(){});"
            "setTimeout(refreshStat,2000);}"
          "setTimeout(refreshStat,2000);"
          "</script>";

  if (mode == MODE_RFC2217) {
    page += "<div class='rec'>&#128225; WiFi-serial client attached &mdash; target UART bridged to your PC.<br>";
    if (recording) page += "&#9679; Also logging to SD: <code>" + htmlEscape(currentFile) + "</code>";
    else           page += "Press <b>BOOT</b> to start SD logging alongside the WiFi session.";
    page += "</div></body></html>";
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

// ----------------------------------------------- flash streaming helpers ---
static void flashStreamHeader() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(F("<!doctype html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Flashing...</title><style>"
    "body{font-family:system-ui,Arial;margin:1rem;background:#111;color:#eee}"
    "h1{font-size:1.15rem;margin:.3rem 0}"
    ".pbg{background:#374151;border-radius:5px;height:18px;overflow:hidden;margin:.4rem 0}"
    ".pfill{height:100%;width:0;background:linear-gradient(90deg,#1d4ed8,#3b82f6);transition:width .3s}"
    ".pct{font-size:.82rem;color:#9ca3af;margin-bottom:.3rem}"
    ".log{background:#000;padding:.5rem .8rem;border-radius:8px;font-size:.76rem;"
    "line-height:1.5;color:#86efac;height:58vh;overflow-y:auto;white-space:pre-wrap;font-family:monospace}"
    "a.btn{background:#2563eb;color:#fff;border-radius:6px;padding:.35rem .7rem;"
    "text-decoration:none;display:inline-block;margin:.15rem;font-size:.9rem}"
    "#done{margin-top:.7rem;display:none}"
    "</style></head><body>"
    "<h1 id='hd'>&#x26A1; Flashing target...</h1>"
    "<div class='pbg'><div id='bar' class='pfill'></div></div>"
    "<div class='pct' id='pct'>Preparing...</div>"
    "<pre class='log' id='log'></pre>"
    "<div id='done'></div>"
    "<script>"
    "var $=function(id){return document.getElementById(id)};"
    "function U(p,m){"
      "if(p>=0){$('bar').style.width=p+'%';$('pct').textContent=p+'%';}"
      "if(m){var l=$('log');l.textContent+=m+'\\n';l.scrollTop=l.scrollHeight;}}"
    "function done(ok,h){"
      "$('hd').textContent=ok?'\\u2705 Flash complete':'\\u274C Flash failed';"
      "$('pct').style.display='none';"
      "$('bar').style.background=ok?'#16a34a':'#dc2626';"
      "$('bar').style.width='100%';"
      "$('done').style.display='block';"
      "$('done').innerHTML='<a class=\"btn\" href=\"'+h+'\">Flash again</a>"
        " <a class=\"btn\" href=\"/\">Home</a>';}"
    "</script>"
  ));
  // ~1 KB padding so Chrome starts rendering before the first script tag
  server.sendContent(F("<!-- flush "));
  for (int i = 0; i < 28; i++) server.sendContent(F("                                        "));
  server.sendContent(F(" -->"));
}

static void flashStreamFooter(bool ok, const String &backHref) {
  char buf[160];
  // need to JS-escape the href (it's a URL so only & is possible — safe enough)
  snprintf(buf, sizeof(buf), "<script>done(%s,'%s')</script></body></html>",
           ok ? "true" : "false", backHref.c_str());
  server.sendContent(buf);
  server.sendContent("");  // end chunked transfer
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

  flashStreamHeader();
  flashEnableStream();
  String log;
  bool ok = flashTargetFromFile(FLASH_TMP_PATH, offset, log);
  flashDisableStream();

  xSemaphoreTake(sdMutex, portMAX_DELAY);
  SD_MMC.remove(FLASH_TMP_PATH);
  xSemaphoreGive(sdMutex);
  mode = MODE_LOGGER;
  refreshStorage();
  DBG("[WEB] Flash result: %s\n", ok ? "OK" : "FAILED");
  flashStreamFooter(ok, "/flash");
}

// ----------------------------------------------- live graph data -----------
static void handleDataLatest() {
#if LOG_PARSE_ENABLE
  uint32_t since = strtoul(server.arg("since").c_str(), nullptr, 0);
  server.sendHeader("Cache-Control", "no-cache");

  int count = g_dataCount, tail = g_dataTail;
  int start = (tail - count + LOG_GRAPH_BUF) % LOG_GRAPH_BUF;

  // collect indices newer than `since`
  String ms = "", sa = "", sv = "", st = "";
  int n = 0;
  for (int i = 0; i < count; i++) {
    int idx = (start + i) % LOG_GRAPH_BUF;
    if (g_dataBuf[idx].ms <= since) continue;
    if (n) { ms += ','; sa += ','; sv += ','; st += ','; }
    ms += String(g_dataBuf[idx].ms);
    sa += String(g_dataBuf[idx].a, 3);
    sv += String(g_dataBuf[idx].v, 3);
    st += String(g_dataBuf[idx].t, 3);
    n++;
  }

  String json = "{\"ok\":1,\"recording\":";
  json += recording ? "true" : "false";
  json += ",\"n\":" + String(n);
  if (n) {
    json += ",\"ms\":[" + ms + "],\"a\":[" + sa + "],\"v\":[" + sv + "],\"t\":[" + st + "]";
  }
  json += "}";
  server.send(200, "application/json", json);
#else
  server.send(200, "application/json", F("{\"ok\":0,\"n\":0}"));
#endif
}

static void handleGraph() {
  String p; p.reserve(5120);
  p += F("<!doctype html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Live Graph</title><style>"
    "html,body{margin:0;height:100%;background:#111;color:#eee;font-family:system-ui,Arial}"
    "body{display:flex;flex-direction:column}"
    "header{padding:.4rem .8rem;background:#1f2937;display:flex;align-items:center;gap:.8rem;flex-shrink:0}"
    "h1{font-size:.95rem;margin:0}#rst{font-size:.8rem;color:#9ca3af;margin-left:auto}"
    "a{color:#60a5fa;font-size:.82rem;text-decoration:none}"
    "canvas{flex:1;display:block;width:100%;min-height:0}"
    ".hint{font-size:.75rem;color:#6b7280;padding:.2rem .8rem;flex-shrink:0}"
    "</style></head><body>"
    "<header><h1>Live Graph</h1>"
    "<span id='rst'>Connecting...</span>"
    "<a href='/'>&#x2190; Home</a></header>"
    "<canvas id='c'></canvas>"
    "<div class='hint'>Format: " LOG_PARSE_FMT " &nbsp;|&nbsp; window: 30 s &nbsp;|&nbsp;"
    " edit <code>LOG_PARSE_FMT</code> in config.h to match your controller output</div>"
    "<script>"
    "var PANELS=[{k:'a',l:'Angle',c:'#60a5fa'},{k:'v',l:'Ang.Vel',c:'#34d399'},{k:'t',l:'Torque',c:'#f87171'}];"
    "var WIN=30000,samples=[],lastMs=0;"
    "function draw(){"
      "var cv=document.getElementById('c');"
      "var W=cv.clientWidth,H=cv.clientHeight;"
      "if(!W||!H)return;"
      "cv.width=W;cv.height=H;"
      "var ctx=cv.getContext('2d');"
      "ctx.clearRect(0,0,W,H);"
      "if(samples.length<2){"
        "ctx.fillStyle='#6b7280';ctx.font='13px system-ui';ctx.textAlign='center';"
        "ctx.fillText('Waiting for data — start logging on the target',W/2,H/2);"
        "return;}"
      "var PL=52,PR=8,PT=4,PB=22;"
      "var panH=Math.floor((H-PB-PANELS.length*(PT+2))/PANELS.length);"
      "var tMax=lastMs,tMin=tMax-WIN,pw=W-PL-PR;"
      "var vis=samples.filter(function(s){return s.ms>=tMin;});"
      "if(vis.length<2)return;"
      "PANELS.forEach(function(p,pi){"
        "var y0=PT+pi*(panH+PT+2);"
        "var vals=vis.map(function(s){return s[p.k];});"
        "var yMn=Math.min.apply(null,vals),yMx=Math.max.apply(null,vals);"
        "if(yMn===yMx){yMn-=1;yMx+=1;}"
        "var yr=yMx-yMn;yMn-=yr*.08;yMx+=yr*.08;"
        "ctx.fillStyle='#181818';ctx.fillRect(PL,y0,pw,panH);"
        "ctx.strokeStyle='#2a2a2a';ctx.lineWidth=1;"
        "for(var g=0;g<=4;g++){"
          "var gy=y0+panH*g/4;"
          "ctx.beginPath();ctx.moveTo(PL,gy);ctx.lineTo(PL+pw,gy);ctx.stroke();"
          "ctx.fillStyle='#6b7280';ctx.font='9px monospace';ctx.textAlign='right';"
          "ctx.fillText((yMx-(yMx-yMn)*g/4).toFixed(2),PL-3,gy+3);}"
        "ctx.fillStyle=p.c;ctx.font='bold 10px system-ui';ctx.textAlign='left';"
        "ctx.fillText(p.l,PL+4,y0+12);"
        "ctx.beginPath();ctx.strokeStyle=p.c;ctx.lineWidth=1.5;"
        "var first=true;"
        "vis.forEach(function(s){"
          "var x=PL+pw*(s.ms-tMin)/WIN;"
          "var y=y0+panH*(1-(s[p.k]-yMn)/(yMx-yMn));"
          "first?(ctx.moveTo(x,y),first=false):ctx.lineTo(x,y);});"
        "ctx.stroke();"
        "ctx.strokeStyle='#374151';ctx.lineWidth=1;ctx.strokeRect(PL,y0,pw,panH);});"
      "ctx.fillStyle='#6b7280';ctx.font='9px monospace';ctx.textAlign='center';"
      "for(var ti=0;ti<=6;ti++){"
        "var tx=PL+(W-PL-PR)*ti/6;"
        "ctx.fillText(Math.round(-WIN/1000+WIN/1000*ti/6)+'s',tx,H-PB/2+3);}}"
    "function poll(){"
      "fetch('/data/latest?since='+lastMs).then(function(r){return r.json();}).then(function(d){"
        "if(d.n>0){"
          "for(var i=0;i<d.n;i++)"
            "samples.push({ms:d.ms[i],a:d.a[i],v:d.v[i],t:d.t[i]});"
          "lastMs=d.ms[d.n-1];"
          "var cut=lastMs-WIN-2000;"
          "while(samples.length&&samples[0].ms<cut)samples.shift();}"
        "document.getElementById('rst').textContent="
          "d.recording?'\\u25CF Recording':'\\u25CB Idle';"
        "draw();"
      "}).catch(function(){});"
      "setTimeout(poll,250);}"
    "window.addEventListener('resize',draw);"
    "poll();"
    "</script></body></html>");
  server.send(200, "text/html", p);
}

// ----------------------------------------------- live graph status bar -----
static void handleApiStatusBar() {
  server.send(200, "text/html", statusBarHtml());
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

  flashStreamHeader();
  flashEnableStream();
  String log;
  bool ok = flashTargetFromFile(("/" + name).c_str(), offset, log);
  flashDisableStream();
  mode = MODE_LOGGER;
  refreshStorage();
  DBG("[WEB] Flash-from-SD %s: %s\n", name.c_str(), ok ? "OK" : "FAILED");
  flashStreamFooter(ok, "/flashfile?name=" + name);
}

// ----------------------------------------------- web serial monitor --------
static void handleMonitor() {
  server.send(200, "text/html", F(
    "<!doctype html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Serial Monitor</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{background:#0d1117;color:#c9d1d9;font-family:monospace;height:100vh;display:flex;flex-direction:column}"
    "#hdr{background:#161b22;padding:6px 12px;display:flex;gap:8px;align-items:center;"
          "border-bottom:1px solid #30363d;flex-shrink:0}"
    "#hdr strong{color:#58a6ff;font-size:14px}"
    "button{padding:3px 10px;border:1px solid #30363d;background:#21262d;color:#c9d1d9;"
           "cursor:pointer;font-size:12px;border-radius:5px}"
    "button:hover{background:#30363d}"
    "#dot{width:8px;height:8px;border-radius:50%;background:#3fb950;display:inline-block;flex-shrink:0}"
    "#dot.off{background:#6e7681}"
    "#status{font-size:11px;color:#6e7681;margin-left:auto}"
    "#term{flex:1;overflow-y:auto;padding:10px 14px;font-size:13px;line-height:1.55;"
          "white-space:pre-wrap;word-break:break-all}"
    "</style></head><body>"
    "<div id='hdr'>"
    "<span id='dot' class='off'></span>"
    "<strong>&#9654; Serial Monitor</strong>"
    "<button onclick='clr()'>Clear</button>"
    "<button onclick=\"location.href='/'\">&#8592; Back</button>"
    "<span id='status'>Connecting...</span>"
    "</div>"
    "<div id='term'></div>"
    "<script>"
    "var p=0,D=document.getElementById('term'),"
        "st=document.getElementById('status'),dt=document.getElementById('dot');"
    "function clr(){D.textContent=''}"
    "function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}"
    "function poll(){"
      "fetch('/monitor/data?from='+p)"
      ".then(function(r){"
        "var w=r.headers.get('X-Write-Pos');if(w)p=parseInt(w);return r.text();"
      "})"
      ".then(function(t){"
        "if(t.length){"
          "var atBottom=D.scrollHeight-D.clientHeight-D.scrollTop<40;"
          "D.innerHTML+=esc(t);"
          "if(atBottom)D.scrollTop=D.scrollHeight;"
        "}"
        "dt.className='';st.textContent='Live • '+p+' B';"
        "setTimeout(poll,150);"
      "})"
      ".catch(function(){dt.className='off';st.textContent='Reconnecting...';setTimeout(poll,1000);});"
    "}"
    "poll();"
    "</script></body></html>"
  ));
}

static void handleMonitorData() {
  uint32_t from = 0;
  if (server.hasArg("from")) from = (uint32_t)strtoul(server.arg("from").c_str(), nullptr, 10);
  uint32_t w = g_monWrite;

  uint32_t avail = w - from;
  if (avail > (uint32_t)MON_BUF_SIZE) {
    from  = w - (uint32_t)MON_BUF_SIZE;
    avail = (uint32_t)MON_BUF_SIZE;
  }

  server.sendHeader("X-Write-Pos", String(w));
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Expose-Headers", "X-Write-Pos");

  if (avail == 0) { server.send(200, "text/plain", ""); return; }

  String data; data.reserve(avail);
  for (uint32_t i = 0; i < avail; i++) {
    char c = (char)g_monBuf[(from + i) % MON_BUF_SIZE];
    data += (c ? c : ' ');   // replace null bytes (rare) with space
  }
  server.send(200, "text/plain", data);
}

void webBegin() {
  server.on("/",          handleRoot);
  server.on("/download",  handleDownload);
  server.on("/delete",    handleDelete);
  server.on("/deleteall", handleDeleteAll);
  server.on("/format",    handleFormat);
  server.on("/zip",       handleZip);
  server.on("/flash",          HTTP_GET,  handleFlashPage);
  server.on("/flash",          HTTP_POST, handleFlashResult, handleFlashUpload);
  server.on("/flashfile",      HTTP_GET,  handleFlashFileGet);
  server.on("/flashfile",      HTTP_POST, handleFlashFileDo);
  server.on("/graph",          HTTP_GET,  handleGraph);
  server.on("/data/latest",    HTTP_GET,  handleDataLatest);
  server.on("/api/statusbar",  HTTP_GET,  handleApiStatusBar);
  server.on("/monitor",        HTTP_GET,  handleMonitor);
  server.on("/monitor/data",   HTTP_GET,  handleMonitorData);
  server.begin();
}

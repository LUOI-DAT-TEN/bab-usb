#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <FS.h>
#include <Adafruit_NeoPixel.h>

// ====================== CONFIG ======================
#ifndef WIFI_SSID
#define WIFI_SSID "EvilDuck_S3"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "12345678"
#endif

#define DEBUG_SERIAL_BAUD 115200

// LED
#define LED_PIN 1
#define LED_NUM_PIXELS 1
#define LED_BRIGHTNESS 50

// ====================== GLOBALS ======================
Adafruit_NeoPixel strip(LED_NUM_PIXELS, LED_PIN, NEO_GRB);
WebServer server(80);
DNSServer dnsServer;

// ====================== LED ======================
void ledBegin() {
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.show();
}

void ledSetColor(uint8_t r, uint8_t g, uint8_t b) {
  strip.setPixelColor(0, r, g, b);
  strip.show();
}

// ====================== STORAGE (SPIFFS) ======================
namespace storage {
  bool begin() {
    return SPIFFS.begin(true);
  }

  String readFile(const String &path) {
    String p = path.startsWith("/") ? path : "/" + path;
    File f = SPIFFS.open(p, FILE_READ);
    if (!f) return "";
    String content = f.readString();
    f.close();
    return content;
  }

  bool writeFile(const String &path, const String &content) {
    String p = path.startsWith("/") ? path : "/" + path;
    File f = SPIFFS.open(p, FILE_WRITE);
    if (!f) return false;
    size_t written = f.print(content);
    f.close();
    return written == content.length();
  }

  bool deleteFile(const String &path) {
    String p = path.startsWith("/") ? path : "/" + path;
    return SPIFFS.remove(p);
  }

  bool exists(const String &path) {
    String p = path.startsWith("/") ? path : "/" + path;
    return SPIFFS.exists(p);
  }

  String listScriptsJson() {
    String json = "[";
    bool first = true;
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String name = file.name();
        if (name.startsWith("/")) name = name.substring(1);
        if (name.endsWith(".duck") || name.endsWith(".txt")) {
          if (!first) json += ",";
          json += "\"" + name + "\"";
          first = false;
        }
      }
      file = root.openNextFile();
    }
    json += "]";
    return json;
  }
}

// ====================== HID ======================
#include "USB.h"
#include "USBHIDKeyboard.h"
USBHIDKeyboard Keyboard;

void hidBegin() {
  Keyboard.begin();
  USB.begin();
}
void runDuckScript(const String &script) {
  int start = 0;
  while (start < script.length()) {
    int end = script.indexOf('\n', start);
    if (end == -1) end = script.length();
    
    String line = script.substring(start, end);
    line.trim();
    start = end + 1;

    if (line.length() == 0 || line.startsWith("REM")) continue;

    if (line.startsWith("STRING ")) {
      Keyboard.print(line.substring(7));
    } 
    else if (line.startsWith("STRINGLN ")) {
      Keyboard.println(line.substring(9));
    } 
    else if (line == "ENTER" || line == "RETURN") {
      Keyboard.press(KEY_RETURN);
      Keyboard.releaseAll();
    } 
    else if (line.startsWith("DELAY ")) {
      int ms = line.substring(6).toInt();
      if (ms > 0) delay(ms);
    } 
    else if (line.startsWith("GUI ") || line.startsWith("WINDOWS ")) {
      Keyboard.press(KEY_LEFT_GUI);
      // Có thể thêm phím tiếp theo nếu cần (ví dụ: r)
      String arg = line.substring(line.indexOf(' ') + 1);
      if (arg == "r" || arg == "R") {
        Keyboard.press('r');
      }
      Keyboard.releaseAll();
    } 
    else if (line.length() > 0) {
      // Thử gõ trực tiếp nếu là lệnh đơn (như ENTER, TAB...)
      if (line == "TAB") Keyboard.press(KEY_TAB);
      else if (line == "SPACE") Keyboard.press(' ');
      Keyboard.releaseAll();
    }

    delay(30); // Nhỏ delay giữa lệnh để ổn định
  }
}
// ====================== WEB UI ======================
const char* html = R"rawhtml(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>EvilDuck S3</title>
  <style>
    body { font-family: Arial, sans-serif; margin:0; background:#1e1e1e; color:#ddd; }
    .container { max-width: 1000px; margin: 20px auto; padding: 20px; }
    h1 { color: #00ff9d; }
    .panel { background:#2d2d2d; border-radius:8px; padding:15px; margin-bottom:15px; }
    button { padding:8px 16px; margin:5px; border:none; border-radius:4px; cursor:pointer; }
    .btn-primary { background:#00ff9d; color:black; }
    .btn-danger { background:#ff4444; color:white; }
    textarea { width:100%; height:300px; background:#1e1e1e; color:#ddd; border:1px solid #444; border-radius:4px; font-family: monospace; }
    ul { list-style:none; padding:0; }
    li { padding:8px; background:#333; margin:4px 0; border-radius:4px; cursor:pointer; }
    li:hover { background:#444; }
    #status { padding:10px; margin:10px 0; border-radius:4px; }
  </style>
</head>
<body>
  <div class="container">
    <h1>🦆 EvilDuck S3 (No SD)</h1>
    <div class="panel">
      <button class="btn-primary" onclick="newScript()">+ Script Mới</button>
      <button onclick="listScripts()">Làm mới danh sách</button>
    </div>

    <div class="panel">
      <h3>Danh sách Script</h3>
      <ul id="scriptList"></ul>
    </div>

    <div class="panel">
      <h3>Chỉnh sửa Script: <span id="currentScript"></span></h3>
      <textarea id="editor" spellcheck="false"></textarea>
      <button class="btn-primary" onclick="saveScript()">💾 Lưu</button>
      <button class="btn-primary" onclick="runScript()">▶️ Chạy Script</button>
      <button class="btn-danger" onclick="deleteScript()">🗑️ Xóa</button>
    </div>

    <div id="status"></div>
  </div>

  <script>
    let currentFile = "";

    function showStatus(msg, color="green") {
      const status = document.getElementById("status");
      status.textContent = msg;
      status.style.background = color === "green" ? "#004d00" : "#4d0000";
      setTimeout(() => status.textContent = "", 4000);
    }

    function listScripts() {
      fetch('/scripts').then(r => r.json()).then(data => {
        const ul = document.getElementById("scriptList");
        ul.innerHTML = "";
        data.forEach(name => {
          const li = document.createElement("li");
          li.textContent = name;
          li.onclick = () => loadScript(name);
          ul.appendChild(li);
        });
      });
    }

    function newScript() {
      currentFile = "new_script_" + Date.now() + ".duck";
      document.getElementById("currentScript").textContent = currentFile;
      document.getElementById("editor").value = "REM EvilDuck Script\nSTRING Hello from EvilDuck S3!\nENTER\n";
    }

    function loadScript(name) {
      currentFile = name;
      document.getElementById("currentScript").textContent = name;
      fetch('/script?name=' + encodeURIComponent(name))
        .then(r => r.text())
        .then(text => document.getElementById("editor").value = text);
    }

    function saveScript() {
      if (!currentFile) return;
      const content = document.getElementById("editor").value;
      fetch('/script?name=' + encodeURIComponent(currentFile), {
        method: 'POST',
        headers: {'Content-Type': 'text/plain'},
        body: content
      }).then(() => showStatus("Đã lưu " + currentFile));
    }

    function runScript() {
      if (!currentFile) return;
      fetch('/run?name=' + encodeURIComponent(currentFile))
        .then(() => showStatus("Đang chạy script..."));
    }

    function deleteScript() {
      if (!currentFile || !confirm("Xóa script này?")) return;
      fetch('/script?name=' + encodeURIComponent(currentFile), {method: 'DELETE'})
        .then(() => {
          showStatus("Đã xóa " + currentFile, "red");
          currentFile = "";
          document.getElementById("currentScript").textContent = "";
          document.getElementById("editor").value = "";
          listScripts();
        });
    }

    // Load danh sách khi mở trang
    window.onload = listScripts;
  </script>
</body>
</html>
)rawhtml";

// ====================== SERVER HANDLERS ======================
void handleRoot() {
  server.send(200, "text/html", html);
}

void handleScripts() {
  server.send(200, "application/json", storage::listScriptsJson());
}

void handleScript() {
  String name = server.arg("name");
  if (name == "") { server.send(400, "text/plain", "Missing name"); return; }

  if (server.method() == HTTP_GET) {
    String content = storage::readFile(name);
    server.send(200, "text/plain", content);
  }
  else if (server.method() == HTTP_POST) {
    String content = server.arg("plain");
    bool ok = storage::writeFile(name, content);
    server.send(ok ? 200 : 500, "text/plain", ok ? "Saved" : "Failed");
  }
  else if (server.method() == HTTP_DELETE) {
    bool ok = storage::deleteFile(name);
    server.send(ok ? 200 : 500, "text/plain", ok ? "Deleted" : "Failed");
  }
}

void handleRun() {
  String name = server.arg("name");
  if (name == "") { server.send(400); return; }

  String script = storage::readFile(name);
  if (script.length() > 0) {
    ledSetColor(0, 0, 255);     // Blue = running
    runDuckScript(script);
    ledSetColor(0, 255, 0);     // Green = done
    server.send(200, "text/plain", "Script executed");
  } else {
    server.send(404);
  }
}

// ====================== SETUP ======================
void setup() {
  Serial.begin(DEBUG_SERIAL_BAUD);
  Serial.println("\n=== EvilDuck S3 - No SD + Full Web UI ===");

  ledBegin();
  ledSetColor(255, 100, 0);
  hidBegin();
  storage::begin();

  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  dnsServer.start(53, "*", WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/scripts", handleScripts);
  server.on("/script", HTTP_GET, handleScript);
  server.on("/script", HTTP_POST, handleScript);
  server.on("/script", HTTP_DELETE, handleScript);
  server.on("/run", handleRun);

  server.begin();

  ledSetColor(0, 255, 0);
  Serial.printf("WiFi: %s | IP: 192.168.4.1\n", WIFI_SSID);
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  delay(2);
}

/* Use UART1 in STM32*/

#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <LittleFS.h>

/* ---------------- USER CONFIG ----------------------------------------- */
const char *WIFI_SSID = "Your SSID";
const char *WIFI_PASS = "YOur password";

#define USE_AP_MODE       false

#define UART_TX_PIN       17       // -> STM32 PA10 (RX)
#define UART_RX_PIN       16       // <- STM32 PA9  (TX)
#define UART_BAUD         115200

#define USE_NRST_PIN      false    
#define NRST_PIN          4
/* ---------------------------------------------------------------------- */

/* Protocol bytes - must match the STM32 bootloader */
#define CMD_HANDSHAKE     0x7F
#define RESP_ACK          0x79
#define CHUNK_SIZE        256

#define FW_PATH           "/firmware.bin"

WebServer server(80);
File      uploadFile;
String    lastResult = "No firmware uploaded yet.";

/* ===================== UART low-level ================================== */
static void flushRx() { while (Serial2.available()) Serial2.read(); }

static int waitByte(uint32_t timeout_ms) {
  uint32_t t0 = millis();
  while ((millis() - t0) < timeout_ms) {
    if (Serial2.available()) return Serial2.read();
    delay(1);                     
  }
  return -1;
}

static void resetSTM32() {
#if USE_NRST_PIN
  pinMode(NRST_PIN, OUTPUT);
  digitalWrite(NRST_PIN, LOW);      // assert reset
  delay(30);
  pinMode(NRST_PIN, INPUT);         
  delay(60);                        
#endif
}

/* ===================== Flash the STM32 ================================= */
static bool flashSTM32(String &msg) {
  File f = LittleFS.open(FW_PATH, "r");
  if (!f || f.size() == 0) { msg = "No firmware.bin in LittleFS - upload one first."; return false; }
  uint32_t size = f.size();

  resetSTM32();                     
  flushRx();


  bool acked = false;
  uint32_t t0 = millis();
  while ((millis() - t0) < 6000) {
    Serial2.write((uint8_t)CMD_HANDSHAKE);
    if (waitByte(250) == RESP_ACK) { acked = true; break; }
  }
  if (!acked) { f.close(); msg = "No response from STM32. Reset the board and retry."; return false; }

  // 2. Send 4-byte size (little-endian), then wait for the erase ACK.
  uint8_t szb[4] = { (uint8_t)(size),       (uint8_t)(size >> 8),
                     (uint8_t)(size >> 16), (uint8_t)(size >> 24) };
  Serial2.write(szb, 4);
  if (waitByte(10000) != RESP_ACK) { f.close(); msg = "STM32 rejected the size / erase failed."; return false; }


  uint8_t buf[CHUNK_SIZE];
  uint32_t remaining = size;
  while (remaining > 0) {
    uint32_t n = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
    f.read(buf, n);
    Serial2.write(buf, n);
    Serial2.flush();                // wait until bytes are actually out
    if (waitByte(5000) != RESP_ACK) { f.close(); msg = "Transfer failed mid-way (no chunk ACK)."; return false; }
    remaining -= n;
  }
  f.close();
  msg = "Success! Sent " + String(size) + " bytes. STM32 is running the new app.";
  return true;
}

/* ===================== Web handlers =================================== */
static const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1">
<title>STM32 Updater</title><style>
body{font-family:system-ui,sans-serif;max-width:540px;margin:40px auto;padding:0 16px;color:#222}
h2{font-weight:500}.card{border:1px solid #ddd;border-radius:12px;padding:20px;margin:16px 0}
input[type=file]{margin:10px 0}button{font-size:16px;padding:10px 18px;border:0;border-radius:8px;
background:#1d9e75;color:#fff;cursor:pointer}button.alt{background:#534ab7}
.r{background:#f1efe8;border-radius:8px;padding:12px;margin-top:12px;font-family:monospace;font-size:13px}
small{color:#666}</style></head><body>
<h2>STM32F446 firmware updater</h2>
<div class=card>
  <b>1. Upload .bin</b><br><small>Stored in ESP32 LittleFS as firmware.bin</small><br>
  <form method=POST action="/upload" enctype="multipart/form-data">
    <input type=file name=fw accept=".bin" required><br>
    <button type=submit>Upload</button>
  </form>
</div>
<div class=card>
  <b>2. Flash to STM32</b><br><small>Streams over UART to the bootloader, then runs it</small><br><br>
  <form method=POST action="/flash"><button class=alt type=submit>Flash STM32</button></form>
</div>
<div class=r>%RESULT%</div>
</body></html>
)HTML";

static void handleRoot() {
  String html = FPSTR(PAGE);
  html.replace("%RESULT%", lastResult);
  server.send(200, "text/html", html);
}

static void handleUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (LittleFS.exists(FW_PATH)) LittleFS.remove(FW_PATH);
    uploadFile = LittleFS.open(FW_PATH, "w");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) { uploadFile.close();
      lastResult = "Uploaded " + String(up.totalSize) + " bytes. Ready to flash."; }
  }
}

static void handleFlash() {
  String msg;
  flashSTM32(msg);
  lastResult = msg;
  server.sendHeader("Location", "/");
  server.send(303);                 // redirect back to the page with result
}

/* ===================== Setup / loop =================================== */
void setup() {
  Serial.begin(115200);
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  if (!LittleFS.begin(true)) {       
    Serial.println("LittleFS mount failed");
  }

#if USE_AP_MODE
  WiFi.mode(WIFI_AP);
  WiFi.softAP("STM32-Updater");
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
#else
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.print("\nOpen http://"); Serial.println(WiFi.localIP());
#endif

  server.on("/", HTTP_GET, handleRoot);
  server.on("/upload", HTTP_POST, []() { server.sendHeader("Location", "/"); server.send(303); }, handleUpload);
  server.on("/flash", HTTP_POST, handleFlash);
  server.begin();
}

void loop() {
  server.handleClient();
}

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Arduino_JSON.h> 
#include <HTTPClient.h>
#include <AsyncTCP.h>
#include <Adafruit_GFX.h>
#include <FastLED_NeoMatrix.h>
#include <FastLED.h>
#include <time.h>
// --- [設定區] ---
#define PIN_DATA      13
#define MATRIX_WIDTH  32
#define MATRIX_HEIGHT 8
int brightness = 20;

const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASS;

String openWeatherMapApiKey = API_KEY;
String city = "Taipei";
String countryCode = "TW";

// 時區設定
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 28800; // UTC+8
const int   daylightOffset_sec = 0;

#ifndef PSTR
#define PSTR 
#endif

CRGB leds[MATRIX_WIDTH * MATRIX_HEIGHT];
FastLED_NeoMatrix *matrix = new FastLED_NeoMatrix(leds, MATRIX_WIDTH, MATRIX_HEIGHT, 
  NEO_MATRIX_TOP + NEO_MATRIX_RIGHT + NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG);

AsyncWebServer server(80);

enum Mode { CLOCK, WEATHER, RAINBOW, FIRE, MATRIX_RAIN, OFF };
Mode currentMode = CLOCK;

String weatherTemp = "--";
String weatherDesc = "";
uint8_t hue = 0;          // 用於彩虹
unsigned long lastWeatherUpdate = 0;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>Matrix Hub</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0/css/all.min.css">
  <style>
    :root { --bg: #0f172a; --card: #1e293b; --primary: #38bdf8; --text: #f1f5f9; }
    body { font-family: sans-serif; background: var(--bg); color: var(--text); display: flex; flex-direction: column; align-items: center; padding: 20px; }
    .status-card { background: var(--card); padding: 15px; border-radius: 12px; width: 100%; max-width: 350px; text-align: center; margin-bottom: 20px; border: 1px solid #334155; }
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; width: 100%; max-width: 350px; }
    .btn { background: var(--card); border: 1px solid #334155; color: var(--text); padding: 20px; border-radius: 12px; cursor: pointer; display: flex; flex-direction: column; align-items: center; transition: 0.2s; }
    .btn i { font-size: 24px; margin-bottom: 8px; }
    .btn.active { background: var(--primary); color: white; box-shadow: 0 0 15px var(--primary); }
    .settings { background: var(--card); width: 100%; max-width: 350px; margin-top: 20px; padding: 15px; border-radius: 12px; box-sizing: border-box; }
    input[type=range] { width: 100%; margin-top: 10px; }
  </style>
</head><body>
  <h2><i class="fa-solid fa- dice-d6"></i> MATRIX HUB</h2>
  <div class="status-card">Mode: <b id="mode-text">Clock</b></div>
  <div class="grid">
    <button class="btn active" id="btn0" onclick="setMode(0,'Clock')"><i class="fa-regular fa-clock"></i>Time</button>
    <button class="btn" id="btn1" onclick="setMode(1,'Weather')"><i class="fa-solid fa-cloud-sun"></i>Weather</button>
    <button class="btn" id="btn2" onclick="setMode(2,'Rainbow')"><i class="fa-solid fa-wand-magic-sparkles"></i>Rainbow</button>
    <button class="btn" id="btn3" onclick="setMode(3,'Fire')"><i class="fa-solid fa-fire"></i>Fire</button>
    <button class="btn" id="btn4" onclick="setMode(4,'Matrix Rain')"><i class="fa-solid fa-code"></i>Matrix</button>
    <button class="btn" id="btn5" onclick="setMode(5,'Off')"><i class="fa-solid fa-power-off"></i>Off</button>
  </div>
  <div class="settings">
    Brightness: <span id="bval">20</span>
    <input type="range" min="5" max="150" value="20" oninput="setBright(this.value)">
  </div>
  <script>
    function setMode(v,n){
      document.getElementById('mode-text').innerText=n;
      document.querySelectorAll('.btn').forEach(b=>b.classList.remove('active'));
      document.getElementById('btn'+v).classList.add('active');
      fetch('/setMode?val='+v);
    }
    function setBright(v){
      document.getElementById('bval').innerText=v;
      fetch('/setBright?val='+v);
    }
  </script>
</body></html>)rawliteral";


void getWeatherData() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + countryCode + "&APPID=" + openWeatherMapApiKey + "&units=metric";
    http.begin(url);
    if (http.GET() > 0) {
      JSONVar myObject = JSON.parse(http.getString());
      weatherTemp = String((int)myObject["main"]["temp"]);
      weatherDesc = (const char*)myObject["weather"][0]["main"];
    }
    http.end();
  }
}

// 燈效：火焰
void effectFire() {
  static byte heat[MATRIX_WIDTH][MATRIX_HEIGHT];
  for(int x=0; x<MATRIX_WIDTH; x++) {
    for(int y=0; y<MATRIX_HEIGHT; y++) heat[x][y] = qsub8(heat[x][y], random8(0, 15));
  }
  for(int x=0; x<MATRIX_WIDTH; x++) {
    for(int y=MATRIX_HEIGHT-1; y>=1; y--) heat[x][y] = (heat[x][y-1] + heat[x][y-2]) / 2;
  }
  if(random8() < 80) { int x = random8(MATRIX_WIDTH); heat[x][0] = qadd8(heat[x][0], random8(160,255)); }
  for(int x=0; x<MATRIX_WIDTH; x++) {
    for(int y=0; y<MATRIX_HEIGHT; y++) matrix->drawPixel(x, (MATRIX_HEIGHT-1)-y, HeatColor(heat[x][y]));
  }
}

// 燈效：數位雨 (Matrix Rain)
void effectMatrixRain() {
  fadeToBlackBy(leds, MATRIX_WIDTH * MATRIX_HEIGHT, 60);
  for(int x=0; x<MATRIX_WIDTH; x++) {
    if(random8() < 10) matrix->drawPixel(x, 0, CRGB(0, 255, 60));
  }
  // 簡易下移邏輯 (透過 LED 陣列拷貝)
  for(int i=MATRIX_WIDTH*(MATRIX_HEIGHT-1)-1; i>=0; i--) leds[i+MATRIX_WIDTH] = leds[i];
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<NEOPIXEL, PIN_DATA>(leds, MATRIX_WIDTH * MATRIX_HEIGHT);
  matrix->begin();
  matrix->setBrightness(brightness);
  matrix->setTextWrap(false);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nIP: " + WiFi.localIP().toString());

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/setMode", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("val")) {
      currentMode = (Mode)request->getParam("val")->value().toInt();
      if(currentMode == WEATHER) getWeatherData();
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/setBright", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("val")) {
      brightness = request->getParam("val")->value().toInt();
      matrix->setBrightness(brightness);
    }
    request->send(200, "text/plain", "OK");
  });

  server.begin();
  getWeatherData(); // 初始抓一次天氣
}

void loop() {
  // 每 15 分鐘自動更新天氣
  if (millis() - lastWeatherUpdate > 900000) {
    getWeatherData();
    lastWeatherUpdate = millis();
  }

  // 除非是特殊效果，否則每幀清屏
  if (currentMode != MATRIX_RAIN) matrix->fillScreen(0);

  switch (currentMode) {
    case CLOCK: {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        char buf[6]; strftime(buf, 6, "%H:%M", &timeinfo);
        matrix->setTextColor(matrix->Color(0, 255, 200));
        matrix->setCursor(2, 0); matrix->print(buf);
      }
      break;
    }
    case WEATHER: {
      matrix->setTextColor(matrix->Color(255, 150, 0));
      matrix->setCursor(0, 0);
      matrix->print(weatherTemp + "C"); // $8x32$ 空間有限，僅顯示溫度
      break;
    }
    case RAINBOW: {
      fill_rainbow(leds, MATRIX_WIDTH * MATRIX_HEIGHT, hue++, 8);
      break;
    }
    case FIRE:
      effectFire();
      break;
    case MATRIX_RAIN:
      effectMatrixRain();
      break;
    case OFF:
      matrix->fillScreen(0);
      break;
  }

  matrix->show();
  
  // 針對不同模式控制速度
  int d = 100;
  if(currentMode == FIRE) d = 30;
  if(currentMode == MATRIX_RAIN) d = 50;
  if(currentMode == RAINBOW) d = 20;
  delay(d);
}
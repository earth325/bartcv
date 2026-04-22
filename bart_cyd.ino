/*
 * BART Train Tracker for ESP32-2432S028R (Cheap Yellow Display)
 * Station: Castro Valley (CAST) — Blue Line only
 * Northbound: toward Daly City / SF
 * Southbound: toward Dublin/Pleasanton
 *
 * Libraries required:
 *   - TFT_eSPI  (configured for CYD — see SETUP_INSTRUCTIONS.md)
 *   - ArduinoJson
 *   - WiFi (built-in ESP32)
 *   - HTTPClient (built-in ESP32)
 */

#include <WiFi.h>
#include <WiFiClientSecure.h> // <--- ADD THIS LINE
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// ─── USER CONFIG ──────────────────────────────────────────────────────────────
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

// Free public BART API key — or register your own at:
// http://api.bart.gov/docs/overview/authentication.aspx
const char* BART_API_KEY  = "MW9S-E7SL-26DU-VV8V";

// Castro Valley station abbreviation
const char* STATION       = "CAST";

// How often to refresh (milliseconds)
const unsigned long REFRESH_INTERVAL = 30000;  // 30 seconds
// ─────────────────────────────────────────────────────────────────────────────

TFT_eSPI tft = TFT_eSPI();

// ── Color palette ──
#define COL_BG         0x0841
#define COL_PANEL      0x1082
#define COL_ACCENT     0xFD20
#define COL_NORTH_HDR  0x04FF   // Cyan  — northbound (toward Daly City / SF)
#define COL_SOUTH_HDR  0xFB4A   // Amber — southbound (toward Dublin/Pleasanton)
#define COL_TEXT       0xFFFF
#define COL_DIM        0x7BEF
#define COL_GREEN      0x07E0
#define COL_YELLOW     0xFFE0
#define COL_WHITE      0xFFFF

// ── Layout (landscape 320×240) ──
#define SCREEN_W   320
#define SCREEN_H   240
#define HDR_H       36   // title bar height
#define SUBHDR_H    18   // direction header height
#define ROW_H       40   // departure row height — 4 rows x 40px = 160px fits in ~168px available
#define FOOTER_H    14
#define HALF_W     160

struct Departure {
  String destination;
  String minutes;
  String color;
  String hexColor;
};

struct DirectionData {
  String label;
  Departure deps[4];
  int count;
};

DirectionData northData, southData;

unsigned long lastRefresh = 0;

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COL_BG);

  drawSplash();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(10, 200);
  tft.print("Connecting to WiFi");

  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    tft.print(".");
    dots++;
    if (dots > 20) {
      tft.fillRect(10, 195, 300, 20, COL_BG);
      tft.setCursor(10, 200);
      tft.print("Connecting to WiFi");
      dots = 0;
    }
  }

  Serial.println("WiFi connected: " + WiFi.localIP().toString());
  tft.fillScreen(COL_BG);
  fetchAndDraw();
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();
  if (now - lastRefresh >= REFRESH_INTERVAL) {
    fetchAndDraw();
  }
  static unsigned long lastTick = 0;
  if (now - lastTick >= 1000) {
    lastTick = now;
    drawRefreshCountdown(REFRESH_INTERVAL - (now - lastRefresh));
  }
}

// ─────────────────────────────────────────────────────────────────────────────
void fetchAndDraw() {
  lastRefresh = millis();

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(3000);
  }

  String url = "https://api.bart.gov/api/etd.aspx?cmd=etd&orig=";
  url += STATION;
  url += "&key=";
  url += BART_API_KEY;
  url += "&json=y";
  Serial.println("Fetching: " + url);

  // Use WiFiClientSecure to handle HTTPS
  WiFiClientSecure client;
  client.setInsecure(); // This bypasses SSL certificate verification

  HTTPClient http;
  
  // Pass the secure client to the HTTPClient
  if (http.begin(client, url)) { 
    int code = http.GET();
    
    if (code == 200) {
      String payload = http.getString();
      Serial.println("Got response, parsing...");
      parseAndStore(payload);
      drawUI();
    } else {
      Serial.println("HTTP error: " + String(code));
      drawError("HTTP " + String(code));
    }
    http.end();
  } else {
    Serial.println("Unable to connect");
    drawError("Conn Fail");
  }
}
// ─────────────────────────────────────────────────────────────────────────────
void parseAndStore(const String& json) {
  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.println("JSON error: " + String(err.c_str()));
    drawError("JSON parse fail");
    return;
  }

  northData.count = 0;
  northData.label = "To Daly City / SF";
  southData.count = 0;
  southData.label = "To Dublin/Pleasanton";

  JsonArray etdArray = doc["root"]["station"][0]["etd"].as<JsonArray>();

  for (JsonObject etd : etdArray) {
    String dest = etd["destination"].as<String>();
    JsonArray estimates = etd["estimate"].as<JsonArray>();

    for (JsonObject est : estimates) {
      String mins   = est["minutes"].as<String>();
      String dir    = est["direction"].as<String>();  // "North" or "South"
      String color  = est["color"].as<String>();
      String hexStr = est["hexcolor"].as<String>();
      if (hexStr.startsWith("#")) hexStr = hexStr.substring(1);

      Departure d;
      d.destination = dest;
      d.minutes     = mins;
      d.color       = color;
      d.hexColor    = hexStr;

      if (dir == "North" && northData.count < 4) {
        northData.deps[northData.count++] = d;
      } else if (dir == "South" && southData.count < 4) {
        southData.deps[southData.count++] = d;
      }
    }
  }

  Serial.println("North trains: " + String(northData.count));
  Serial.println("South trains: " + String(southData.count));
}

// ─────────────────────────────────────────────────────────────────────────────
uint16_t bartHexToColor565(const String& hex) {
  long val = strtol(hex.c_str(), NULL, 16);
  uint8_t r = (val >> 16) & 0xFF;
  uint8_t g = (val >> 8)  & 0xFF;
  uint8_t b =  val        & 0xFF;
  return tft.color565(r, g, b);
}

uint16_t minuteColor(const String& mins) {
  if (mins == "Leaving") return COL_GREEN;
  int m = mins.toInt();
  if (m <= 5)  return COL_GREEN;
  if (m <= 10) return COL_YELLOW;
  return COL_WHITE;
}

// ─────────────────────────────────────────────────────────────────────────────
void drawUI() {
  tft.fillScreen(COL_BG);

  // ── Title bar ──
  tft.fillRect(0, 0, SCREEN_W, HDR_H, COL_PANEL);
  tft.setTextColor(COL_ACCENT, COL_PANEL);
  tft.setTextSize(2);
  tft.setCursor(8, 10);
  tft.print("CASTRO VALLEY BART");

  // ── Centre divider ──
  tft.drawFastVLine(HALF_W, HDR_H, SCREEN_H - HDR_H - FOOTER_H, COL_DIM);

  // ── North header (cyan) ──
  tft.fillRect(0, HDR_H, HALF_W, SUBHDR_H, COL_NORTH_HDR);
  tft.setTextColor(COL_BG, COL_NORTH_HDR);
  tft.setTextSize(1);
  tft.setCursor(4, HDR_H + 5);
  tft.print("TO DUBLIN/PLSNT");

  // ── South header (amber) ──
  tft.fillRect(HALF_W + 1, HDR_H, HALF_W - 1, SUBHDR_H, COL_SOUTH_HDR);
  tft.setTextColor(COL_BG, COL_SOUTH_HDR);
  tft.setCursor(HALF_W + 4, HDR_H + 5);
  tft.print("TO DALY CITY / SF");

  // ── Departures ──
  drawDepartures(northData, 0);
  drawDepartures(southData, HALF_W + 2);

  // ── Footer ──
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(4, SCREEN_H - FOOTER_H);
  tft.print("Auto-refresh every 30s");
}

// ─────────────────────────────────────────────────────────────────────────────
void drawDepartures(const DirectionData& data, int xOff) {
  // Rows start just below the direction sub-header
  int yStart = HDR_H + SUBHDR_H + 2;
  int colW   = HALF_W - 2;

  if (data.count == 0) {
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setTextSize(1);
    tft.setCursor(xOff + 4, yStart + 16);
    tft.print("No trains");
    return;
  }

  for (int i = 0; i < data.count && i < 4; i++) {
    const Departure& d = data.deps[i];
    int rowY = yStart + i * ROW_H;

    // Row background (alternating)
    uint16_t rowBg = (i % 2 == 0) ? COL_PANEL : COL_BG;
    tft.fillRect(xOff, rowY, colW, ROW_H - 1, rowBg);

    // BART line color swatch
    uint16_t lineCol = bartHexToColor565(d.hexColor);
    tft.fillRect(xOff + 2, rowY + 3, 5, ROW_H - 7, lineCol);

   // Look for this section in your drawDepartures function and replace it:

// Destination name
tft.setTextColor(COL_TEXT, rowBg);
tft.setTextSize(1);
tft.setCursor(xOff + 11, rowY + 5);

String dest = d.destination;
// --- ADD/CHANGE THIS LOGIC ---
// If the destination name is too long, truncate it to fit the column width
if (dest.length() > 12) { 
    dest = dest.substring(0, 11) + "."; 
}
// -----------------------------
tft.print(dest);

    // Line color label
    tft.setTextColor(lineCol, rowBg);
    tft.setCursor(xOff + 11, rowY + 17);
    String lineLabel = d.color;
    if (lineLabel.length() > 10) lineLabel = lineLabel.substring(0, 10);
    tft.print(lineLabel + " Line");

    // Minutes — big, color-coded, right-aligned
    uint16_t mCol = minuteColor(d.minutes);
    tft.setTextColor(mCol, rowBg);
    tft.setTextSize(2);
    String mStr = (d.minutes == "Leaving") ? "NOW" : d.minutes + " min";
    int mW = mStr.length() * 12;
    tft.setCursor(xOff + colW - mW - 4, rowY + 12);
    tft.print(mStr);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
void drawRefreshCountdown(unsigned long remaining) {
  int secs = remaining / 1000;
  tft.setTextColor(COL_DIM, COL_PANEL);
  tft.setTextSize(1);
  char buf[8];
  snprintf(buf, sizeof(buf), "%2ds", secs);
  tft.setCursor(SCREEN_W - 28, 12);
  tft.print(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
void drawSplash() {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(3);
  tft.setCursor(55, 80);
  tft.print("BART");
  tft.setTextSize(2);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setCursor(30, 115);
  tft.print("Castro Valley");
  tft.setTextSize(1);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(70, 145);
  tft.print("Live Departures");
}

// ─────────────────────────────────────────────────────────────────────────────
void drawError(const String& msg) {
  tft.fillScreen(COL_BG);
  tft.setTextColor(0xF800, COL_BG);
  tft.setTextSize(2);
  tft.setCursor(20, 100);
  tft.print("Error:");
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(20, 125);
  tft.print(msg);
  tft.setCursor(20, 145);
  tft.print("Retrying in 30s...");
}

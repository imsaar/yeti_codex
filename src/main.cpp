#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <U8g2lib.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <Wire.h>
#include <stdlib.h>

#include "config.h"

enum class Emotion {
  Neutral,
  Happy,
  Sad,
  Sleepy,
  Angry,
  Surprised,
  Thinking,
  Love
};

enum class DisplayMode {
  Face,
  Info
};

struct Reminder {
  bool active;
  uint32_t dueMs;
  String message;
};

static constexpr size_t kMaxNotes = 8;
static constexpr size_t kMaxReminders = 8;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
WebServer server(80);

Emotion currentEmotion = Emotion::Neutral;
DisplayMode currentDisplayMode = DisplayMode::Face;
String speechText = "Hello";
String notes[kMaxNotes];
size_t notesCount = 0;
Reminder reminders[kMaxReminders];

String infoTemperature = "Loading...";
int infoWeatherCode = -1;
bool infoTempValid = false;
bool infoUseFahrenheit = true;
double infoLatitude = 47.6062;
double infoLongitude = -122.3321;
bool infoHasCoordinates = true;
uint32_t lastInfoTempFetchMs = 0;

int debugLastWeatherCode = -1;
String debugLastWeatherPayload = "";

static constexpr uint32_t kInfoTempRefreshMs = 10UL * 60UL * 1000UL;
static constexpr uint32_t kInfoRetryMs = 20UL * 1000UL;
static constexpr uint32_t kFaceRefreshMs = 33UL;
static constexpr uint32_t kInfoDisplayRefreshMs = 1000UL;

uint32_t nextBlinkMs = 0;
uint32_t blinkUntilMs = 0;
uint32_t nextFaceRefreshMs = 0;
bool blinkClosed = false;

void drawFace();
void drawInfo();

String currentIpAddress() {
  return WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

String displayModeToString(DisplayMode mode) {
  return mode == DisplayMode::Info ? "info" : "face";
}

bool tryParseDisplayMode(const String& name, DisplayMode& outMode) {
  String value = name;
  value.toLowerCase();
  if (value == "face") {
    outMode = DisplayMode::Face;
    return true;
  }
  if (value == "info") {
    outMode = DisplayMode::Info;
    return true;
  }
  return false;
}

bool tryParseDoubleStrict(const String& input, double& outValue) {
  String trimmed = input;
  trimmed.trim();
  if (trimmed.length() == 0) return false;
  char* endPtr = nullptr;
  outValue = strtod(trimmed.c_str(), &endPtr);
  return endPtr != nullptr && *endPtr == '\0';
}

String infoTempUnitLabel() {
  return infoUseFahrenheit ? "F" : "C";
}

String tempUnitOptionsHtml() {
  String html;
  html += "<option value='f'";
  if (infoUseFahrenheit) html += " selected";
  html += ">Fahrenheit</option>";
  html += "<option value='c'";
  if (!infoUseFahrenheit) html += " selected";
  html += ">Celsius</option>";
  return html;
}

String urlEncode(const String& input) {
  static const char hex[] = "0123456789ABCDEF";
  String encoded;
  encoded.reserve(input.length() * 3);
  for (size_t i = 0; i < input.length(); ++i) {
    uint8_t c = static_cast<uint8_t>(input[i]);
    bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += hex[c >> 4];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

String htmlEscape(const String& input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    if (c == '&') {
      out += "&amp;";
    } else if (c == '<') {
      out += "&lt;";
    } else if (c == '>') {
      out += "&gt;";
    } else if (c == '"') {
      out += "&quot;";
    } else if (c == '\'') {
      out += "&#39;";
    } else {
      out += c;
    }
  }
  return out;
}

String truncateForDebug(const String& input, size_t maxLen = 220) {
  if (input.length() <= maxLen) return input;
  return input.substring(0, maxLen) + "...";
}

bool fetchInfoTemperature() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!infoHasCoordinates) return false;

  debugLastWeatherCode = -1;
  debugLastWeatherPayload = "";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(infoLatitude, 4) +
               "&longitude=" + String(infoLongitude, 4) +
               "&current=temperature_2m,weather_code&temperature_unit=" +
               String(infoUseFahrenheit ? "fahrenheit" : "celsius");
  if (!http.begin(client, url)) return false;
  debugLastWeatherCode = http.GET();
  if (debugLastWeatherCode != 200) {
    debugLastWeatherPayload = truncateForDebug(http.getString());
    http.end();
    return false;
  }
  String payload = http.getString();
  debugLastWeatherPayload = truncateForDebug(payload);
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  if (!doc["current"]["temperature_2m"].is<float>() && !doc["current"]["temperature_2m"].is<double>()) {
    return false;
  }

  float tempValue = doc["current"]["temperature_2m"].as<float>();
  String unit = infoTempUnitLabel();
  if (doc["current"]["weather_code"].is<int>()) {
    infoWeatherCode = doc["current"]["weather_code"].as<int>();
  } else {
    infoWeatherCode = -1;
  }

  infoTemperature = String(tempValue, 1) + String(" ") + unit;
  infoTempValid = true;
  return true;
}

void drawWeatherIcon(int weatherCode, int x, int y) {
  // Sun / clear
  if (weatherCode == 0) {
    display.drawCircle(x + 6, y + 6, 4);
    display.drawLine(x + 6, y, x + 6, y - 2);
    display.drawLine(x + 6, y + 12, x + 6, y + 14);
    display.drawLine(x, y + 6, x - 2, y + 6);
    display.drawLine(x + 12, y + 6, x + 14, y + 6);
    return;
  }

  // Cloud base
  auto drawCloud = [&](int cx, int cy) {
    display.drawCircle(cx - 3, cy, 3);
    display.drawCircle(cx + 2, cy - 1, 4);
    display.drawCircle(cx + 7, cy, 3);
    display.drawLine(cx - 6, cy + 3, cx + 10, cy + 3);
  };

  // Partly cloudy
  if (weatherCode == 1 || weatherCode == 2) {
    display.drawCircle(x + 3, y + 4, 3);
    display.drawLine(x + 3, y, x + 3, y - 1);
    drawCloud(x + 7, y + 6);
    return;
  }

  // Cloudy / fog
  if (weatherCode == 3 || weatherCode == 45 || weatherCode == 48) {
    drawCloud(x + 6, y + 6);
    if (weatherCode == 45 || weatherCode == 48) {
      display.drawLine(x, y + 12, x + 12, y + 12);
      display.drawLine(x + 1, y + 14, x + 13, y + 14);
    }
    return;
  }

  // Snow
  if ((weatherCode >= 71 && weatherCode <= 77) || weatherCode == 85 || weatherCode == 86) {
    drawCloud(x + 6, y + 5);
    display.drawLine(x + 4, y + 11, x + 4, y + 15);
    display.drawLine(x + 2, y + 13, x + 6, y + 13);
    display.drawLine(x + 9, y + 11, x + 9, y + 15);
    display.drawLine(x + 7, y + 13, x + 11, y + 13);
    return;
  }

  // Thunderstorm
  if (weatherCode >= 95) {
    drawCloud(x + 6, y + 5);
    display.drawLine(x + 7, y + 10, x + 4, y + 14);
    display.drawLine(x + 4, y + 14, x + 8, y + 14);
    display.drawLine(x + 8, y + 14, x + 5, y + 18);
    return;
  }

  // Rain / drizzle / showers (default wet icon)
  drawCloud(x + 6, y + 5);
  display.drawLine(x + 4, y + 11, x + 3, y + 15);
  display.drawLine(x + 8, y + 11, x + 7, y + 15);
  display.drawLine(x + 12, y + 11, x + 11, y + 15);
}

void serviceInfoData() {
  uint32_t now = millis();

  uint32_t tempInterval = infoTempValid ? kInfoTempRefreshMs : kInfoRetryMs;
  if (lastInfoTempFetchMs == 0 || (now - lastInfoTempFetchMs >= tempInterval)) {
    lastInfoTempFetchMs = now;
    fetchInfoTemperature();
  }
}

String emotionToString(Emotion emotion) {
  switch (emotion) {
    case Emotion::Neutral:
      return "neutral";
    case Emotion::Happy:
      return "happy";
    case Emotion::Sad:
      return "sad";
    case Emotion::Sleepy:
      return "sleepy";
    case Emotion::Angry:
      return "angry";
    case Emotion::Surprised:
      return "surprised";
    case Emotion::Thinking:
      return "thinking";
    case Emotion::Love:
      return "love";
  }
  return "neutral";
}

Emotion parseEmotion(const String& name) {
  String value = name;
  value.toLowerCase();

  if (value == "happy") return Emotion::Happy;
  if (value == "sad") return Emotion::Sad;
  if (value == "sleepy") return Emotion::Sleepy;
  if (value == "angry") return Emotion::Angry;
  if (value == "surprised") return Emotion::Surprised;
  if (value == "thinking") return Emotion::Thinking;
  if (value == "love") return Emotion::Love;
  return Emotion::Neutral;
}

bool tryParseEmotion(const String& name, Emotion& outEmotion) {
  String value = name;
  value.toLowerCase();

  if (value == "neutral") {
    outEmotion = Emotion::Neutral;
    return true;
  }
  if (value == "happy") {
    outEmotion = Emotion::Happy;
    return true;
  }
  if (value == "sad") {
    outEmotion = Emotion::Sad;
    return true;
  }
  if (value == "sleepy") {
    outEmotion = Emotion::Sleepy;
    return true;
  }
  if (value == "angry") {
    outEmotion = Emotion::Angry;
    return true;
  }
  if (value == "surprised") {
    outEmotion = Emotion::Surprised;
    return true;
  }
  if (value == "thinking") {
    outEmotion = Emotion::Thinking;
    return true;
  }
  if (value == "love") {
    outEmotion = Emotion::Love;
    return true;
  }
  return false;
}

void setEmotion(Emotion emotion) {
  currentEmotion = emotion;
  Serial.print("Emotion set to: ");
  Serial.println(emotionToString(currentEmotion));
}

void scheduleBlink(uint32_t now) {
  // Slightly irregular blink interval feels less robotic.
  nextBlinkMs = now + random(1800, 4200);
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
    delay(300);
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
    return;
  }

  Serial.println();
  Serial.println("WiFi connect timeout, starting local AP.");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Companion-313", "companion313");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void sendJson(int statusCode, JsonDocument& doc) {
  String payload;
  serializeJson(doc, payload);
  server.send(statusCode, "application/json", payload);
}

bool parseJsonBody(JsonDocument& doc) {
  if (!server.hasArg("plain")) {
    return false;
  }

  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  return !err;
}

void handleStatus() {
  JsonDocument doc;
  doc["emotion"] = emotionToString(currentEmotion);
  doc["mode"] = displayModeToString(currentDisplayMode);
  doc["speech"] = speechText;
  doc["ip"] = currentIpAddress();
  doc["info_temperature"] = infoTemperature;
  doc["info_temperature_unit"] = infoTempUnitLabel();
  doc["info_weather_code"] = infoWeatherCode;
  doc["info_latitude"] = infoLatitude;
  doc["info_longitude"] = infoLongitude;
  doc["debug_weather_api_code"] = debugLastWeatherCode;
  doc["debug_weather_api_payload"] = debugLastWeatherPayload;

  JsonArray notesArr = doc["notes"].to<JsonArray>();
  for (size_t i = 0; i < notesCount; ++i) {
    notesArr.add(notes[i]);
  }

  JsonArray remindersArr = doc["reminders"].to<JsonArray>();
  uint32_t now = millis();
  for (size_t i = 0; i < kMaxReminders; ++i) {
    if (!reminders[i].active) continue;

    JsonObject item = remindersArr.add<JsonObject>();
    item["message"] = reminders[i].message;
    item["ms_remaining"] = reminders[i].dueMs > now ? (reminders[i].dueMs - now) : 0;
  }

  sendJson(200, doc);
}

void handleEmotion() {
  String emotionArg;

  if (server.hasArg("emotion")) {
    emotionArg = server.arg("emotion");
  } else {
    JsonDocument doc;
    if (!parseJsonBody(doc) || !doc["emotion"].is<const char*>()) {
      JsonDocument error;
      error["error"] =
          "Expected emotion via query/form arg or JSON body: {\"emotion\":\"happy\"}";
      sendJson(400, error);
      return;
    }
    emotionArg = doc["emotion"].as<String>();
  }

  Emotion parsedEmotion = Emotion::Neutral;
  if (!tryParseEmotion(emotionArg, parsedEmotion)) {
    JsonDocument error;
    error["error"] = "Invalid emotion";
    error["received"] = emotionArg;
    error["allowed"] = "neutral,happy,sad,sleepy,angry,surprised,thinking,love";
    sendJson(400, error);
    return;
  }

  setEmotion(parsedEmotion);
  drawFace();

  JsonDocument result;
  result["ok"] = true;
  result["emotion"] = emotionToString(currentEmotion);
  sendJson(200, result);
}

void handleEmotionGet() {
  JsonDocument doc;
  doc["emotion"] = emotionToString(currentEmotion);
  sendJson(200, doc);
}

void handleSpeak() {
  String textArg;
  if (server.hasArg("text")) {
    textArg = server.arg("text");
  } else {
    JsonDocument doc;
    if (!parseJsonBody(doc) || !doc["text"].is<const char*>()) {
      JsonDocument error;
      error["error"] = "Expected text via query/form arg or JSON body: {\"text\":\"hello\"}";
      sendJson(400, error);
      return;
    }
    textArg = doc["text"].as<String>();
  }

  speechText = textArg;
  if (speechText.length() > 40) {
    speechText = speechText.substring(0, 40);
  }

  JsonDocument result;
  result["ok"] = true;
  sendJson(200, result);
}

void handleNotesAdd() {
  String noteArg;
  if (server.hasArg("note")) {
    noteArg = server.arg("note");
  } else {
    JsonDocument doc;
    if (!parseJsonBody(doc) || !doc["note"].is<const char*>()) {
      JsonDocument error;
      error["error"] = "Expected note via query/form arg or JSON body: {\"note\":\"buy milk\"}";
      sendJson(400, error);
      return;
    }
    noteArg = doc["note"].as<String>();
  }

  if (notesCount >= kMaxNotes) {
    for (size_t i = 1; i < kMaxNotes; ++i) {
      notes[i - 1] = notes[i];
    }
    notesCount = kMaxNotes - 1;
  }

  notes[notesCount++] = noteArg;

  JsonDocument result;
  result["ok"] = true;
  result["count"] = notesCount;
  sendJson(200, result);
}

void handleNotesList() {
  JsonDocument doc;
  JsonArray arr = doc["notes"].to<JsonArray>();

  for (size_t i = 0; i < notesCount; ++i) {
    arr.add(notes[i]);
  }

  sendJson(200, doc);
}

void handleRemindersAdd() {
  int minutes = 0;
  String messageArg;
  if (server.hasArg("minutes") && server.hasArg("message")) {
    minutes = server.arg("minutes").toInt();
    messageArg = server.arg("message");
  } else {
    JsonDocument doc;
    if (!parseJsonBody(doc) || !doc["minutes"].is<int>() || !doc["message"].is<const char*>()) {
      JsonDocument error;
      error["error"] = "Expected minutes/message via query/form args or JSON body";
      sendJson(400, error);
      return;
    }
    minutes = doc["minutes"].as<int>();
    messageArg = doc["message"].as<String>();
  }

  if (minutes <= 0) {
    JsonDocument error;
    error["error"] = "minutes must be > 0";
    sendJson(400, error);
    return;
  }

  size_t slot = kMaxReminders;
  for (size_t i = 0; i < kMaxReminders; ++i) {
    if (!reminders[i].active) {
      slot = i;
      break;
    }
  }

  if (slot == kMaxReminders) {
    JsonDocument error;
    error["error"] = "Reminder storage full";
    sendJson(507, error);
    return;
  }

  reminders[slot].active = true;
  reminders[slot].dueMs = millis() + static_cast<uint32_t>(minutes) * 60000UL;
  reminders[slot].message = messageArg;

  JsonDocument result;
  result["ok"] = true;
  result["slot"] = slot;
  sendJson(200, result);
}

void handleClear() {
  notesCount = 0;
  speechText = "Cleared";
  for (size_t i = 0; i < kMaxReminders; ++i) {
    reminders[i].active = false;
  }

  JsonDocument result;
  result["ok"] = true;
  sendJson(200, result);
}

String emotionOptionsHtml(Emotion selectedEmotion) {
  struct Item {
    const char* name;
    Emotion value;
  };
  static const Item kItems[] = {
      {"neutral", Emotion::Neutral},       {"happy", Emotion::Happy},
      {"sad", Emotion::Sad},               {"sleepy", Emotion::Sleepy},
      {"angry", Emotion::Angry},           {"surprised", Emotion::Surprised},
      {"thinking", Emotion::Thinking},     {"love", Emotion::Love},
  };

  String html;
  for (size_t i = 0; i < sizeof(kItems) / sizeof(kItems[0]); ++i) {
    html += "<option value='";
    html += kItems[i].name;
    html += "'";
    if (kItems[i].value == selectedEmotion) {
      html += " selected";
    }
    html += ">";
    html += kItems[i].name;
    html += "</option>";
  }
  return html;
}

String modeOptionsHtml(DisplayMode selectedMode) {
  String html;
  html += "<option value='face'";
  if (selectedMode == DisplayMode::Face) html += " selected";
  html += ">face</option>";
  html += "<option value='info'";
  if (selectedMode == DisplayMode::Info) html += " selected";
  html += ">info</option>";
  return html;
}

String statusMessageFromCode(const String& code) {
  if (code == "ok_emotion") return "Emotion updated.";
  if (code == "ok_speak") return "Speech updated.";
  if (code == "ok_note") return "Note added.";
  if (code == "ok_reminder") return "Reminder added.";
  if (code == "ok_clear") return "Cleared notes and reminders.";
  if (code == "ok_mode") return "Display mode updated.";
  if (code == "ok_info") return "Info settings updated.";
  if (code == "err_emotion") return "Invalid emotion.";
  if (code == "err_speak") return "Speech text missing.";
  if (code == "err_note") return "Note text missing.";
  if (code == "err_reminder") return "Reminder needs minutes > 0 and message.";
  if (code == "err_reminders_full") return "Reminder storage full.";
  if (code == "err_mode") return "Invalid mode. Use face or info.";
  if (code == "err_info") return "Latitude/longitude required and must be valid.";
  return "";
}

void sendUiRedirect(const char* code) {
  String location = "/";
  if (code != nullptr && code[0] != '\0') {
    location += "?msg=";
    location += code;
  }
  server.sendHeader("Location", location, true);
  server.send(303, "text/plain", "See Other");
}

void handleRoot() {
  String msg = statusMessageFromCode(server.arg("msg"));
  String html;
  html.reserve(8500);

  html += "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Companion 313</title>";
  html += "<style>";
  html += "body{font-family:Trebuchet MS,Segoe UI,sans-serif;background:#0c1424;color:#e9efff;margin:0;padding:16px}";
  html += ".wrap{max-width:960px;margin:0 auto}.card{border:1px solid #2e466a;background:#12203a;border-radius:10px;padding:12px;margin-bottom:10px}";
  html += ".row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}.grid{display:grid;gap:10px;grid-template-columns:repeat(auto-fit,minmax(260px,1fr))}";
  html += ".topgrid{display:grid;gap:10px;grid-template-columns:1fr 1fr 1fr;margin-bottom:10px}.stack{display:grid;gap:10px}";
  html += "input,select,button{background:#0f1a2f;color:#e9efff;border:1px solid #3b5d90;border-radius:8px;padding:8px}";
  html += "input,select{flex:1;min-width:110px}button{cursor:pointer}ul{margin:6px 0 0 18px}.muted{color:#9fb3d8}";
  html += ".msg{padding:8px;border-radius:8px;background:#173158;border:1px solid #3b5d90;margin:8px 0}";
  html += "code{display:block;white-space:pre-wrap;word-break:break-word;background:#0b1528;padding:6px;border-radius:6px}";
  html += "@media (max-width:800px){.topgrid{grid-template-columns:1fr}}";
  html += "a{color:#80d5ff}";
  html += "</style></head><body><div class='wrap'>";
  html += "<h1>Companion 313 Control Panel</h1>";

  if (msg.length() > 0) {
    html += "<div class='msg'>";
    html += msg;
    html += "</div>";
  }

  html += "<div class='topgrid'>";
  html += "<div class='card'><h2>Status</h2>";
  html += "<p><b>IP:</b> ";
  html += currentIpAddress();
  html += "<br><b>Speech:</b> ";
  html += htmlEscape(speechText);
  html += "<br><b>Notes:</b> ";
  html += String(notesCount);
  html += "<br><b>Info Temp:</b> ";
  html += htmlEscape(infoTemperature);
  html += "<br><b>Info Temp Unit:</b> ";
  html += infoTempUnitLabel();
  html += "<br><b>Info Lat/Lon:</b> ";
  html += String(infoLatitude, 4);
  html += ", ";
  html += String(infoLongitude, 4);
  html += "</p><p><a href='/'>Refresh status</a> | <a href='/status'>Raw /status JSON</a></p></div>";

  html += "<div class='stack'>";
  html += "<div class='card'><h2>Info Settings</h2><form method='post' action='/ui/info'><div class='row'>";
  html += "<input name='latitude' value='";
  html += String(infoLatitude, 6);
  html += "' placeholder='Latitude (e.g. 47.6062)'>";
  html += "</div><div class='row'>";
  html += "<input name='longitude' value='";
  html += String(infoLongitude, 6);
  html += "' placeholder='Longitude (e.g. -122.3321)'>";
  html += "</div><div class='row'><select name='temperature_unit'>";
  html += tempUnitOptionsHtml();
  html += "</select><button type='submit'>Save Coords</button></div></form></div>";

  html += "<div class='card'><h2>Speak</h2><form method='post' action='/ui/speak'><div class='row'>";
  html += "<input name='text' maxlength='40' placeholder='Text for display'>";
  html += "<button type='submit'>Send Speech</button></div></form></div>";
  html += "</div>";

  html += "<div class='stack'>";
  html += "<div class='card'><h2>Display Mode</h2><form method='post' action='/ui/mode'><div class='row'><select name='mode'>";
  html += modeOptionsHtml(currentDisplayMode);
  html += "</select><button type='submit'>Set Mode</button></div></form></div>";

  html += "<div class='card'><h2>Emotion</h2><form method='post' action='/ui/emotion'><div class='row'><select name='emotion'>";
  html += emotionOptionsHtml(currentEmotion);
  html += "</select><button type='submit'>Set Emotion</button></div></form></div>";
  html += "</div>";
  html += "</div>";

  html += "<div class='grid'>";

  html += "<div class='card'><h2>Add Note</h2><form method='post' action='/ui/notes'><div class='row'>";
  html += "<input name='note' placeholder='New note'>";
  html += "<button type='submit'>Add Note</button></div></form></div>";

  html += "<div class='card'><h2>Add Reminder</h2><form method='post' action='/ui/reminders'><div class='row'>";
  html += "<input name='minutes' type='number' min='1' value='10' style='max-width:90px'>";
  html += "<input name='message' placeholder='Reminder message'>";
  html += "<button type='submit'>Add Reminder</button></div></form></div>";

  html += "<div class='card'><h2>Maintenance</h2><form method='post' action='/ui/clear'>";
  html += "<button type='submit'>Clear Notes + Reminders</button></form></div>";

  html += "</div>";

  html += "<div class='card'><h2>Notes</h2><ul>";
  if (notesCount == 0) {
    html += "<li class='muted'>No notes</li>";
  } else {
    for (size_t i = 0; i < notesCount; ++i) {
      html += "<li>";
      html += htmlEscape(notes[i]);
      html += "</li>";
    }
  }
  html += "</ul></div>";

  html += "<div class='card'><h2>Reminders</h2><ul>";
  uint32_t now = millis();
  bool foundReminder = false;
  for (size_t i = 0; i < kMaxReminders; ++i) {
    if (!reminders[i].active) continue;
    foundReminder = true;
    uint32_t remaining = reminders[i].dueMs > now ? (reminders[i].dueMs - now) : 0;
    html += "<li>";
    html += htmlEscape(reminders[i].message);
    html += " (";
    html += String(remaining / 1000);
    html += "s remaining)</li>";
  }
  if (!foundReminder) {
    html += "<li class='muted'>No active reminders</li>";
  }
  html += "</ul></div>";

  html += "<div class='card'><h2>Weather Debug</h2>";
  html += "<p><b>Current Temperature:</b> ";
  html += htmlEscape(infoTemperature);
  html += "<br><b>Weather API Code:</b> ";
  html += String(debugLastWeatherCode);
  html += "<br><b>Weather Code:</b> ";
  html += String(infoWeatherCode);
  html += "</p>";
  html += "<p><b>Weather API Payload:</b><br><code>";
  html += htmlEscape(debugLastWeatherPayload);
  html += "</code></p></div>";

  html += "<div class='card'><h2>API Endpoints</h2>";
  html += "<p class='muted'>GET /status, POST /emotion, POST /speak, GET/POST /notes, POST /reminders, POST /clear, POST /ui/mode, POST /ui/info</p>";
  html += "</div>";

  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleUiEmotion() {
  if (!server.hasArg("emotion")) {
    sendUiRedirect("err_emotion");
    return;
  }
  Emotion parsed = Emotion::Neutral;
  if (!tryParseEmotion(server.arg("emotion"), parsed)) {
    sendUiRedirect("err_emotion");
    return;
  }
  setEmotion(parsed);
  drawFace();
  sendUiRedirect("ok_emotion");
}

void handleUiMode() {
  if (!server.hasArg("mode")) {
    sendUiRedirect("err_mode");
    return;
  }
  DisplayMode parsed = DisplayMode::Face;
  if (!tryParseDisplayMode(server.arg("mode"), parsed)) {
    sendUiRedirect("err_mode");
    return;
  }
  currentDisplayMode = parsed;
  sendUiRedirect("ok_mode");
}

void handleUiInfoSettings() {
  if (!server.hasArg("latitude") || !server.hasArg("longitude")) {
    sendUiRedirect("err_info");
    return;
  }
  String latStr = server.arg("latitude");
  String lonStr = server.arg("longitude");
  latStr.trim();
  lonStr.trim();
  if (latStr.length() == 0 || lonStr.length() == 0) {
    sendUiRedirect("err_info");
    return;
  }

  double lat = 0.0;
  double lon = 0.0;
  if (!tryParseDoubleStrict(latStr, lat) || !tryParseDoubleStrict(lonStr, lon)) {
    sendUiRedirect("err_info");
    return;
  }
  if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
    sendUiRedirect("err_info");
    return;
  }

  bool useFahrenheit = infoUseFahrenheit;
  if (server.hasArg("temperature_unit")) {
    String unit = server.arg("temperature_unit");
    unit.trim();
    unit.toLowerCase();
    if (unit == "f") {
      useFahrenheit = true;
    } else if (unit == "c") {
      useFahrenheit = false;
    } else {
      sendUiRedirect("err_info");
      return;
    }
  }

  infoLatitude = lat;
  infoLongitude = lon;
  infoUseFahrenheit = useFahrenheit;
  infoHasCoordinates = true;
  infoTempValid = false;
  lastInfoTempFetchMs = 0;
  serviceInfoData();

  sendUiRedirect("ok_info");
}

void handleUiSpeak() {
  if (!server.hasArg("text")) {
    sendUiRedirect("err_speak");
    return;
  }
  speechText = server.arg("text");
  if (speechText.length() == 0) {
    sendUiRedirect("err_speak");
    return;
  }
  if (speechText.length() > 40) {
    speechText = speechText.substring(0, 40);
  }
  sendUiRedirect("ok_speak");
}

void handleUiNotesAdd() {
  if (!server.hasArg("note")) {
    sendUiRedirect("err_note");
    return;
  }
  String note = server.arg("note");
  if (note.length() == 0) {
    sendUiRedirect("err_note");
    return;
  }
  if (notesCount >= kMaxNotes) {
    for (size_t i = 1; i < kMaxNotes; ++i) {
      notes[i - 1] = notes[i];
    }
    notesCount = kMaxNotes - 1;
  }
  notes[notesCount++] = note;
  sendUiRedirect("ok_note");
}

void handleUiRemindersAdd() {
  if (!server.hasArg("minutes") || !server.hasArg("message")) {
    sendUiRedirect("err_reminder");
    return;
  }
  int minutes = server.arg("minutes").toInt();
  String message = server.arg("message");
  if (minutes <= 0 || message.length() == 0) {
    sendUiRedirect("err_reminder");
    return;
  }
  size_t slot = kMaxReminders;
  for (size_t i = 0; i < kMaxReminders; ++i) {
    if (!reminders[i].active) {
      slot = i;
      break;
    }
  }
  if (slot == kMaxReminders) {
    sendUiRedirect("err_reminders_full");
    return;
  }
  reminders[slot].active = true;
  reminders[slot].dueMs = millis() + static_cast<uint32_t>(minutes) * 60000UL;
  reminders[slot].message = message;
  sendUiRedirect("ok_reminder");
}

void handleUiClear() {
  notesCount = 0;
  speechText = "Cleared";
  for (size_t i = 0; i < kMaxReminders; ++i) {
    reminders[i].active = false;
  }
  sendUiRedirect("ok_clear");
}

void setupServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/emotion", HTTP_POST, handleEmotion);
  server.on("/speak", HTTP_POST, handleSpeak);
  server.on("/notes", HTTP_GET, handleNotesList);
  server.on("/notes", HTTP_POST, handleNotesAdd);
  server.on("/reminders", HTTP_POST, handleRemindersAdd);
  server.on("/clear", HTTP_POST, handleClear);
  server.on("/ui/mode", HTTP_POST, handleUiMode);
  server.on("/ui/info", HTTP_POST, handleUiInfoSettings);
  server.on("/ui/emotion", HTTP_POST, handleUiEmotion);
  server.on("/ui/speak", HTTP_POST, handleUiSpeak);
  server.on("/ui/notes", HTTP_POST, handleUiNotesAdd);
  server.on("/ui/reminders", HTTP_POST, handleUiRemindersAdd);
  server.on("/ui/clear", HTTP_POST, handleUiClear);
  server.begin();

  Serial.println("HTTP API started on port 80");
}

void drawEyes(int y, int h, int curve, bool closed) {
  const int leftX = 30;
  const int rightX = 78;
  const int eyeW = 20;

  if (closed) {
    display.drawHLine(leftX, y + h / 2, eyeW);
    display.drawHLine(rightX, y + h / 2, eyeW);
    return;
  }

  display.drawRBox(leftX, y, eyeW, h, curve);
  display.drawRBox(rightX, y, eyeW, h, curve);
}

void drawPupils(int y, int h, int offsetX) {
  if (h < 8) return;
  const int leftCenterX = 40 + offsetX;
  const int rightCenterX = 88 + offsetX;
  const int centerY = y + h / 2;
  display.drawDisc(leftCenterX, centerY, 2);
  display.drawDisc(rightCenterX, centerY, 2);
  // Tiny glint makes eyes look less flat.
  display.drawPixel(leftCenterX - 1, centerY - 1);
  display.drawPixel(rightCenterX - 1, centerY - 1);
}

void drawBrows(int leftX1, int leftY1, int leftX2, int leftY2, int rightX1, int rightY1, int rightX2,
               int rightY2) {
  display.drawLine(leftX1, leftY1, leftX2, leftY2);
  display.drawLine(rightX1, rightY1, rightX2, rightY2);
}

void drawMouthFlat(int y, int w) {
  int x = (128 - w) / 2;
  display.drawHLine(x, y, w);
}

void drawMouthSmile(int y, int w) {
  int x = (128 - w) / 2;
  display.drawLine(x, y, x + w / 2, y + 3);
  display.drawLine(x + w / 2, y + 3, x + w, y);
  display.drawPixel(x + 1, y + 1);
  display.drawPixel(x + w - 1, y + 1);
}

void drawMouthFrown(int y, int w) {
  int x = (128 - w) / 2;
  display.drawLine(x, y + 3, x + w / 2, y);
  display.drawLine(x + w / 2, y, x + w, y + 3);
  display.drawPixel(x + 1, y + 2);
  display.drawPixel(x + w - 1, y + 2);
}

void drawMouthOpen(int cx, int cy, int r) {
  display.drawCircle(cx, cy, r);
  if (r >= 5) {
    display.drawCircle(cx, cy, r - 1);
  }
}

void drawCheeks() {
  display.drawDisc(22, 42, 1);
  display.drawDisc(26, 44, 1);
  display.drawDisc(106, 42, 1);
  display.drawDisc(102, 44, 1);
}

void drawThoughtBubble(int wobble) {
  int baseY = 49 - wobble;
  display.drawDisc(54, baseY - 5, 1);
  display.drawDisc(61, baseY - 3, 2);
  display.drawDisc(71, baseY, 3);
  display.drawCircle(82, baseY + 2, 5);
  display.drawCircle(89, baseY + 1, 4);
  display.drawCircle(94, baseY + 3, 3);
}

void drawSleepZ(int phase) {
  int y = 10 + phase;
  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(95, y, "Z");
  display.drawStr(104, y + 4, "z");
  display.drawStr(111, y + 8, "z");
}

void drawHeart(int cx, int cy, int r) {
  display.drawDisc(cx - r / 2, cy - r / 2, r / 2 + 1);
  display.drawDisc(cx + r / 2, cy - r / 2, r / 2 + 1);
  display.drawTriangle(cx - r - 1, cy - r / 3, cx + r + 1, cy - r / 3, cx, cy + r + 1);
}

void drawFace() {
  display.clearBuffer();

  bool closed = blinkClosed || currentEmotion == Emotion::Sleepy;
  uint32_t now = millis();
  int glance = static_cast<int>((now / 400UL) % 3UL) - 1;  // -1, 0, 1 subtle scanning look
  int pulse2 = static_cast<int>((now / 220UL) % 2UL);      // 0/1
  int pulse3 = static_cast<int>((now / 260UL) % 3UL) - 1;  // -1/0/1
  int bob = static_cast<int>((now / 300UL) % 4UL);         // 0..3
  int bobY = (bob < 2) ? bob : (3 - bob);                  // 0,1,1,0

  switch (currentEmotion) {
    case Emotion::Happy:
      drawEyes(15 + bobY, 15, 5, closed);
      if (!closed) {
        drawPupils(15 + bobY, 15, glance);
      }
      drawBrows(30, 12 + bobY, 48, 10 + bobY, 78, 10 + bobY, 96, 12 + bobY);
      drawCheeks();
      drawMouthSmile(41 + pulse2, 26);
      break;
    case Emotion::Sad:
      drawEyes(18 + pulse2, 10, 4, closed);
      if (!closed) {
        drawPupils(18 + pulse2, 10, 0);
      }
      drawBrows(28, 11 + pulse2, 48, 16 + pulse2, 80, 16 + pulse2, 100, 11 + pulse2);
      drawMouthFrown(43 + pulse2, 24);
      display.drawPixel(25, 36 + (pulse2 * 2));
      display.drawPixel(103, 36 + ((1 - pulse2) * 2));
      break;
    case Emotion::Sleepy:
      drawEyes(24 + pulse2, 4, 2, true);
      display.drawLine(30, 20 + pulse2, 50, 20 + pulse2);
      display.drawLine(78, 20 + pulse2, 98, 20 + pulse2);
      drawMouthFlat(46 + pulse2, 14);
      display.drawPixel(63 + pulse2, 50);
      drawSleepZ(pulse2);
      break;
    case Emotion::Angry:
      drawEyes(18, 12 + pulse2, 2, closed);
      if (!closed) {
        drawPupils(18, 12 + pulse2, pulse3);
      }
      drawBrows(25, 14 - pulse2, 49, 9 - pulse2, 103, 14 - pulse2, 79, 9 - pulse2);
      drawMouthFlat(44, 22 + pulse2);
      display.drawLine(52, 47 + pulse2, 76, 47 + pulse2);
      display.drawLine(52, 48 + pulse2, 76, 48 + pulse2);
      break;
    case Emotion::Surprised:
      drawEyes(14, 18 + pulse2, 9, closed);
      if (!closed) {
        drawPupils(14, 18 + pulse2, 0);
      }
      drawBrows(30, 10 - pulse2, 48, 9 - pulse2, 78, 9 - pulse2, 96, 10 - pulse2);
      drawMouthOpen(64, 45, 5 + pulse2);
      break;
    case Emotion::Thinking:
      drawEyes(17, 11, 4, closed);
      if (!closed) {
        drawPupils(17, 11, -1 + pulse2);
      }
      drawBrows(29, 12, 47, 11 + pulse2, 78, 12, 97, 14 + pulse2);
      drawMouthFlat(44, 14);
      drawThoughtBubble(pulse2);
      break;
    case Emotion::Love: {
      int heartPulse = 5 + pulse2;
      if (closed) {
        drawEyes(18, 10, 3, true);
      } else {
        drawHeart(40, 24 + bobY, heartPulse);
        drawHeart(88, 24 + bobY, heartPulse);
      }
      drawBrows(30, 12 + bobY, 48, 11 + bobY, 78, 11 + bobY, 96, 12 + bobY);
      drawCheeks();
      drawMouthSmile(41 + pulse2, 28);
      break;
    }
    case Emotion::Neutral:
    default:
      drawEyes(17 + (bobY ? 1 : 0), 12, 5, closed);
      if (!closed) {
        drawPupils(17 + (bobY ? 1 : 0), 12, pulse3);
      }
      drawBrows(30, 12, 48, 12 + (bobY ? 1 : 0), 78, 12, 96, 12 + (bobY ? 1 : 0));
      drawMouthFlat(44 + (bobY ? 1 : 0), 18);
      break;
  }

  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(2, 61, speechText.c_str());

  display.sendBuffer();
}

void drawInfo() {
  display.clearBuffer();

  String tempStr = infoTemperature;

  display.setFont(u8g2_font_fub17_tf);
  int tempW = display.getStrWidth(tempStr.c_str());
  const int iconW = 16;
  const int gap = 4;
  int totalW = tempW + gap + iconW;
  int tempX = (128 - totalW) / 2;
  if (tempX < 0) tempX = 0;
  display.drawStr(tempX, 42, tempStr.c_str());
  drawWeatherIcon(infoWeatherCode, tempX + tempW + gap, 26);

  display.sendBuffer();
}

void serviceBlink() {
  uint32_t now = millis();

  if (!blinkClosed && now >= nextBlinkMs) {
    blinkClosed = true;
    blinkUntilMs = now + 120;
  }

  if (blinkClosed && now >= blinkUntilMs) {
    blinkClosed = false;
    scheduleBlink(now);
  }
}

void serviceReminders() {
  uint32_t now = millis();
  for (size_t i = 0; i < kMaxReminders; ++i) {
    if (!reminders[i].active) continue;

    if (now >= reminders[i].dueMs) {
      reminders[i].active = false;
      setEmotion(Emotion::Surprised);
      speechText = reminders[i].message;
      if (speechText.length() > 40) {
        speechText = speechText.substring(0, 40);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  randomSeed(esp_random());

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  display.setI2CAddress(static_cast<uint8_t>(OLED_I2C_ADDRESS << 1));
  display.begin();
  display.clearBuffer();

#if EMOTION_BUTTON_PIN >= 0
  pinMode(EMOTION_BUTTON_PIN, INPUT_PULLUP);
#endif

  for (size_t i = 0; i < kMaxReminders; ++i) {
    reminders[i].active = false;
  }

  connectWiFi();
  speechText = currentIpAddress();
  serviceInfoData();
  setupServer();
  scheduleBlink(millis());
  if (currentDisplayMode == DisplayMode::Info) {
    drawInfo();
  } else {
    drawFace();
  }
}

void loop() {
  server.handleClient();
  serviceBlink();
  serviceReminders();
  serviceInfoData();

#if EMOTION_BUTTON_PIN >= 0
  static bool prevPressed = false;
  bool pressed = digitalRead(EMOTION_BUTTON_PIN) == LOW;
  if (pressed && !prevPressed) {
    int next = (static_cast<int>(currentEmotion) + 1) % 8;
    setEmotion(static_cast<Emotion>(next));
  }
  prevPressed = pressed;
#endif

  uint32_t now = millis();
  if (now >= nextFaceRefreshMs) {
    uint32_t refreshMs =
        currentDisplayMode == DisplayMode::Info ? kInfoDisplayRefreshMs : kFaceRefreshMs;
    nextFaceRefreshMs = now + refreshMs;
    if (currentDisplayMode == DisplayMode::Info) {
      drawInfo();
    } else {
      drawFace();
    }
  }
}

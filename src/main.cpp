#include <Arduino.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#include "config.h"

enum class Emotion {
  Neutral,
  Happy,
  Sad,
  Sleepy,
  Angry,
  Surprised,
  Thinking
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
String speechText = "Hello";
String notes[kMaxNotes];
size_t notesCount = 0;
Reminder reminders[kMaxReminders];

uint32_t nextBlinkMs = 0;
uint32_t blinkUntilMs = 0;
uint32_t nextFaceRefreshMs = 0;
bool blinkClosed = false;

void drawFace();

String currentIpAddress() {
  return WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
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
  WiFi.softAP("Companion-309", "companion309");
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
  doc["speech"] = speechText;
  doc["ip"] = currentIpAddress();

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
    error["allowed"] = "neutral,happy,sad,sleepy,angry,surprised,thinking";
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
      {"thinking", Emotion::Thinking},
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

String statusMessageFromCode(const String& code) {
  if (code == "ok_emotion") return "Emotion updated.";
  if (code == "ok_speak") return "Speech updated.";
  if (code == "ok_note") return "Note added.";
  if (code == "ok_reminder") return "Reminder added.";
  if (code == "ok_clear") return "Cleared notes and reminders.";
  if (code == "err_emotion") return "Invalid emotion.";
  if (code == "err_speak") return "Speech text missing.";
  if (code == "err_note") return "Note text missing.";
  if (code == "err_reminder") return "Reminder needs minutes > 0 and message.";
  if (code == "err_reminders_full") return "Reminder storage full.";
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
  html.reserve(7000);

  html += "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Companion 309</title>";
  html += "<style>";
  html += "body{font-family:Trebuchet MS,Segoe UI,sans-serif;background:#0c1424;color:#e9efff;margin:0;padding:16px}";
  html += ".wrap{max-width:960px;margin:0 auto}.card{border:1px solid #2e466a;background:#12203a;border-radius:10px;padding:12px;margin-bottom:10px}";
  html += ".row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}.grid{display:grid;gap:10px;grid-template-columns:repeat(auto-fit,minmax(260px,1fr))}";
  html += "input,select,button{background:#0f1a2f;color:#e9efff;border:1px solid #3b5d90;border-radius:8px;padding:8px}";
  html += "input,select{flex:1;min-width:110px}button{cursor:pointer}ul{margin:6px 0 0 18px}.muted{color:#9fb3d8}";
  html += ".msg{padding:8px;border-radius:8px;background:#173158;border:1px solid #3b5d90;margin:8px 0}";
  html += "a{color:#80d5ff}";
  html += "</style></head><body><div class='wrap'>";
  html += "<h1>Companion 309 Control Panel</h1>";
  html += "<p class='muted'>Server-rendered controls. No browser JavaScript required.</p>";

  if (msg.length() > 0) {
    html += "<div class='msg'>";
    html += msg;
    html += "</div>";
  }

  html += "<div class='card'><h2>Status</h2>";
  html += "<p><b>IP:</b> ";
  html += currentIpAddress();
  html += "<br><b>Emotion:</b> ";
  html += emotionToString(currentEmotion);
  html += "<br><b>Speech:</b> ";
  html += speechText;
  html += "<br><b>Notes:</b> ";
  html += String(notesCount);
  html += "</p><p><a href='/'>Refresh status</a> | <a href='/status'>Raw /status JSON</a></p></div>";

  html += "<div class='grid'>";

  html += "<div class='card'><h2>Emotion</h2><form method='post' action='/ui/emotion'><div class='row'><select name='emotion'>";
  html += emotionOptionsHtml(currentEmotion);
  html += "</select><button type='submit'>Set Emotion</button></div></form></div>";

  html += "<div class='card'><h2>Speak</h2><form method='post' action='/ui/speak'><div class='row'>";
  html += "<input name='text' maxlength='40' placeholder='Text for display'>";
  html += "<button type='submit'>Send Speech</button></div></form></div>";

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
      html += notes[i];
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
    html += reminders[i].message;
    html += " (";
    html += String(remaining / 1000);
    html += "s remaining)</li>";
  }
  if (!foundReminder) {
    html += "<li class='muted'>No active reminders</li>";
  }
  html += "</ul></div>";

  html += "<div class='card'><h2>API Endpoints</h2>";
  html += "<p class='muted'>GET /status, POST /emotion, POST /speak, GET/POST /notes, POST /reminders, POST /clear</p>";
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

void drawMouthLine(int y, int w, bool smile) {
  int x = (128 - w) / 2;
  display.drawHLine(x, y, w);

  if (smile) {
    display.drawPixel(x, y - 1);
    display.drawPixel(x + w - 1, y - 1);
  } else {
    display.drawPixel(x, y + 1);
    display.drawPixel(x + w - 1, y + 1);
  }
}

void drawFace() {
  display.clearBuffer();

  uint32_t now = millis();
  bool closed = blinkClosed || currentEmotion == Emotion::Sleepy;

  switch (currentEmotion) {
    case Emotion::Happy:
      drawEyes(16, 14, 5, closed);
      drawMouthLine(43, 26, true);
      break;
    case Emotion::Sad:
      drawEyes(18, 10, 4, closed);
      drawMouthLine(44, 24, false);
      break;
    case Emotion::Sleepy:
      drawEyes(23, 4, 2, true);
      drawMouthLine(45, 16, false);
      break;
    case Emotion::Angry:
      drawEyes(18, 12, 2, closed);
      display.drawLine(26, 15, 48, 11);
      display.drawLine(102, 15, 80, 11);
      drawMouthLine(43, 20, false);
      break;
    case Emotion::Surprised:
      drawEyes(14, 18, 9, closed);
      display.drawCircle(64, 45, 6);
      break;
    case Emotion::Thinking:
      drawEyes(17, 11, 4, closed);
      display.drawDisc(56, 43, 2);
      display.drawDisc(63, 45, 3);
      display.drawDisc(72, 48, 4);
      break;
    case Emotion::Neutral:
    default:
      drawEyes(17, 12, 5, closed);
      drawMouthLine(43, 18, false);
      break;
  }

  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(2, 61, speechText.c_str());

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
  setupServer();
  scheduleBlink(millis());
  drawFace();
}

void loop() {
  server.handleClient();
  serviceBlink();
  serviceReminders();

#if EMOTION_BUTTON_PIN >= 0
  static bool prevPressed = false;
  bool pressed = digitalRead(EMOTION_BUTTON_PIN) == LOW;
  if (pressed && !prevPressed) {
    int next = (static_cast<int>(currentEmotion) + 1) % 7;
    setEmotion(static_cast<Emotion>(next));
  }
  prevPressed = pressed;
#endif

  uint32_t now = millis();
  if (now >= nextFaceRefreshMs) {
    nextFaceRefreshMs = now + 33;
    drawFace();
  }
}

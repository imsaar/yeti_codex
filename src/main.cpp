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
  JsonDocument doc;
  if (!parseJsonBody(doc) || !doc["text"].is<const char*>()) {
    JsonDocument error;
    error["error"] = "Expected JSON: {\"text\":\"hello\"}";
    sendJson(400, error);
    return;
  }

  speechText = doc["text"].as<String>();
  if (speechText.length() > 40) {
    speechText = speechText.substring(0, 40);
  }

  JsonDocument result;
  result["ok"] = true;
  sendJson(200, result);
}

void handleNotesAdd() {
  JsonDocument doc;
  if (!parseJsonBody(doc) || !doc["note"].is<const char*>()) {
    JsonDocument error;
    error["error"] = "Expected JSON: {\"note\":\"buy milk\"}";
    sendJson(400, error);
    return;
  }

  if (notesCount >= kMaxNotes) {
    for (size_t i = 1; i < kMaxNotes; ++i) {
      notes[i - 1] = notes[i];
    }
    notesCount = kMaxNotes - 1;
  }

  notes[notesCount++] = doc["note"].as<String>();

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
  JsonDocument doc;
  if (!parseJsonBody(doc) || !doc["minutes"].is<int>() || !doc["message"].is<const char*>()) {
    JsonDocument error;
    error["error"] = "Expected JSON: {\"minutes\":10,\"message\":\"stand up\"}";
    sendJson(400, error);
    return;
  }

  int minutes = doc["minutes"].as<int>();
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
  reminders[slot].message = doc["message"].as<String>();

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

void handleRoot() {
  server.send(200, "text/plain",
              "Companion 309 API\n"
              "GET /status\n"
              "POST /emotion {emotion}\n"
              "POST /speak {text}\n"
              "GET /notes\n"
              "POST /notes {note}\n"
              "POST /reminders {minutes,message}\n"
              "POST /clear\n");
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

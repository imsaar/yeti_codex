#include <Arduino.h>
#include <WiFi.h>
#include <stdlib.h>

#include "config.h"
#include "types.h"
#include "utils.h"
#include "display.h"
#include "time_sync.h"
#include "weather.h"
#include "web_server.h"

Emotion currentEmotion = Emotion::Neutral;
DisplayMode currentDisplayMode = DisplayMode::Face;
String speechText = "Hello";
String notes[kMaxNotes];
size_t notesCount = 0;
Reminder reminders[kMaxReminders];

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
  if (WiFi.status() == WL_CONNECTED) {
    initNtp();
  }
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

#include "web_server.h"
#include "display.h"
#include "time_sync.h"
#include "weather.h"
#include "utils.h"
#include <WiFi.h>
#include <time.h>

// State owned by main.cpp
extern Emotion currentEmotion;
extern DisplayMode currentDisplayMode;
extern String speechText;
extern String notes[kMaxNotes];
extern size_t notesCount;
extern Reminder reminders[kMaxReminders];

// Functions defined in main.cpp
void setEmotion(Emotion emotion);
String emotionToString(Emotion emotion);
bool tryParseEmotion(const String& name, Emotion& outEmotion);

WebServer server(80);

// ---------------------------------------------------------------------------
// Helpers used only by web handlers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// HTTP handlers
// ---------------------------------------------------------------------------

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
  doc["info_local_time"] = getLocalTimeString();
  doc["info_timezone_abbr"] = infoTimezoneAbbr;
  doc["info_utc_offset_seconds"] = infoUtcOffsetSeconds;
  doc["debug_time_utc_epoch"] = (long)time(NULL);
  doc["ntp_synced"] = ntpSynced;
  doc["sntp_callback_fired"] = sntpCallbackFired;
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
  html += "<br><b>Local Time:</b> ";
  html += htmlEscape(getLocalTimeString());
  if (infoTimezoneAbbr.length() > 0) {
    html += " (";
    html += htmlEscape(infoTimezoneAbbr);
    html += ")";
  }
  if (!sntpCallbackFired) {
    html += " <span class='muted'>[NTP not synced]</span>";
  }
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

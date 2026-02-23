#include "weather.h"
#include "utils.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>

String infoTemperature = "Loading...";
int infoWeatherCode = -1;
bool infoTempValid = false;
bool infoUseFahrenheit = true;
double infoLatitude = 47.6062;
double infoLongitude = -122.3321;
bool infoHasCoordinates = true;
uint32_t lastInfoTempFetchMs = 0;
long infoUtcOffsetSeconds = 0;
String infoTimezoneAbbr = "";
bool infoTimeValid = false;

int debugLastWeatherCode = -1;
String debugLastWeatherPayload = "";

String infoTempUnitLabel() {
  return infoUseFahrenheit ? "F" : "C";
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
               String(infoUseFahrenheit ? "fahrenheit" : "celsius") +
               "&timezone=auto";
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

  if (!doc["utc_offset_seconds"].isNull()) {
    long offset = doc["utc_offset_seconds"].as<long>();
    if (offset >= -50400L && offset <= 50400L) {  // valid UTC-14 to UTC+14
      infoUtcOffsetSeconds = offset;
      infoTimeValid = true;
      Serial.printf("UTC offset set: %ld s\n", offset);
    }
  }
  if (!doc["timezone_abbreviation"].isNull()) {
    infoTimezoneAbbr = doc["timezone_abbreviation"].as<String>();
  }

  infoTemperature = String(tempValue, 1) + String(" ") + unit;
  infoTempValid = true;
  return true;
}

void serviceInfoData() {
  uint32_t now = millis();

  uint32_t tempInterval = infoTempValid ? kInfoTempRefreshMs : kInfoRetryMs;
  if (lastInfoTempFetchMs == 0 || (now - lastInfoTempFetchMs >= tempInterval)) {
    lastInfoTempFetchMs = now;
    fetchInfoTemperature();
  }
}

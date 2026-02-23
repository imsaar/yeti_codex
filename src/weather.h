#pragma once
#include <Arduino.h>

extern String infoTemperature;
extern int infoWeatherCode;
extern bool infoTempValid;
extern bool infoUseFahrenheit;
extern double infoLatitude;
extern double infoLongitude;
extern bool infoHasCoordinates;
extern uint32_t lastInfoTempFetchMs;
extern long infoUtcOffsetSeconds;
extern String infoTimezoneAbbr;
extern bool infoTimeValid;
extern int debugLastWeatherCode;
extern String debugLastWeatherPayload;

static constexpr uint32_t kInfoTempRefreshMs = 10UL * 60UL * 1000UL;
static constexpr uint32_t kInfoRetryMs = 20UL * 1000UL;

String infoTempUnitLabel();
bool fetchInfoTemperature();
void serviceInfoData();

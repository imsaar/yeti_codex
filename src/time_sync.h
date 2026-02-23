#pragma once
#include <Arduino.h>
#include "esp_sntp.h"

extern volatile bool sntpCallbackFired;
extern volatile uint32_t sntpEpochAtSync;
extern volatile uint32_t sntpMillisAtSync;
extern bool ntpSynced;

void onSntpSync(struct timeval* tv);
void initNtp();
String getLocalTimeString();

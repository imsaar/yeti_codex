#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "types.h"

extern WebServer server;

String currentIpAddress();
void setupServer();

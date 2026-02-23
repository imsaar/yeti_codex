#pragma once
#include <Arduino.h>

String urlEncode(const String& input);
String htmlEscape(const String& input);
String truncateForDebug(const String& input, size_t maxLen = 220);

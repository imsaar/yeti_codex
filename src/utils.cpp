#include "utils.h"

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

String truncateForDebug(const String& input, size_t maxLen) {
  if (input.length() <= maxLen) return input;
  return input.substring(0, maxLen) + "...";
}

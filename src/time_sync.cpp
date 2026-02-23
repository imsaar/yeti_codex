#include "time_sync.h"
#include "weather.h"  // for infoTimeValid, infoUtcOffsetSeconds

volatile bool sntpCallbackFired = false;
volatile uint32_t sntpEpochAtSync = 0;   // UTC epoch at last sync (uint32_t = atomic read on ESP32, valid until 2106)
volatile uint32_t sntpMillisAtSync = 0;  // millis() captured at same instant
bool ntpSynced = false;

void onSntpSync(struct timeval* tv) {
  sntpEpochAtSync  = (uint32_t)tv->tv_sec;  // atomic 32-bit write, no race condition
  sntpMillisAtSync = (uint32_t)millis();
  sntpCallbackFired = true;
  ntpSynced = true;
  Serial.printf("SNTP sync: epoch %lu\n", (unsigned long)tv->tv_sec);
}

void initNtp() {
  esp_sntp_set_time_sync_notification_cb(onSntpSync);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  uint32_t started = millis();
  while (!sntpCallbackFired && millis() - started < 10000) {
    delay(200);
  }
  ntpSynced = sntpCallbackFired;
  Serial.println(ntpSynced ? "NTP synced." : "NTP sync timed out.");
}

String getLocalTimeString() {
  if (!infoTimeValid || !sntpCallbackFired) return "--:--";
  // Use the epoch captured atomically in the SNTP callback, advanced by elapsed millis.
  // This avoids time(NULL) which can oscillate while the SNTP task makes step corrections.
  // uint32_t reads/writes are atomic on the 32-bit ESP32 CPU, so no race condition.
  uint32_t utcNow = sntpEpochAtSync + (millis() - sntpMillisAtSync) / 1000UL;
  if (utcNow < 1000000000UL) return "--:--";  // sanity check: before year 2001
  long secsInDay = (long)(utcNow % 86400UL) + infoUtcOffsetSeconds;
  secsInDay = ((secsInDay % 86400L) + 86400L) % 86400L;  // normalize to [0, 86400)
  int h = (int)(secsInDay / 3600L);
  int m = (int)((secsInDay % 3600L) / 60L);
  bool pm = h >= 12;
  if (h == 0) h = 12;
  else if (h > 12) h -= 12;
  char buf[10];
  snprintf(buf, sizeof(buf), "%d:%02d%s", h, m, pm ? "P" : "A");
  return String(buf);
}

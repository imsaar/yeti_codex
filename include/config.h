#pragma once

// Load Wi-Fi credentials from local, gitignored secrets file.
// Create include/wifi_secrets.h from include/wifi_secrets.example.h.
#include "wifi_secrets.h"

#ifndef WIFI_SSID
#error "WIFI_SSID is not defined. Create include/wifi_secrets.h from include/wifi_secrets.example.h"
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD is not defined. Create include/wifi_secrets.h from include/wifi_secrets.example.h"
#endif

// 0.96 inch SSD1306 OLED over I2C. Adjust if your wiring differs.
#define OLED_SDA_PIN 8
#define OLED_SCL_PIN 9
#define OLED_I2C_ADDRESS 0x3C

// Optional: physical button to cycle emotions (set to -1 to disable).
#define EMOTION_BUTTON_PIN -1

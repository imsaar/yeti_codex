#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "config.h"
#include "types.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C display;

extern uint32_t nextBlinkMs;
extern uint32_t blinkUntilMs;
extern uint32_t nextFaceRefreshMs;
extern bool blinkClosed;

static constexpr uint32_t kFaceRefreshMs = 33UL;
static constexpr uint32_t kInfoDisplayRefreshMs = 1000UL;

void drawFace();
void drawInfo();
void scheduleBlink(uint32_t now);
void serviceBlink();
void drawWeatherIcon(int weatherCode, int x, int y);

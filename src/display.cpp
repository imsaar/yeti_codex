#include "display.h"
#include "weather.h"
#include "time_sync.h"

// State owned by main.cpp
extern Emotion currentEmotion;
extern DisplayMode currentDisplayMode;
extern String speechText;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

uint32_t nextBlinkMs = 0;
uint32_t blinkUntilMs = 0;
uint32_t nextFaceRefreshMs = 0;
bool blinkClosed = false;

void scheduleBlink(uint32_t now) {
  // Slightly irregular blink interval feels less robotic.
  nextBlinkMs = now + random(1800, 4200);
}

void drawWeatherIcon(int weatherCode, int x, int y) {
  // Sun / clear
  if (weatherCode == 0) {
    display.drawCircle(x + 6, y + 6, 4);
    display.drawLine(x + 6, y, x + 6, y - 2);
    display.drawLine(x + 6, y + 12, x + 6, y + 14);
    display.drawLine(x, y + 6, x - 2, y + 6);
    display.drawLine(x + 12, y + 6, x + 14, y + 6);
    return;
  }

  // Cloud base
  auto drawCloud = [&](int cx, int cy) {
    display.drawCircle(cx - 3, cy, 3);
    display.drawCircle(cx + 2, cy - 1, 4);
    display.drawCircle(cx + 7, cy, 3);
    display.drawLine(cx - 6, cy + 3, cx + 10, cy + 3);
  };

  // Partly cloudy
  if (weatherCode == 1 || weatherCode == 2) {
    display.drawCircle(x + 3, y + 4, 3);
    display.drawLine(x + 3, y, x + 3, y - 1);
    drawCloud(x + 7, y + 6);
    return;
  }

  // Cloudy / fog
  if (weatherCode == 3 || weatherCode == 45 || weatherCode == 48) {
    drawCloud(x + 6, y + 6);
    if (weatherCode == 45 || weatherCode == 48) {
      display.drawLine(x, y + 12, x + 12, y + 12);
      display.drawLine(x + 1, y + 14, x + 13, y + 14);
    }
    return;
  }

  // Snow
  if ((weatherCode >= 71 && weatherCode <= 77) || weatherCode == 85 || weatherCode == 86) {
    drawCloud(x + 6, y + 5);
    display.drawLine(x + 4, y + 11, x + 4, y + 15);
    display.drawLine(x + 2, y + 13, x + 6, y + 13);
    display.drawLine(x + 9, y + 11, x + 9, y + 15);
    display.drawLine(x + 7, y + 13, x + 11, y + 13);
    return;
  }

  // Thunderstorm
  if (weatherCode >= 95) {
    drawCloud(x + 6, y + 5);
    display.drawLine(x + 7, y + 10, x + 4, y + 14);
    display.drawLine(x + 4, y + 14, x + 8, y + 14);
    display.drawLine(x + 8, y + 14, x + 5, y + 18);
    return;
  }

  // Rain / drizzle / showers (default wet icon)
  drawCloud(x + 6, y + 5);
  display.drawLine(x + 4, y + 11, x + 3, y + 15);
  display.drawLine(x + 8, y + 11, x + 7, y + 15);
  display.drawLine(x + 12, y + 11, x + 11, y + 15);
}

void drawEyes(int y, int h, int curve, bool closed) {
  const int leftX = 30;
  const int rightX = 78;
  const int eyeW = 20;

  if (closed) {
    display.drawHLine(leftX, y + h / 2, eyeW);
    display.drawHLine(rightX, y + h / 2, eyeW);
    return;
  }

  display.drawRBox(leftX, y, eyeW, h, curve);
  display.drawRBox(rightX, y, eyeW, h, curve);
}

void drawPupils(int y, int h, int offsetX) {
  if (h < 8) return;
  const int leftCenterX = 40 + offsetX;
  const int rightCenterX = 88 + offsetX;
  const int centerY = y + h / 2;
  display.drawDisc(leftCenterX, centerY, 2);
  display.drawDisc(rightCenterX, centerY, 2);
  // Tiny glint makes eyes look less flat.
  display.drawPixel(leftCenterX - 1, centerY - 1);
  display.drawPixel(rightCenterX - 1, centerY - 1);
}

void drawBrows(int leftX1, int leftY1, int leftX2, int leftY2, int rightX1, int rightY1, int rightX2,
               int rightY2) {
  display.drawLine(leftX1, leftY1, leftX2, leftY2);
  display.drawLine(rightX1, rightY1, rightX2, rightY2);
}

void drawMouthFlat(int y, int w) {
  int x = (128 - w) / 2;
  display.drawHLine(x, y, w);
}

void drawMouthSmile(int y, int w) {
  int x = (128 - w) / 2;
  display.drawLine(x, y, x + w / 2, y + 3);
  display.drawLine(x + w / 2, y + 3, x + w, y);
  display.drawPixel(x + 1, y + 1);
  display.drawPixel(x + w - 1, y + 1);
}

void drawMouthFrown(int y, int w) {
  int x = (128 - w) / 2;
  display.drawLine(x, y + 3, x + w / 2, y);
  display.drawLine(x + w / 2, y, x + w, y + 3);
  display.drawPixel(x + 1, y + 2);
  display.drawPixel(x + w - 1, y + 2);
}

void drawMouthOpen(int cx, int cy, int r) {
  display.drawCircle(cx, cy, r);
  if (r >= 5) {
    display.drawCircle(cx, cy, r - 1);
  }
}

void drawCheeks() {
  display.drawDisc(22, 42, 1);
  display.drawDisc(26, 44, 1);
  display.drawDisc(106, 42, 1);
  display.drawDisc(102, 44, 1);
}

void drawThoughtBubble(int wobble) {
  int baseY = 49 - wobble;
  display.drawDisc(54, baseY - 5, 1);
  display.drawDisc(61, baseY - 3, 2);
  display.drawDisc(71, baseY, 3);
  display.drawCircle(82, baseY + 2, 5);
  display.drawCircle(89, baseY + 1, 4);
  display.drawCircle(94, baseY + 3, 3);
}

void drawSleepZ(int phase) {
  int y = 10 + phase;
  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(95, y, "Z");
  display.drawStr(104, y + 4, "z");
  display.drawStr(111, y + 8, "z");
}

void drawHeart(int cx, int cy, int r) {
  display.drawDisc(cx - r / 2, cy - r / 2, r / 2 + 1);
  display.drawDisc(cx + r / 2, cy - r / 2, r / 2 + 1);
  display.drawTriangle(cx - r - 1, cy - r / 3, cx + r + 1, cy - r / 3, cx, cy + r + 1);
}

void drawFace() {
  display.clearBuffer();

  bool closed = blinkClosed || currentEmotion == Emotion::Sleepy;
  uint32_t now = millis();
  int glance = static_cast<int>((now / 400UL) % 3UL) - 1;  // -1, 0, 1 subtle scanning look
  int pulse2 = static_cast<int>((now / 220UL) % 2UL);      // 0/1
  int pulse3 = static_cast<int>((now / 260UL) % 3UL) - 1;  // -1/0/1
  int bob = static_cast<int>((now / 300UL) % 4UL);         // 0..3
  int bobY = (bob < 2) ? bob : (3 - bob);                  // 0,1,1,0

  switch (currentEmotion) {
    case Emotion::Happy:
      drawEyes(15 + bobY, 15, 5, closed);
      if (!closed) {
        drawPupils(15 + bobY, 15, glance);
      }
      drawBrows(30, 12 + bobY, 48, 10 + bobY, 78, 10 + bobY, 96, 12 + bobY);
      drawCheeks();
      drawMouthSmile(41 + pulse2, 26);
      break;
    case Emotion::Sad:
      drawEyes(18 + pulse2, 10, 4, closed);
      if (!closed) {
        drawPupils(18 + pulse2, 10, 0);
      }
      drawBrows(28, 11 + pulse2, 48, 16 + pulse2, 80, 16 + pulse2, 100, 11 + pulse2);
      drawMouthFrown(43 + pulse2, 24);
      display.drawPixel(25, 36 + (pulse2 * 2));
      display.drawPixel(103, 36 + ((1 - pulse2) * 2));
      break;
    case Emotion::Sleepy:
      drawEyes(24 + pulse2, 4, 2, true);
      display.drawLine(30, 20 + pulse2, 50, 20 + pulse2);
      display.drawLine(78, 20 + pulse2, 98, 20 + pulse2);
      drawMouthFlat(46 + pulse2, 14);
      display.drawPixel(63 + pulse2, 50);
      drawSleepZ(pulse2);
      break;
    case Emotion::Angry:
      drawEyes(18, 12 + pulse2, 2, closed);
      if (!closed) {
        drawPupils(18, 12 + pulse2, pulse3);
      }
      drawBrows(25, 14 - pulse2, 49, 9 - pulse2, 103, 14 - pulse2, 79, 9 - pulse2);
      drawMouthFlat(44, 22 + pulse2);
      display.drawLine(52, 47 + pulse2, 76, 47 + pulse2);
      display.drawLine(52, 48 + pulse2, 76, 48 + pulse2);
      break;
    case Emotion::Surprised:
      drawEyes(14, 18 + pulse2, 9, closed);
      if (!closed) {
        drawPupils(14, 18 + pulse2, 0);
      }
      drawBrows(30, 10 - pulse2, 48, 9 - pulse2, 78, 9 - pulse2, 96, 10 - pulse2);
      drawMouthOpen(64, 45, 5 + pulse2);
      break;
    case Emotion::Thinking:
      drawEyes(17, 11, 4, closed);
      if (!closed) {
        drawPupils(17, 11, -1 + pulse2);
      }
      drawBrows(29, 12, 47, 11 + pulse2, 78, 12, 97, 14 + pulse2);
      drawMouthFlat(44, 14);
      drawThoughtBubble(pulse2);
      break;
    case Emotion::Love: {
      int heartPulse = 5 + pulse2;
      if (closed) {
        drawEyes(18, 10, 3, true);
      } else {
        drawHeart(40, 24 + bobY, heartPulse);
        drawHeart(88, 24 + bobY, heartPulse);
      }
      drawBrows(30, 12 + bobY, 48, 11 + bobY, 78, 11 + bobY, 96, 12 + bobY);
      drawCheeks();
      drawMouthSmile(41 + pulse2, 28);
      break;
    }
    case Emotion::Neutral:
    default:
      drawEyes(17 + (bobY ? 1 : 0), 12, 5, closed);
      if (!closed) {
        drawPupils(17 + (bobY ? 1 : 0), 12, pulse3);
      }
      drawBrows(30, 12, 48, 12 + (bobY ? 1 : 0), 78, 12, 96, 12 + (bobY ? 1 : 0));
      drawMouthFlat(44 + (bobY ? 1 : 0), 18);
      break;
  }

  display.setFont(u8g2_font_5x8_tr);
  display.drawStr(2, 61, speechText.c_str());

  display.sendBuffer();
}

void drawInfo() {
  display.clearBuffer();

  display.setFont(u8g2_font_fub17_tf);

  // Local time at top
  String timeStr = getLocalTimeString();
  int timeW = display.getStrWidth(timeStr.c_str());
  display.drawStr((128 - timeW) / 2, 22, timeStr.c_str());

  // Temperature + weather icon centered below
  String tempStr = infoTemperature;
  int tempW = display.getStrWidth(tempStr.c_str());
  const int iconW = 16;
  const int gap = 4;
  int totalW = tempW + gap + iconW;
  int tempX = (128 - totalW) / 2;
  if (tempX < 0) tempX = 0;
  display.drawStr(tempX, 52, tempStr.c_str());
  drawWeatherIcon(infoWeatherCode, tempX + tempW + gap, 36);

  display.sendBuffer();
}

void serviceBlink() {
  uint32_t now = millis();

  if (!blinkClosed && now >= nextBlinkMs) {
    blinkClosed = true;
    blinkUntilMs = now + 120;
  }

  if (blinkClosed && now >= blinkUntilMs) {
    blinkClosed = false;
    scheduleBlink(now);
  }
}

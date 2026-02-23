#pragma once
#include <Arduino.h>

enum class Emotion { Neutral, Happy, Sad, Sleepy, Angry, Surprised, Thinking, Love };

enum class DisplayMode { Face, Info };

struct Reminder {
  bool active;
  uint32_t dueMs;
  String message;
};

static constexpr size_t kMaxNotes = 8;
static constexpr size_t kMaxReminders = 8;

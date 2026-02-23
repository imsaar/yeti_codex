# Bug: Local time display showed wrong year, negative year, or alternated between two values

**Symptoms observed (in order):**
1. Time displayed with year `4148512` (e.g. `Wed Aug 3 20:25:02 4148512`)
2. After partial fix: time displayed with negative year `-5314667`
3. After switching to `time(NULL)`: display oscillated between two wrong times (e.g. `12:05P` and `5:37A`) every second

---

## Root cause 1 — Epoch accumulated instead of assigned in SNTP callback

```cpp
// BUGGY
void onSntpSync(struct timeval* tv) {
    sntpEpochAtSync = sntpEpochAtSync + tv->tv_sec;  // adds to itself on every sync
    ...
}
```

`sntpEpochAtSync` was initialized to `0`, so the first NTP sync was correct
(`0 + epoch = epoch`). However, the ESP32 SNTP client re-syncs automatically
every hour. On each re-sync the callback fires again, adding another full epoch
value (~1.77 billion seconds) to the accumulator. After enough re-syncs the
stored value grew to absurd levels, producing years in the millions.

### Fix

Simple assignment instead of accumulation:

```cpp
sntpEpochAtSync = tv->tv_sec;
```

---

## Root cause 2 — Non-atomic read of a 64-bit `volatile` on a 32-bit CPU

```cpp
volatile time_t sntpEpochAtSync = 0;  // time_t is int64_t on ESP32
```

On the ESP32 (Xtensa LX6, 32-bit), reading a 64-bit value requires two
separate 32-bit load instructions. The SNTP background FreeRTOS task can
preempt the main task between those two loads and write a new value. The main
task then reads a half-old, half-new 64-bit integer — a "torn read" — which
produces garbage, including large negative numbers (observed: year `-5314667`).

### Fix

Store the epoch as `uint32_t`. A 32-bit read/write is a single instruction on
the Xtensa LX6 and is therefore atomic. Current Unix timestamps (~1.77 billion)
fit in `uint32_t`, which overflows in year 2106.

```cpp
volatile uint32_t sntpEpochAtSync  = 0;  // atomic 32-bit read/write on ESP32
volatile uint32_t sntpMillisAtSync = 0;

void onSntpSync(struct timeval* tv) {
    sntpEpochAtSync  = (uint32_t)tv->tv_sec;
    sntpMillisAtSync = (uint32_t)millis();
    sntpCallbackFired = true;
    ntpSynced = true;
}
```

---

## Root cause 3 — `time(NULL)` oscillates during SNTP step corrections

Switching to `time(NULL)` (attempted as a simpler alternative) caused the
display to alternate between two different wrong values on consecutive
1-second redraws.

The ESP32 SNTP client runs as a background FreeRTOS task and periodically
applies **immediate step corrections** to the system clock (`SNTP_SYNC_MODE_IMMED`
is the default). Between two consecutive calls to `time(NULL)` — one second
apart — the SNTP task can step the clock forward or backward, producing a
different result each call and making the displayed time oscillate.

### Fix

Do not call `time(NULL)` in the display path. Instead, use the epoch captured
in the SNTP callback (stable, set once per sync) and advance it using
`millis()`:

```cpp
uint32_t utcNow = sntpEpochAtSync + (millis() - sntpMillisAtSync) / 1000UL;
```

This gives a stable, monotonically increasing clock between NTP syncs with no
exposure to SNTP step corrections.

---

## Root cause 4 — `gmtime_r` behaved unexpectedly on this IDF version

When the UTC epoch and UTC offset were both confirmed correct via `/status`
JSON, the displayed time was still wrong by ~3.5 hours. The intermediate values
fed to `gmtime_r` were arithmetically correct, but the decomposed hour/minute
values were wrong, suggesting the IDF's `gmtime_r` was applying an unexpected
timezone adjustment internally.

### Fix

Replace `gmtime_r` with manual arithmetic to decompose seconds-into-day,
avoiding any dependence on libc timezone state:

```cpp
long secsInDay = (long)(utcNow % 86400UL) + infoUtcOffsetSeconds;
secsInDay = ((secsInDay % 86400L) + 86400L) % 86400L;  // normalize to [0, 86400)
int h = (int)(secsInDay / 3600L);
int m = (int)((secsInDay % 3600L) / 60L);
```

The double-modulo normalization handles cases where the UTC offset pushes the
time before midnight (e.g. UTC 02:00 with a −8 h offset).

---

## Final working implementation

```cpp
volatile bool     sntpCallbackFired = false;
volatile uint32_t sntpEpochAtSync   = 0;
volatile uint32_t sntpMillisAtSync  = 0;

void onSntpSync(struct timeval* tv) {
    sntpEpochAtSync  = (uint32_t)tv->tv_sec;
    sntpMillisAtSync = (uint32_t)millis();
    sntpCallbackFired = true;
    ntpSynced = true;
}

String getLocalTimeString() {
    if (!infoTimeValid || !sntpCallbackFired) return "--:--";
    uint32_t utcNow = sntpEpochAtSync + (millis() - sntpMillisAtSync) / 1000UL;
    if (utcNow < 1000000000UL) return "--:--";  // before year 2001 — not synced yet
    long secsInDay = (long)(utcNow % 86400UL) + infoUtcOffsetSeconds;
    secsInDay = ((secsInDay % 86400L) + 86400L) % 86400L;
    int h  = (int)(secsInDay / 3600L);
    int m  = (int)((secsInDay % 3600L) / 60L);
    bool pm = h >= 12;
    if (h == 0) h = 12;
    else if (h > 12) h -= 12;
    char buf[10];
    snprintf(buf, sizeof(buf), "%d:%02d%s", h, m, pm ? "P" : "A");
    return String(buf);
}
```

The UTC offset (`infoUtcOffsetSeconds`) is fetched from the Open-Meteo API
response field `utc_offset_seconds` and already accounts for DST.

---

## Files changed

- `src/main.cpp` — `onSntpSync()`, `getLocalTimeString()`, and the
  `sntpEpochAtSync` / `sntpMillisAtSync` variable declarations

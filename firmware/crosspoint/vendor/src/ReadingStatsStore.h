/* WODLE-PORT: reading-statistics store — wraps the pure ReadingStatsCore
 * with day-index resolution (RTC + UTC offset) and JSON persistence.
 * Fed by the status bar's page-turn observation (see BaseTheme); saved on
 * sleep and on a dirty timer in the main loop. */
#pragma once

#include <ReadingStatsCore.h>

#include <cstdint>

class ReadingStatsStore {
  static ReadingStatsStore instance;

  ReadingStats::Core core;
  bool dirty = false;

 public:
  static ReadingStatsStore& getInstance() { return instance; }

  // Record one reading turn: +1 page, plus activeMs of reading time when the
  // interval was accepted (0 = pause/flip, page still counts).
  void onPageTurn(unsigned long activeMs);

  // Local day index (localEpoch / 86400) or 0 when the clock was never set.
  int32_t today() const;

  const ReadingStats::Core& data() const { return core; }

  bool isDirty() const { return dirty; }
  bool saveToFile();
  bool loadFromFile();
};

#define READING_STATS ReadingStatsStore::getInstance()

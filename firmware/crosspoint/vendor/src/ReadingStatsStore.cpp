/* WODLE-PORT: see ReadingStatsStore.h. */
#include "ReadingStatsStore.h"

#include <ArduinoJson.h>
#include <ClockFormat.h>
#include <HalStorage.h>
#include <Logging.h>

#include <ctime>

#include "CrossPointSettings.h"

ReadingStatsStore ReadingStatsStore::instance;

namespace {
constexpr char STATS_FILE[] = "/.crosspoint/stats.json";
// HalClock's plausibility floor (2025-01-01 UTC) — before the user ever sets
// the clock, samples land in the day-0 catch-all bucket.
constexpr long long PLAUSIBLE_EPOCH = 1735689600LL;
}  // namespace

int32_t ReadingStatsStore::today() const {
  const long long now = static_cast<long long>(time(nullptr));
  if (now < PLAUSIBLE_EPOCH) return 0;
  const long long local = now + static_cast<long long>(ClockFormat::offsetMinutes(SETTINGS.clockUtcOffsetQ)) * 60;
  return static_cast<int32_t>(local / 86400);
}

void ReadingStatsStore::onPageTurn(unsigned long activeMs) {
  core.record(today(), 1, static_cast<uint32_t>(activeMs / 1000));
  dirty = true;
}

bool ReadingStatsStore::saveToFile() {
  JsonDocument doc;
  doc["lifetimeSeconds"] = core.lifetimeSeconds();
  doc["lifetimePages"] = core.lifetimePages();
  JsonArray days = doc["days"].to<JsonArray>();
  for (int i = 0; i < ReadingStats::MAX_DAYS; i++) {
    const ReadingStats::DayBucket& b = core.buckets()[i];
    if (b.day < 0) continue;
    JsonObject o = days.add<JsonObject>();
    o["d"] = b.day;
    o["s"] = b.seconds;
    o["p"] = b.pages;
  }

  String json;
  serializeJson(doc, json);
  Storage.mkdir("/.crosspoint");
  const bool ok = Storage.writeFile(STATS_FILE, json);
  if (ok) dirty = false;
  return ok;
}

bool ReadingStatsStore::loadFromFile() {
  if (!Storage.exists(STATS_FILE)) return true;  // fresh device: empty stats
  String json = Storage.readFile(STATS_FILE);
  if (json.isEmpty()) return true;

  JsonDocument doc;
  if (deserializeJson(doc, json.c_str())) {
    LOG_ERR("STAT", "stats.json parse error, starting fresh");
    return false;
  }

  core.restore(doc["lifetimeSeconds"] | 0ULL, doc["lifetimePages"] | 0ULL);
  for (JsonObjectConst o : doc["days"].as<JsonArrayConst>()) {
    const int32_t day = o["d"] | -1;
    if (day < 0) continue;
    core.restoreBucket(day, o["s"] | 0U, o["p"] | 0U);
  }
  dirty = false;
  return true;
}

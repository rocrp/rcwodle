/* WODLE-PORT: see ReadingStatsActivity.h. */
#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
void formatDuration(char* buf, size_t n, uint64_t seconds) {
  const unsigned long totalMinutes = static_cast<unsigned long>(seconds / 60);
  if (totalMinutes >= 60) {
    snprintf(buf, n, "%luh %lum", totalMinutes / 60, totalMinutes % 60);
  } else {
    snprintf(buf, n, "%lum", totalMinutes);
  }
}
}  // namespace

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void ReadingStatsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void ReadingStatsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READING_STATS));

  const auto& stats = READING_STATS.data();
  const int32_t today = READING_STATS.today();

  uint32_t todaySec = 0, todayPages = 0;
  stats.dayTotals(today, todaySec, todayPages);
  uint32_t weekSec = 0, weekPages = 0;
  stats.lastDaysTotals(today, 7, weekSec, weekPages);

  char duration[24];
  char line[96];
  const int x = metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID) + 10;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + lineHeight;

  auto putStat = [&](StrId label, uint64_t seconds, uint64_t pages) {
    renderer.drawText(UI_12_FONT_ID, x, y, I18N.get(label), true, EpdFontFamily::BOLD);
    y += lineHeight;
    formatDuration(duration, sizeof(duration), seconds);
    snprintf(line, sizeof(line), "%s  ·  %llu %s", duration, (unsigned long long)pages, tr(STR_STATS_PAGES));
    renderer.drawText(UI_12_FONT_ID, x + 20, y, line);
    y += lineHeight + 8;
  };

  putStat(StrId::STR_STATS_TODAY, todaySec, todayPages);
  putStat(StrId::STR_STATS_LAST7, weekSec, weekPages);
  putStat(StrId::STR_STATS_LIFETIME, stats.lifetimeSeconds(), stats.lifetimePages());

  if (todayPages > 0 && todaySec > 0) {
    const unsigned long secPerPage = static_cast<unsigned long>(todaySec / todayPages);
    snprintf(line, sizeof(line), "%s: %lum %02lus", tr(STR_STATS_AVG), secPerPage / 60, secPerPage % 60);
    renderer.drawText(UI_12_FONT_ID, x, y, line);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

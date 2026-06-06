/* WODLE-PORT: reading statistics screen (Settings → System → Reading Stats).
 * Today / last-7-days / lifetime time+pages from ReadingStatsStore, plus
 * today's average pace. Static screen, Back exits. */
#pragma once

#include "activities/Activity.h"

class ReadingStatsActivity final : public Activity {
 public:
  explicit ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStats", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};

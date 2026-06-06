/* WODLE-PORT: hardware diagnostics screen (Settings → System → Diagnostics).
 * Live view of every peripheral the HIL checklist cares about — battery
 * gauge, charger/USB, AHT20, frontlight, touch, SD, heap, wake reason and
 * the last logical input — so bring-up works even with a dead UART console.
 * Auto-refreshes every couple of seconds with a fast (DU) pass; Confirm
 * forces a full-quality refresh. */
#pragma once

#include "activities/Activity.h"

class DiagnosticsActivity final : public Activity {
 public:
  explicit DiagnosticsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Diagnostics", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  unsigned long lastDrawMs = 0;
  bool fullRefresh = false;
  const char* lastInput = "-";
};

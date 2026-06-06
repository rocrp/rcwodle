#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Reader status bar configuration activity
class StatusBarSettingsActivity final : public Activity {
 public:
  explicit StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("StatusBarSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;

  int selectedIndex = 0;
  // WODLE-PORT: displayed-row -> MenuItem map, built in onEnter(). Clock rows
  // appear only with an RTC (upstream behaviour); temp/humidity rows only
  // when the AHT20 responds — so the visible set is no longer a plain prefix.
  std::vector<uint8_t> visibleItems;

  void handleSelection();
};

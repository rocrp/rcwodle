/* WODLE-PORT: manual time editor for the on-chip RTC (no WiFi -> no NTP).
 * Edits LOCAL time as two fields (hours/minutes, 24h); Confirm switches
 * fields, Up/Down adjust, Back saves (converted to UTC via the configured
 * offset) and exits. Modeled on ClockOffsetActivity. */
#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class TimeSetActivity final : public Activity {
 public:
  explicit TimeSetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TimeSet", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;

  enum Field { FIELD_HOURS = 0, FIELD_MINUTES = 1, FIELD_COUNT };
  Field activeField = FIELD_HOURS;

  // Working copy: LOCAL wall-clock time being edited.
  uint8_t hours = 12;
  uint8_t minutes = 0;

  void loadFromClock();
  void saveToClock() const;
  void adjustActiveField(int delta);
};

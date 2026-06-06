/* WODLE-PORT: see TimeSetActivity.h. */
#include "TimeSetActivity.h"

#include <ClockFormat.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void TimeSetActivity::onEnter() {
  Activity::onEnter();
  loadFromClock();
  activeField = FIELD_HOURS;
  requestUpdate();
}

void TimeSetActivity::onExit() {
  saveToClock();
  Activity::onExit();
}

void TimeSetActivity::loadFromClock() {
  uint8_t utcH = 0, utcM = 0;
  if (halClock.getTime(utcH, utcM)) {
    int h = 0, m = 0;
    // Convert the stored UTC to the local time the user expects to edit.
    ClockFormat::localHM(static_cast<long long>(utcH) * 3600 + static_cast<long long>(utcM) * 60,
                         SETTINGS.clockUtcOffsetQ, h, m);
    hours = static_cast<uint8_t>(h);
    minutes = static_cast<uint8_t>(m);
  } else {
    hours = 12;  // time never set: start from noon
    minutes = 0;
  }
}

void TimeSetActivity::saveToClock() const {
  int utcH = 0, utcM = 0;
  ClockFormat::localToUtcHM(hours, minutes, SETTINGS.clockUtcOffsetQ, utcH, utcM);
  halClock.writeTimeToRTC(static_cast<uint8_t>(utcH), static_cast<uint8_t>(utcM), 0);
}

void TimeSetActivity::adjustActiveField(int delta) {
  if (activeField == FIELD_HOURS) {
    hours = static_cast<uint8_t>((hours + delta + 24) % 24);
  } else {
    minutes = static_cast<uint8_t>((minutes + delta + 60) % 60);
  }
}

void TimeSetActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activeField = static_cast<Field>((activeField + 1) % FIELD_COUNT);
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    adjustActiveField(+1);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    adjustActiveField(-1);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this] {
    adjustActiveField(+1);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this] {
    adjustActiveField(-1);
    requestUpdate();
  });
}

void TimeSetActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SET_TIME));

  const int centreY = pageHeight / 2 - 40;
  auto widthOf = [&](const char* s) { return renderer.getTextWidth(UI_12_FONT_ID, s, EpdFontFamily::BOLD); };
  constexpr int fieldPaddingX = 6;
  constexpr int colonGap = 5;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int fieldHeight = lineHeight + 2;

  char hoursStr[4];
  snprintf(hoursStr, sizeof(hoursStr), "%02d", hours);
  char minutesStr[4];
  snprintf(minutesStr, sizeof(minutesStr), "%02d", minutes);

  const int boxW = widthOf("00") + fieldPaddingX * 2;
  const int colonWidth = widthOf(":");
  const int totalWidth = boxW + colonGap + colonWidth + colonGap + boxW;

  int x = (pageWidth - totalWidth) / 2;

  auto drawField = [&](const char* text, const int boxX, const Field field) {
    const bool selected = activeField == field;
    renderer.fillRectDither(boxX, centreY, boxW, fieldHeight, selected ? Color::LightGray : Color::White);
    renderer.drawRect(boxX, centreY, boxW, fieldHeight, true);
    if (selected) {
      renderer.drawRect(boxX + 1, centreY + 1, boxW - 2, fieldHeight - 2, true);
    }
    const int textX = boxX + (boxW - widthOf(text)) / 2;
    renderer.drawText(UI_12_FONT_ID, textX, centreY, text, true, EpdFontFamily::BOLD);
  };

  drawField(hoursStr, x, FIELD_HOURS);
  x += boxW + colonGap;
  renderer.drawText(UI_12_FONT_ID, x, centreY, ":", true, EpdFontFamily::BOLD);
  x += colonWidth + colonGap;
  drawField(minutesStr, x, FIELD_MINUTES);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

/* WODLE-PORT: see DiagnosticsActivity.h. Body text is intentionally
 * English-only — it's a service screen for hardware bring-up. */
#include "DiagnosticsActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WodleAht20.h>
#include <WodleBattery.h>
#include <WodleFrontlight.h>
#include <WodleTouch.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long AUTO_REFRESH_MS = 2000;

const char* wakeupReasonName(HalGPIO::WakeupReason r) {
  switch (r) {
    case HalGPIO::WakeupReason::PowerButton:
      return "PowerButton";
    case HalGPIO::WakeupReason::AfterFlash:
      return "AfterFlash";
    case HalGPIO::WakeupReason::AfterUSBPower:
      return "AfterUSBPower";
    default:
      return "Other (cold boot/reset)";
  }
}
}  // namespace

void DiagnosticsActivity::onEnter() {
  Activity::onEnter();
  fullRefresh = true;  // first paint at full quality
  requestUpdate();
}

void DiagnosticsActivity::loop() {
  // Record the last logical input so keys AND touch zones can be verified
  // through the production mapping path. Back still exits (after drawing
  // its name one last time would be pointless — just leave).
  using Button = MappedInputManager::Button;
  static const struct {
    Button button;
    const char* name;
  } kButtons[] = {
      {Button::Up, "Up"},       {Button::Down, "Down"},           {Button::Left, "Left"},
      {Button::Right, "Right"}, {Button::Confirm, "Confirm"},     {Button::Power, "Power"},
      {Button::PageBack, "PageBack"}, {Button::PageForward, "PageForward"},
  };

  if (mappedInput.wasPressed(Button::Back)) {
    finish();
    return;
  }

  for (const auto& b : kButtons) {
    if (mappedInput.wasPressed(b.button)) {
      lastInput = b.name;
      if (b.button == Button::Confirm) fullRefresh = true;  // manual GC pass
      requestUpdate();
      return;
    }
  }

  if (millis() - lastDrawMs >= AUTO_REFRESH_MS) requestUpdate();
}

void DiagnosticsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_DIAGNOSTICS),
                 CROSSPOINT_VERSION);

  char line[64];
  const int x = metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 8;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + lineHeight;

  auto put = [&](const char* text) {
    renderer.drawText(UI_10_FONT_ID, x, y, text);
    y += lineHeight;
  };

  snprintf(line, sizeof(line), "Uptime: %lus   Heap free: %u KB", millis() / 1000,
           (unsigned)(ESP.getFreeHeap() / 1024));
  put(line);

  if (WodleBattery::available()) {
    snprintf(line, sizeof(line), "Battery: %d%%  %d mV (gauge OK)", WodleBattery::percent(),
             WodleBattery::millivolts());
  } else {
    snprintf(line, sizeof(line), "Battery: gauge NOT RESPONDING");
  }
  put(line);

  snprintf(line, sizeof(line), "USB power: %s", gpio.isUsbConnected() ? "connected" : "not connected");
  put(line);

  float tempC = 0, rhPct = 0;
  if (!WodleAht20::available()) {
    snprintf(line, sizeof(line), "AHT20: NOT RESPONDING");
  } else if (WodleAht20::read(tempC, rhPct)) {
    snprintf(line, sizeof(line), "AHT20: %.1f°C  %.1f%% RH", (double)tempC, (double)rhPct);
  } else {
    snprintf(line, sizeof(line), "AHT20: OK, first reading pending");
  }
  put(line);

  snprintf(line, sizeof(line), "Frontlight: %d%%", WodleFrontlight::level());
  put(line);

  snprintf(line, sizeof(line), "Touch: %s", WodleTouch::available() ? "CST836U OK" : "NOT RESPONDING");
  put(line);

  snprintf(line, sizeof(line), "SD card: %s", Storage.ready() ? "ready" : "NOT READY");
  put(line);

  snprintf(line, sizeof(line), "Wake reason: %s", wakeupReasonName(gpio.getWakeupReason()));
  put(line);

  snprintf(line, sizeof(line), "Last input: %s", lastInput);
  put(line);

  y += lineHeight;
  renderer.drawText(UI_10_FONT_ID, x, y, "Auto-refresh 2s (fast). Confirm = full refresh.");

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_REFRESH), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(fullRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  fullRefresh = false;
  lastDrawMs = millis();
}

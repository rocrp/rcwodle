#include "EpubReaderFootnotesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "TapClassifier.h"  // WODLE-PORT: TOP_STRIP_PX for top-strip tap-back
#include "components/UITheme.h"
#include "fontIds.h"

void EpubReaderFootnotesActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void EpubReaderFootnotesActivity::onExit() { Activity::onExit(); }

// WODLE-PORT: shared Confirm/tap activation — return the selected footnote href.
void EpubReaderFootnotesActivity::activateSelected() {
  if (selectedIndex >= 0 && selectedIndex < static_cast<int>(footnotes.size())) {
    setResult(FootnoteResult{footnotes[selectedIndex].href});
    finish();
  }
}

// WODLE-PORT: bespoke hit-test mirroring render()'s custom scrollOffset layout (this
// screen does not use GUI.drawList). Rows start at 60+contentY, lineHeight=36, the first
// visible row is footnotes[scrollOffset]. Returns the absolute footnote index, or -1.
int EpubReaderFootnotesActivity::hitTestFootnote(int tapX, int tapY) const {
  const int total = static_cast<int>(footnotes.size());
  if (total <= 0) return -1;

  const auto orientation = renderer.getOrientation();
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int contentY = isPortraitInverted ? 50 : 0;
  constexpr int lineHeight = 36;
  const int listTop = 60 + contentY;
  const int visibleCount = std::max(1, (renderer.getScreenHeight() - contentY) / lineHeight);

  // Selection highlight spans the full screen width, so accept any X within the screen.
  if (tapX < 0 || tapX >= renderer.getScreenWidth()) return -1;
  if (tapY < listTop) return -1;
  const int visibleRow = (tapY - listTop) / lineHeight;
  if (visibleRow < 0 || visibleRow >= visibleCount) return -1;

  const int index = scrollOffset + visibleRow;
  if (index < scrollOffset || index >= total || index >= scrollOffset + visibleCount) return -1;
  return index;
}

void EpubReaderFootnotesActivity::loop() {
  // WODLE-PORT: direct tap-to-select. consumeTap coords are logical-PORTRAIT; this reader
  // sub-activity inherits the reader's orientation, so tap-to-select is gated on Portrait
  // (full rotated-orientation tap mapping is a future enhancement). A hit selects+swallows;
  // a non-top-strip miss is swallowed (so a center-zone miss can't synthesize Confirm and
  // open the highlighted footnote); a TOP-STRIP miss FALLS THROUGH so the synthesized BACK
  // zone button reaches the Back handler below.
  if (renderer.getOrientation() == GfxRenderer::Orientation::Portrait) {
    int tx, ty;
    if (mappedInput.consumeTap(tx, ty)) {
      const int n = hitTestFootnote(tx, ty);
      if (n >= 0) {
        selectedIndex = n;
        activateSelected();  // same activation as Confirm
        return;
      }
      if (ty >= TapClassifier::TOP_STRIP_PX) return;  // swallow non-top-strip miss; top-strip falls through to BACK
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }

  buttonNavigator.onNext([this] {
    if (!footnotes.empty()) {
      selectedIndex = (selectedIndex + 1) % footnotes.size();
      requestUpdate();
    }
  });

  buttonNavigator.onPrevious([this] {
    if (!footnotes.empty()) {
      selectedIndex = (selectedIndex - 1 + footnotes.size()) % footnotes.size();
      requestUpdate();
    }
  });
}

void EpubReaderFootnotesActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  // Landscape orientation: reserve a horizontal gutter for button hints.
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  // Inverted portrait: reserve vertical space for hints at the top.
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  // Landscape CW places hints on the left edge; CCW keeps them on the right.
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;

  // Manual centering to honor content gutters.
  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_FOOTNOTES), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_FOOTNOTES), true, EpdFontFamily::BOLD);

  if (footnotes.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, 90 + contentY, tr(STR_NO_FOOTNOTES));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  constexpr int lineHeight = 36;
  const int screenWidth = renderer.getScreenWidth();
  const int marginLeft = contentX + 20;

  const int visibleCount = std::max(1, (renderer.getScreenHeight() - contentY) / lineHeight);
  if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
  if (selectedIndex >= scrollOffset + visibleCount) scrollOffset = selectedIndex - visibleCount + 1;

  // WODLE-PORT: pick ONE font for the whole list. Resolving uiFontFor per row
  // gave rows mixed sizes (UI_10 for ASCII/covered-CJK rows vs the larger SD
  // reading font for uncovered-CJK rows) — the size inconsistency the user saw.
  // Resolve over all visible labels so the entire list is uniform: if ANY label
  // needs the SD fallback, every row uses it.
  std::string allLabels;
  for (int i = scrollOffset; i < static_cast<int>(footnotes.size()) && i < scrollOffset + visibleCount; i++) {
    allLabels += (footnotes[i].number[0] == '\0') ? tr(STR_LINK) : footnotes[i].number;
  }
  const auto listFont = renderer.uiFontFor(UI_10_FONT_ID, allLabels.c_str());

  for (int i = scrollOffset; i < static_cast<int>(footnotes.size()) && i < scrollOffset + visibleCount; i++) {
    const int y = 60 + contentY + (i - scrollOffset) * lineHeight;
    const bool isSelected = (i == selectedIndex);

    if (isSelected) {
      renderer.fillRect(0, y, screenWidth, lineHeight, true);
    }

    // Show footnote number and abbreviated href
    std::string label = footnotes[i].number;
    if (label.empty()) {
      label = tr(STR_LINK);
    }
    renderer.drawText(listFont, marginLeft, y + 4, label.c_str(), !isSelected);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

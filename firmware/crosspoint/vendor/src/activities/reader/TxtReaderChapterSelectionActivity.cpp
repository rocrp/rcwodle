// WODLE-PORT addition (not upstream). See header.
#include "TxtReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Chapter titles are arbitrary book text (e.g. hanzi) that the UI fonts'
// translation-charset subset doesn't cover — draw items with the reader font
// (full-coverage SD font when one is selected) instead of UI_10_FONT_ID.
int TxtReaderChapterSelectionActivity::itemFontId() const { return SETTINGS.getReaderFontId(); }

int TxtReaderChapterSelectionActivity::rowHeight() const {
  return std::max(30, renderer.getLineHeight(itemFontId()) + 4);
}

int TxtReaderChapterSelectionActivity::getPageItems() const {
  const int screenHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int startY = 60 + hintGutterHeight;
  const int availableHeight = screenHeight - startY - rowHeight();
  return std::max(1, availableHeight / rowHeight());
}

// Page index whose [start, nextStart) range contains the byte offset.
int TxtReaderChapterSelectionActivity::findPageForOffset(const uint32_t offset) const {
  if (pageOffsets.empty()) return 0;
  auto it = std::upper_bound(pageOffsets.begin(), pageOffsets.end(), static_cast<size_t>(offset));
  return static_cast<int>(std::distance(pageOffsets.begin(), it)) - 1;
}

// Last chapter whose heading starts at or before the end of the given page.
int TxtReaderChapterSelectionActivity::findChapterIndexForPage(const int page) const {
  int result = 0;
  for (size_t i = 0; i < chapters.size(); i++) {
    if (findPageForOffset(chapters[i].offset) <= page) {
      result = static_cast<int>(i);
    } else {
      break;
    }
  }
  return result;
}

void TxtReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = findChapterIndexForPage(currentPage);
  requestUpdate();
}

void TxtReaderChapterSelectionActivity::loop() {
  const int pageItems = getPageItems();
  const int totalItems = static_cast<int>(chapters.size());

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!chapters.empty() && selectorIndex >= 0 && selectorIndex < totalItems) {
      setResult(PageResult{static_cast<uint32_t>(findPageForOffset(chapters[selectorIndex].offset))});
      finish();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  buttonNavigator.onNextRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
}

void TxtReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;
  const int pageItems = getPageItems();

  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_SELECT_CHAPTER), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_SELECT_CHAPTER), true, EpdFontFamily::BOLD);

  if (chapters.empty()) {
    const int emptyX = contentX + (contentWidth - renderer.getTextWidth(UI_10_FONT_ID, tr(STR_NO_CHAPTERS))) / 2;
    renderer.drawText(UI_10_FONT_ID, emptyX, 120 + contentY, tr(STR_NO_CHAPTERS));
    renderer.displayBuffer();
    return;
  }

  const int pageStartIndex = selectorIndex / pageItems * pageItems;
  const int pageEndIndex = std::min(static_cast<int>(chapters.size()), pageStartIndex + pageItems);
  const int itemFont = itemFontId();
  const int row = rowHeight();

  // SD fonts stream glyphs from the card — prewarm the visible titles in one
  // batch so drawText doesn't thrash the 8-slot on-demand ring buffer.
  if (renderer.isSdCardFont(itemFont)) {
    std::string visible;
    for (int i = pageStartIndex; i < pageEndIndex; i++) {
      visible += chapters[i].title;
    }
    renderer.ensureSdCardFontReady(itemFont, visible.c_str(), /*styleMask=*/0x01);
  }

  renderer.fillRect(contentX, 60 + contentY + (selectorIndex % pageItems) * row - 2, contentWidth - 1, row);
  for (int i = pageStartIndex; i < pageEndIndex; i++) {
    const char* title = chapters[i].title.empty() ? tr(STR_UNNAMED) : chapters[i].title.c_str();
    renderer.drawText(itemFont, contentX + 20, 60 + contentY + (i % pageItems) * row, title, i != selectorIndex);
  }

  if (renderer.getOrientation() != GfxRenderer::LandscapeClockwise) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}

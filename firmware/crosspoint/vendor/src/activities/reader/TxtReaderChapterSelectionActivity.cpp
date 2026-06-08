// WODLE-PORT addition (not upstream). See header.
#include "TxtReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "TapClassifier.h"  // WODLE-PORT: TOP_STRIP_PX for top-strip tap-back
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

// WODLE-PORT: shared Confirm/tap activation — resolve the page for the selected chapter.
void TxtReaderChapterSelectionActivity::activateSelected() {
  const int totalItems = static_cast<int>(chapters.size());
  if (!chapters.empty() && selectorIndex >= 0 && selectorIndex < totalItems) {
    setResult(PageResult{static_cast<uint32_t>(findPageForOffset(chapters[selectorIndex].offset))});
    finish();
  }
}

// WODLE-PORT: bespoke hit-test mirroring render()'s custom row layout (this screen does
// not use GUI.drawList). Rows start at 60+contentY, each rowHeight() tall, paged by
// getPageItems(). Returns the absolute chapter index under the tap, or -1.
int TxtReaderChapterSelectionActivity::hitTestChapter(int tapX, int tapY) const {
  const int totalItems = static_cast<int>(chapters.size());
  if (totalItems <= 0) return -1;

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int contentY = isPortraitInverted ? 50 : 0;
  const int listTop = 60 + contentY;
  const int row = rowHeight();
  const int pageItems = getPageItems();
  if (row <= 0 || pageItems <= 0) return -1;

  if (tapX < contentX || tapX >= contentX + contentWidth) return -1;
  if (tapY < listTop) return -1;
  const int visibleRow = (tapY - listTop) / row;
  if (visibleRow < 0 || visibleRow >= pageItems) return -1;

  const int pageStartIndex = selectorIndex / pageItems * pageItems;
  const int index = pageStartIndex + visibleRow;
  if (index < pageStartIndex || index >= totalItems || index >= pageStartIndex + pageItems) return -1;
  return index;
}

void TxtReaderChapterSelectionActivity::loop() {
  const int pageItems = getPageItems();
  const int totalItems = static_cast<int>(chapters.size());

  // WODLE-PORT: direct tap-to-select. consumeTap coords are logical-PORTRAIT; this reader
  // sub-activity inherits the reader's orientation, so tap-to-select is gated on Portrait
  // (full rotated-orientation tap mapping is a future enhancement). A hit selects+swallows;
  // a non-top-strip miss is swallowed (so a center-zone miss can't synthesize Confirm and
  // open the highlighted chapter); a TOP-STRIP miss FALLS THROUGH so the synthesized BACK
  // zone button reaches the Back handler below.
  if (renderer.getOrientation() == GfxRenderer::Orientation::Portrait) {
    int tx, ty;
    if (mappedInput.consumeTap(tx, ty)) {
      const int n = hitTestChapter(tx, ty);
      if (n >= 0) {
        selectorIndex = n;
        activateSelected();  // same activation as Confirm
        return;
      }
      if (ty >= TapClassifier::TOP_STRIP_PX) return;  // swallow non-top-strip miss; top-strip falls through to BACK
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
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

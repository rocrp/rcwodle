#include "EpubReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "TapClassifier.h"  // WODLE-PORT: TOP_STRIP_PX for top-strip tap-back
#include "components/UITheme.h"
#include "fontIds.h"

int EpubReaderChapterSelectionActivity::getTotalItems() const { return epub->getTocItemsCount(); }

void EpubReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  selectorIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (selectorIndex == -1) {
    selectorIndex = 0;
  }

  // Trigger first update
  requestUpdate();
}

void EpubReaderChapterSelectionActivity::onExit() { Activity::onExit(); }

// WODLE-PORT: shared Confirm/tap activation — resolve the TOC item at selectorIndex.
void EpubReaderChapterSelectionActivity::activateSelected() {
  const auto tocItem = epub->getTocItem(selectorIndex);
  if (tocItem.spineIndex == -1) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  } else {
    setResult(ChapterResult{tocItem.spineIndex, tocItem.anchor});
    finish();
  }
}

void EpubReaderChapterSelectionActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);
  const int totalItems = getTotalItems();

  // WODLE-PORT: direct tap-to-select. consumeTap coords are logical-PORTRAIT; this reader
  // sub-activity inherits the reader's orientation, so tap-to-select is gated on Portrait
  // (full rotated-orientation tap mapping is a future enhancement). A hit selects+swallows;
  // a non-top-strip miss is swallowed (so a center-zone miss can't synthesize Confirm and
  // open the highlighted row); a TOP-STRIP miss FALLS THROUGH so the synthesized BACK zone
  // button reaches the Back handler below.
  if (renderer.getOrientation() == GfxRenderer::Orientation::Portrait) {
    int tx, ty;
    if (mappedInput.consumeTap(tx, ty)) {
      if (totalItems > 0) {
        const int n = GUI.hitTestList(renderer, listRect(), totalItems, selectorIndex, /*hasSubtitle=*/false, tx, ty);
        if (n >= 0) {
          selectorIndex = n;
          activateSelected();  // same activation as Confirm
          return;
        }
      }
      if (ty >= TapClassifier::TOP_STRIP_PX) return;  // swallow non-top-strip miss; top-strip falls through to BACK
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
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

// WODLE-PORT: single source of truth for the chapter-list rect (render + tap).
Rect EpubReaderChapterSelectionActivity::listRect() const {
  const auto metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
  return Rect{screen.x, contentTop, screen.width, contentHeight};
}

void EpubReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_SELECT_CHAPTER));

  const int totalItems = getTotalItems();
  GUI.drawList(renderer, listRect(), totalItems, selectorIndex,  // WODLE-PORT: shared rect
               [this](int index) {
                 auto item = epub->getTocItem(index);
                 std::string indent((item.level - 1) * 2, ' ');
                 return indent + item.title;
               });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

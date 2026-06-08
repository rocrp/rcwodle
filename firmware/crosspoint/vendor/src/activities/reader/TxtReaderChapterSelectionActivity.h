// WODLE-PORT addition (not upstream): chapter selection for TXT books,
// modeled on XtcReaderChapterSelectionActivity. Returns PageResult with the
// page index resolved from the chapter's byte offset.
#pragma once
#include <TxtChapterScanner.h>

#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class TxtReaderChapterSelectionActivity final : public Activity {
  // Owned by the parent TxtReaderActivity, which outlives this child.
  const std::vector<TxtChapter>& chapters;
  const std::vector<size_t>& pageOffsets;
  ButtonNavigator buttonNavigator;
  int currentPage = 0;
  int selectorIndex = 0;

  int itemFontId() const;
  int rowHeight() const;
  int getPageItems() const;
  int findChapterIndexForPage(int page) const;
  int findPageForOffset(uint32_t offset) const;

  // WODLE-PORT: this list is custom-rendered (not GUI.drawList), so tap hit-testing
  // mirrors render()'s own row math here instead of using the theme helper.
  int hitTestChapter(int tapX, int tapY) const;  // absolute chapter index or -1
  void activateSelected();                        // shared Confirm/tap activation

 public:
  explicit TxtReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const std::vector<TxtChapter>& chapters,
                                             const std::vector<size_t>& pageOffsets, int currentPage)
      : Activity("TxtReaderChapterSelection", renderer, mappedInput),
        chapters(chapters),
        pageOffsets(pageOffsets),
        currentPage(currentPage) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};

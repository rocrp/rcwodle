#pragma once

#include <Txt.h>
#include <TxtChapterScanner.h>

#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include "PartialClockTicker.h"

class TxtReaderActivity final : public Activity {
  std::unique_ptr<Txt> txt;
  PartialClockTicker partialClock;  // WODLE-PORT: minute-fresh clock (experimental)

  int currentPage = 0;
  int totalPages = 1;
  int pagesUntilFullRefresh = 0;

  // WODLE-PORT: deferred anti-aliasing. A page turn does a fast DU refresh of the
  // BW page immediately; the slow 4-gray pass is run once from loop() after the
  // reader dwells (stops flipping). Cleared on every page turn / nav.
  bool aaRefinePending = false;
  unsigned long lastRenderMs = 0UL;

  // Streaming text reader - stores file offsets for each page
  std::vector<size_t> pageOffsets;  // File offset for start of each page
  // WODLE-PORT: detected chapter headings (第X章 / Chapter N …) for navigation
  std::vector<TxtChapter> chapters;
  std::vector<std::string> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;

  // Cached settings for cache validation (different fonts/margins require re-indexing)
  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  void renderPage();
  // WODLE-PORT: draw the current page's text lines into the framebuffer (no
  // display). Reused by renderPage() (BW + each grayscale plane) and the deferred
  // AA pass in loop().
  void renderPageLines() const;
  // WODLE-PORT: deferred grayscale AA pass for the current page, run from loop().
  void refineCurrentPageAA();
  void renderStatusBar() const;

  void initializeReader();
  bool loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset);
  void buildPageIndex();
  bool loadPageIndexCache();
  void savePageIndexCache() const;
  void saveProgress() const;
  void loadProgress();

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Txt> txt)
      : Activity("TxtReader", renderer, mappedInput), txt(std::move(txt)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  ScreenshotInfo getScreenshotInfo() const override;
};

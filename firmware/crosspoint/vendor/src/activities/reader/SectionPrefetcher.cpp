#include "SectionPrefetcher.h"

#include <Epub.h>
#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <Logging.h>

namespace {
constexpr char CACHE_DIR[] = "/.crosspoint";  // matches ReaderActivity::loadEpub

// RAII take/give for the mutex (FreeRTOS shim → RT-Thread recursive mutex).
struct Guard {
  SemaphoreHandle_t m;
  explicit Guard(SemaphoreHandle_t mm) : m(mm) {
    if (m) xSemaphoreTake(m, portMAX_DELAY);
  }
  ~Guard() {
    if (m) xSemaphoreGive(m);
  }
};

// Worker-incremented, stat-read diagnostic counter (a benign uint32 race is fine).
uint32_t s_buildsDone = 0;
}  // namespace

SectionPrefetcher& SectionPrefetcher::instance() {
  static SectionPrefetcher s;
  return s;
}

uint32_t SectionPrefetcher::buildsDone() { return s_buildsDone; }

// Port bridge for `wodle stat` — avoids a port→vendor/src header dependency.
uint32_t wodlePrefetchBuildsDone() { return s_buildsDone; }

bool SectionPrefetcher::ensureThread() {
  if (task_) return true;
  if (!mutex_) mutex_ = xSemaphoreCreateMutex();
  if (!mutex_) return false;
  // Stack is doubled by the shim; createSectionFile + the HTML parser are stack-hungry.
  if (xTaskCreate(&SectionPrefetcher::trampoline, "epubprefetch", 8192, this, 1, &task_) != pdPASS) {
    task_ = nullptr;
    LOG_ERR("PFT", "FAILED to init prefetch thread");
    return false;
  }
  return true;
}

bool SectionPrefetcher::beginBook(GfxRenderer& renderer, const std::string& filepath) {
  // Load the worker's OWN Epub outside the lock (SD I/O), then swap it in. Reads the
  // on-SD metadata cache the UI thread already built, so this is cheap.
  auto e = std::make_shared<Epub>(filepath, CACHE_DIR);
  if (!e->load()) {
    LOG_ERR("PFT", "prefetch Epub load failed: %s", filepath.c_str());
    return false;
  }
  if (!mutex_) mutex_ = xSemaphoreCreateMutex();
  {
    Guard g(mutex_);
    renderer_ = &renderer;
    epub_ = std::move(e);
    pendingSpine_ = -1;
    lastBuiltSpine_ = -1;
  }
  if (!ensureThread()) {
    Guard g(mutex_);
    epub_.reset();
    active_ = false;
    return false;
  }
  active_ = true;
  LOG_DBG("PFT", "prefetch attached: %s", filepath.c_str());
  return true;
}

void SectionPrefetcher::endBook() {
  if (!mutex_) return;
  Guard g(mutex_);
  epub_.reset();
  pendingSpine_ = -1;
  active_ = false;
}

void SectionPrefetcher::request(int spineIndex, const Layout& layout) {
  if (!active_ || !task_) return;
  {
    Guard g(mutex_);
    if (!epub_) return;
    pendingSpine_ = spineIndex;
    pendingLayout_ = layout;
  }
  xTaskNotifyGive(task_);
}

void SectionPrefetcher::trampoline(void* self) {
  static_cast<SectionPrefetcher*>(self)->workerLoop();
}

void SectionPrefetcher::workerLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Snapshot the request + the book's Epub/renderer under the lock. Holding a
    // shared_ptr copy keeps the Epub alive even if the book closes mid-build.
    std::shared_ptr<Epub> epub;
    GfxRenderer* renderer = nullptr;
    int spine = -1;
    Layout lay;
    {
      Guard g(mutex_);
      if (epub_ && renderer_ && pendingSpine_ >= 0 && pendingSpine_ != lastBuiltSpine_) {
        epub = epub_;
        renderer = renderer_;
        spine = pendingSpine_;
        lay = pendingLayout_;
      }
    }
    if (!epub || spine < 0 || !renderer) continue;

    Section section(epub, spine, *renderer);
    // If the cache already exists (the UI built it first, or a prior round did), skip.
    if (!section.loadSectionFile(lay.fontId, lay.lineCompression, lay.extraParagraphSpacing, lay.paragraphAlignment,
                                 lay.viewportWidth, lay.viewportHeight, lay.hyphenationEnabled, lay.embeddedStyle,
                                 lay.imageRendering, lay.focusReadingEnabled)) {
      LOG_DBG("PFT", "prefetch building spine %d", spine);
      if (!section.createSectionFile(lay.fontId, lay.lineCompression, lay.extraParagraphSpacing, lay.paragraphAlignment,
                                     lay.viewportWidth, lay.viewportHeight, lay.hyphenationEnabled, lay.embeddedStyle,
                                     lay.imageRendering, lay.focusReadingEnabled)) {
        LOG_ERR("PFT", "prefetch build failed for spine %d", spine);
      } else {
        s_buildsDone++;
      }
    }
    {
      Guard g(mutex_);
      lastBuiltSpine_ = spine;
    }
  }
}

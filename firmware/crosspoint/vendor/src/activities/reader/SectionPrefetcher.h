#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <memory>
#include <string>

class Epub;
class GfxRenderer;

// WODLE-PORT: background EPUB section-cache prefetch.
//
// A persistent worker thread builds the *next* section's page cache (.bin) off the
// UI thread so crossing a chapter boundary doesn't stall. It uses its OWN isolated
// Epub instance, so there is no shared mutable in-memory state with the UI thread:
//   - SD access is serialized by HalStorage's recursive mutex,
//   - the renderer's fontMap is read-only during a reading session,
//   - createSectionFile() publishes the .bin atomically (temp + rename).
//
// It is a PURE optimization: the reader always validates the cache on real load and
// falls back to a synchronous build, so any worker failure or race only ever costs a
// redundant build — never a wrong render or corruption. Singleton: one worker for the
// whole app; the active book is swapped per open.
class SectionPrefetcher {
 public:
  struct Layout {
    int fontId = 0;
    float lineCompression = 1.0f;
    bool extraParagraphSpacing = false;
    uint8_t paragraphAlignment = 0;
    uint16_t viewportWidth = 0;
    uint16_t viewportHeight = 0;
    bool hyphenationEnabled = false;
    bool embeddedStyle = false;
    uint8_t imageRendering = 0;
    bool focusReadingEnabled = false;
  };

  static SectionPrefetcher& instance();

  // Attach to a freshly opened book: open an isolated Epub for `filepath` and start the
  // worker thread on first use. Returns false if the Epub can't load or the thread can't
  // start (the reader then prefetches inline). `renderer` is the shared global renderer.
  bool beginBook(GfxRenderer& renderer, const std::string& filepath);

  // Detach the current book; the worker stops servicing requests until the next book.
  void endBook();

  bool active() const { return active_; }

  // Diagnostics: number of section caches the worker has built this session
  // (surfaced via `wodle stat` to confirm the prefetch thread is doing work).
  static uint32_t buildsDone();

  // Request a background build of `spineIndex`. Coalesces repeats; the worker skips a
  // spine it already built for the current book.
  void request(int spineIndex, const Layout& layout);

 private:
  SectionPrefetcher() = default;
  SectionPrefetcher(const SectionPrefetcher&) = delete;
  SectionPrefetcher& operator=(const SectionPrefetcher&) = delete;

  static void trampoline(void* self);
  void workerLoop();
  bool ensureThread();

  GfxRenderer* renderer_ = nullptr;       // guarded by mutex_
  std::shared_ptr<Epub> epub_;            // guarded by mutex_
  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  bool active_ = false;

  // Guarded by mutex_:
  int pendingSpine_ = -1;
  Layout pendingLayout_{};
  int lastBuiltSpine_ = -1;
};

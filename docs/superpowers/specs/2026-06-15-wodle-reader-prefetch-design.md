# Design: background EPUB section-prefetch (reader responsiveness)

Date: 2026-06-15
Repo: `~/w/_hw/rcwodle` — crosspoint EPUB reader on SF32LB525 (RT-Thread)
Status: approved, implementing (Phase 2; Phase 1 flash-fonts shipped)

## Problem

`EpubReaderActivity::silentIndexNextChapterIfNeeded()` already prebuilds the **next** section's
page cache when the user reaches the penultimate page — but it runs **synchronously on the main/UI
thread** (`nextSection.createSectionFile(...)`, EpubReaderActivity.cpp:940). For a large next
chapter that means a multi-second input freeze while the penultimate page is on screen. Move that
build to a background worker so the UI stays responsive.

## Safety analysis (the load-bearing part)

`createSectionFile()` is **layout-only**: it unzips the chapter HTML, parses it
(`ChapterHtmlSlimParser`), paginates using **read-only** font metrics (`renderer.getLineHeight()`
reads the immutable `fontMap`), and serializes pages to an SD `.bin`. It does **not** draw, touch
the framebuffer, or call `ensureSdCardFontReady()` (no mutation of `sdCardFonts_`). Verified shared
resources:

- **SD/FS**: every `HalStorage` op is wrapped in a recursive `StorageMutex` (HalStorage.cpp) and
  RT-Thread DFS adds FS-level locking → concurrent SD access is **safe**.
- **fontMap**: immutable during a reading session (rebuilt only on a font-setting change, which
  happens outside the reader) → read-only access is **safe**.
- **Epub object**: shared `epub` pointer is the one real hazard (cssParser / bookMetadataCache /
  per-call ZipFile handles). **Eliminated by isolation** — the worker gets its **own `Epub`
  instance** for the same file, so there is *no shared mutable in-memory state* between threads.
- **PSRAM bump allocator** (`WodlePsram::alloc`) is not locked, but it's init-only; the section
  build uses the libc heap (malloc/free), so no concurrent PSRAM alloc.

## Design: optimistic isolated worker

`SectionPrefetcher` (new): an RT-Thread worker + request mailbox + semaphore.

- Owns its **own `Epub`** (constructed from the book filepath + cache dir, `load()`ed once when the
  reader opens the book). Same `cachePath` (hash of filepath) → writes the same `.bin` cache the
  main thread reads.
- API: `begin(filepath, cacheDir)`, `request(spineIndex, layoutParams)`, `stop()`.
- Worker loop: wait on semaphore → if a fresh request → build that section's cache with its own Epub
  → loop. Dedups (skips if the requested spine == last built). Low priority (below main + render).
- `silentIndexNextChapterIfNeeded()` posts a `request(nextSpineIndex, params)` instead of building
  synchronously.

**Why it's safe to ship:** the worker is a *pure optimization*. The main thread, on actually crossing
to the next section, still calls `loadSectionFile()` which validates version + font-hash + layout
params; if the worker hasn't finished (or produced a stale/mismatched cache after a settings change),
validation fails and the main thread builds synchronously — exactly today's behavior. A worker bug
therefore degrades to the status quo, never a wrong render. The only new crash surface is the worker
thread itself: mitigated by ample stack (8 KB) and reusing the proven `createSectionFile` path.

## Lifecycle

- Created when `EpubReaderActivity` opens a book; `stop()` (signal + join) in its destructor / on
  book close. Settings changes that invalidate caches recreate the reader → worker restarts.

## Verification

1. Host: firmware builds; unit-test `SectionPrefetcher`'s request/dedup state machine with a mock
   builder (no engine needed).
2. HIL: a **temporary serial-logging build** (`-DENABLE_SERIAL_LOG -DLOG_LEVEL=2`) flashed to the
   device, read a multi-chapter EPUB across section boundaries, confirm the worker logs cache builds
   with no errors and page turns stay responsive; then ship the clean (no-log) build.

## Out of scope

- TXT worker thread (TXT indexing already yields cooperatively via `vTaskDelay`). - BMC→PSRAM. -
  Prefetching more than one section ahead.

## As-built (2026-06-15)

- `SectionPrefetcher` singleton (`vendor/src/activities/reader/`): persistent worker, isolated Epub,
  mutex-guarded request slot, coalesced + deduped by spine. Wired into `EpubReaderActivity`
  (`onEnter`→`beginBook`, `onExit`→`endBook`, `silentIndexNextChapterIfNeeded`→`request` with sync
  fallback when inactive). `SHIM_TASK_PRIO=20` keeps the worker below the main thread (15), so input
  preempts it.
- **Keystone:** `Section::createSectionFile()` now writes the cache to `<bin>.tmp` and atomically
  renames on success (`Section.cpp`) — readers never see a partial `.bin`, so the prefetcher is
  safe-by-construction.
- **Diagnostic:** `wodle stat` gains `prefetch=N` (builds done this session) via a free-function
  bridge (`wodlePrefetchBuildsDone`), so the worker can be verified on the production build without
  serial logging.
- **Verified:** 214/214 host tests pass (incl. the EPUB cache suite, exercising the atomic write);
  SCons firmware build OK (3.50 MB).
- **HIL pending:** the device booted into the app after the Phase-1 flash, and there's no software
  path back to recovery, so Phase 2 isn't flashed yet. To verify: re-enter recovery (power+down
  10 s), `wodle_flash.py write main.bin --addr 0x12218000`, open a multi-chapter **EPUB**, page
  across a section boundary, and confirm `wodle stat` shows `prefetch` incrementing with no crash.

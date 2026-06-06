// WODLE-PORT (rocrp fork a1f5077, adapted): GIF decode for EPUB images.
// Differences from the fork version, which predates upstream d9bcef7:
// - FsFile -> HalFile, drawPixelWithRenderMode -> DirectPixelWriter
// - full-image cache buffer -> streaming PixelCache band, with two GIF-only
//   guards: caching needs a full-canvas first frame (offset frames would
//   leave black gap rows in the cache where the screen shows white), and
//   out-of-raster-order rows (interlaced GIFs) abort the cache stream
//   instead of writing before the band start.
// - transparent pixels still skip the framebuffer (page background shows
//   through) but write WHITE into the cache, which replays over a black-
//   initialized band.
#include "GifToFramebufferConverter.h"

#include <AnimatedGIF.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <new>

#include "DirectPixelWriter.h"
#include "DitherUtils.h"
#include "PixelCache.h"

namespace {

// Context struct passed through AnimatedGIF callbacks to avoid global mutable state.
// The draw callback receives this via pDraw->pUser (set by playFrame()).
// The file I/O callbacks receive the HalFile* via pFile->fHandle (set by gifOpen()).
struct GifContext {
  GfxRenderer* renderer{nullptr};
  const RenderConfig* config{nullptr};
  int screenWidth{0};
  int screenHeight{0};

  // Scaling state
  float scale{1.f};
  int srcWidth{0};
  int srcHeight{0};
  int dstWidth{0};
  int dstHeight{0};
  int lastDstY{-1};  // Track last rendered destination Y to avoid duplicates

  PixelCache cache;
  bool caching{false};
  bool firstLine{true};
};

// File I/O callbacks use pFile->fHandle to access the HalFile*,
// avoiding the need for global file state.
void* gifOpen(const char* filename, int32_t* size) {
  HalFile* f = new HalFile();
  if (!Storage.openFileForRead("GIF", std::string(filename), *f)) {
    delete f;
    return nullptr;
  }
  *size = f->size();
  return f;
}

void gifClose(void* handle) {
  HalFile* f = reinterpret_cast<HalFile*>(handle);
  if (f) {
    f->close();
    delete f;
  }
}

int32_t gifRead(GIFFILE* pFile, uint8_t* pBuf, int32_t len) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return 0;
  int32_t bytesRead = f->read(pBuf, len);
  if (bytesRead < 0) return 0;
  pFile->iPos += bytesRead;
  return bytesRead;
}

int32_t gifSeek(GIFFILE* pFile, int32_t pos) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return -1;
  if (!f->seek(pos)) return -1;
  pFile->iPos = pos;
  return pos;
}

// AnimatedGIF object is ~26 KB due to internal LZW tables and line buffer.
// Heap-allocate on demand so memory is only used during active decode.
constexpr size_t GIF_DECODER_APPROX_SIZE = 28 * 1024;
constexpr size_t MIN_FREE_HEAP_FOR_GIF = GIF_DECODER_APPROX_SIZE + 16 * 1024;

void gifDrawCallback(GIFDRAW* pDraw) {
  GifContext* ctx = reinterpret_cast<GifContext*>(pDraw->pUser);
  if (!ctx || !ctx->config || !ctx->renderer) return;

  // Caching can only replay correctly when the first frame covers the whole
  // canvas: an offset frame leaves band gap rows (black) where the screen
  // shows the page background (white). Decide on the first delivered line.
  if (ctx->firstLine) {
    ctx->firstLine = false;
    if (ctx->caching && (pDraw->iX != 0 || pDraw->iY != 0 || pDraw->iWidth != ctx->srcWidth ||
                         pDraw->iHeight != ctx->srcHeight)) {
      LOG_DBG("GIF", "First frame is not full-canvas (%d,%d %dx%d) - caching disabled", pDraw->iX, pDraw->iY,
              pDraw->iWidth, pDraw->iHeight);
      ctx->cache.abort();
      ctx->caching = false;
    }
  }

  // Canvas Y position for this line (frame offset + line within frame)
  int canvasY = pDraw->iY + pDraw->y;

  // Calculate destination Y with scaling
  int dstY = (int)(canvasY * ctx->scale);

  // Skip if we already rendered this destination row (multiple source rows map to same dest)
  if (dstY == ctx->lastDstY) return;
  ctx->lastDstY = dstY;

  // Check bounds
  if (dstY >= ctx->dstHeight) return;

  int outY = ctx->config->y + dstY;
  if (outY >= ctx->screenHeight) return;

  // Get palette and transparency info
  uint8_t* pPixels = pDraw->pPixels;
  uint8_t* palette24 = pDraw->pPalette24;
  bool hasTransparency = pDraw->ucHasTransparency;
  uint8_t transparentIdx = pDraw->ucTransparent;
  int frameX = pDraw->iX;
  int frameWidth = pDraw->iWidth;

  int dstWidth = ctx->dstWidth;
  int srcWidth = ctx->srcWidth;
  int outXBase = ctx->config->x;
  int screenWidth = ctx->screenWidth;
  bool useDithering = ctx->config->useDithering;
  bool caching = ctx->caching;

  // Pre-compute orientation and render-mode state once per row
  DirectPixelWriter pw;
  pw.init(*ctx->renderer);
  pw.beginRow(outY);

  // Reposition the streaming cache band (see PngToFramebufferConverter).
  // Interlaced GIFs deliver lines OUT of raster order — a row above the band
  // start would underflow the band buffer, so caching aborts instead.
  DirectCacheWriter cw;
  if (caching) {
    if (dstY < ctx->cache.bandStart) {
      LOG_DBG("GIF", "Out-of-order row %d < band %d (interlaced?) - caching disabled", dstY, ctx->cache.bandStart);
      ctx->cache.abort();
      caching = false;
      ctx->caching = false;
    } else if (!ctx->cache.advanceTo(dstY)) {
      caching = false;
      ctx->caching = false;
    } else {
      cw.init(ctx->cache.buffer, ctx->cache.bytesPerRow, ctx->cache.bandRows, ctx->cache.originX);
      cw.beginRow(outY, ctx->config->y + ctx->cache.bandStart);
    }
  }

  // Render scaled row using Bresenham-style integer stepping (no floating-point division)
  int srcX = 0;
  int error = 0;

  for (int dstX = 0; dstX < dstWidth; dstX++) {
    int outX = outXBase + dstX;
    if (outX < screenWidth) {
      // Check if source canvas X falls within the frame region
      int framePixelX = srcX - frameX;
      bool opaque = false;
      uint8_t ditheredGray = 3;  // white: transparent / outside the frame
      if (framePixelX >= 0 && framePixelX < frameWidth) {
        uint8_t idx = pPixels[framePixelX];
        if (!hasTransparency || idx != transparentIdx) {
          opaque = true;
          uint8_t* p = &palette24[idx * 3];
          uint8_t gray = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
          if (useDithering) {
            ditheredGray = applyBayerDither4Level(gray, outX, outY);
          } else {
            ditheredGray = gray / 85;
            if (ditheredGray > 3) ditheredGray = 3;
          }
        }
      }
      // Transparent pixels leave the framebuffer alone (page background shows
      // through) but must still land in the cache as white.
      if (opaque) pw.writePixel(outX, ditheredGray);
      if (caching) cw.writePixel(outX, ditheredGray);
    }

    // Bresenham-style stepping: advance srcX based on ratio srcWidth/dstWidth
    error += srcWidth;
    while (error >= dstWidth) {
      error -= dstWidth;
      srcX++;
    }
  }
}

}  // namespace

bool GifToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_GIF) {
    LOG_ERR("GIF", "Not enough heap for GIF decoder (%u free, need %u)", (unsigned)freeHeap,
            (unsigned)MIN_FREE_HEAP_FOR_GIF);
    return false;
  }

  std::unique_ptr<AnimatedGIF> gif(new (std::nothrow) AnimatedGIF());
  if (!gif) {
    LOG_ERR("GIF", "Failed to allocate GIF decoder for dimensions");
    return false;
  }

  gif->begin(GIF_PALETTE_RGB888);

  int rc = gif->open(imagePath.c_str(), gifOpen, gifClose, gifRead, gifSeek, nullptr);
  if (rc != 1) {
    LOG_ERR("GIF", "Failed to open GIF for dimensions: %s", imagePath.c_str());
    return false;
  }
  const ScopedCleanup cleanup{[&gif]() { gif->close(); }};

  out.width = gif->getCanvasWidth();
  out.height = gif->getCanvasHeight();
  return true;
}

bool GifToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                    const RenderConfig& config) {
  LOG_DBG("GIF", "Decoding GIF: %s", imagePath.c_str());

  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_GIF) {
    LOG_ERR("GIF", "Not enough heap for GIF decoder (%u free, need %u)", (unsigned)freeHeap,
            (unsigned)MIN_FREE_HEAP_FOR_GIF);
    return false;
  }

  // Heap-allocate GIF decoder (~26 KB) - freed at end of function
  std::unique_ptr<AnimatedGIF> gif(new (std::nothrow) AnimatedGIF());
  if (!gif) {
    LOG_ERR("GIF", "Failed to allocate GIF decoder");
    return false;
  }

  gif->begin(GIF_PALETTE_RGB888);

  GifContext ctx;
  ctx.renderer = &renderer;
  ctx.config = &config;
  ctx.screenWidth = renderer.getScreenWidth();
  ctx.screenHeight = renderer.getScreenHeight();

  int rc = gif->open(imagePath.c_str(), gifOpen, gifClose, gifRead, gifSeek, gifDrawCallback);
  if (rc != 1) {
    LOG_ERR("GIF", "Failed to open GIF: %s", imagePath.c_str());
    return false;
  }
  const ScopedCleanup cleanup{[&gif]() { gif->close(); }};

  ctx.srcWidth = gif->getCanvasWidth();
  ctx.srcHeight = gif->getCanvasHeight();

  if (!validateImageDimensions(ctx.srcWidth, ctx.srcHeight, "GIF")) {
    return false;
  }

  // Calculate output dimensions (same logic as PNG converter)
  if (config.useExactDimensions && config.maxWidth > 0 && config.maxHeight > 0) {
    ctx.dstWidth = config.maxWidth;
    ctx.dstHeight = config.maxHeight;
    ctx.scale = (float)ctx.dstWidth / ctx.srcWidth;
  } else {
    float scaleX = (float)config.maxWidth / ctx.srcWidth;
    float scaleY = (float)config.maxHeight / ctx.srcHeight;
    ctx.scale = (scaleX < scaleY) ? scaleX : scaleY;
    if (ctx.scale > 1.0f) ctx.scale = 1.0f;  // Don't upscale

    ctx.dstWidth = (int)(ctx.srcWidth * ctx.scale);
    ctx.dstHeight = (int)(ctx.srcHeight * ctx.scale);
  }
  ctx.lastDstY = -1;  // Reset row tracking

  LOG_DBG("GIF", "GIF %dx%d -> %dx%d (scale %.2f)", ctx.srcWidth, ctx.srcHeight, ctx.dstWidth, ctx.dstHeight,
          ctx.scale);

  // Stream the pixel cache to disk; the callback delivers one line at a time.
  // Upscales (exact-dimensions only) deliver SPARSE destination rows — the
  // screen leaves the gaps untouched but the cache band would zero-fill them
  // black, so caching is skipped entirely for scale > 1.
  ctx.caching = !config.cachePath.empty() && ctx.scale <= 1.0f;
  if (!config.cachePath.empty() && !ctx.caching) {
    LOG_DBG("GIF", "Upscaled decode (%.2f) - caching disabled", ctx.scale);
  }
  if (ctx.caching) {
    if (!ctx.cache.begin(config.cachePath, ctx.dstWidth, ctx.dstHeight, config.x, config.y, 1)) {
      LOG_ERR("GIF", "Failed to start cache stream, continuing without caching");
      ctx.caching = false;
    }
  }

  unsigned long decodeStart = millis();
  int delayMs = 0;
  rc = gif->playFrame(false, &delayMs, &ctx);  // first frame only (static decode)
  unsigned long decodeTime = millis() - decodeStart;

  if (rc < 0) {
    LOG_ERR("GIF", "Decode failed: %d", gif->getLastError());
    if (ctx.caching) ctx.cache.abort();
    return false;
  }

  LOG_DBG("GIF", "GIF decoding complete - render time: %lu ms", decodeTime);

  // The cache may only be finalized when every destination row was actually
  // delivered — finalize() zero-fills (black) whatever is missing, which a
  // .pxc replay would paint over the white page (codex review finding).
  if (ctx.caching && ctx.lastDstY < ctx.dstHeight - 1) {
    LOG_DBG("GIF", "Rows %d..%d never delivered - dropping cache", ctx.lastDstY + 1, ctx.dstHeight - 1);
    ctx.cache.abort();
    ctx.caching = false;
  }

  // Finalize the streamed cache (caching may have been cleared mid-decode).
  if (ctx.caching) {
    ctx.cache.finalize();
  }

  return true;
}

bool GifToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasGifExtension(extension);
}

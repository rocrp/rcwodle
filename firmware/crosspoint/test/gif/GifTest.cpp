// GifToFramebufferConverter host tests over committed fixtures:
//   gif_stripes.gif      40x32, vertical 10px black/white stripes
//   gif_transparent.gif  16x16, transparent background, black 8x8 square at (4,4)
//
// Framebuffer assertions compare the decoder's output against the same
// pattern hand-drawn with GfxRenderer::drawPixel — orientation-agnostic and
// pins the decoder to the renderer's own coordinate system.
#include <GfxRenderer.h>
#include <GifToFramebufferConverter.h>
#include <HalDisplay.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

HalDisplay display;
GfxRenderer renderer(display);

class GifEnv : public ::testing::Environment {
 public:
  void SetUp() override {
    display.begin();
    renderer.begin();
  }
};
const auto* const kEnv = ::testing::AddGlobalTestEnvironment(new GifEnv());

std::string fixture(const char* name) { return std::string(FIXTURE_DIR "/") + name; }

std::vector<uint8_t> snapshotFb() {
  std::vector<uint8_t> fb(HalDisplay::BUFFER_SIZE);
  memcpy(fb.data(), display.getFrameBuffer(), fb.size());
  return fb;
}

// Stripe fixture pixel model: black when (x/10) is even.
bool stripeIsBlack(int x) { return (x / 10) % 2 == 0; }

constexpr int kStripesW = 40;
constexpr int kStripesH = 32;

RenderConfig exactConfig(int x, int y, int w, int h) {
  RenderConfig cfg;
  cfg.x = x;
  cfg.y = y;
  cfg.maxWidth = w;
  cfg.maxHeight = h;
  cfg.useExactDimensions = true;
  cfg.useDithering = false;  // deterministic gray quantization
  return cfg;
}

TEST(Gif, Dimensions) {
  ImageDimensions dims{};
  ASSERT_TRUE(GifToFramebufferConverter::getDimensionsStatic(fixture("gif_stripes.gif"), dims));
  EXPECT_EQ(dims.width, kStripesW);
  EXPECT_EQ(dims.height, kStripesH);
}

TEST(Gif, SupportsFormat) {
  EXPECT_TRUE(GifToFramebufferConverter::supportsFormat(".gif"));
  EXPECT_FALSE(GifToFramebufferConverter::supportsFormat(".png"));
}

TEST(Gif, DecodeMatchesHandDrawnPattern) {
  const int x0 = 16, y0 = 24;
  GifToFramebufferConverter conv;

  renderer.clearScreen();
  ASSERT_TRUE(conv.decodeToFramebuffer(fixture("gif_stripes.gif"), renderer,
                                       exactConfig(x0, y0, kStripesW, kStripesH)));
  const auto decoded = snapshotFb();

  // Hand-draw the same pattern: BW mode draws gray<3 as black, leaves white alone.
  renderer.clearScreen();
  for (int y = 0; y < kStripesH; y++) {
    for (int x = 0; x < kStripesW; x++) {
      if (stripeIsBlack(x)) renderer.drawPixel(x0 + x, y0 + y, true);
    }
  }
  const auto expected = snapshotFb();

  EXPECT_EQ(decoded, expected);

  // Sanity: the image actually contains ink (catches an all-white false pass).
  size_t ink = 0;
  for (uint8_t b : decoded) ink += 8 - __builtin_popcount(b);
  EXPECT_EQ(ink, (size_t)(kStripesW / 2) * kStripesH);
}

TEST(Gif, CacheStreamsCorrectPackedRows) {
  const std::string cachePath = "/tmp/cp_gif_test_cache.pxc";
  remove(cachePath.c_str());

  auto cfg = exactConfig(0, 0, kStripesW, kStripesH);
  cfg.cachePath = cachePath;

  renderer.clearScreen();
  GifToFramebufferConverter conv;
  ASSERT_TRUE(conv.decodeToFramebuffer(fixture("gif_stripes.gif"), renderer, cfg));

  FILE* f = fopen(cachePath.c_str(), "rb");
  ASSERT_NE(f, nullptr);
  uint16_t w = 0, h = 0;
  ASSERT_EQ(fread(&w, 2, 1, f), 1u);
  ASSERT_EQ(fread(&h, 2, 1, f), 1u);
  EXPECT_EQ(w, kStripesW);
  EXPECT_EQ(h, kStripesH);

  const int bytesPerRow = (kStripesW + 3) / 4;
  std::vector<uint8_t> row(bytesPerRow);

  // Expected packed row: 2 bits per pixel MSB-first, black=0b00, white=0b11.
  std::vector<uint8_t> expectedRow(bytesPerRow, 0);
  for (int x = 0; x < kStripesW; x++) {
    const uint8_t v = stripeIsBlack(x) ? 0 : 3;
    expectedRow[x >> 2] |= v << (6 - (x & 3) * 2);
  }

  for (int y = 0; y < kStripesH; y++) {
    ASSERT_EQ(fread(row.data(), 1, row.size(), f), row.size()) << "row " << y;
    EXPECT_EQ(row, expectedRow) << "row " << y;
  }
  // Exactly header + rows — nothing trailing.
  EXPECT_EQ(fgetc(f), EOF);
  fclose(f);
  remove(cachePath.c_str());
}

TEST(Gif, TransparentPixelsCacheWhiteAndRenderOpaqueOnly) {
  const std::string cachePath = "/tmp/cp_gif_test_transparent.pxc";
  remove(cachePath.c_str());

  auto cfg = exactConfig(8, 8, 16, 16);
  cfg.cachePath = cachePath;

  renderer.clearScreen();
  GifToFramebufferConverter conv;
  ASSERT_TRUE(conv.decodeToFramebuffer(fixture("gif_transparent.gif"), renderer, cfg));
  const auto decoded = snapshotFb();

  // Screen: only the opaque 8x8 square may have drawn ink.
  renderer.clearScreen();
  for (int y = 4; y < 12; y++) {
    for (int x = 4; x < 12; x++) {
      renderer.drawPixel(8 + x, 8 + y, true);
    }
  }
  EXPECT_EQ(decoded, snapshotFb());

  // Cache: transparent pixels must be WHITE (0b11), the square black (0b00) —
  // a replay over the black-initialized band would otherwise paint the page.
  FILE* f = fopen(cachePath.c_str(), "rb");
  ASSERT_NE(f, nullptr);
  uint16_t w = 0, h = 0;
  ASSERT_EQ(fread(&w, 2, 1, f), 1u);
  ASSERT_EQ(fread(&h, 2, 1, f), 1u);
  EXPECT_EQ(w, 16);
  EXPECT_EQ(h, 16);
  const int bytesPerRow = (16 + 3) / 4;
  std::vector<uint8_t> row(bytesPerRow);
  for (int y = 0; y < 16; y++) {
    ASSERT_EQ(fread(row.data(), 1, row.size(), f), row.size());
    for (int x = 0; x < 16; x++) {
      const uint8_t v = (row[x >> 2] >> (6 - (x & 3) * 2)) & 0x03;
      const bool inSquare = (x >= 4 && x < 12 && y >= 4 && y < 12);
      EXPECT_EQ(v, inSquare ? 0 : 3) << "pixel " << x << "," << y;
    }
  }
  fclose(f);
  remove(cachePath.c_str());
}

TEST(Gif, InterlacedRendersButAbortsCache) {
  // Interlaced GIFs deliver rows out of raster order; the streaming cache
  // can't replay that, so caching must abort (file removed) while the screen
  // render — which doesn't depend on row order — still completes correctly.
  const std::string cachePath = "/tmp/cp_gif_test_interlaced.pxc";
  remove(cachePath.c_str());

  const int x0 = 4, y0 = 4;
  auto cfg = exactConfig(x0, y0, kStripesW, kStripesH);
  cfg.cachePath = cachePath;

  renderer.clearScreen();
  GifToFramebufferConverter conv;
  ASSERT_TRUE(conv.decodeToFramebuffer(fixture("gif_interlaced.gif"), renderer, cfg));
  const auto decoded = snapshotFb();

  FILE* f = fopen(cachePath.c_str(), "rb");
  EXPECT_EQ(f, nullptr) << "interlaced cache must be aborted and removed";
  if (f) fclose(f);

  renderer.clearScreen();
  for (int y = 0; y < kStripesH; y++) {
    for (int x = 0; x < kStripesW; x++) {
      if (stripeIsBlack(x)) renderer.drawPixel(x0 + x, y0 + y, true);
    }
  }
  EXPECT_EQ(decoded, snapshotFb());
}

TEST(Gif, DownscaleDecodes) {
  // Fit-in-box scaling path (no exact dimensions): 40x32 into 20x20 box.
  RenderConfig cfg;
  cfg.x = 0;
  cfg.y = 0;
  cfg.maxWidth = 20;
  cfg.maxHeight = 20;
  cfg.useDithering = false;

  renderer.clearScreen();
  GifToFramebufferConverter conv;
  ASSERT_TRUE(conv.decodeToFramebuffer(fixture("gif_stripes.gif"), renderer, cfg));

  // 20x16 output, stripes halved to 5px: half the pixels are ink.
  size_t ink = 0;
  for (uint8_t b : snapshotFb()) ink += 8 - __builtin_popcount(b);
  EXPECT_EQ(ink, (size_t)(20 / 2) * 16);
}

}  // namespace

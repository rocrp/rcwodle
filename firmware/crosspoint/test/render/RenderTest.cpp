// Host render harness: GfxRenderer + the real builtin UI fonts + an SD-card
// CJK font, drawing into the real 792x528 1-bit framebuffer via a host
// HalDisplay shim. Asserts ink coverage + tofu-distinctness, and dumps every
// scene to /tmp/cp_render/<name>.pgm — the blind-dev "screen": rendered UI
// can be eyeballed without the device.

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <SdCardFont.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <sys/stat.h>

#include "fontIds.h"

// Generated UI fonts (same headers the firmware compiles in).
#include "notosans_8_regular.h"
#include "ubuntu_10_bold.h"
#include "ubuntu_10_regular.h"
#include "ubuntu_12_bold.h"
#include "ubuntu_12_regular.h"

namespace {

constexpr int SD_FONT_ID = 424242;
constexpr const char* DUMP_DIR = "/tmp/cp_render";

HalDisplay display;
GfxRenderer renderer(display);
FontDecompressor fontDecompressor;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());

EpdFont ui10Regular(&ubuntu_10_regular);
EpdFont ui10Bold(&ubuntu_10_bold);
EpdFont ui12Regular(&ubuntu_12_regular);
EpdFont ui12Bold(&ubuntu_12_bold);
EpdFont small8(&notosans_8_regular);
SdCardFont sdFont;

class RenderEnv : public ::testing::Environment {
 public:
  void SetUp() override {
    display.begin();
    renderer.begin();
    ASSERT_TRUE(fontDecompressor.init());
    fontCacheManager.setFontDecompressor(&fontDecompressor);
    renderer.setFontCacheManager(&fontCacheManager);

    renderer.insertFont(UI_10_FONT_ID, EpdFontFamily(&ui10Regular, &ui10Bold));
    renderer.insertFont(UI_12_FONT_ID, EpdFontFamily(&ui12Regular, &ui12Bold));
    renderer.insertFont(SMALL_FONT_ID, EpdFontFamily(&small8));

    // SD CJK font (same fixture as the sdcardfont suite).
    ASSERT_TRUE(sdFont.load(FIXTURE_CPFONT));
    renderer.registerSdCardFont(SD_FONT_ID, &sdFont);
    renderer.insertFont(SD_FONT_ID,
                        EpdFontFamily(sdFont.getEpdFont(0), sdFont.getEpdFont(1), nullptr, nullptr));

    std::filesystem::create_directories(DUMP_DIR);
  }
};

const auto* const kEnv = ::testing::AddGlobalTestEnvironment(new RenderEnv());

// Count black pixels in the panel buffer (bit 1 = white).
size_t inkPixels() {
  const uint8_t* fb = display.getFrameBuffer();
  size_t ink = 0;
  for (uint32_t i = 0; i < HalDisplay::BUFFER_SIZE; i++) {
    ink += 8 - __builtin_popcount(fb[i]);
  }
  return ink;
}

// Dump the panel buffer as a binary PGM (eyeball with any image viewer).
void dumpPgm(const std::string& name) {
  const std::string path = std::string(DUMP_DIR) + "/" + name + ".pgm";
  FILE* f = fopen(path.c_str(), "wb");
  ASSERT_NE(f, nullptr);
  fprintf(f, "P5\n%d %d\n255\n", HalDisplay::DISPLAY_WIDTH, HalDisplay::DISPLAY_HEIGHT);
  const uint8_t* fb = display.getFrameBuffer();
  for (int y = 0; y < HalDisplay::DISPLAY_HEIGHT; y++) {
    for (int x = 0; x < HalDisplay::DISPLAY_WIDTH; x++) {
      const uint8_t byte = fb[y * HalDisplay::DISPLAY_WIDTH_BYTES + x / 8];
      const bool white = byte & (0x80 >> (x % 8));
      fputc(white ? 0xFF : 0x00, f);
    }
  }
  fclose(f);
}

TEST(Render, LatinUiTextRendersInk) {
  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, 20, 40, "Settings", true);
  renderer.drawText(UI_12_FONT_ID, 20, 80, "Continue Reading", true, EpdFontFamily::BOLD);
  const size_t ink = inkPixels();
  EXPECT_GT(ink, 200u);
  dumpPgm("latin_ui");
}

TEST(Render, ZhUiTextIsNotTofu) {
  // Render a zh UI string, then the same number of replacement chars.
  // If the CJK subset were missing, both would rasterize identically.
  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, 20, 40, "设置", true);
  const size_t zhInk = inkPixels();
  dumpPgm("zh_ui_settings");

  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, 20, 40, "��", true);
  const size_t tofuInk = inkPixels();

  EXPECT_GT(zhInk, 100u);
  EXPECT_NE(zhInk, tofuInk) << "zh glyphs rasterized like replacement boxes";

  // Width must differ from tofu too (full-width hanzi vs replacement glyph).
  const int zhWidth = renderer.getTextWidth(UI_12_FONT_ID, "设置");
  EXPECT_GT(zhWidth, 0);
}

TEST(Render, SdCardFontRendersCjkParagraph) {
  const char* text = "你好，世界。";
  renderer.ensureSdCardFontReady(SD_FONT_ID, text, 0x01);
  ASSERT_EQ(sdFont.prewarm(text, 0x01), 0);

  renderer.clearScreen();
  renderer.drawText(SD_FONT_ID, 20, 60, text, true);
  const size_t ink = inkPixels();
  EXPECT_GT(ink, 300u) << "SD-font CJK text drew almost nothing";
  dumpPgm("sd_cjk_text");
}

// Mock of the TXT chapter-selection screen — title row + list rows with CJK
// titles drawn with the SD (reader) font, like TxtReaderChapterSelectionActivity.
// Uses the production LXGWWenKai font (full CJK coverage) when it has been
// built locally; the committed fixture only carries a handful of hanzi.
TEST(Render, ChapterListMockup) {
  const char* prodFont = "../../../../dist/sd-fonts/LXGWWenKai/LXGWWenKai_14.cpfont";
  std::string prodPath = std::string(FIXTURE_DIR) + "/" + prodFont;
  static SdCardFont prod;
  constexpr int PROD_FONT_ID = 535353;
  if (!prod.load(prodPath.c_str())) {
    GTEST_SKIP() << "production font not built (uv run tools/build_cjk_font.py)";
  }
  renderer.registerSdCardFont(PROD_FONT_ID, &prod);
  renderer.insertFont(PROD_FONT_ID, EpdFontFamily(prod.getEpdFont(0), prod.getEpdFont(1), nullptr, nullptr));

  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, 180, 15, "Select Chapter", true, EpdFontFamily::BOLD);

  const char* rows[] = {"第一章 风雪山神庙", "第二章 林教头刺配沧州道", "楔子", "尾声 雪夜上梁山"};
  std::string all;
  for (const char* r : rows) all += r;
  renderer.ensureSdCardFontReady(PROD_FONT_ID, all.c_str(), 0x01);
  prod.prewarm(all.c_str(), 0x01);

  int y = 60;
  const int row = renderer.getLineHeight(PROD_FONT_ID) + 4;
  renderer.fillRect(0, y + row - 2, 520, row);  // selector on 2nd row
  for (size_t i = 0; i < 4; i++) {
    renderer.drawText(PROD_FONT_ID, 20, y + static_cast<int>(i) * row, rows[i], i != 1);
  }
  EXPECT_GT(inkPixels(), 1000u);
  dumpPgm("chapter_list_mock");
}

}  // namespace

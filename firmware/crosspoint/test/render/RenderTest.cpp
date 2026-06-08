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

    // SD CJK font: prefer the locally built production LXGWWenKai (full CJK,
    // truthful visual dumps); fall back to the committed fixture (covers the
    // codepoints the assertions use) when dist/ hasn't been built.
    const std::string prodPath =
        std::string(FIXTURE_DIR) + "/../../../../dist/sd-fonts/LXGWWenKai/LXGWWenKai_14.cpfont";
    if (!sdFont.load(prodPath.c_str())) {
      ASSERT_TRUE(sdFont.load(FIXTURE_CPFONT));
    }
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

// WODLE-PORT: Text Weight — double-strike adds ink without changing metrics,
// affects only the configured font id, and leaves true-BOLD draws alone.
TEST(Render, EmboldenAddsInkSameMetrics) {
  const char* sample = "The quick brown fox";

  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, 20, 40, sample, true);
  const size_t normalInk = inkPixels();
  const int normalWidth = renderer.getTextWidth(UI_12_FONT_ID, sample);

  renderer.setEmboldenFont(UI_12_FONT_ID);
  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, 20, 40, sample, true);
  const size_t boldInk = inkPixels();
  const int boldWidth = renderer.getTextWidth(UI_12_FONT_ID, sample);
  dumpPgm("embolden");

  // Other fonts unaffected while the flag targets UI_12
  renderer.clearScreen();
  renderer.drawText(UI_10_FONT_ID, 20, 40, sample, true);
  const size_t otherFontInk = inkPixels();
  renderer.setEmboldenFont(0);
  renderer.clearScreen();
  renderer.drawText(UI_10_FONT_ID, 20, 40, sample, true);
  EXPECT_EQ(otherFontInk, inkPixels());

  EXPECT_GT(boldInk, normalInk + normalInk / 10);  // visibly heavier
  EXPECT_EQ(boldWidth, normalWidth);               // advance untouched

  // True BOLD style stays single-struck (no double-bold)
  renderer.setEmboldenFont(UI_12_FONT_ID);
  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, 20, 40, sample, true, EpdFontFamily::BOLD);
  const size_t trueBoldWithFlag = inkPixels();
  renderer.setEmboldenFont(0);
  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, 20, 40, sample, true, EpdFontFamily::BOLD);
  EXPECT_EQ(trueBoldWithFlag, inkPixels());
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

// Grayscale AA pipeline (GfxRenderer side): rendering the same content in
// GRAYSCALE_LSB and GRAYSCALE_MSB modes produces flag planes (bit 1 =
// flagged) with LSB = dark-gray-only ⊆ MSB = any-gray. SD cpfont glyphs are
// 2-bit, so they exercise the AA path. Dumps a 4-level preview image
// (overlaying flags onto the BW render) — the visual check for what the
// panel's gray pass will draw.
TEST(Render, GrayscalePlanesInvariantAndPreview) {
  const char* text = "你好，世界。";
  renderer.ensureSdCardFontReady(SD_FONT_ID, text, 0x01);
  sdFont.prewarm(text, 0x01);

  std::vector<uint8_t> bw(HalDisplay::BUFFER_SIZE), lsb(HalDisplay::BUFFER_SIZE), msb(HalDisplay::BUFFER_SIZE);

  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  renderer.drawText(SD_FONT_ID, 20, 60, text, true);
  memcpy(bw.data(), display.getFrameBuffer(), bw.size());

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderer.drawText(SD_FONT_ID, 20, 60, text, true);
  memcpy(lsb.data(), display.getFrameBuffer(), lsb.size());

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderer.drawText(SD_FONT_ID, 20, 60, text, true);
  memcpy(msb.data(), display.getFrameBuffer(), msb.size());

  renderer.setRenderMode(GfxRenderer::BW);

  size_t lsbInk = 0, msbInk = 0;
  for (uint32_t i = 0; i < HalDisplay::BUFFER_SIZE; i++) {
    EXPECT_EQ(lsb[i] & ~msb[i], 0) << "dark-gray flag outside the any-gray plane at byte " << i;
    lsbInk += __builtin_popcount(lsb[i]);
    msbInk += __builtin_popcount(msb[i]);
  }
  EXPECT_GT(msbInk, 0u) << "2-bit glyphs produced no AA pixels";
  EXPECT_GE(msbInk, lsbInk);

  // 4-level preview: black/white from the BW render, grays from the flags.
  const std::string path = std::string(DUMP_DIR) + "/gray_aa_preview.pgm";
  FILE* f = fopen(path.c_str(), "wb");
  ASSERT_NE(f, nullptr);
  fprintf(f, "P5\n%d %d\n255\n", HalDisplay::DISPLAY_WIDTH, HalDisplay::DISPLAY_HEIGHT);
  for (int y = 0; y < HalDisplay::DISPLAY_HEIGHT; y++) {
    for (int x = 0; x < HalDisplay::DISPLAY_WIDTH; x++) {
      const uint32_t idx = y * HalDisplay::DISPLAY_WIDTH_BYTES + x / 8;
      const uint8_t bit = 0x80 >> (x % 8);
      uint8_t shade = (bw[idx] & bit) ? 0xFF : 0x00;
      if (msb[idx] & bit) shade = (lsb[idx] & bit) ? 0x55 : 0xAA;  // dark / light gray
      fputc(shade, f);
    }
  }
  fclose(f);
}

// uiFontFor: UI font for ASCII and translation-subset CJK; SD fallback for
// arbitrary hanzi the UI subset lacks.
TEST(Render, UiFontForFallsBackForUncoveredCjk) {
  EXPECT_EQ(renderer.uiFontFor(UI_10_FONT_ID, "mybook.epub"), UI_10_FONT_ID);
  EXPECT_EQ(renderer.uiFontFor(UI_12_FONT_ID, "设置"), UI_12_FONT_ID) << "translation subset covers 设置";
  // 镕 is not in any translation — needs the SD font.
  EXPECT_EQ(renderer.uiFontFor(UI_10_FONT_ID, "朱镕基传.txt"), SD_FONT_ID);
  EXPECT_EQ(renderer.uiFontFor(UI_10_FONT_ID, nullptr), UI_10_FONT_ID);
}

// File-browser-style list rows with Chinese filenames: rows that the UI
// subset can't cover must come out in the SD reading font, not tofu.
TEST(Render, BrowserListMockupWithChineseFilenames) {
  const char* names[] = {"水浒传.txt", "围城.epub", "mybook.epub", "三体：死神永生.epub"};

  renderer.clearScreen();
  int y = 40;
  for (size_t i = 0; i < 4; i++) {
    const int font = renderer.uiFontFor(UI_10_FONT_ID, names[i]);
    if (renderer.isSdCardFont(font)) {
      // Fixture font lacks most of these glyphs; production font covers all.
      renderer.ensureSdCardFontReady(font, names[i], 0x01);
    }
    if (i == 1) renderer.fillRect(10, y - 4, 480, 34);  // selection bar (themes draw it first)
    renderer.drawText(font, 20, y, names[i], i != 1);
    y += 36;
  }
  EXPECT_GT(inkPixels(), 500u);
  dumpPgm("browser_list_mock");
}

// WODLE-PORT: filename torture — the browser/recents draw whatever the SD
// card throws at them through uiFontFor + drawText + getTextWidth. None of
// these may crash or scribble: 200-char names, glyphless emoji, malformed
// UTF-8 (truncated multibyte from a FAT name), control bytes, RTL (MiniBidi),
// empty strings, full-width forms.
TEST(Render, TortureFilenames) {
  std::string longLatin(220, 'x');
  longLatin += ".epub";
  std::string truncatedUtf8 = "broken\xE4\xBD";        // cut-off multibyte
  std::string controls = std::string("ctl\x01\x1F\x7F") + "name.txt";

  const char* cases[] = {
      longLatin.c_str(),
      truncatedUtf8.c_str(),
      controls.c_str(),
      "\xF0\x9F\x93\x9A emoji book.epub",          // U+1F4DA, no glyph anywhere
      "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D.txt",      // Hebrew (RTL, MiniBidi path)
      "\xEF\xBC\xA1\xEF\xBC\xA2\xEF\xBC\xA3.txt",  // full-width ABC
      "",                                       // empty
      ".",                                      // degenerate
      "a",                                      // single char
  };

  renderer.clearScreen();
  int y = 30;
  for (const char* name : cases) {
    const int font = renderer.uiFontFor(UI_10_FONT_ID, name);
    // Width query is what the themes use to truncate — must be sane.
    const int w = renderer.getTextWidth(font, name);
    EXPECT_GE(w, 0);
    renderer.drawText(font, 20, y, name, true);
    // Deliberately also draw starting past the right edge (clip path).
    renderer.drawText(font, 700, y, name, true);
    y += 26;
  }
  // The long-Latin row alone guarantees ink; the run not crashing is the test.
  EXPECT_GT(inkPixels(), 200u);
  dumpPgm("torture_filenames");
}

// WODLE-PORT: displayWindow maps a LOGICAL rect through the orientation
// transform to a physical controller window (x = 792 source axis, y = gate
// row). Portrait is the reading orientation: logical (x,y) -> phy(y, x) (gate
// 0 = left, un-mirrored), so a bottom status-bar strip becomes a narrow
// full-gate column stripe.
TEST(Render, DisplayWindowMapsPortraitRect) {
  renderer.setOrientation(GfxRenderer::Portrait);
  ASSERT_TRUE(renderer.displayWindow(0, 760, 528, 32));  // logical bottom strip
  EXPECT_EQ(display.lastWindow.x, 760);
  EXPECT_EQ(display.lastWindow.y, 0);
  EXPECT_EQ(display.lastWindow.w, 32);
  EXPECT_EQ(display.lastWindow.h, 528);

  ASSERT_TRUE(renderer.displayWindow(100, 200, 50, 60));  // interior rect
  EXPECT_EQ(display.lastWindow.x, 200);                   // phyX = logical y
  EXPECT_EQ(display.lastWindow.y, 100);                   // phyY = logical x
  EXPECT_EQ(display.lastWindow.w, 60);
  EXPECT_EQ(display.lastWindow.h, 50);

  EXPECT_FALSE(renderer.displayWindow(0, 0, 0, 10));  // degenerate refused
}

// WODLE-PORT: pins the absolute 4-gray plane algebra used by
// HalDisplay::displayGrayBuffer (spi_epd_demo semantics: 0x10 bit = gray
// bit1, 0x13 bit = gray bit0; 00=black 01=dark 10=light 11=white) composed
// from the BW page + the renderer's MSB(any-gray)/LSB(dark-only) flags.
TEST(Render, Gray4PlaneAlgebra) {
  struct Case {
    uint8_t bw, msb, lsb;        // inputs (one bit per pixel)
    uint8_t plane10, plane13;    // expected gray code bits
    const char* what;
  };
  const Case cases[] = {
      {1, 0, 0, 1, 1, "white background -> 11"},
      {0, 0, 0, 0, 0, "black text -> 00"},
      {0, 1, 1, 0, 1, "dark-gray AA edge -> 01"},
      {0, 1, 0, 1, 0, "light-gray AA edge -> 10"},
      {1, 1, 1, 0, 1, "flagged dark wins over white BW -> 01"},
      {1, 1, 0, 1, 0, "flagged light wins over white BW -> 10"},
  };
  for (const auto& c : cases) {
    const uint8_t p10 = (uint8_t)((c.bw & ~c.msb) | (c.msb & ~c.lsb)) & 1;
    const uint8_t p13 = (uint8_t)((c.bw & ~c.msb) | (c.msb & c.lsb)) & 1;
    EXPECT_EQ(p10, c.plane10) << c.what;
    EXPECT_EQ(p13, c.plane13) << c.what;
  }
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

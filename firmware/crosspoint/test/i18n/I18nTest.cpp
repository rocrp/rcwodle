// Host tests for the generated I18n tables (vendor/lib/I18n, regenerated via
// upstream gen_i18n.py) and the zh-CN UI font coverage: every character the
// zh translation uses must have a real glyph in every builtin UI font
// (regenerated with a CJK subset by tools/build_ui_cjk_fonts.py) — otherwise
// the UI would render tofu/replacement boxes. Catches chinese.yaml edits that
// forgot the font regen.

#include <EpdFont.h>
#include <I18n.h>
#include <Utf8.h>
#include <gtest/gtest.h>

#include <cstring>

// Generated UI fonts (static EpdFontData definitions).
#include "notosans_8_regular.h"
#include "ubuntu_10_bold.h"
#include "ubuntu_10_regular.h"
#include "ubuntu_12_bold.h"
#include "ubuntu_12_regular.h"

namespace {

TEST(I18nTest, AllLanguagesResolveAllKeys) {
  I18n& i18n = I18n::getInstance();
  for (int lang = 0; lang < static_cast<int>(Language::_COUNT); lang++) {
    i18n.setLanguage(static_cast<Language>(lang));
    for (int id = 0; id < static_cast<int>(StrId::_COUNT); id++) {
      const char* s = i18n.get(static_cast<StrId>(id));
      ASSERT_NE(s, nullptr) << "lang " << lang << " key " << id;
      EXPECT_STRNE(s, "???") << "lang " << lang << " key " << id;
      EXPECT_GT(std::strlen(s), 0u) << "lang " << lang << " key " << id;
    }
  }
  i18n.setLanguage(Language::EN);
}

TEST(I18nTest, ChineseSpotChecks) {
  I18n& i18n = I18n::getInstance();
  i18n.setLanguage(Language::ZH);
  EXPECT_STREQ(i18n.get(StrId::STR_SETTINGS_TITLE), "设置");
  EXPECT_STREQ(i18n.get(StrId::STR_LANGUAGE), "语言");
  EXPECT_STREQ(i18n.get(StrId::STR_CONFIRM), "确认");
  EXPECT_STREQ(i18n.get(StrId::STR_CONTINUE_READING), "继续阅读");
  // Shared-with-English strings dedup to the EN table (bit-15 offset).
  EXPECT_STREQ(i18n.get(StrId::STR_CROSSPOINT), "CrossPoint");
  i18n.setLanguage(Language::EN);
}

TEST(I18nTest, ChineseLanguageMetadata) {
  EXPECT_STREQ(I18n::getInstance().getLanguageName(Language::ZH), "简体中文");
  EXPECT_EQ(I18n::languageFromCode("ZH"), Language::ZH);
  const char* charset = I18n::getCharacterSet(Language::ZH);
  ASSERT_NE(charset, nullptr);
  EXPECT_NE(std::strstr(charset, "设"), nullptr);
  EXPECT_NE(std::strstr(charset, "置"), nullptr);
}

// Every character of every language's charset must map to a real glyph (not
// the U+FFFD replacement fallback) in all builtin UI fonts.
TEST(I18nTest, UiFontsCoverAllLanguageCharsets) {
  const struct {
    const char* name;
    const EpdFontData* data;
  } uiFonts[] = {
      {"ubuntu_10_regular", &ubuntu_10_regular}, {"ubuntu_10_bold", &ubuntu_10_bold},
      {"ubuntu_12_regular", &ubuntu_12_regular}, {"ubuntu_12_bold", &ubuntu_12_bold},
      {"notosans_8_regular", &notosans_8_regular},
  };

  for (const auto& f : uiFonts) {
    const EpdFont font(f.data);
    const EpdGlyph* replacement = font.getGlyph(0xFFFD);
    ASSERT_NE(replacement, nullptr) << f.name << " lacks U+FFFD";

    for (int lang = 0; lang < static_cast<int>(Language::_COUNT); lang++) {
      const char* charset = I18n::getCharacterSet(static_cast<Language>(lang));
      ASSERT_NE(charset, nullptr);
      const unsigned char* p = reinterpret_cast<const unsigned char*>(charset);
      while (*p) {
        const uint32_t cp = utf8NextCodepoint(&p);
        if (cp == 0) break;
        if (cp == 0xFFFD || cp == '\n') continue;
        const EpdGlyph* glyph = font.getGlyph(cp);
        ASSERT_NE(glyph, nullptr) << f.name << " U+" << std::hex << cp;
        EXPECT_NE(glyph, replacement)
            << f.name << " falls back to tofu for U+" << std::hex << cp << " (lang " << std::dec << lang << ")";
      }
    }
  }
}

}  // namespace

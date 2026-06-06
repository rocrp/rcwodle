// Host tests for the EPUB open path over the real fixture EPUB —
// container.xml → content.opf (metadata/manifest/spine) → toc (nav/ncx) →
// metadata cache build + reload, item reads, CSS discovery. This is the most
// complex zero-HIL pipeline in the port; these tests pin it down on host.
// Image conversion (cover/thumb) is stubbed out (see ConverterStubs.cpp).

#include <Epub.h>
#include <FsHelpers.h>
#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <string>

namespace {

class StringPrint final : public Print {
 public:
  size_t write(uint8_t c) override {
    data.push_back(static_cast<char>(c));
    return 1;
  }
  size_t write(const uint8_t* buf, size_t size) override {
    data.append(reinterpret_cast<const char*>(buf), size);
    return size;
  }
  std::string data;
};

class EpubFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    // Per-test dir: ctest -j runs each TEST as its own process — a shared
    // cache dir races with remove_all() in concurrent SetUps.
    cacheDir_ = std::string(::testing::TempDir()) + "epub_cache_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::remove_all(cacheDir_);
    std::filesystem::create_directories(cacheDir_);
    epub_ = std::make_unique<Epub>(FIXTURE_EPUB, cacheDir_);
    ASSERT_TRUE(epub_->load());
  }

  std::string cacheDir_;
  std::unique_ptr<Epub> epub_;
};

TEST_F(EpubFixture, ParsesOpfMetadata) {
  EXPECT_EQ(epub_->getTitle(), "Tables? In CrossPoint?");
  EXPECT_EQ(epub_->getLanguage(), "en-US");
  EXPECT_EQ(epub_->getBasePath(), "EPUB/");
}

TEST_F(EpubFixture, ParsesSpine) {
  ASSERT_EQ(epub_->getSpineItemsCount(), 4);
  EXPECT_GE(epub_->resolveHrefToSpineIndex("text/ch001.xhtml"), 0);
  const auto first = epub_->getSpineItem(0);
  EXPECT_FALSE(first.href.empty());
  EXPECT_GT(epub_->getBookSize(), 0u);
  EXPECT_GT(epub_->getCumulativeSpineItemSize(3), epub_->getCumulativeSpineItemSize(0));
}

TEST_F(EpubFixture, ParsesToc) {
  ASSERT_GT(epub_->getTocItemsCount(), 0);
  const auto toc0 = epub_->getTocItem(0);
  EXPECT_FALSE(toc0.title.empty());
  const int spineIdx = epub_->getSpineIndexForTocIndex(0);
  EXPECT_GE(spineIdx, 0);
  EXPECT_LT(spineIdx, epub_->getSpineItemsCount());
}

TEST_F(EpubFixture, ReadsChapterContent) {
  const auto item = epub_->getSpineItem(epub_->resolveHrefToSpineIndex("text/ch001.xhtml"));
  size_t size = 0;
  EXPECT_TRUE(epub_->getItemSize(item.href, &size));
  EXPECT_GT(size, 0u);

  StringPrint out;
  ASSERT_TRUE(epub_->readItemContentsToStream(item.href, out, 512));
  EXPECT_EQ(out.data.size(), size);
  EXPECT_NE(out.data.find("<html"), std::string::npos);
}

TEST_F(EpubFixture, DiscoversCss) {
  ASSERT_NE(epub_->getCssParser(), nullptr);
}

TEST_F(EpubFixture, ProgressIsMonotonic) {
  const float early = epub_->calculateProgress(0, 0.5f);
  const float late = epub_->calculateProgress(3, 0.5f);
  EXPECT_GE(early, 0.0f);
  EXPECT_LE(late, 100.0f);
  EXPECT_LT(early, late);
}

TEST_F(EpubFixture, ReloadsFromMetadataCache) {
  // Fresh instance, same cache dir: must load via BookMetadataCache, not a
  // fresh OPF parse (buildIfMissing=false forbids rebuilding).
  Epub again(FIXTURE_EPUB, cacheDir_);
  ASSERT_TRUE(again.load(/*buildIfMissing=*/false));
  EXPECT_EQ(again.getTitle(), "Tables? In CrossPoint?");
  EXPECT_EQ(again.getSpineItemsCount(), 4);
  EXPECT_EQ(again.getTocItemsCount(), epub_->getTocItemsCount());
}

// WODLE-PORT: books with neither nav nor NCX get a synthesized flat TOC from
// the spine (filename-derived titles) so chapter navigation still works.
TEST(EpubFallbackToc, SynthesizedFromSpine) {
  const std::string cacheDir = std::string(::testing::TempDir()) + "epub_cache_fallback_toc";
  std::filesystem::remove_all(cacheDir);
  std::filesystem::create_directories(cacheDir);

  Epub epub(FIXTURE_EPUB_NOTOC, cacheDir);
  ASSERT_TRUE(epub.load());

  ASSERT_EQ(epub.getSpineItemsCount(), 3);
  ASSERT_EQ(epub.getTocItemsCount(), 3);

  const auto first = epub.getTocItem(0);
  EXPECT_EQ(first.title, "chapter01");
  EXPECT_EQ(first.spineIndex, 0);
  EXPECT_EQ(first.level, 0);

  EXPECT_EQ(epub.getTocItem(1).title, "chapter02");
  EXPECT_EQ(epub.getTocItem(1).spineIndex, 1);

  // Percent-encoded href -> decoded, extension stripped.
  EXPECT_EQ(epub.getTocItem(2).title, "the end");
  EXPECT_EQ(epub.getTocItem(2).spineIndex, 2);

  // Spine->toc reverse mapping works for the synthesized entries.
  EXPECT_EQ(epub.getTocIndexForSpineIndex(2), 2);
}

TEST(EpubErrors, MissingFileFailsLoad) {
  Epub missing("/nonexistent/book.epub", std::string(::testing::TempDir()) + "epub_cache_missing");
  EXPECT_FALSE(missing.load());
}

// Upstream #2249: EPUB-internal references may be percent-encoded
// ("Chapter%201.xhtml" for "Chapter 1.xhtml"); the parsers decode them via
// this helper before lookup. Malformed escapes must pass through untouched.
TEST(FsHelpersUri, DecodesPercentEscapes) {
  EXPECT_EQ(FsHelpers::decodeUriEscapes("Chapter%201.xhtml"), "Chapter 1.xhtml");
  EXPECT_EQ(FsHelpers::decodeUriEscapes("a%2Fb%2fc"), "a/b/c");  // both hex cases
  EXPECT_EQ(FsHelpers::decodeUriEscapes("plain/path.xhtml"), "plain/path.xhtml");
  EXPECT_EQ(FsHelpers::decodeUriEscapes("bad%zz%1"), "bad%zz%1");  // malformed: untouched
  EXPECT_EQ(FsHelpers::decodeUriEscapes(""), "");
}

}  // namespace

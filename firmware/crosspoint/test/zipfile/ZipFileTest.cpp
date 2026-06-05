/* Host test: ZipFile + InflateReader + uzlib over a real EPUB fixture —
 * exercises the entry point of the whole book pipeline with real data. */

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "Print.h"
#include "ZipFile.h"

#ifndef FIXTURE_EPUB
#error "FIXTURE_EPUB must be defined by the build"
#endif

namespace
{
class StringSink : public Print
{
public:
    std::string data;
    size_t write(uint8_t b) override
    {
        data += (char)b;
        return 1;
    }
    size_t write(const uint8_t *buf, size_t size) override
    {
        data.append((const char *)buf, size);
        return size;
    }
};
} // namespace

class ZipFileFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        /* ZipFile stores a const std::string& — the path must outlive it
         * (upstream callers pass a long-lived member; a temporary dangles) */
        zip = std::make_unique<ZipFile>(path);
        ASSERT_TRUE(zip->open());
    }
    std::string path = FIXTURE_EPUB; /* declared before zip: outlives it */
    std::unique_ptr<ZipFile> zip;
};

TEST_F(ZipFileFixture, EnumeratesEpubStructure)
{
    bool sawMimetype = false, sawContainer = false, sawOpf = false;
    zip->enumerateFilePaths([&](std::string_view path) {
        std::string p(path);
        if (p == "mimetype") sawMimetype = true;
        if (p == "META-INF/container.xml") sawContainer = true;
        if (p.size() > 4 && p.substr(p.size() - 4) == ".opf") sawOpf = true;
    });
    EXPECT_TRUE(sawMimetype);
    EXPECT_TRUE(sawContainer);
    EXPECT_TRUE(sawOpf);
}

TEST_F(ZipFileFixture, MimetypeContentIsCorrect)
{
    /* per EPUB OCF, the mimetype entry is stored uncompressed with exactly
     * this content — a strong end-to-end read check */
    StringSink sink;
    ASSERT_TRUE(zip->readFileToStream("mimetype", sink, 64));
    EXPECT_EQ(sink.data, "application/epub+zip");
}

TEST_F(ZipFileFixture, InflatedSizeMatchesStreamedBytes)
{
    size_t expected = 0;
    ASSERT_TRUE(zip->getInflatedFileSize("META-INF/container.xml", &expected));
    ASSERT_GT(expected, 0u);

    StringSink sink;
    ASSERT_TRUE(zip->readFileToStream("META-INF/container.xml", sink, 128));
    EXPECT_EQ(sink.data.size(), expected);

    /* container.xml must point at an OPF rootfile */
    EXPECT_NE(sink.data.find("rootfile"), std::string::npos);
    EXPECT_NE(sink.data.find(".opf"), std::string::npos);
}

TEST_F(ZipFileFixture, ReadFileToMemoryMatchesStream)
{
    StringSink sink;
    ASSERT_TRUE(zip->readFileToStream("META-INF/container.xml", sink, 100));

    size_t size = 0;
    uint8_t *mem = zip->readFileToMemory("META-INF/container.xml", &size);
    ASSERT_NE(mem, nullptr);
    EXPECT_EQ(std::string((const char *)mem, size), sink.data);
    free(mem);
}

TEST_F(ZipFileFixture, MissingEntryFailsGracefully)
{
    size_t size = 0;
    EXPECT_FALSE(zip->getInflatedFileSize("does/not/exist.xhtml", &size));
    StringSink sink;
    EXPECT_FALSE(zip->readFileToStream("does/not/exist.xhtml", sink, 64));
}

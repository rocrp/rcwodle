add_executable(EpubTest
  EpubTest.cpp
  ConverterStubs.cpp
  ${PORT_ROOT}/test/host_shims/HalStorageHost.cpp
  ${PORT_ROOT}/test/host_shims/uzlib_chksums.c
  ${PORT_ROOT}/vendor/lib/Epub/Epub.cpp
  ${PORT_ROOT}/vendor/lib/Epub/Epub/BookMetadataCache.cpp
  ${PORT_ROOT}/vendor/lib/Epub/Epub/css/CssParser.cpp
  ${PORT_ROOT}/vendor/lib/Epub/Epub/parsers/ContainerParser.cpp
  ${PORT_ROOT}/vendor/lib/Epub/Epub/parsers/ContentOpfParser.cpp
  ${PORT_ROOT}/vendor/lib/Epub/Epub/parsers/TocNavParser.cpp
  ${PORT_ROOT}/vendor/lib/Epub/Epub/parsers/TocNcxParser.cpp
  ${PORT_ROOT}/vendor/lib/FsHelpers/FsHelpers.cpp
  ${PORT_ROOT}/vendor/lib/ZipFile/ZipFile.cpp
  ${PORT_ROOT}/vendor/lib/InflateReader/InflateReader.cpp
  ${PORT_ROOT}/vendor/lib/Logging/Logging.cpp
  ${PORT_ROOT}/vendor/lib/uzlib/src/tinflate.c
  ${PORT_ROOT}/vendor/lib/expat/xmlparse.c
  ${PORT_ROOT}/vendor/lib/expat/xmlrole.c
  ${PORT_ROOT}/vendor/lib/expat/xmltok.c
)

# host_shims FIRST so Arduino.h/HalStorage.h resolve to the host variants.
target_include_directories(EpubTest PRIVATE
  ${PORT_ROOT}/test/host_shims
  ${PORT_ROOT}/port/arduino
  ${PORT_ROOT}/vendor/lib/Epub
  ${PORT_ROOT}/vendor/lib/ZipFile
  ${PORT_ROOT}/vendor/lib/InflateReader
  ${PORT_ROOT}/vendor/lib/FsHelpers
  ${PORT_ROOT}/vendor/lib/JpegToBmpConverter
  ${PORT_ROOT}/vendor/lib/PngToBmpConverter
  ${PORT_ROOT}/vendor/lib/Serialization
  ${PORT_ROOT}/vendor/lib/XmlParserUtils
  ${PORT_ROOT}/vendor/lib/expat
  ${PORT_ROOT}/vendor/lib/Logging
  ${PORT_ROOT}/vendor/lib/Memory
  ${PORT_ROOT}/vendor/lib/uzlib/src
)

target_compile_definitions(EpubTest PRIVATE
  FIXTURE_EPUB="${PORT_ROOT}/test/fixtures/test_tables.epub"
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  ENABLE_SERIAL_LOG
  LOG_LEVEL=0
)

target_link_libraries(EpubTest PRIVATE
  cp_test_common
  GTest::gtest_main
)

gtest_discover_tests(EpubTest)

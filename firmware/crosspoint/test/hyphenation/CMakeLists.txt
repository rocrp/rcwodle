add_executable(HyphenatorTest
  HyphenatorTest.cpp
  ${PORT_ROOT}/vendor/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${PORT_ROOT}/vendor/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${PORT_ROOT}/vendor/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${PORT_ROOT}/vendor/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
  ${PORT_ROOT}/vendor/lib/Utf8/Utf8.cpp
)

target_include_directories(HyphenatorTest PRIVATE
  ${PORT_ROOT}/vendor/lib/Epub
  ${PORT_ROOT}/vendor/lib/Epub/Epub/hyphenation
  ${PORT_ROOT}/vendor/lib/Utf8
)

target_link_libraries(HyphenatorTest PRIVATE
  cp_test_common
  GTest::gtest_main
)

gtest_discover_tests(HyphenatorTest)

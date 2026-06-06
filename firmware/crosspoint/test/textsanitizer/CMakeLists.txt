# TextSanitizer — pure byte classifier from the EPUB parser, no shims needed.
add_executable(TextSanitizerTest
  TextSanitizerTest.cpp
  ${PORT_ROOT}/vendor/lib/Epub/Epub/parsers/TextSanitizer.cpp
)

target_include_directories(TextSanitizerTest PRIVATE
  ${PORT_ROOT}/vendor/lib/Epub/Epub/parsers
)

target_link_libraries(TextSanitizerTest PRIVATE
  cp_test_common
  GTest::gtest_main
)

gtest_discover_tests(TextSanitizerTest)

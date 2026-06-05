add_executable(I18nTest
  I18nTest.cpp
  ${PORT_ROOT}/vendor/lib/I18n/I18n.cpp
  ${PORT_ROOT}/vendor/lib/I18n/I18nStrings.cpp
  ${PORT_ROOT}/vendor/lib/EpdFont/EpdFont.cpp
  ${PORT_ROOT}/vendor/lib/Utf8/Utf8.cpp
)

target_include_directories(I18nTest PRIVATE
  ${PORT_ROOT}/vendor/lib/I18n
  ${PORT_ROOT}/vendor/lib/EpdFont
  ${PORT_ROOT}/vendor/lib/EpdFont/builtinFonts
  ${PORT_ROOT}/vendor/lib/Utf8
)

target_link_libraries(I18nTest PRIVATE
  cp_test_common
  GTest::gtest_main
)

gtest_discover_tests(I18nTest)

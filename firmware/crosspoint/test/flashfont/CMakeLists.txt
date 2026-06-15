# Flash-resident XIP font parser test. FlashFontParse.h is header-only and
# renderer-free, so we link only the glyph-lookup sources + the shared fixture.
add_executable(FlashFontTest
  FlashFontTest.cpp
  ${PORT_ROOT}/vendor/lib/EpdFont/EpdFont.cpp
  ${PORT_ROOT}/vendor/lib/EpdFont/EpdFontFamily.cpp
  ${PORT_ROOT}/vendor/lib/Logging/Logging.cpp
  ${PORT_ROOT}/vendor/lib/Utf8/Utf8.cpp
)

target_include_directories(FlashFontTest PRIVATE
  ${PORT_ROOT}/test/host_shims
  ${PORT_ROOT}/port/arduino
  ${PORT_ROOT}/vendor/src
  ${PORT_ROOT}/vendor/lib/EpdFont
  ${PORT_ROOT}/vendor/lib/Logging
  ${PORT_ROOT}/vendor/lib/Memory
  ${PORT_ROOT}/vendor/lib/Utf8
)

# Same committed fixture as the SD-font suite (uv run tools/build_cjk_font.py --fixture).
target_compile_definitions(FlashFontTest PRIVATE
  FIXTURE_CPFONT="${PORT_ROOT}/test/fixtures/TestCJK_14.cpfont"
  ENABLE_SERIAL_LOG
  LOG_LEVEL=0
)

target_link_libraries(FlashFontTest PRIVATE
  cp_test_common
  GTest::gtest_main
)

gtest_discover_tests(FlashFontTest)

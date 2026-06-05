add_executable(ReleaseJsonParserTest
  ReleaseJsonParserTest.cpp
  ${PORT_ROOT}/vendor/lib/JsonParser/ReleaseJsonParser.cpp
  ${PORT_ROOT}/vendor/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(ReleaseJsonParserTest PRIVATE
  ${PORT_ROOT}/vendor/lib/JsonParser
)

target_link_libraries(ReleaseJsonParserTest PRIVATE
  cp_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReleaseJsonParserTest)

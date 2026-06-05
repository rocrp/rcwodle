add_executable(StreamingJsonParserTest
  StreamingJsonParserTest.cpp
  ${PORT_ROOT}/vendor/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(StreamingJsonParserTest PRIVATE
  ${PORT_ROOT}/vendor/lib/JsonParser
)

target_link_libraries(StreamingJsonParserTest PRIVATE
  cp_test_common
  GTest::gtest_main
)

gtest_discover_tests(StreamingJsonParserTest)

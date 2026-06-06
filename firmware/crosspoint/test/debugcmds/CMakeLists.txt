# WodleDebugCmdCore — pure queue/parser/injection logic behind the `wodle`
# MSH command (header-only, no shims needed).
add_executable(DebugCmdCoreTest
  DebugCmdCoreTest.cpp
)

target_include_directories(DebugCmdCoreTest PRIVATE
  ${PORT_ROOT}/port/hal
)

target_link_libraries(DebugCmdCoreTest PRIVATE
  cp_test_common
  GTest::gtest_main
)

gtest_discover_tests(DebugCmdCoreTest)

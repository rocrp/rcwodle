# Aht20Codec — pure decode/CRC math from port/hal, no shims needed.
add_executable(Aht20CodecTest
  Aht20CodecTest.cpp
)

target_include_directories(Aht20CodecTest PRIVATE
  ${PORT_ROOT}/port/hal
)

target_link_libraries(Aht20CodecTest PRIVATE
  cp_test_common
  GTest::gtest_main
)

gtest_discover_tests(Aht20CodecTest)

add_executable(PortShimsTest
  PortShimsTest.cpp
  ${PORT_ROOT}/port/arduino/Base64Shim.cpp
  ${PORT_ROOT}/port/arduino/MD5Shim.cpp
)

# Only the RT-Thread-free shim headers (WString/Print/base64/MD5) are
# host-buildable; Arduino.h itself pulls rtthread.h and stays target-only.
target_include_directories(PortShimsTest PRIVATE
  ${PORT_ROOT}/port/arduino
)

target_link_libraries(PortShimsTest PRIVATE
  cp_test_common
  GTest::gtest_main
)

gtest_discover_tests(PortShimsTest)

add_executable(TextEncodingTest
  TextEncodingTest.cpp
  JpegStub.cpp
  ${PORT_ROOT}/test/host_shims/HalStorageHost.cpp
  ${PORT_ROOT}/vendor/lib/TextEncoding/TextEncoding.cpp
  ${PORT_ROOT}/vendor/lib/TextEncoding/GbkTable.cpp
  ${PORT_ROOT}/vendor/lib/Txt/Txt.cpp
  ${PORT_ROOT}/vendor/lib/FsHelpers/FsHelpers.cpp
  ${PORT_ROOT}/vendor/lib/Logging/Logging.cpp
)

# host_shims FIRST so HalStorage.h/Arduino.h resolve to the host variants.
target_include_directories(TextEncodingTest PRIVATE
  ${PORT_ROOT}/test/host_shims
  ${PORT_ROOT}/port/arduino
  ${PORT_ROOT}/vendor/lib/TextEncoding
  ${PORT_ROOT}/vendor/lib/Txt
  ${PORT_ROOT}/vendor/lib/FsHelpers
  ${PORT_ROOT}/vendor/lib/JpegToBmpConverter
  ${PORT_ROOT}/vendor/lib/Serialization
  ${PORT_ROOT}/vendor/lib/Logging
  ${PORT_ROOT}/vendor/lib/Memory
)

# Fixtures generated with Python codecs as ground truth (see test header).
target_compile_definitions(TextEncodingTest PRIVATE
  FIXTURE_DIR="${PORT_ROOT}/test/fixtures"
  ENABLE_SERIAL_LOG
  LOG_LEVEL=0
)

target_link_libraries(TextEncodingTest PRIVATE
  cp_test_common
  GTest::gtest_main
)

gtest_discover_tests(TextEncodingTest)

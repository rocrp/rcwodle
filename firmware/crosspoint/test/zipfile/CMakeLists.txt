add_executable(ZipFileTest
  ZipFileTest.cpp
  ${PORT_ROOT}/test/host_shims/HalStorageHost.cpp
  ${PORT_ROOT}/vendor/lib/ZipFile/ZipFile.cpp
  ${PORT_ROOT}/vendor/lib/InflateReader/InflateReader.cpp
  ${PORT_ROOT}/vendor/lib/Logging/Logging.cpp
  ${PORT_ROOT}/vendor/lib/uzlib/src/tinflate.c
  ${PORT_ROOT}/test/host_shims/uzlib_chksums.c
)

# host_shims FIRST so Arduino.h/HalStorage.h/HardwareSerial.h resolve to the
# RT-Thread-free host variants; port/arduino supplies the pure headers
# (WString/Print/Stream).
target_include_directories(ZipFileTest PRIVATE
  ${PORT_ROOT}/test/host_shims
  ${PORT_ROOT}/port/arduino
  ${PORT_ROOT}/vendor/lib/ZipFile
  ${PORT_ROOT}/vendor/lib/InflateReader
  ${PORT_ROOT}/vendor/lib/Logging
  ${PORT_ROOT}/vendor/lib/Memory
  ${PORT_ROOT}/vendor/lib/uzlib/src
)

target_compile_definitions(ZipFileTest PRIVATE
  FIXTURE_EPUB="${PORT_ROOT}/test/fixtures/test_tables.epub"
  ENABLE_SERIAL_LOG
  LOG_LEVEL=0
)

target_link_libraries(ZipFileTest PRIVATE
  cp_test_common
  GTest::gtest_main
)

gtest_discover_tests(ZipFileTest)

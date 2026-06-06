# HalStorage write-failure latch — own binary: the latch is process-global, so
# sharing a test binary with suites that exercise writes would be order-fragile.
add_executable(StorageLatchTest
  StorageLatchTest.cpp
  ${PORT_ROOT}/test/host_shims/HalStorageHost.cpp
  ${PORT_ROOT}/test/host_shims/uzlib_chksums.c
)

target_include_directories(StorageLatchTest PRIVATE
  ${PORT_ROOT}/test/host_shims
  ${PORT_ROOT}/port/arduino
)

target_link_libraries(StorageLatchTest PRIVATE
  cp_test_common
  GTest::gtest_main
)

gtest_discover_tests(StorageLatchTest)

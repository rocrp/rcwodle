// WODLE-PORT: the sticky write-failure latch that backs the once-per-boot
// "Storage write failed" popup (src/main.cpp loop).
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <string>

namespace {

TEST(StorageLatch, StartsClearInFreshProcess) {
  // Own binary => nothing has written yet.
  EXPECT_FALSE(HalStorage::hadWriteFailure());
}

TEST(StorageLatch, SuccessfulWriteDoesNotLatch) {
  const std::string path = std::string(::testing::TempDir()) + "latch_ok.bin";
  HalFile f;
  ASSERT_TRUE(Storage.openFileForWrite("TST", path.c_str(), f));
  const uint8_t data[4] = {1, 2, 3, 4};
  EXPECT_EQ(f.write(data, sizeof(data)), sizeof(data));
  f.close();
  EXPECT_FALSE(HalStorage::hadWriteFailure());
}

TEST(StorageLatch, FailedOpenForWriteLatches) {
  HalFile f;
  EXPECT_FALSE(Storage.openFileForWrite("TST", "/nonexistent-dir/nope/latch.bin", f));
  EXPECT_TRUE(HalStorage::hadWriteFailure());
}

}  // namespace

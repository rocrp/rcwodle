# GifToFramebufferConverter — decodes committed GIF fixtures through the real
# GfxRenderer + AnimatedGIF on host (reuses the render suite's HalDisplay shim).
add_executable(GifTest
  GifTest.cpp
  ${PORT_ROOT}/test/render/shims/HalDisplayHost.cpp
  ${PORT_ROOT}/test/host_shims/HalStorageHost.cpp
  ${PORT_ROOT}/test/host_shims/uzlib_chksums.c
  ${PORT_ROOT}/vendor/extlib/AnimatedGIF/src/AnimatedGIF.cpp
  ${PORT_ROOT}/vendor/lib/Epub/Epub/converters/GifToFramebufferConverter.cpp
  ${PORT_ROOT}/vendor/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${PORT_ROOT}/vendor/lib/GfxRenderer/GfxRenderer.cpp
  ${PORT_ROOT}/vendor/lib/GfxRenderer/FontCacheManager.cpp
  ${PORT_ROOT}/vendor/lib/GfxRenderer/Bitmap.cpp
  ${PORT_ROOT}/vendor/lib/GfxRenderer/BitmapHelpers.cpp
  ${PORT_ROOT}/vendor/lib/EpdFont/EpdFont.cpp
  ${PORT_ROOT}/vendor/lib/EpdFont/EpdFontFamily.cpp
  ${PORT_ROOT}/vendor/lib/EpdFont/FontDecompressor.cpp
  ${PORT_ROOT}/vendor/lib/EpdFont/SdCardFont.cpp
  ${PORT_ROOT}/vendor/lib/MiniBidi/BidiUtils.cpp
  ${PORT_ROOT}/vendor/lib/MiniBidi/minibidi.c
  ${PORT_ROOT}/vendor/lib/Utf8/Utf8.cpp
  ${PORT_ROOT}/vendor/lib/Logging/Logging.cpp
  ${PORT_ROOT}/vendor/lib/InflateReader/InflateReader.cpp
  ${PORT_ROOT}/vendor/lib/uzlib/src/tinflate.c
)

# Include order matters: render shims (HalDisplay) and host_shims
# (Arduino/HalStorage) MUST come before port/hal (see test/render).
target_include_directories(GifTest PRIVATE
  ${PORT_ROOT}/test/render/shims
  ${PORT_ROOT}/test/host_shims
  ${PORT_ROOT}/port/arduino
  ${PORT_ROOT}/port/hal
  ${PORT_ROOT}/vendor/extlib/AnimatedGIF/src
  ${PORT_ROOT}/vendor/lib/Epub
  ${PORT_ROOT}/vendor/lib/Epub/Epub/converters
  ${PORT_ROOT}/vendor/lib/FsHelpers
  ${PORT_ROOT}/vendor/lib/GfxRenderer
  ${PORT_ROOT}/vendor/lib/EpdFont
  ${PORT_ROOT}/vendor/lib/MiniBidi
  ${PORT_ROOT}/vendor/lib/InflateReader
  ${PORT_ROOT}/vendor/lib/Utf8
  ${PORT_ROOT}/vendor/lib/Logging
  ${PORT_ROOT}/vendor/lib/Memory
  ${PORT_ROOT}/vendor/lib/uzlib/src
  ${PORT_ROOT}/vendor/src
)

target_sources(GifTest PRIVATE
  ${PORT_ROOT}/vendor/lib/FsHelpers/FsHelpers.cpp
)

target_compile_definitions(GifTest PRIVATE
  FIXTURE_DIR="${PORT_ROOT}/test/fixtures"
  ENABLE_SERIAL_LOG
  LOG_LEVEL=0
)

target_link_libraries(GifTest PRIVATE
  cp_test_common
  GTest::gtest_main
)

gtest_discover_tests(GifTest)

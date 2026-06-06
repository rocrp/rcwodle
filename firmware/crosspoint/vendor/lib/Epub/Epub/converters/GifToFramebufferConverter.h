/* WODLE-PORT (rocrp fork a1f5077): GIF support for EPUB images via
 * bitbank2/AnimatedGIF (vendored in extlib/, MAX_WIDTH patched to 800).
 * Static decode only — first frame of an animation. Adapted to the
 * streaming PixelCache API from upstream d9bcef7. */
#pragma once

#include "ImageToFramebufferDecoder.h"

class GifToFramebufferConverter final : public ImageToFramebufferDecoder {
 public:
  static bool getDimensionsStatic(const std::string& imagePath, ImageDimensions& out);

  bool decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer, const RenderConfig& config) override;

  bool getDimensions(const std::string& imagePath, ImageDimensions& dims) const override {
    return getDimensionsStatic(imagePath, dims);
  }

  static bool supportsFormat(const std::string& extension);
  const char* getFormatName() const override { return "GIF"; }
};

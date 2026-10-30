// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_ZITTER_FINGERPRINT_NOISE_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_ZITTER_FINGERPRINT_NOISE_H_

#include <cstdint>

#include "base/containers/span.h"
#include "base/memory/scoped_refptr.h"
#include "third_party/blink/renderer/platform/platform_export.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

class SkPixmap;

namespace blink {

class StaticBitmapImage;

// Zitter: small, stable changes to what a page reads back from canvas, WebGL
// and audio rendering. The seed comes from a per-profile session key and the
// page's top-level site, so one site sees the same values on every read, while
// another site, profile or browser session sees different ones.
class PLATFORM_EXPORT ZitterFingerprintNoise {
 public:
  static ZitterFingerprintNoise ForSite(base::span<const uint8_t> key,
                                        const String& top_level_site);

  // Flips the lowest bit of one colour channel in a bounded number of visible
  // pixels. Only 8-bit RGBA/BGRA pixmaps are changed.
  void ApplyToPixels(const SkPixmap& pixmap) const;
  // A raster copy of `image` with ApplyToPixels(), or `image` itself when it
  // cannot be read back as 8-bit pixels.
  scoped_refptr<StaticBitmapImage> ApplyToImage(
      scoped_refptr<StaticBitmapImage> image) const;
  // Scales each sample down by a site-specific amount of at most 1e-4
  // (-80 dB): inaudible, but enough to change a fingerprint's sum.
  void ApplyToAudio(base::span<float> samples) const;

  uint64_t seed() const { return seed_; }

 private:
  explicit ZitterFingerprintNoise(uint64_t seed) : seed_(seed) {}
  uint64_t seed_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_ZITTER_FINGERPRINT_NOISE_H_

// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "third_party/blink/renderer/platform/graphics/zitter_fingerprint_noise.h"

#include <algorithm>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "crypto/hmac.h"
#include "third_party/blink/renderer/platform/graphics/static_bitmap_image.h"
#include "third_party/blink/renderer/platform/graphics/unaccelerated_static_bitmap_image.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkPixmap.h"

namespace blink {
namespace {

// Enough tries to hit visible pixels of sparse text, few enough to stay cheap
// for large canvases read every frame.
constexpr size_t kPixelAttempts = 256;

uint64_t SplitMix64(uint64_t& state) {
  uint64_t z = (state += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

base::span<uint8_t, 4u> Pixel(const SkPixmap& pixmap, int x, int y) {
  // SAFETY: callers pass coordinates inside `pixmap`, whose pixels are 4 bytes.
  return UNSAFE_BUFFERS(base::span<uint8_t, 4u>(
      static_cast<uint8_t*>(pixmap.writable_addr(x, y)), 4u));
}

// Changes one colour channel by one step; premultiplied values stay at or
// below alpha. Transparent pixels are left alone.
bool Nudge(base::span<uint8_t, 4u> pixel, size_t channel, bool premultiplied,
           bool opaque) {
  const uint8_t alpha = opaque ? 255 : pixel[3];
  if (!alpha) {
    return false;
  }
  uint8_t& value = pixel[channel];
  uint8_t next = value ^ 1;
  if (premultiplied && next > alpha) {
    next = value - 1;
  }
  value = next;
  return true;
}

}  // namespace

ZitterFingerprintNoise ZitterFingerprintNoise::ForSite(
    base::span<const uint8_t> key,
    const String& top_level_site) {
  const auto site = top_level_site.Utf8();
  const auto mac = crypto::hmac::SignSha256(key, base::as_byte_span(site));
  uint64_t seed = 0;
  for (size_t i = 0; i < sizeof(seed); ++i) {
    seed = (seed << 8) | mac[i];
  }
  return ZitterFingerprintNoise(seed);
}

void ZitterFingerprintNoise::ApplyToPixels(const SkPixmap& pixmap) const {
  const SkColorType type = pixmap.colorType();
  if (type != kRGBA_8888_SkColorType && type != kBGRA_8888_SkColorType) {
    return;
  }
  const int width = pixmap.width();
  const int height = pixmap.height();
  if (width <= 0 || height <= 0 || !pixmap.writable_addr()) {
    return;
  }
  const bool premultiplied = pixmap.alphaType() == kPremul_SkAlphaType;
  const bool opaque = pixmap.alphaType() == kOpaque_SkAlphaType;
  const uint64_t count = static_cast<uint64_t>(width) * height;
  // The same size of read gets the same pixels, so repeated reads agree.
  uint64_t state = seed_ ^ (static_cast<uint64_t>(width) << 32) ^ height;
  bool changed = false;
  for (size_t i = 0; i < std::min<uint64_t>(count, kPixelAttempts); ++i) {
    const uint64_t random = SplitMix64(state);
    const uint64_t index = random % count;
    changed |= Nudge(Pixel(pixmap, static_cast<int>(index % width),
                           static_cast<int>(index / width)),
                     (random >> 32) % 3, premultiplied, opaque);
  }
  // Sparse drawings may dodge every try; change their first visible pixel.
  for (int y = 0; !changed && y < height; ++y) {
    for (int x = 0; !changed && x < width; ++x) {
      changed = Nudge(Pixel(pixmap, x, y), seed_ % 3, premultiplied, opaque);
    }
  }
}

scoped_refptr<StaticBitmapImage> ZitterFingerprintNoise::ApplyToImage(
    scoped_refptr<StaticBitmapImage> image) const {
  if (!image) {
    return image;
  }
  PaintImage paint_image = image->PaintImageForCurrentFrame();
  const SkImageInfo info = paint_image.GetSkImageInfo();
  if (info.colorType() != kRGBA_8888_SkColorType &&
      info.colorType() != kBGRA_8888_SkColorType) {
    return image;
  }
  SkBitmap bitmap;
  if (!bitmap.tryAllocPixels(info) ||
      !paint_image.readPixels(info, bitmap.getPixels(), bitmap.rowBytes(), 0,
                              0)) {
    return image;
  }
  SkPixmap pixmap;
  if (!bitmap.peekPixels(&pixmap)) {
    return image;
  }
  ApplyToPixels(pixmap);
  bitmap.setImmutable();
  return UnacceleratedStaticBitmapImage::Create(
      SkImages::RasterFromBitmap(bitmap), image->Orientation());
}

void ZitterFingerprintNoise::ApplyToAudio(base::span<float> samples) const {
  // A different factor for every sample: a handful of shared factors would
  // let unrelated sites collide.
  uint64_t state = seed_;
  for (float& sample : samples) {
    const double random = static_cast<double>(SplitMix64(state) >> 11) * 0x1.0p-53;
    sample = static_cast<float>(sample * (1.0 - 1e-4 * random));
  }
}

}  // namespace blink

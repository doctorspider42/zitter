// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Chromium's former components/vector_icons/reload_custom.icon, removed in
// Chromium 159; Zitter keeps its reload glyph.
#ifndef ZITTER_CHROMIUM_PROTOTYPE_RELOAD_ICON_H_
#define ZITTER_CHROMIUM_PROTOTYPE_RELOAD_ICON_H_

#include "ui/gfx/vector_icon_types.h"

namespace zitter::prototype {
namespace reload_icon {

inline constexpr gfx::PathElement kPath32[] = {
    gfx::CANVAS_DIMENSIONS, 32,
    gfx::MOVE_TO, 25.1f, 20.15f,
    gfx::LINE_TO, 25.08f, 20.14f,
    gfx::CUBIC_TO, 23.51f, 23.59f, 20.04f, 26, 16, 26,
    gfx::CUBIC_TO, 10.48f, 26, 6, 21.52f, 6, 16,
    gfx::CUBIC_TO, 6, 10.48f, 10.48f, 6, 16, 6,
    gfx::CUBIC_TO, 19.02f, 6, 21.72f, 7.34f, 23.55f, 9.45f,
    gfx::LINE_TO, 23.55f, 9.45f,
    gfx::LINE_TO, 19, 14,
    gfx::LINE_TO, 25.8f, 14,
    gfx::LINE_TO, 28.83f, 14,
    gfx::LINE_TO, 30, 14,
    gfx::LINE_TO, 30, 3,
    gfx::LINE_TO, 25.67f, 7.33f,
    gfx::CUBIC_TO, 23.3f, 4.67f, 19.85f, 3, 16, 3,
    gfx::CUBIC_TO, 8.82f, 3, 3, 8.82f, 3, 16,
    gfx::CUBIC_TO, 3, 23.18f, 8.82f, 29, 16, 29,
    gfx::CUBIC_TO, 21.27f, 29, 25.8f, 25.86f, 27.84f, 21.34f,
    gfx::CUBIC_TO, 27.96f, 21.13f, 28.03f, 20.88f, 28.03f, 20.61f,
    gfx::CUBIC_TO, 28.03f, 19.78f, 27.36f, 19.11f, 26.53f, 19.11f,
    gfx::CUBIC_TO, 25.87f, 19.11f, 25.3f, 19.55f, 25.1f, 20.15f,
    gfx::CLOSE,
};
inline constexpr gfx::PathElement kPath16[] = {
    gfx::CANVAS_DIMENSIONS, 16,
    gfx::MOVE_TO, 15, 1,
    gfx::LINE_TO, 15, 7,
    gfx::LINE_TO, 9, 7,
    gfx::LINE_TO, 11.62f, 4.37f,
    gfx::CUBIC_TO, 10.67f, 3.43f, 9.44f, 3, 8, 3,
    gfx::CUBIC_TO, 5.09f, 3, 3, 5.1f, 3, 8,
    gfx::CUBIC_TO, 3, 10.9f, 5.09f, 13, 8, 13,
    gfx::CUBIC_TO, 10.3f, 13, 12.08f, 11.81f, 12.75f, 9.83f,
    gfx::LINE_TO, 12.77f, 9.79f,
    gfx::CUBIC_TO, 12.93f, 9.44f, 13.47f, 9.18f, 13.76f, 9.18f,
    gfx::CUBIC_TO, 14.15f, 9.18f, 14.79f, 9.41f, 14.79f, 10.01f,
    gfx::CUBIC_TO, 14.79f, 10.14f, 14.71f, 10.46f, 14.66f, 10.57f,
    gfx::CUBIC_TO, 13.66f, 13.13f, 10.98f, 15, 8.03f, 15,
    gfx::CUBIC_TO, 4.15f, 15, 1, 11.87f, 1, 8,
    gfx::CUBIC_TO, 1, 4.13f, 4.15f, 1, 8.03f, 1,
    gfx::CUBIC_TO, 9.96f, 1, 11.7f, 1.77f, 12.97f, 3.03f,
    gfx::CLOSE,
};
inline constexpr gfx::VectorIconRep kReps[] = {{kPath32}, {kPath16}};

}  // namespace reload_icon

inline constexpr gfx::VectorIcon kReloadIcon(reload_icon::kReps, 2, "zitter_reload");

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_RELOAD_ICON_H_

// SPDX-License-Identifier: ISC
// Copyright (c) 2026 Lucide Icons and Contributors
#ifndef ZITTER_CHROMIUM_PROTOTYPE_SETTINGS_ICON_H_
#define ZITTER_CHROMIUM_PROTOTYPE_SETTINGS_ICON_H_

#include "ui/gfx/vector_icon_types.h"

namespace zitter::prototype {
namespace settings_icon {

// Lucide Settings, as used in the assistant preview.
inline constexpr gfx::PathElement kPath[] = {
    gfx::CANVAS_DIMENSIONS, 24, gfx::STROKE, 2,
    gfx::MOVE_TO, 9.671f, 4.136f,
    gfx::R_ARC_TO, 2.34f, 2.34f, 0, 0, 1, 4.659f, 0,
    gfx::R_ARC_TO, 2.34f, 2.34f, 0, 0, 0, 3.319f, 1.915f,
    gfx::R_ARC_TO, 2.34f, 2.34f, 0, 0, 1, 2.33f, 4.033f,
    gfx::R_ARC_TO, 2.34f, 2.34f, 0, 0, 0, 0, 3.831f,
    gfx::R_ARC_TO, 2.34f, 2.34f, 0, 0, 1, -2.33f, 4.033f,
    gfx::R_ARC_TO, 2.34f, 2.34f, 0, 0, 0, -3.319f, 1.915f,
    gfx::R_ARC_TO, 2.34f, 2.34f, 0, 0, 1, -4.659f, 0,
    gfx::R_ARC_TO, 2.34f, 2.34f, 0, 0, 0, -3.32f, -1.915f,
    gfx::R_ARC_TO, 2.34f, 2.34f, 0, 0, 1, -2.33f, -4.033f,
    gfx::R_ARC_TO, 2.34f, 2.34f, 0, 0, 0, 0, -3.831f,
    gfx::ARC_TO, 2.34f, 2.34f, 0, 0, 1, 6.35f, 6.051f,
    gfx::R_ARC_TO, 2.34f, 2.34f, 0, 0, 0, 3.319f, -1.915f,
    gfx::NEW_PATH, gfx::STROKE, 2, gfx::CIRCLE, 12, 12, 3,
};
inline constexpr gfx::VectorIconRep kReps[] = {{kPath}};

}  // namespace settings_icon

inline constexpr gfx::VectorIcon kSettingsGearIcon(settings_icon::kReps, 1,
                                                  "zitter_settings_gear");

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_SETTINGS_ICON_H_

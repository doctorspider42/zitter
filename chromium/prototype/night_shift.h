// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_NIGHT_SHIFT_H_
#define ZITTER_CHROMIUM_PROTOTYPE_NIGHT_SHIFT_H_

class Profile;

namespace zitter::prototype {

inline constexpr char kNightShiftId[] = "kdpnmnbnedpcomicojommdikkdepclio";
void EnsureNightShift(Profile* profile);

}  // namespace zitter::prototype

#endif

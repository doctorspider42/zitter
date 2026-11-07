#ifndef ZITTER_CHROMIUM_APP_STARTUP_TIMING_H_
#define ZITTER_CHROMIUM_APP_STARTUP_TIMING_H_

#include <string_view>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"

namespace zitter {

// Raw monotonic timestamps keep log delivery outside the measured intervals.
inline void MarkStartup(std::string_view stage,
                        base::TimeTicks ticks = base::TimeTicks()) {
  const auto* command = base::CommandLine::ForCurrentProcess();
  if (!command->HasSwitch("zitter-startup-timing") ||
      command->HasSwitch("type")) {
    return;
  }
  if (ticks.is_null()) ticks = base::TimeTicks::Now();
  const auto line = base::StrCat(
      {"ZITTER_STARTUP ", stage, " ",
       base::NumberToString(ticks.since_origin().InMicroseconds()), "\n"});
  RAW_LOG(INFO, line.c_str());
}

}  // namespace zitter
#endif  // ZITTER_CHROMIUM_APP_STARTUP_TIMING_H_

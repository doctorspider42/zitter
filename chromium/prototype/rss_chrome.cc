// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/rss_reader.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_observer.h"

namespace zitter::prototype {
namespace {

constexpr char kReaderKey[] = "zitter.rss.reader";

class ChromeRssReader final : public RssReader, public ProfileObserver {
 public:
  explicit ChromeRssReader(Profile& profile) : RssReader(profile) {
    observation_.Observe(&profile);
  }

 private:
  void OnProfileWillBeDestroyed(Profile*) override {
    observation_.Reset();
    Shutdown();
  }

  base::ScopedObservation<Profile, ProfileObserver> observation_{this};
};

}  // namespace

RssReader* RssReader::For(Profile* profile) {
  if (auto* reader = profile->GetUserData(kReaderKey))
    return static_cast<RssReader*>(reader);
  auto reader = std::make_unique<ChromeRssReader>(*profile);
  auto* result = reader.get();
  profile->SetUserData(kReaderKey, std::move(reader));
  return result;
}

}  // namespace zitter::prototype

// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_ADBLOCK_FILTER_LISTS_H_
#define ZITTER_CHROMIUM_PROTOTYPE_ADBLOCK_FILTER_LISTS_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "url/gurl.h"

namespace network::mojom { class URLLoaderFactory; }

namespace zitter::prototype {

struct AdblockFilterList {
  std::string id, name, url;
  bool optional = false, default_on = true, builtin = false;
  size_t rules = 0;
  base::Time updated;
};

const std::vector<AdblockFilterList>& AdblockFilterListSources();

// Owns the shared cache. Each browser host decides which optional lists to
// fetch and which cached texts to use in each profile's engine.
class AdblockFilterLists {
 public:
  using Texts = std::shared_ptr<const std::vector<std::string>>;
  using Wanted = base::RepeatingCallback<bool(const AdblockFilterList&)>;
  using Factory = base::RepeatingCallback<network::mojom::URLLoaderFactory*()>;

  AdblockFilterLists(Wanted wanted, Factory factory,
                     base::RepeatingClosure changed,
                     base::RepeatingClosure status_changed);
  ~AdblockFilterLists();
  AdblockFilterLists(const AdblockFilterLists&) = delete;
  AdblockFilterLists& operator=(const AdblockFilterLists&) = delete;

  void Start(base::FilePath directory, bool automatic);
  void Refresh(bool force);
  void SetTextsForTesting(std::vector<std::string> texts);
  void SetLoopbackSourceForTesting(GURL origin);

  const std::vector<AdblockFilterList>& lists() const { return lists_; }
  const Texts& texts() const { return texts_; }
  bool updating() const { return !pending_.empty(); }
  const std::string& last_error() const { return last_error_; }

 private:
  struct CacheEntry;
  struct Fetch;
  class OwnedNetwork;
  using FetchResult = base::OnceCallback<void(std::optional<std::string>)>;

  static std::vector<CacheEntry> ReadCache(base::FilePath directory);
  void LoadCache(std::vector<CacheEntry> entries);
  void Download(const GURL& url, size_t limit, FetchResult result);
  void OnRoot(size_t index, std::optional<std::string> body);
  void NextInclude(std::unique_ptr<Fetch> fetch);
  void OnInclude(std::unique_ptr<Fetch> fetch, std::optional<std::string> part);
  void Store(size_t index, std::string text);
  void Stored(size_t index, std::string text, size_t rules, bool ok);
  void Finish(size_t index, bool ok);
  void Schedule(base::TimeDelta delay);
  GURL SourceURL(size_t index) const;

  const Wanted wanted_;
  const Factory factory_;
  const base::RepeatingClosure changed_;
  const base::RepeatingClosure status_changed_;
  std::vector<AdblockFilterList> lists_;
  Texts texts_;
  std::unique_ptr<OwnedNetwork> network_;
  base::FilePath directory_;
  GURL loopback_;
  std::set<size_t> pending_;
  std::string last_error_;
  bool ready_ = false;
  bool automatic_ = false;
  bool force_after_read_ = false;
  bool testing_texts_ = false;
  base::OneShotTimer timer_;
  base::WeakPtrFactory<AdblockFilterLists> weak_factory_{this};
};

}  // namespace zitter::prototype
#endif  // ZITTER_CHROMIUM_PROTOTYPE_ADBLOCK_FILTER_LISTS_H_

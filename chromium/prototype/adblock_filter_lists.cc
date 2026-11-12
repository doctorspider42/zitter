// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/adblock_filter_lists.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/no_destructor.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "chromium/prototype/adblock.rs.h"
#include "content/public/browser/network_service_instance.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/cert_verifier/public/mojom/cert_verifier_service_factory.mojom.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace zitter::prototype {
namespace {

constexpr size_t kMaxList = 16 * 1024 * 1024;
constexpr size_t kMaxIncludes = 24;
constexpr size_t kMinimumRules = 1000;
constexpr auto kListAge = base::Days(4);
constexpr auto kListTraffic = net::DefineNetworkTrafficAnnotation(
    "zitter_filter_lists", R"(
      semantics {
        sender: "Zitter ad blocking"
        description: "Downloads public filter lists used to block ads and trackers."
        trigger: "When enabled lists are missing or stale, or when the user requests an update."
        data: "None. No cookies or credentials."
        destination: OTHER
        destination_other: "Public filter list publishers"
      }
      policy {
        cookies_allowed: NO
        setting: "Settings > Ad blocking"
        policy_exception_justification: "Not implemented."
      })");

bool HasListHeader(std::string_view text) {
  return text.starts_with("[Adblock") || text.starts_with("! Title:");
}

bool ValidInclude(std::string_view name) {
  return !name.empty() && name.size() <= 80 && name != "." && name != ".." &&
         std::ranges::all_of(name, [](char c) {
           return base::IsAsciiAlphaNumeric(c) || c == '-' || c == '_' || c == '.';
         });
}

}  // namespace

const std::vector<AdblockFilterList>& AdblockFilterListSources() {
  static const base::NoDestructor<std::vector<AdblockFilterList>> lists(
      std::initializer_list<AdblockFilterList>{
          {"easylist", "EasyList", "https://easylist.to/easylist/easylist.txt"},
          {"easyprivacy", "EasyPrivacy", "https://easylist.to/easylist/easyprivacy.txt"},
          {"zitter", "Zamienniki Zittera", "", true, true, true},
          {"antiadblock", "Adblock Warning Removal List",
           "https://easylist-downloads.adblockplus.org/antiadblockfilters.txt", true},
          {"ublock", "uBlock filters",
           "https://ublockorigin.github.io/uAssets/filters/filters.txt", true, false},
      });
  return *lists;
}

class AdblockFilterLists::OwnedNetwork {
 public:
  network::mojom::URLLoaderFactory* Factory() {
    if (!context_ || !context_.is_connected()) {
      context_.reset();
      factory_.reset();
      auto params = network::mojom::NetworkContextParams::New();
      params->http_cache_enabled = false;
      params->cert_verifier_params = content::GetCertVerifierParams(
          cert_verifier::mojom::CertVerifierCreationParams::New());
      content::CreateNetworkContextInNetworkService(
          context_.BindNewPipeAndPassReceiver(), std::move(params));
    }
    if (!factory_ || !factory_.is_connected()) {
      factory_.reset();
      auto params = network::mojom::URLLoaderFactoryParams::New();
      params->process_id = network::OriginatingProcessId::browser();
      params->is_trusted = true;
      params->automatically_assign_isolation_info = true;
      context_->CreateURLLoaderFactory(factory_.BindNewPipeAndPassReceiver(),
                                        std::move(params));
    }
    return factory_.get();
  }

 private:
  mojo::Remote<network::mojom::NetworkContext> context_;
  mojo::Remote<network::mojom::URLLoaderFactory> factory_;
};

struct AdblockFilterLists::CacheEntry {
  std::string text;
  size_t rules = 0;
  base::Time updated;
};

struct AdblockFilterLists::Fetch {
  size_t index;
  std::string body;
  std::vector<std::pair<std::string, std::string>> includes;
  size_t next = 0;
  size_t remaining;
};

AdblockFilterLists::AdblockFilterLists(Wanted wanted, Factory factory,
                                       base::RepeatingClosure changed,
                                       base::RepeatingClosure status_changed)
    : wanted_(std::move(wanted)), factory_(std::move(factory)),
      changed_(std::move(changed)), status_changed_(std::move(status_changed)),
      lists_(AdblockFilterListSources()),
      network_(factory_ ? nullptr : std::make_unique<OwnedNetwork>()) {
  auto texts = std::make_shared<std::vector<std::string>>(lists_.size());
  for (size_t i = 0; i < lists_.size(); ++i) {
    if (lists_[i].builtin) {
      (*texts)[i] = std::string(adblock::builtin_list());
      lists_[i].rules = adblock::count_rules((*texts)[i]);
    }
  }
  texts_ = std::move(texts);
}

AdblockFilterLists::~AdblockFilterLists() = default;

void AdblockFilterLists::Start(base::FilePath directory, bool automatic) {
  if (!directory_.empty() || testing_texts_)
    return;
  directory_ = std::move(directory);
  automatic_ = automatic;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&AdblockFilterLists::ReadCache, directory_),
      base::BindOnce(&AdblockFilterLists::LoadCache, weak_factory_.GetWeakPtr()));
}

std::vector<AdblockFilterLists::CacheEntry> AdblockFilterLists::ReadCache(
    base::FilePath directory) {
  const auto& lists = AdblockFilterListSources();
  std::vector<CacheEntry> entries(lists.size());
  if (base::IsLink(directory))
    return entries;
  for (size_t i = 0; i < lists.size(); ++i) {
    if (lists[i].builtin)
      continue;
    const auto path = directory.AppendASCII(lists[i].id + ".txt");
    std::string text;
    base::File::Info info;
    if (base::IsLink(path) ||
        !base::ReadFileToStringWithMaxSize(path, &text, kMaxList) ||
        !HasListHeader(text) || !base::IsStringUTF8(text) ||
        !base::GetFileInfo(path, &info)) {
      continue;
    }
    const auto count = adblock::count_rules(text);
    if (count >= kMinimumRules)
      entries[i] = {std::move(text), count, info.last_modified};
  }
  return entries;
}

void AdblockFilterLists::LoadCache(std::vector<CacheEntry> entries) {
  auto next = std::make_shared<std::vector<std::string>>(*texts_);
  for (size_t i = 0; i < entries.size(); ++i) {
    if (!entries[i].text.empty()) {
      lists_[i].rules = entries[i].rules;
      lists_[i].updated = entries[i].updated;
      (*next)[i] = std::move(entries[i].text);
    }
  }
  texts_ = std::move(next);
  ready_ = true;
  changed_.Run();
  if (std::exchange(force_after_read_, false))
    Refresh(true);
  else
    Schedule(base::Seconds(30));
}

void AdblockFilterLists::Schedule(base::TimeDelta delay) {
  if (automatic_) {
    timer_.Start(FROM_HERE, delay,
                 base::BindOnce(&AdblockFilterLists::Refresh,
                                weak_factory_.GetWeakPtr(), false));
  }
}

void AdblockFilterLists::Refresh(bool force) {
  if (directory_.empty() || testing_texts_)
    return;
  if (!ready_) {
    force_after_read_ |= force;
    return;
  }
  if (!updating())
    last_error_.clear();
  const auto now = base::Time::Now();
  for (size_t i = 0; i < lists_.size(); ++i) {
    const auto& list = lists_[i];
    if (list.builtin || !wanted_.Run(list) || pending_.contains(i) ||
        (!force && !list.updated.is_null() && now - list.updated < kListAge)) {
      continue;
    }
    pending_.insert(i);
    Download(SourceURL(i), kMaxList,
             base::BindOnce(&AdblockFilterLists::OnRoot,
                            weak_factory_.GetWeakPtr(), i));
  }
  status_changed_.Run();
  Schedule(base::Hours(24));
}

GURL AdblockFilterLists::SourceURL(size_t index) const {
  return loopback_.is_empty()
             ? GURL(lists_[index].url)
             : loopback_.Resolve("/adblock-lists/" + lists_[index].id + ".txt");
}

void AdblockFilterLists::Download(const GURL& url, size_t limit,
                                  FetchResult result) {
  auto* factory = factory_ ? factory_.Run() : network_->Factory();
  if (!factory || limit == 0) {
    std::move(result).Run(std::nullopt);
    return;
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->redirect_mode = network::mojom::RedirectMode::kError;
  auto loader = network::SimpleURLLoader::Create(std::move(request), kListTraffic);
  loader->SetTimeoutDuration(base::Seconds(30));
  loader->SetRetryOptions(1, network::SimpleURLLoader::RETRY_ON_NETWORK_CHANGE);
  auto* raw = loader.get();
  raw->DownloadToTempFile(
      factory,
      base::BindOnce([](std::unique_ptr<network::SimpleURLLoader> loader,
                        size_t limit, FetchResult result, base::FilePath path) {
        if (path.empty()) {
          std::move(result).Run(std::nullopt);
          return;
        }
        const auto* response = loader->ResponseInfo();
        const bool success = response && response->headers &&
                             response->headers->response_code() == 200;
        base::ThreadPool::PostTaskAndReplyWithResult(
            FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                        base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
            base::BindOnce([](base::FilePath path, size_t limit, bool success)
                               -> std::optional<std::string> {
              std::string body;
              const bool read = success &&
                  base::ReadFileToStringWithMaxSize(path, &body, limit);
              base::DeleteFile(path);
              if (!read || !base::IsStringUTF8(body))
                return std::nullopt;
              return body;
            }, std::move(path), limit, success), std::move(result));
      }, std::move(loader), limit, std::move(result)), limit);
}

void AdblockFilterLists::OnRoot(size_t index, std::optional<std::string> body) {
  if (!body || !HasListHeader(*body)) {
    Finish(index, false);
    return;
  }
  auto fetch = std::make_unique<Fetch>();
  fetch->index = index;
  fetch->remaining = kMaxList - body->size();
  fetch->body = std::move(*body);
  for (auto line : base::SplitStringPiece(fetch->body, "\n",
                                         base::TRIM_WHITESPACE,
                                         base::SPLIT_WANT_NONEMPTY)) {
    if (!line.starts_with("!#include "))
      continue;
    const auto name = base::TrimWhitespaceASCII(line.substr(10), base::TRIM_ALL);
    if (!ValidInclude(name) || fetch->includes.size() == kMaxIncludes) {
      Finish(index, false);
      return;
    }
    fetch->includes.emplace_back(name, "");
  }
  if (fetch->includes.empty()) {
    Store(index, std::move(fetch->body));
    return;
  }
  NextInclude(std::move(fetch));
}

void AdblockFilterLists::NextInclude(std::unique_ptr<Fetch> fetch) {
  const GURL url = SourceURL(fetch->index).Resolve(fetch->includes[fetch->next].first);
  const size_t limit = fetch->remaining;
  Download(url, limit,
           base::BindOnce(&AdblockFilterLists::OnInclude,
                          weak_factory_.GetWeakPtr(), std::move(fetch)));
}

void AdblockFilterLists::OnInclude(std::unique_ptr<Fetch> fetch,
                                   std::optional<std::string> part) {
  if (!part || part->empty() || part->size() > fetch->remaining) {
    Finish(fetch->index, false);
    return;
  }
  fetch->remaining -= part->size();
  fetch->includes[fetch->next++].second = std::move(*part);
  if (fetch->next < fetch->includes.size()) {
    NextInclude(std::move(fetch));
    return;
  }
  std::string merged;
  size_t next = 0;
  for (auto line : base::SplitStringPiece(fetch->body, "\n",
                                         base::KEEP_WHITESPACE,
                                         base::SPLIT_WANT_ALL)) {
    if (base::TrimWhitespaceASCII(line, base::TRIM_ALL).starts_with("!#include "))
      merged.append(fetch->includes[next++].second);
    else
      merged.append(line);
    merged.push_back('\n');
  }
  Store(fetch->index, std::move(merged));
}

void AdblockFilterLists::Store(size_t index, std::string text) {
  const auto path = directory_.AppendASCII(lists_[index].id + ".txt");
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                  base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce([](base::FilePath path, std::string text) {
        const size_t rules = text.size() <= kMaxList ? adblock::count_rules(text) : 0;
        const bool ok = rules >= kMinimumRules && !base::IsLink(path.DirName()) &&
                        base::CreateDirectory(path.DirName()) && !base::IsLink(path) &&
                        base::ImportantFileWriter::WriteFileAtomically(path, text);
        return std::make_tuple(std::move(text), rules, ok);
      }, path, std::move(text)),
      base::BindOnce(&AdblockFilterLists::Stored,
                     weak_factory_.GetWeakPtr(), index));
}

void AdblockFilterLists::Stored(size_t index, std::string text, size_t rules,
                                 bool ok) {
  if (ok) {
    auto next = std::make_shared<std::vector<std::string>>(*texts_);
    (*next)[index] = std::move(text);
    texts_ = std::move(next);
    lists_[index].rules = rules;
    lists_[index].updated = base::Time::Now();
    changed_.Run();
  }
  Finish(index, ok);
}

void AdblockFilterLists::Finish(size_t index, bool ok) {
  pending_.erase(index);
  if (!ok)
    last_error_ = lists_[index].name;
  status_changed_.Run();
}

void AdblockFilterLists::SetTextsForTesting(std::vector<std::string> texts) {
  testing_texts_ = true;
  timer_.Stop();
  weak_factory_.InvalidateWeakPtrs();
  pending_.clear();
  texts.resize(lists_.size());
  for (size_t i = 0; i < lists_.size(); ++i) {
    if (lists_[i].builtin)
      texts[i] = std::string(adblock::builtin_list());
    lists_[i].rules = adblock::count_rules(texts[i]);
    lists_[i].updated = base::Time::Now();
  }
  texts_ = std::make_shared<const std::vector<std::string>>(std::move(texts));
  changed_.Run();
}

void AdblockFilterLists::SetLoopbackSourceForTesting(GURL origin) {
  CHECK(origin.SchemeIsHTTPOrHTTPS() && origin.host() == "127.0.0.1" && origin.has_port());
  loopback_ = std::move(origin);
}

}  // namespace zitter::prototype

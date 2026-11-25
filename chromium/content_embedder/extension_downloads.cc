// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "extension_downloads.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/i18n/time_formatting.h"
#include "base/callback_list.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "components/download/public/common/download_interrupt_reasons.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/download_url_parameters.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_manager.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_function.h"
#include "extensions/browser/extension_function_registry.h"
#include "extensions/common/extension.h"
#include "extensions/common/permissions/permissions_data.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "profile_context.h"
#include "services/network/public/cpp/resource_request_body.h"
#include "url/gurl.h"

namespace zitter::content_embedder {
namespace {

using download::DownloadItem;

const char* DangerName(download::DownloadDangerType type) {
  switch (type) {
    case download::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE:
      return "file";
    case download::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
      return "url";
    case download::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
      return "content";
    case download::DOWNLOAD_DANGER_TYPE_UNCOMMON_CONTENT:
      return "uncommon";
    case download::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST:
      return "host";
    case download::DOWNLOAD_DANGER_TYPE_POTENTIALLY_UNWANTED:
      return "unwanted";
    case download::DOWNLOAD_DANGER_TYPE_USER_VALIDATED:
      return "accepted";
    default:
      return "safe";
  }
}

const char* StateName(DownloadItem::DownloadState state) {
  switch (state) {
    case DownloadItem::COMPLETE:
      return "complete";
    case DownloadItem::INTERRUPTED:
    case DownloadItem::CANCELLED:
      return "interrupted";
    default:
      return "in_progress";
  }
}

// downloads.DownloadItem, as Chrome reports it.
base::DictValue DownloadDetails(const DownloadItem& item) {
  auto result = base::DictValue()
                    .Set("id", static_cast<int>(item.GetId()))
                    .Set("url", item.GetOriginalUrl().spec())
                    .Set("finalUrl", item.GetURL().spec())
                    .Set("referrer", item.GetReferrerUrl().spec())
                    .Set("filename", item.GetTargetFilePath().AsUTF8Unsafe())
                    .Set("incognito", false)
                    .Set("danger", DangerName(item.GetDangerType()))
                    .Set("mime", item.GetMimeType())
                    .Set("startTime", base::TimeFormatAsIso8601(item.GetStartTime()))
                    .Set("state", StateName(item.GetState()))
                    .Set("paused", item.IsPaused())
                    .Set("canResume", item.CanResume())
                    .Set("bytesReceived", static_cast<double>(item.GetReceivedBytes()))
                    .Set("totalBytes", static_cast<double>(item.GetTotalBytes()))
                    .Set("fileSize", static_cast<double>(
                        item.GetState() == DownloadItem::COMPLETE ? item.GetReceivedBytes() : -1))
                    .Set("exists", !item.GetFileExternallyRemoved());
  if (!item.GetEndTime().is_null())
    result.Set("endTime", base::TimeFormatAsIso8601(item.GetEndTime()));
  if (item.GetState() == DownloadItem::CANCELLED) {
    result.Set("error", "USER_CANCELED");
  } else if (item.GetState() == DownloadItem::INTERRUPTED) {
    result.Set("error", download::DownloadInterruptReasonToString(item.GetLastReason()));
  }
  return result;
}

// Never create the manager before the profile has configured its downloads:
// that would freeze the download delegate's setup halfway.
content::DownloadManager* Manager(content::BrowserContext* context) {
  if (!context || context->IsOffTheRecord() ||
      !static_cast<ProfileContext*>(context)->downloads_enabled()) {
    return nullptr;
  }
  return context->GetDownloadManager();
}

std::vector<DownloadItem*> UserDownloads(content::DownloadManager& manager) {
  content::DownloadManager::DownloadVector all;
  manager.GetAllDownloads(&all);
  std::vector<DownloadItem*> items;
  for (DownloadItem* item : all) {
    if (!item->IsTransient() && !item->IsTemporary())
      items.push_back(item);
  }
  return items;
}

// downloads.DownloadQuery: the fields extensions use to find their downloads.
std::vector<DownloadItem*> FindDownloads(content::DownloadManager& manager,
                                         const base::DictValue& query) {
  auto items = UserDownloads(manager);
  const auto id = query.FindInt("id");
  const auto* url = query.FindString("url");
  const auto* final_url = query.FindString("finalUrl");
  const auto* state = query.FindString("state");
  const auto* filename = query.FindString("filename");
  const auto exists = query.FindBool("exists");
  const auto paused = query.FindBool("paused");
  const auto* terms = query.FindList("query");
  std::erase_if(items, [&](DownloadItem* item) {
    if ((id && static_cast<int>(item->GetId()) != *id) ||
        (url && item->GetOriginalUrl().spec() != *url) ||
        (final_url && item->GetURL().spec() != *final_url) ||
        (state && StateName(item->GetState()) != *state) ||
        (filename && item->GetTargetFilePath().AsUTF8Unsafe() != *filename) ||
        (exists && item->GetFileExternallyRemoved() == *exists) ||
        (paused && item->IsPaused() != *paused)) {
      return true;
    }
    if (terms) {
      const auto haystack = base::ToLowerASCII(
          item->GetURL().spec() + " " + item->GetTargetFilePath().AsUTF8Unsafe());
      for (const auto& term : *terms) {
        if (!term.is_string())
          continue;
        const auto needle = base::ToLowerASCII(term.GetString());
        const bool negative = needle.starts_with('-');
        const bool found = haystack.find(negative ? needle.substr(1) : needle) != std::string::npos;
        if (found == negative)
          return true;
      }
    }
    return false;
  });
  bool newest_first = true;
  if (const auto* order = query.FindList("orderBy"); order && !order->empty() &&
                                                     order->front().is_string()) {
    newest_first = order->front().GetString() != "startTime";
  }
  std::ranges::sort(items, [&](DownloadItem* a, DownloadItem* b) {
    return newest_first ? a->GetStartTime() > b->GetStartTime()
                        : a->GetStartTime() < b->GetStartTime();
  });
  const int limit = query.FindInt("limit").value_or(1000);
  if (limit > 0 && items.size() > static_cast<size_t>(limit))
    items.resize(limit);
  return items;
}

class DownloadsFunction : public ExtensionFunction {
 protected:
  ~DownloadsFunction() override = default;

  content::DownloadManager* ManagerFor() {
    return extension() && extension()->permissions_data()->HasAPIPermission(
                              extensions::mojom::APIPermissionID::kDownloads)
               ? Manager(browser_context())
               : nullptr;
  }

  DownloadItem* ItemFor(int id) {
    auto* manager = ManagerFor();
    auto* item = manager ? manager->GetDownload(static_cast<uint32_t>(id)) : nullptr;
    return item && !item->IsTransient() && !item->IsTemporary() ? item : nullptr;
  }
};

class DownloadsDownloadFunction final : public DownloadsFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.download", DOWNLOADS_DOWNLOAD)

 private:
  ~DownloadsDownloadFunction() override = default;

  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(args().size() == 1 && args()[0].is_dict());
    const auto& options = args()[0].GetDict();
    auto* manager = ManagerFor();
    const auto* address = options.FindString("url");
    const GURL url(address ? *address : std::string());
    if (!manager)
      return RespondNow(Error("Downloads are not available here."));
    if (!url.is_valid() || !(url.SchemeIsHTTPOrHTTPS() || url.SchemeIs("data")) ||
        url.has_username() || url.has_password()) {
      return RespondNow(Error("Invalid URL."));
    }
    std::u16string name;
    if (const auto* filename = options.FindString("filename"); filename && !filename->empty()) {
      const auto path = base::FilePath::FromUTF8Unsafe(*filename);
      if (path.IsAbsolute() || path.ReferencesParent())
        return RespondNow(Error("Invalid filename."));
      // The download folder holds files, not the extension's subfolders.
      name = base::UTF8ToUTF16(path.BaseName().AsUTF8Unsafe());
    }
    constexpr auto annotation = net::DefineNetworkTrafficAnnotation("zitter_extension_download", R"(
      semantics {
        sender: "Extension downloads API"
        description: "Downloads a file that an installed extension requested."
        trigger: "An extension with the downloads permission calls downloads.download."
        data: "The URL, method, headers and body the extension supplied."
        destination: WEBSITE
      }
      policy {
        cookies_allowed: YES
        cookies_store: "user"
        setting: "Remove the extension in Settings to stop its downloads."
        policy_exception_justification: "The user installed and approved the extension."
      })");
    auto params = std::make_unique<download::DownloadUrlParameters>(url, annotation);
    params->set_download_source(download::DownloadSource::EXTENSION_API);
    params->set_content_initiated(false);
    params->set_initiator(extension()->origin());
    if (!name.empty())
      params->set_suggested_name(name);
    if (const auto* method = options.FindString("method")) {
      if (*method != "GET" && *method != "POST")
        return RespondNow(Error("Invalid method."));
      params->set_method(*method);
    }
    if (const auto* body = options.FindString("body"))
      params->set_post_body(network::ResourceRequestBody::CreateFromCopyOfBytes(
          base::as_byte_span(*body)));
    if (const auto* headers = options.FindList("headers")) {
      for (const auto& header : *headers) {
        const auto* entry = header.GetIfDict();
        const auto* key = entry ? entry->FindString("name") : nullptr;
        const auto* value = entry ? entry->FindString("value") : nullptr;
        if (!key || !value)
          return RespondNow(Error("Invalid header."));
        params->add_request_header(*key, *value);
      }
    }
    params->set_callback(base::BindOnce(&DownloadsDownloadFunction::OnStarted, this));
    manager->DownloadUrl(std::move(params));
    return RespondLater();
  }

  void OnStarted(DownloadItem* item, download::DownloadInterruptReason reason) {
    if (!item || reason != download::DOWNLOAD_INTERRUPT_REASON_NONE) {
      Respond(Error(download::DownloadInterruptReasonToString(reason)));
      return;
    }
    Respond(WithArguments(static_cast<int>(item->GetId())));
  }
};

class DownloadsSearchFunction final : public DownloadsFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.search", DOWNLOADS_SEARCH)

 private:
  ~DownloadsSearchFunction() override = default;
  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(args().size() == 1 && args()[0].is_dict());
    auto* manager = ManagerFor();
    if (!manager)
      return RespondNow(Error("Downloads are not available here."));
    base::ListValue result;
    for (auto* item : FindDownloads(*manager, args()[0].GetDict()))
      result.Append(DownloadDetails(*item));
    return RespondNow(WithArguments(std::move(result)));
  }
};

enum class ItemAction { kPause, kResume, kCancel, kOpen, kShow };

template <ItemAction kAction>
class DownloadsItemFunction : public DownloadsFunction {
 protected:
  ~DownloadsItemFunction() override = default;
  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(!args().empty() && args()[0].is_int());
    auto* item = ItemFor(args()[0].GetInt());
    if (!item)
      return RespondNow(Error("Invalid download id " + base::NumberToString(args()[0].GetInt())));
    switch (kAction) {
      case ItemAction::kPause:
        if (item->GetState() != DownloadItem::IN_PROGRESS)
          return RespondNow(Error("Download must be in progress"));
        item->Pause();
        break;
      case ItemAction::kResume:
        if (!item->CanResume())
          return RespondNow(Error("Download must be resumable"));
        item->Resume(/*user_resume=*/true);
        break;
      case ItemAction::kCancel:
        if (item->GetState() == DownloadItem::IN_PROGRESS)
          item->Cancel(/*user_cancel=*/true);
        break;
      case ItemAction::kOpen:
        if (!extension()->permissions_data()->HasAPIPermission(
                extensions::mojom::APIPermissionID::kDownloadsOpen) ||
            !user_gesture()) {
          return RespondNow(Error("User gesture and the downloads.open permission are required"));
        }
        if (item->GetState() != DownloadItem::COMPLETE)
          return RespondNow(Error("Download must be complete"));
        item->OpenDownload();
        break;
      case ItemAction::kShow:
        item->ShowDownloadInShell();
        break;
    }
    return RespondNow(NoArguments());
  }
};

#define ZITTER_DOWNLOAD_ITEM_FUNCTION(Class, Action, Name, Histogram)       \
  class Class final : public DownloadsItemFunction<ItemAction::Action> {    \
   public:                                                                  \
    DECLARE_EXTENSION_FUNCTION(Name, Histogram)                             \
   private:                                                                 \
    ~Class() override = default;                                            \
  };
ZITTER_DOWNLOAD_ITEM_FUNCTION(DownloadsPauseFunction, kPause, "downloads.pause", DOWNLOADS_PAUSE)
ZITTER_DOWNLOAD_ITEM_FUNCTION(DownloadsResumeFunction, kResume, "downloads.resume", DOWNLOADS_RESUME)
ZITTER_DOWNLOAD_ITEM_FUNCTION(DownloadsCancelFunction, kCancel, "downloads.cancel", DOWNLOADS_CANCEL)
ZITTER_DOWNLOAD_ITEM_FUNCTION(DownloadsOpenFunction, kOpen, "downloads.open", DOWNLOADS_OPEN)
ZITTER_DOWNLOAD_ITEM_FUNCTION(DownloadsShowFunction, kShow, "downloads.show", DOWNLOADS_SHOW)
#undef ZITTER_DOWNLOAD_ITEM_FUNCTION

// Removes downloads from the list. The files stay, as in Chrome.
class DownloadsEraseFunction final : public DownloadsFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.erase", DOWNLOADS_ERASE)

 private:
  ~DownloadsEraseFunction() override = default;
  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(args().size() == 1 && args()[0].is_dict());
    auto* manager = ManagerFor();
    if (!manager)
      return RespondNow(Error("Downloads are not available here."));
    base::ListValue erased;
    for (auto* item : FindDownloads(*manager, args()[0].GetDict())) {
      erased.Append(static_cast<int>(item->GetId()));
      item->Remove();
    }
    return RespondNow(WithArguments(std::move(erased)));
  }
};

class DownloadsRemoveFileFunction final : public DownloadsFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("downloads.removeFile", DOWNLOADS_REMOVEFILE)

 private:
  ~DownloadsRemoveFileFunction() override = default;
  ResponseAction Run() override {
    EXTENSION_FUNCTION_VALIDATE(!args().empty() && args()[0].is_int());
    auto* item = ItemFor(args()[0].GetInt());
    if (!item || item->GetState() != DownloadItem::COMPLETE || item->GetFileExternallyRemoved())
      return RespondNow(Error("Download must be complete and its file must exist"));
    item->DeleteFile(base::BindOnce(&DownloadsRemoveFileFunction::OnDeleted, this));
    return RespondLater();
  }
  void OnDeleted(bool success) {
    Respond(success ? NoArguments() : Error("Could not remove the file."));
  }
};

// The download list replaces Chrome's shelf; there is nothing to hide.
template <bool kAccepted>
class DownloadsUnsupportedFunction : public DownloadsFunction {
 protected:
  ~DownloadsUnsupportedFunction() override = default;
  ResponseAction Run() override {
    return kAccepted ? RespondNow(NoArguments())
                     : RespondNow(Error("This downloads function is not supported in Zitter."));
  }
};

#define ZITTER_DOWNLOAD_STUB(Class, Accepted, Name, Histogram)     \
  class Class final : public DownloadsUnsupportedFunction<Accepted> { \
   public:                                                          \
    DECLARE_EXTENSION_FUNCTION(Name, Histogram)                     \
   private:                                                         \
    ~Class() override = default;                                    \
  };
ZITTER_DOWNLOAD_STUB(DownloadsSetShelfEnabledFunction, true, "downloads.setShelfEnabled",
                     DOWNLOADS_SETSHELFENABLED)
ZITTER_DOWNLOAD_STUB(DownloadsSetUiOptionsFunction, true, "downloads.setUiOptions",
                     DOWNLOADS_SETUIOPTIONS)
ZITTER_DOWNLOAD_STUB(DownloadsGetFileIconFunction, false, "downloads.getFileIcon",
                     DOWNLOADS_GETFILEICON)
ZITTER_DOWNLOAD_STUB(DownloadsAcceptDangerFunction, false, "downloads.acceptDanger",
                     DOWNLOADS_ACCEPTDANGER)
ZITTER_DOWNLOAD_STUB(DownloadsShowDefaultFolderFunction, false, "downloads.showDefaultFolder",
                     DOWNLOADS_SHOWDEFAULTFOLDER)
#undef ZITTER_DOWNLOAD_STUB

class DownloadEvents final : public ExtensionDownloadEvents,
                             public content::DownloadManager::Observer,
                             public DownloadItem::Observer {
 public:
  // Follows the manager only once the profile creates it, so extensions
  // never change when or how the profile sets up its downloads.
  explicit DownloadEvents(content::BrowserContext& context) : context_(context) {
    auto& profile = static_cast<ProfileContext&>(context);
    if (auto* manager = profile.attached_download_manager()) {
      Attach(*manager);
      return;
    }
    subscription_ = profile.AddDownloadManagerCallback(base::BindRepeating(
        [](base::WeakPtr<DownloadEvents> events, content::DownloadManager& manager) {
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
              FROM_HERE, base::BindOnce(&DownloadEvents::AttachTo, events, &manager));
        },
        weak_factory_.GetWeakPtr()));
  }
  ~DownloadEvents() override = default;

 private:
  void AttachTo(content::DownloadManager* manager) {
    if (static_cast<ProfileContext&>(*context_).attached_download_manager() == manager)
      Attach(*manager);
  }

  void Attach(content::DownloadManager& manager) {
    if (manager_observation_.IsObserving())
      return;
    manager_observation_.Observe(&manager);
    for (auto* item : UserDownloads(manager))
      Track(*item);
  }

  void Track(DownloadItem& item) {
    if (item.IsTransient() || item.IsTemporary() || item_observations_.IsObservingSource(&item))
      return;
    item_observations_.AddObservation(&item);
    last_[item.GetId()] = DownloadDetails(item);
  }

  void Broadcast(extensions::events::HistogramValue histogram, const char* name,
                 base::ListValue args) {
    extensions::EventRouter::Get(&*context_)->BroadcastEvent(
        std::make_unique<extensions::Event>(histogram, name, base::MakeRefCounted<extensions::EventArgs>(std::move(args)), &*context_));
  }

  // content::DownloadManager::Observer:
  void OnDownloadCreated(content::DownloadManager*, DownloadItem* item) override {
    if (item->IsTransient() || item->IsTemporary())
      return;
    Track(*item);
    Broadcast(extensions::events::DOWNLOADS_ON_CREATED, "downloads.onCreated",
              base::ListValue().Append(DownloadDetails(*item)));
  }
  void ManagerGoingDown(content::DownloadManager*) override {
    item_observations_.RemoveAllObservations();
    manager_observation_.Reset();
  }

  // DownloadItem::Observer:
  void OnDownloadUpdated(DownloadItem* item) override {
    auto current = DownloadDetails(*item);
    auto& previous = last_[item->GetId()];
    static constexpr const char* kKeys[] = {
        "url", "finalUrl", "filename", "danger", "mime", "startTime", "endTime", "state",
        "canResume", "paused", "error", "totalBytes", "fileSize", "exists"};
    auto delta = base::DictValue().Set("id", static_cast<int>(item->GetId()));
    bool changed = false;
    for (const char* key : kKeys) {
      const auto* before = previous.Find(key);
      const auto* after = current.Find(key);
      if ((!before && !after) || (before && after && *before == *after))
        continue;
      base::DictValue change;
      if (before)
        change.Set("previous", before->Clone());
      if (after)
        change.Set("current", after->Clone());
      delta.Set(key, std::move(change));
      changed = true;
    }
    previous = std::move(current);
    if (changed) {
      Broadcast(extensions::events::DOWNLOADS_ON_CHANGED, "downloads.onChanged",
                base::ListValue().Append(std::move(delta)));
    }
  }
  void OnDownloadRemoved(DownloadItem* item) override {
    last_.erase(item->GetId());
    Broadcast(extensions::events::DOWNLOADS_ON_ERASED, "downloads.onErased",
              base::ListValue().Append(static_cast<int>(item->GetId())));
  }
  void OnDownloadDestroyed(DownloadItem* item) override {
    last_.erase(item->GetId());
    item_observations_.RemoveObservation(item);
  }

  const raw_ref<content::BrowserContext> context_;
  std::map<uint32_t, base::DictValue> last_;
  base::ScopedObservation<content::DownloadManager, content::DownloadManager::Observer>
      manager_observation_{this};
  base::ScopedMultiSourceObservation<DownloadItem, DownloadItem::Observer>
      item_observations_{this};
  base::CallbackListSubscription subscription_;
  base::WeakPtrFactory<DownloadEvents> weak_factory_{this};
};

}  // namespace

void RegisterExtensionDownloadFunctions(ExtensionFunctionRegistry& registry) {
  registry.RegisterFunction<DownloadsDownloadFunction>();
  registry.RegisterFunction<DownloadsSearchFunction>();
  registry.RegisterFunction<DownloadsPauseFunction>();
  registry.RegisterFunction<DownloadsResumeFunction>();
  registry.RegisterFunction<DownloadsCancelFunction>();
  registry.RegisterFunction<DownloadsOpenFunction>();
  registry.RegisterFunction<DownloadsShowFunction>();
  registry.RegisterFunction<DownloadsEraseFunction>();
  registry.RegisterFunction<DownloadsRemoveFileFunction>();
  registry.RegisterFunction<DownloadsSetShelfEnabledFunction>();
  registry.RegisterFunction<DownloadsSetUiOptionsFunction>();
  registry.RegisterFunction<DownloadsGetFileIconFunction>();
  registry.RegisterFunction<DownloadsAcceptDangerFunction>();
  registry.RegisterFunction<DownloadsShowDefaultFolderFunction>();
}

std::unique_ptr<ExtensionDownloadEvents> CreateExtensionDownloadEvents(
    content::BrowserContext& context) {
  if (context.IsOffTheRecord())
    return nullptr;
  return std::make_unique<DownloadEvents>(context);
}

}  // namespace zitter::content_embedder

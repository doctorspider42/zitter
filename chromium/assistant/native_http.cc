// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include "base/environment.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/run_loop.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_executor.h"
#include "base/timer/timer.h"
#include "chromium/assistant/native_transport.h"
#include "base/json/json_reader.h"
#include "net/base/address_list.h"
#include "net/base/elements_upload_data_stream.h"
#include "net/base/host_port_pair.h"
#include "net/base/io_buffer.h"
#include "net/base/load_flags.h"
#include "net/base/network_anonymization_key.h"
#include "net/base/upload_bytes_element_reader.h"
#include "net/base/url_util.h"
#include "net/dns/host_resolver.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"
#include "net/log/net_log.h"
#include "net/log/net_log_with_source.h"
#include "net/proxy_resolution/proxy_config_service_fixed.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_info.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "third_party/re2/src/re2/re2.h"
#include "url/gurl.h"
namespace zitter::assistant {
void runtime_stream(rust::Str id, rust::Str kind, rust::Str chunk);
namespace {
constexpr auto kProviderTraffic =
    net::DefineNetworkTrafficAnnotation("zitter_native_assistant_network", R"(
  semantics { sender: "Zitter native assistant helper"
    description: "Sends explicitly shared conversation context to the chosen model backend and reads public research sources."
    trigger: "An assistant message or a configured task requires a model or authorized web research."
    data: "Conversation text, permitted page context and screenshots, model configuration, provider credentials, or research queries."
    destination: OTHER destination_other: "The selected model provider, local Ollama server or public HTTPS research source." }
  policy { cookies_allowed: NO setting: "The assistant and each data permission can be disabled in Settings."
    policy_exception_justification: "Not implemented." })");
std::string Json(base::DictValue v) {
  return base::WriteJson(v).value_or("{}");
}
std::string Failure(std::string_view message) {
  return Json(base::DictValue().Set("error", message));
}
bool PublicIP(const net::IPAddress& ip) {
  if (!ip.IsPubliclyRoutable() || ip.IsIPv4MappedIPv6())
    return false;
  if (ip.IsIPv6()) {
    const auto& b = ip.bytes();
    return (b[0] & 0xe0) == 0x20 && !(b[0] == 0x20 && b[1] == 0x01) &&
           !(b[0] == 0x20 && b[1] == 0x02);
  }
  return true;
}
bool PublicURL(const GURL& url) {
  return url.is_valid() && url.spec().size() <= 8192 && url.SchemeIs("https") &&
         !url.has_username() && !url.has_password() &&
         url.EffectiveIntPort() == 443;
}
struct Response {
  int status = 0;
  std::string body, location, mime, error, retry_after;
};
// Each request gets an ephemeral context, direct proxy configuration and no
// profile services. Public DNS is checked once and pinned into that context.
class Fetch final : public net::URLRequest::Delegate {
 public:
  Fetch(std::string id,
        std::string kind,
        GURL url,
        std::string body,
        bool public_only)
      : id_(std::move(id)),
        kind_(std::move(kind)),
        url_(url),
        upload_(std::move(body)),
        public_(public_only),
        buffer_(base::MakeRefCounted<net::IOBufferWithSize>(32768)) {}
  Response Run() {
    started_ = base::TimeTicks::Now();
    timer_.Start(FROM_HERE, base::Milliseconds(100),
                 base::BindRepeating(&Fetch::Tick, base::Unretained(this)));
    resolver_ = net::HostResolver::CreateStandaloneResolver(net::NetLog::Get());
    std::string mappings;
    if (public_) {
      if (!PublicURL(url_)) {
        result_.error = "Research only reads public HTTPS pages.";
        return result_;
      }
      resolve_ = resolver_->CreateRequest(
          net::HostPortPair::FromURL(url_), net::NetworkAnonymizationKey(),
          net::handles::kInvalidNetworkHandle, net::NetLogWithSource(),
          std::nullopt);
      int status = resolve_->Start(
          base::BindOnce(&Fetch::Resolved, base::Unretained(this)));
      if (status == net::ERR_IO_PENDING) {
        loop_.Run();
        status = resolved_;
      }
      if (!result_.error.empty())
        return result_;
      if (status != net::OK || resolve_->GetAddressResults().empty()) {
        result_.error = "Research DNS lookup failed";
        return result_;
      }
      for (const auto& address : resolve_->GetAddressResults())
        if (!PublicIP(address.address())) {
          result_.error = "Private network destinations are blocked.";
          return result_;
        }
      mappings = "MAP " + std::string(url_.host()) + " " +
                 resolve_->GetAddressResults().front().ToStringWithoutPort() +
                 ", MAP * ~NOTFOUND";
      // ^NOTFOUND is Chromium's explicit resolver failure target.
      base::ReplaceSubstringsAfterOffset(&mappings, 0, "~NOTFOUND",
                                         "^NOTFOUND");
    }
    net::URLRequestContextBuilder builder;
    builder.DisableHttpCache();
    builder.set_proxy_config_service(
        std::make_unique<net::ProxyConfigServiceFixed>(
            net::ProxyConfigWithAnnotation::CreateDirect()));
    if (public_)
      builder.set_host_mapping_rules(mappings);
    context_ = builder.Build();
    request_ = context_->CreateRequest(url_, net::DEFAULT_PRIORITY, this,
                                       kProviderTraffic,
                                       net::handles::kInvalidNetworkHandle);
    request_->set_disallow_credentials();
    request_->SetLoadFlags(net::LOAD_DISABLE_CACHE |
                           net::LOAD_DO_NOT_SAVE_COOKIES);
    request_->SetExtraRequestHeaderByName(
        "User-Agent", public_ ? "ZitterResearch/1.0" : "ZitterAssistant/1.0",
        true);
    if (public_) request_->SetExtraRequestHeaderByName("Accept", "text/html,text/plain,application/json", true);
    if (!public_ && kind_ != "ollama-models") {
      request_->set_method("POST");
      request_->SetExtraRequestHeaderByName("Content-Type", "application/json",
                                            true);
      request_->set_upload(net::ElementsUploadDataStream::CreateWithReader(
          net::UploadOwnedBytesElementReader::CreateWithString(upload_)));
      if (kind_ == "openai") {
        auto env=base::Environment::Create();
        auto key=env->GetVar("ZITTER_OPENAI_API_KEY");
        if(!key||key->empty())key=env->GetVar("OPENAI_API_KEY");
        if (!key || key->empty() || !net::HttpUtil::IsValidHeaderValue(*key)) {
          result_.error = "Add your OpenAI API key in Settings → AI assistant.";
          return result_;
        }
        request_->SetExtraRequestHeaderByName("Authorization", "Bearer " + *key,
                                              true);
      }
    }
    base::RunLoop transfer;
    quit_ = transfer.QuitClosure();
    request_->Start();
    transfer.Run();
    timer_.Stop();
    request_.reset();
    context_.reset();
    return result_;
  }
  void OnReceivedRedirect(net::URLRequest* request,
                          const net::RedirectInfo& info,
                          bool* defer) override {
    *defer = true;
    result_.status = request->GetResponseCode();
    result_.location = info.new_url.spec();
    request->Cancel();
    if (quit_)
      quit_.Run();
  }
  void OnResponseStarted(net::URLRequest* request, int status) override {
    if (status != net::OK) {
      Stop("Network request failed");
      return;
    }
    result_.status = request->GetResponseCode();
    request->response_headers()->GetMimeType(&result_.mime);
    result_.retry_after = request->response_headers()
                              ->EnumerateHeader(nullptr, "retry-after")
                              .value_or("");
    if (result_.status != 200) {
      request->Cancel();
      if (quit_) quit_.Run();
      return;
    }
    Read();
  }
  void OnReadCompleted(net::URLRequest*, int bytes) override {
    if (Consume(bytes))
      Read();
  }

 private:
  void Tick() {
    if (cancelled(id_) ||
        base::TimeTicks::Now() - started_ >
            (public_ ? base::Seconds(20) : base::Seconds(180)))
      Stop("Request stopped or timed out");
  }
  void Resolved(int status) {
    resolved_ = status;
    loop_.Quit();
  }
  void Stop(std::string_view message) {
    result_.error = message;
    if (request_)
      request_->Cancel();
    if (quit_)
      quit_.Run();
    else
      loop_.Quit();
  }
  bool Consume(int bytes) {
    if (bytes < 0) {
      Stop("Network stream failed");
      return false;
    }
    if (bytes == 0) {
      if (quit_)
        quit_.Run();
      return false;
    }
    result_.body.append(buffer_->data(), bytes);
    if (result_.body.size() > (public_ ? 2u * 1024 * 1024 : 4u * 1024 * 1024)) {
      Stop("Response exceeds assistant limit");
      return false;
    }
    // Feed complete UTF-8 lines, never split a code point at a read boundary.
    if (!public_) {
      pending_.append(buffer_->data(), bytes);
      auto end = pending_.rfind('\n');
      if (end != std::string::npos) {
        const auto frame = pending_.substr(0, end + 1);
        if (!base::IsStringUTF8(frame)) {
          Stop("Provider stream was not valid UTF-8");
          return false;
        }
        runtime_stream(id_, kind_, frame);
        pending_.erase(0, end + 1);
      }
    }
    return true;
  }
  void Read() {
    for (;;) {
      int bytes = request_->Read(buffer_.get(), buffer_->size());
      if (bytes == net::ERR_IO_PENDING || !Consume(bytes))
        return;
    }
  }
  std::string id_, kind_;
  GURL url_;
  std::string upload_;
  bool public_;
  Response result_;
  scoped_refptr<net::IOBufferWithSize> buffer_;
  std::string pending_;
  std::unique_ptr<net::HostResolver> resolver_;
  std::unique_ptr<net::HostResolver::ResolveHostRequest> resolve_;
  std::unique_ptr<net::URLRequestContext> context_;
  std::unique_ptr<net::URLRequest> request_;
  base::TimeTicks started_;
  base::RepeatingTimer timer_;
  base::RunLoop loop_;
  int resolved_ = net::ERR_FAILED;
  base::RepeatingClosure quit_;
};
Response FetchURL(const std::string& id,
                  const std::string& kind,
                  const GURL& url,
                  const std::string& body,
                  bool research) {
  Response response;
  std::thread thread([&] {
    base::SingleThreadTaskExecutor tasks(base::MessagePumpType::IO);
    Fetch fetch(id, kind, url, body, research);
    response = fetch.Run();
  });
  thread.join();
  return response;
}
std::string Plain(std::string html) {
  for (auto tag : {"script", "style", "noscript", "svg", "form"}) {
    re2::RE2 pattern("(?is)<" + std::string(tag) + "\\b[^>]*>.*?</" + tag +
                     "\\s*>");
    re2::RE2::GlobalReplace(&html, pattern, " ");
  }
  re2::RE2::GlobalReplace(&html, re2::RE2("<[^>]*>"), " ");
  html = base::UTF16ToUTF8(base::UnescapeForHTML(base::UTF8ToUTF16(html)));
  re2::RE2::GlobalReplace(&html, re2::RE2("\\s+"), " ");
  base::TrimWhitespaceASCII(html, base::TRIM_ALL, &html);
  return html;
}
base::ListValue Links(const std::string& html, const GURL& base_url) {
  base::ListValue links;
  std::set<std::string> seen;
  re2::RE2 pattern(
      R"re((?is)<a\b[^>]*href\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))[^>]*>(.*?)</a>)re");
  std::string_view input(html);
  std::string quoted, single, unquoted, label;
  while (links.size() < 80 &&
         re2::RE2::FindAndConsume(&input, pattern, &quoted, &single, &unquoted,
                                  &label)) {
    auto href = base::UTF16ToUTF8(
        base::UnescapeForHTML(base::UTF8ToUTF16(!quoted.empty()   ? quoted
                                                : !single.empty() ? single
                                                                  : unquoted)));
    auto url = base_url.Resolve(href);
    std::string target;
    if (url.DomainIs("duckduckgo.com") &&
        net::GetValueForKeyInQuery(url, "uddg", &target))
      url = GURL(target);
    GURL::Replacements clear;
    clear.ClearRef();
    url = url.ReplaceComponents(clear);
    if (!url.SchemeIs("https") || url.has_username() || url.has_password() ||
        !seen.insert(url.spec()).second)
      continue;
    links.Append(base::DictValue()
                     .Set("title", std::string(base::TruncateUTF8ToByteSize(
                                       Plain(label), 180)))
                     .Set("url", url.spec()));
  }
  return links;
}
struct CachedPage {
  base::TimeTicks until;
  base::DictValue page;
};
struct WebCache {
  std::mutex mutex;
  std::map<std::string, CachedPage> pages;
  std::map<std::string, std::weak_ptr<std::timed_mutex>> in_flight;
};
WebCache& Cache() {
  static base::NoDestructor<WebCache> cache;
  return *cache;
}
std::string Timestamp() {
  base::Time::Exploded d;
  base::Time::Now().UTCExplode(&d);
  return base::StringPrintf("%04d-%02d-%02dT%02d:%02d:%02dZ", d.year, d.month,
                            d.day_of_month, d.hour, d.minute, d.second);
}
std::string Research(std::string id, const base::DictValue& request) {
  const auto* args = request.FindDict("args");
  if (!args)
    return Failure("Invalid research request");
  const bool search =
      request.FindString("tool") && *request.FindString("tool") == "web_search";
  std::string raw;
  if (search) {
    const auto* query = args->FindString("query");
    if (!query || query->empty() || base::UTF8ToUTF16(*query).size() > 600)
      return Failure("Invalid search query");
    raw = "https://html.duckduckgo.com/html/?q=" +
          base::EscapeQueryParamValue(*query, false);
  } else {
    const auto* url = args->FindString("url");
    if (!url)
      return Failure("Invalid research URL");
    raw = *url;
  }
  GURL url(raw);
  if (!PublicURL(url))
    return Failure("Research only reads public HTTPS pages.");
  const auto key = url.spec();
  std::shared_ptr<std::timed_mutex> gate;
  {
    std::lock_guard lock(Cache().mutex);
    std::erase_if(Cache().in_flight,
                  [](const auto& entry) { return entry.second.expired(); });
    gate = Cache().in_flight[key].lock();
    if (!gate) {
      gate = std::make_shared<std::timed_mutex>();
      Cache().in_flight[key] = gate;
    }
  }
  // A canceled subscriber never aborts another session's shared page read.
  std::unique_lock<std::timed_mutex> flight(*gate, std::defer_lock);
  while (!flight.try_lock_for(std::chrono::milliseconds(100))) {
    if (cancelled(id))
      return Failure("Assistant task stopped.");
  }
  if (cancelled(id))
    return Failure("Assistant task stopped.");
  base::DictValue page;
  {
    std::lock_guard lock(Cache().mutex);
    auto old = Cache().pages.find(key);
    if (old != Cache().pages.end() &&
        old->second.until > base::TimeTicks::Now())
      page = old->second.page.Clone();
  }
  if (page.empty()) {
    for (int hop = 0; hop < 5; ++hop) {
      const auto response = FetchURL(id, "research", url, "", true);
      if (!response.error.empty())
        return Failure(response.error);
      if (!response.location.empty()) {
        url = GURL(response.location);
        if (!PublicURL(url))
          return Failure("Private or unsupported redirect blocked");
        continue;
      }
      if (response.status != 200)
        return Failure("Research page could not be read");
      if (response.mime != "text/html" && response.mime != "text/plain" &&
          response.mime != "application/json" &&
          response.mime != "application/xhtml+xml")
        return Failure("Unsupported page type");
      page = base::DictValue()
                 .Set("url", url.spec())
                 .Set("text", std::string(base::TruncateUTF8ToByteSize(
                                  Plain(response.body), 24000)))
                 .Set("links", Links(response.body, url))
                 .Set("fetched_at", Timestamp());
      break;
    }
    if (page.empty())
      return Failure("Too many redirects");
    std::lock_guard lock(Cache().mutex);
    if (Cache().pages.size() >= 128)
      Cache().pages.erase(Cache().pages.begin());
    Cache().pages.insert_or_assign(
        key,
        CachedPage{base::TimeTicks::Now() + base::Minutes(5), page.Clone()});
  }
  if (!search)
    return Json(std::move(page));
  base::ListValue results;
  for (const auto& link : *page.FindList("links")) {
    const auto& d = link.GetDict();
    if (GURL(*d.FindString("url")).DomainIs("duckduckgo.com") ||
        d.FindString("title")->empty())
      continue;
    results.Append(link.Clone());
    if (results.size() == 10)
      break;
  }
  return Json(base::DictValue()
                  .Set("search_url", *page.FindString("url"))
                  .Set("text", *page.FindString("text"))
                  .Set("results", std::move(results)));
}
}  // namespace
std::string PerformHTTP(std::string id, const base::DictValue& request) {
  const auto* kind = request.FindString("provider");
  if (!kind)
    return Failure("Unknown provider");
  if (*kind == "research")
    return Research(id, request);
  GURL url("https://api.openai.com/v1/responses");
  const bool list_models = *kind == "ollama-models";
  if (*kind == "ollama" || list_models) {
    auto env = base::Environment::Create();
    GURL endpoint(
        env->GetVar("ZITTER_OLLAMA_URL").value_or("http://127.0.0.1:11434"));
    if (!endpoint.is_valid() || !endpoint.SchemeIs("http") ||
        endpoint.has_username() || endpoint.has_password() ||
        (endpoint.host() != "localhost" && endpoint.host() != "127.0.0.1" &&
         endpoint.host() != "[::1]"))
      return Failure("Ollama must use a local HTTP endpoint.");
    url = endpoint.Resolve(list_models ? "/api/tags" : "/api/chat");
  } else if (*kind != "openai")
    return Failure("Unknown provider");
  if (list_models) {
    const auto response = FetchURL(id, *kind, url, "", false);
    if (response.status != 200) return Failure("Could not list models. Start Ollama and check its local address.");
    const auto parsed = base::JSONReader::ReadDict(response.body, base::JSON_PARSE_RFC);
    const auto* models = parsed ? parsed->FindList("models") : nullptr;
    if (!models) return Failure("Ollama returned an invalid model list.");
    base::ListValue names;
    for (const auto& entry : *models) {
      const auto* model = entry.GetIfDict();
      const auto* name = model ? model->FindString("name") : nullptr;
      if (name && !name->empty() && name->size() <= 120) names.Append(*name);
      if (names.size() == 32) break;
    }
    return Json(base::DictValue().Set("models", std::move(names)));
  }
  const auto* body = request.FindDict("body");
  if (!body)
    return Failure("Invalid provider request");
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (cancelled(id))
      return Failure("Assistant task stopped.");
    auto response = FetchURL(id, *kind, url, Json(body->Clone()), false);
    if (!response.error.empty())
      return Failure(response.error);
    if (response.status == 200) {
      if (!base::IsStringUTF8(response.body))
        return Failure("Provider response was not valid UTF-8");
      return Json(base::DictValue().Set("body", response.body));
    }
    if (attempt < 2 && (response.status == 429 || response.status == 500 ||
                        response.status == 502 || response.status == 503 ||
                        response.status == 504)) {
      double retry = 0;
      const double seconds =
          base::StringToDouble(response.retry_after, &retry) &&
                  std::isfinite(retry) && retry > 0
              ? std::clamp(retry, 0.25, 10.0)
              : 0.5 * (1 << attempt);
      const int ticks = static_cast<int>(std::ceil(seconds * 10));
      for (int tick = 0; tick < ticks; ++tick) {
        if (cancelled(id))
          return Failure("Assistant task stopped.");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      continue;
    }
    if (response.status == 401 && *kind == "openai")
      return Failure("OpenAI rejected the API key. Check the key in settings.");
    if (response.status == 403 || response.status == 404)
      return Failure("The service did not find this model or your account cannot use it.");
    if (response.status == 429) return Failure("The provider limit was reached. Check your plan or API balance.");
    return Failure("Provider request failed. Check credentials, endpoint and model.");
  }
  return Failure("Provider request failed.");
}
}  // namespace zitter::assistant

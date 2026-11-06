// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#include "chromium/prototype/rss_reader.h"
#include <algorithm>
#include <map>
#include <set>
#include "base/files/file_util.h"
#include "base/no_destructor.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/data_decoder/public/cpp/safe_xml_parser.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
namespace zitter::prototype {
namespace {
constexpr size_t kMaxBody = 2 * 1024 * 1024;
constexpr size_t kMaxFeeds = 50, kMaxArticles = 100;
const char kReaderKey[] = "zitter.rss.reader";
using Namespaces = std::map<std::string, std::string>;
Namespaces Names(const base::Value& node, Namespaces names) {
  if (node.is_dict()) if (auto* ns = node.GetDict().FindDict("namespaces"))
    for (const auto [key, value] : *ns) if (value.is_string()) names[key] = value.GetString();
  return names;
}
bool Named(const base::Value& node, std::string_view local, bool atom, const Namespaces& inherited) {
  std::string tag;
  if (!data_decoder::GetXmlElementTagName(node, &tag)) return false;
  const auto colon = tag.find(':');
  const std::string prefix = colon == std::string::npos ? "" : tag.substr(0, colon);
  if ((colon == std::string::npos ? tag : tag.substr(colon + 1)) != local) return false;
  std::string_view uri;
  if (auto found=inherited.find(prefix);found!=inherited.end()) uri=found->second;
  if (node.is_dict()) if (auto* declared=node.GetDict().FindDict("namespaces"))
    if (auto* value=declared->FindString(prefix)) uri=*value;
  return atom ? uri == "http://www.w3.org/2005/Atom" : prefix.empty() && uri.empty();
}
GURL Base(const base::Value& node, GURL base) {
  auto value = data_decoder::GetXmlElementAttribute(node, "xml:base");
  return value.empty() ? base : base.Resolve(value);
}
std::string Truncate(std::string value, size_t maximum) {
  if (value.size() <= maximum) return value;
  while (maximum && (static_cast<unsigned char>(value[maximum]) & 0xc0) == 0x80) --maximum;
  value.resize(maximum); return value;
}
std::string Content(const base::Value& node, int depth = 0) {
  if (depth > 32 || !node.is_dict()) return "";
  if (auto* text = node.GetDict().FindString("text")) return Truncate(*text, 4000);
  std::string result;
  if (auto* children = data_decoder::GetXmlElementChildren(node)) for (const auto& child : *children) {
    std::string tag; data_decoder::GetXmlElementTagName(child, &tag);
    const auto colon = tag.find(':'); if (colon != std::string::npos) tag.erase(0, colon + 1);
    const bool block = tag == "p" || tag == "div" || tag == "br" || tag == "li";
    if (block && !result.empty()) result += "\n\n";
    result += Content(child, depth + 1);
    if (block) result += "\n\n";
    if (result.size() >= 4000) return Truncate(result, 4000);
  }
  return result;
}
std::string Readable(std::string text) {
  std::string out; bool space = false; int newlines = 0;
  for (char c : text) {
    if (c == '\r') continue;
    if (c == '\n') { if (!out.empty() && newlines < 2) out += '\n'; ++newlines; space = false; }
    else if (c == ' ' || c == '\t') { space = true; }
    else { if (space && !out.empty() && out.back() != '\n') out += ' '; out += c; space = false; newlines = 0; }
  }
  base::TrimWhitespaceASCII(out, base::TRIM_ALL, &out); return Truncate(out, 4000);
}
std::string Entities(std::string_view text) {
  std::string out;
  for (size_t i = 0; i < text.size();) {
    if (text[i] != '&') { out += text[i++]; continue; }
    auto end = text.find(';', i + 1);
    if (end == std::string_view::npos || end-i > 32) { out += text[i++]; continue; }
    const auto token = text.substr(i, end-i+1);
    const auto name = text.substr(i+1,end-i-1);
    uint32_t scalar = 0; bool numeric = false;
    if (name.starts_with("#x") || name.starts_with("#X")) numeric = base::HexStringToUInt(name.substr(2),&scalar);
    else if (name.starts_with('#')) numeric = base::StringToUint(name.substr(1),&scalar);
    if (numeric) {
      if (!scalar || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) scalar = 0xfffd;
      base::WriteUnicodeCharacter(scalar,&out);
    } else {
      static const base::NoDestructor<std::map<std::string_view,std::string_view>> extra({
        {"apos","'"},{"nbsp"," "},{"copy","©"},{"reg","®"},{"trade","™"},
        {"hellip","…"},{"mdash","—"},{"ndash","–"},{"lsquo","‘"},{"rsquo","’"},
        {"ldquo","“"},{"rdquo","”"},{"bull","•"},{"euro","€"},{"pound","£"},{"times","×"}});
      if (auto found=extra->find(name);found!=extra->end()) out += found->second;
      else out += base::UTF16ToUTF8(base::UnescapeForHTML(base::UTF8ToUTF16(token)));
    }
    i=end+1;
  }
  return out;
}
// Inert HTML-to-text conversion. This never instantiates a document, executes
// script, resolves an external resource or interprets decoded '<' a second time.
std::string HtmlText(std::string text) {
  std::string out, suppressed;
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '<' && i + 1 < text.size() && (base::IsAsciiAlpha(text[i+1]) || text[i+1] == '/' || text[i+1] == '!')) {
      const auto end = text.find('>', i + 1); if (end == std::string::npos) { out += text.substr(i); break; }
      auto token = base::ToLowerASCII(text.substr(i + 1, end - i - 1));
      bool closing = token.starts_with('/'); if (closing) token.erase(0, 1);
      auto name = token.substr(0, token.find_first_of(" /\t\n"));
      if (name == "script" || name == "style") { if (closing && suppressed == name) suppressed.clear(); else if (!closing) suppressed = name; }
      if (suppressed.empty() && (name == "p" || name == "div" || name == "br" || name == "li")) out += "\n\n";
      i = end + 1;
    } else { if (suppressed.empty()) out += text[i]; ++i; }
  }
  return Readable(Entities(out));
}
std::string Field(const base::Value& node, bool atom, bool rss_html = false) {
  const auto type = data_decoder::GetXmlElementAttribute(node, "type");
  auto text = Content(node);
  return ((atom && type == "html") || (!atom && rss_html)) ? HtmlText(text) : Readable(text);
}
GURL Clean(GURL url, bool strip_fragment = true) {
  if (!url.SchemeIsHTTPOrHTTPS() || url.has_username() || url.has_password() || url.spec().size() > 2048) return {};
  if (!strip_fragment) return url;
  GURL::Replacements replace; replace.ClearRef(); return url.ReplaceComponents(replace);
}
}
// Bounds apply to all visited nodes, including rejected/empty entries. The
// network byte limit alone does not bound namespace or empty-node complexity.
bool RssTreeWithinBudget(const base::Value& node, size_t& nodes, int depth = 0) {
  if (++nodes > 12000 || depth > 64 || !node.is_dict()) return false;
  if (auto* ns = node.GetDict().FindDict("namespaces"); ns && ns->size() > 128) return false;
  if (auto* children = data_decoder::GetXmlElementChildren(node))
    for (const auto& child : *children) if (!RssTreeWithinBudget(child,nodes,depth+1)) return false;
  return true;
}
std::optional<RssFeed> DecodeRss(const base::Value& tree, const GURL& final_url) {
  size_t nodes = 0; if (!RssTreeWithinBudget(tree,nodes)) return std::nullopt;
  const auto names = Names(tree, {});
  const bool atom = Named(tree, "feed", true, names);
  const base::Value* channel = &tree;
  if (!atom) {
    if (!Named(tree, "rss", false, names)) return std::nullopt;
    channel = data_decoder::GetXmlElementChildWithTag(tree, "channel");
    if (!channel) return std::nullopt;
  }
  RssFeed feed; feed.url = Clean(final_url); feed.title = final_url.host();
  const auto channel_names = Names(*channel, names);
  const auto channel_base = Base(*channel, atom ? final_url : Base(tree, final_url));
  const auto* children = data_decoder::GetXmlElementChildren(*channel);
  if (!children) return std::nullopt;
  std::set<std::string> ids, urls;
  for (const auto& child : *children) {
    if (Named(child, "title", atom, channel_names)) feed.title = Truncate(Field(child, atom), 512);
    if (!Named(child, atom ? "entry" : "item", atom, channel_names)) continue;
    RssArticle entry;
    const auto entry_base = Base(child, channel_base);
    const auto entry_names = Names(child, channel_names);
    auto* parts = data_decoder::GetXmlElementChildren(child); if (!parts) continue;
    for (const auto& part : *parts) {
      if (Named(part, "title", atom, entry_names)) entry.title = Truncate(Field(part, atom), 1024);
      if (Named(part, atom ? "id" : "guid", atom, entry_names)) entry.id = Truncate(Content(part), 1024);
      if (Named(part, atom ? "published" : "pubDate", atom, entry_names) || (entry.date.empty() && Named(part, "updated", atom, entry_names))) entry.date = Truncate(Field(part, atom), 128);
      if (Named(part, atom ? "summary" : "description", atom, entry_names) && entry.text.empty()) entry.text = Field(part, atom, true);
      std::string tag; data_decoder::GetXmlElementTagName(part, &tag);
      if (Named(part, "content", atom, entry_names) || (!atom && [&] {
        auto ns = Names(part, entry_names); const auto colon = tag.find(':');
        return colon != std::string::npos && tag.substr(colon+1) == "encoded" && ns[tag.substr(0, colon)] == "http://purl.org/rss/1.0/modules/content/";
      }())) entry.text = Field(part, atom, true);
      if (Named(part, "link", atom, entry_names)) {
        auto rel = data_decoder::GetXmlElementAttribute(part, "rel");
        if (!atom || rel.empty() || rel == "alternate") {
          auto link = atom ? data_decoder::GetXmlElementAttribute(part, "href") : Content(part);
          auto url = Clean(Base(part, entry_base).Resolve(link), false); if (url.is_valid()) entry.url = url;
        }
      }
    }
    if (!entry.url.is_valid()) continue;
    if (entry.id.empty()) entry.id = entry.url.spec();
    if (entry.title.empty()) entry.title = entry.url.host();
    if (!base::Time::FromUTCString(entry.date.c_str(), &entry.published)) entry.published = {};
    feed.articles.push_back(std::move(entry));
  }
  std::stable_sort(feed.articles.begin(),feed.articles.end(),[](const auto& x,const auto& y){return x.published>y.published;});
  size_t retained=0;
  std::erase_if(feed.articles,[&](const auto& a){
    if (retained>=kMaxArticles || ids.contains(a.id) || urls.contains(a.url.spec())) return true;
    ids.insert(a.id); urls.insert(a.url.spec()); ++retained; return false;
  });
  return feed;
}
RssReader* RssReader::For(Profile* profile) {
  if (auto* reader = profile->GetUserData(kReaderKey)) return static_cast<RssReader*>(reader);
  auto reader = std::make_unique<RssReader>(profile); auto* ptr = reader.get();
  profile->SetUserData(kReaderKey, std::move(reader)); return ptr;
}
RssReader::RssReader(Profile* profile) : profile_(profile), files_(base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {
  observation_.Observe(profile);
  timer_.Start(FROM_HERE, base::Minutes(30), this, &RssReader::Refresh);
  if (profile_->IsOffTheRecord()) { ready_ = true; return; }
  files_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce([](base::FilePath path) {
    std::string text; if (!base::ReadFileToStringWithMaxSize(path, &text, 64 * 1024 * 1024)) return base::DictValue();
    auto value = base::JSONReader::Read(text, base::JSON_PARSE_RFC);
    return value && value->is_dict() ? std::move(*value).TakeDict() : base::DictValue();
  }, profile->GetPath().AppendASCII("ZitterRSS.json")), base::BindOnce([](base::WeakPtr<RssReader> self, base::DictValue state) {
    if (!self) return;
    self->Restore(std::move(state)); self->ready_ = true; self->Notify(); self->Refresh();
  }, weak_.GetWeakPtr()));
}
RssReader::~RssReader() { if (save_timer_.IsRunning()) Write(); }
void RssReader::OnProfileWillBeDestroyed(Profile*) {
  if (save_timer_.IsRunning()) Write();
  observation_.Reset(); timer_.Stop(); save_timer_.Stop(); loader_.reset(); weak_.InvalidateWeakPtrs(); profile_ = nullptr;
}
base::CallbackListSubscription RssReader::Observe(base::RepeatingClosure changed) { return changed_.Add(std::move(changed)); }
void RssReader::Notify() { changed_.Notify(); }
void RssReader::Add(GURL url) {
  url = Clean(url);
  if (!ready_ || !url.is_valid()) { status_ = "Wpisz adres kanału HTTP lub HTTPS."; Notify(); return; }
  if (std::any_of(feeds_.begin(), feeds_.end(), [&](const auto& f) { return f.url == url; })) { status_ = "Ten kanał jest już subskrybowany."; Notify(); return; }
  if (feeds_.size() >= kMaxFeeds) { status_ = "Możesz subskrybować do 50 kanałów."; Notify(); return; }
  feeds_.push_back({url, std::string(url.host()), {}, {}}); queue_.push_back(url); Save(); Next(); Notify();
}
void RssReader::Remove(GURL url) {
  std::erase_if(feeds_, [&](const auto& f) { return f.url == url; });
  std::erase(queue_, url); Save(); Notify();
}
void RssReader::Refresh() {
  if (!ready_ || busy_) return;
  for (const auto& feed : feeds_) queue_.push_back(feed.url);
  Next();
}
void RssReader::Next() {
  if (busy_) return;
  if (queue_.empty()) { Notify(); return; }
  fetching_ = queue_.front(); queue_.erase(queue_.begin()); busy_ = true;
  auto request = std::make_unique<network::ResourceRequest>(); request->url = fetching_;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE | net::LOAD_DO_NOT_SAVE_COOKIES;
  constexpr auto annotation = net::DefineNetworkTrafficAnnotation("zitter_rss", R"(semantics {
    sender: "Zitter RSS reader" description: "Fetches public RSS or Atom subscriptions."
    trigger: "User subscribes or refreshes; every 30 minutes while running."
    data: "The subscription URL, without cookies or authentication." destination: WEBSITE }
    policy { cookies_allowed: NO setting: "Remove the subscription in RSS reader." policy_exception_justification: "User requested public feeds." })");
  loader_ = network::SimpleURLLoader::Create(std::move(request), annotation);
  loader_->SetTimeoutDuration(base::Seconds(20));
  loader_->DownloadToString(profile_->GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess().get(), base::BindOnce(&RssReader::Downloaded, weak_.GetWeakPtr()), kMaxBody);
  Notify();
}
void RssReader::Downloaded(std::optional<std::string> body) {
  const auto final_url = Clean(loader_->GetFinalURL()); loader_.reset();
  // DTDs and entity declarations are unnecessary for feeds. Reject before the
  // sandboxed parser, including UTF-16/32 inputs (NULs), to avoid entity loads.
  if (!body || !final_url.is_valid() || body->find('\0') != std::string::npos || base::ToLowerASCII(*body).find("<!doctype") != std::string::npos || base::ToLowerASCII(*body).find("<!entity") != std::string::npos) { Finish(std::nullopt); return; }
  data_decoder::DataDecoder::ParseXmlIsolated(*body, data_decoder::mojom::XmlParser::WhitespaceBehavior::kPreserveSignificant,
    base::BindOnce(&RssReader::Parsed, weak_.GetWeakPtr(), final_url));
}
void RssReader::Parsed(GURL final_url, data_decoder::DataDecoder::ValueOrError result) {
  if (!result.has_value()) { Finish(std::nullopt); return; }
  base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE, {base::TaskPriority::USER_VISIBLE},
    base::BindOnce([](base::Value tree, GURL url) { return DecodeRss(tree,url); },std::move(*result),final_url),
    base::BindOnce(&RssReader::Finish,weak_.GetWeakPtr()));
}
void RssReader::Finish(std::optional<RssFeed> feed) {
  auto found = std::find_if(feeds_.begin(), feeds_.end(), [&](const auto& f) { return f.url == fetching_; });
  if (found != feeds_.end()) {
    if (!feed) found->error = "Could not refresh this feed. Check its URL or try again.";
    else {
      // A redirect alias must not create a second subscription.
      auto duplicate = std::find_if(feeds_.begin(), feeds_.end(), [&](const auto& f) { return &f != &*found && f.url == feed->url; });
      auto merge = [&](const RssFeed& previous) {
        for (auto& article : feed->articles) for (const auto& old : previous.articles)
          if (old.id == article.id || old.url == article.url) { article.read = article.read || old.read; article.saved = article.saved || old.saved; break; }
        for (const auto& old : previous.articles) if (old.saved && std::none_of(feed->articles.begin(), feed->articles.end(), [&](const auto& a) { return a.id == old.id || a.url == old.url; })) {
          if (feed->articles.size() >= kMaxArticles) {
            auto discard = std::find_if(feed->articles.rbegin(), feed->articles.rend(), [](const auto& a) { return !a.saved; });
            if (discard != feed->articles.rend()) feed->articles.erase(std::next(discard).base());
          }
          feed->articles.push_back(old);
        }
        for (const auto& old : previous.articles) if (feed->articles.size() < kMaxArticles && std::none_of(feed->articles.begin(), feed->articles.end(), [&](const auto& a) { return a.id == old.id || a.url == old.url; })) feed->articles.push_back(old);
      };
      merge(*found);
      if (duplicate != feeds_.end()) {
        merge(*duplicate); *duplicate = std::move(*feed); feeds_.erase(found);
        status_ = "Kanał pod docelowym adresem jest już subskrybowany.";
      } else { *found = std::move(*feed); status_.clear(); }
    }
    Save();
  }
  busy_ = false; fetching_ = {}; Next(); Notify();
}
void RssReader::Mark(GURL feed, std::string id, bool read, bool saved) {
  size_t count = 0; for (const auto& f : feeds_) for (const auto& a : f.articles) if (a.saved) ++count;
  if (saved && count >= 500) {
    bool already = false; for (const auto& f : feeds_) if (f.url == feed) for (const auto& a : f.articles) if (a.id == id) already = a.saved;
    if (!already) { status_ = "Możesz zapisać do 500 artykułów."; Notify(); return; }
  }
  for (auto& f : feeds_) if (f.url == feed) for (auto& a : f.articles) if (a.id == id) { a.read = read; a.saved = saved; }
  Save(); Notify();
}
void RssReader::MarkAll(GURL feed) { for (auto& f : feeds_) if (feed.is_empty() || f.url == feed) for (auto& a : f.articles) a.read = true; Save(); Notify(); }
base::DictValue RssReader::Serialize() const {
  base::DictValue state; base::ListValue feeds;
  for (const auto& f : feeds_) {
    base::DictValue feed; feed.Set("url", f.url.spec()); feed.Set("title", f.title); base::ListValue articles;
    for (const auto& a : f.articles) { base::DictValue item; item.Set("id", a.id); item.Set("url", a.url.spec()); item.Set("title", a.title); item.Set("text", a.text); item.Set("date", a.date); item.Set("read", a.read); item.Set("saved", a.saved); articles.Append(std::move(item)); }
    feed.Set("articles", std::move(articles)); feeds.Append(std::move(feed));
  }
  state.Set("feeds", std::move(feeds)); return state;
}
void RssReader::Restore(base::DictValue state) {
  feeds_.clear(); const auto* feeds = state.FindList("feeds"); if (!feeds) return;
  std::set<std::string> urls;
  auto text = [](const base::DictValue& d, const char* key, size_t max) { const auto* s = d.FindString(key); return s ? Truncate(*s, max) : std::string(); };
  for (const auto& value : *feeds) {
    if (feeds_.size() >= kMaxFeeds) break;
    if (!value.is_dict()) continue;
    const auto& d = value.GetDict(); auto url = Clean(GURL(text(d, "url", 2048))); if (!url.is_valid() || !urls.insert(url.spec()).second) continue;
    RssFeed f{url, text(d, "title", 512), {}, {}}; std::set<std::string> ids;
    if (const auto* articles = d.FindList("articles")) for (const auto& item : *articles) {
      if (f.articles.size() >= kMaxArticles + 500) break;
      if (!item.is_dict()) continue;
      const auto& a = item.GetDict(); RssArticle article;
      article.id = text(a, "id", 2048); article.url = Clean(GURL(text(a, "url", 2048)), false);
      if (article.id.empty() || !article.url.is_valid() || !ids.insert(article.id).second) continue;
      article.title = text(a, "title", 1024); article.text = text(a, "text", 4000); article.date = text(a, "date", 128); if (!base::Time::FromUTCString(article.date.c_str(), &article.published)) article.published = {}; article.read = a.FindBool("read").value_or(false); article.saved = a.FindBool("saved").value_or(false); f.articles.push_back(std::move(article));
    }
    feeds_.push_back(std::move(f));
  }
  Prune();
}
void RssReader::Prune() {
  // Payload <= 8 MiB; JSON escaping expands it by at most six, with < 1 MiB
  // structural overhead. The 64 MiB load ceiling therefore accepts every save.
  constexpr size_t budget = 8 * 1024 * 1024;
  size_t bytes = 0, saved = 0;
  for (auto& f : feeds_) { bytes += f.title.size() + f.url.spec().size(); for (auto& a : f.articles) {
    if (a.saved && ++saved > 500) a.saved = false;
    bytes += a.id.size() + a.title.size() + a.text.size() + a.date.size() + a.url.spec().size();
  } }
  if (bytes <= budget) return;
  std::vector<std::pair<size_t, size_t>> candidates;
  for (size_t f = 0; f < feeds_.size(); ++f) for (size_t a = 0; a < feeds_[f].articles.size(); ++a)
    if (!feeds_[f].articles[a].saved) candidates.emplace_back(f, a);
  std::stable_sort(candidates.begin(), candidates.end(), [&](const auto& x, const auto& y) { return feeds_[x.first].articles[x.second].published < feeds_[y.first].articles[y.second].published; });
  std::set<std::pair<size_t,size_t>> remove;
  for (auto key : candidates) {
    if (bytes <= budget) break;
    const auto& a = feeds_[key.first].articles[key.second];
    bytes -= a.id.size() + a.title.size() + a.text.size() + a.date.size() + a.url.spec().size(); remove.insert(key);
  }
  for (size_t f = 0; f < feeds_.size(); ++f) {
    size_t a = 0; std::erase_if(feeds_[f].articles, [&](const auto&) { return remove.contains({f, a++}); });
  }
}
void RssReader::Save() {
  Prune();
  if (!profile_ || profile_->IsOffTheRecord()) return;
  if (!save_timer_.IsRunning()) save_timer_.Start(FROM_HERE, base::Milliseconds(500), this, &RssReader::Write);
}
void RssReader::Write() {
  save_timer_.Stop();
  if (!profile_ || profile_->IsOffTheRecord()) return;
  // Copy the bounded model once per burst; JSON serialization and atomic disk
  // I/O run on the same sequence as startup reads, never on the UI sequence.
  files_->PostTaskAndReplyWithResult(FROM_HERE, base::BindOnce([](base::FilePath path, std::vector<RssFeed> feeds) {
    base::DictValue state; base::ListValue list;
    for (const auto& f : feeds) {
      base::DictValue feed; feed.Set("url", f.url.spec()); feed.Set("title", f.title); base::ListValue articles;
      for (const auto& a : f.articles) { base::DictValue item; item.Set("id", a.id); item.Set("url", a.url.spec()); item.Set("title", a.title); item.Set("text", a.text); item.Set("date", a.date); item.Set("read", a.read); item.Set("saved", a.saved); articles.Append(std::move(item)); }
      feed.Set("articles", std::move(articles)); list.Append(std::move(feed));
    }
    state.Set("feeds", std::move(list)); auto json = base::WriteJson(state);
    return json && json->size() <= 64 * 1024 * 1024 && base::ImportantFileWriter::WriteFileAtomically(path, *json);
  }, profile_->GetPath().AppendASCII("ZitterRSS.json"), feeds_), base::BindOnce([](base::WeakPtr<RssReader> self, bool saved) {
    if (self && !saved) { self->status_ = "Nie udało się zapisać czytnika na dysku."; self->Notify(); }
  }, weak_.GetWeakPtr()));
}
}

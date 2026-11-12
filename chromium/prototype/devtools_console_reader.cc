// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#include "chromium/prototype/devtools_console_reader.h"

#include <utility>

#include "base/check_op.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace zitter::prototype {
namespace {

base::Value Error(std::string message) {
  return base::Value(base::DictValue().Set("error", std::move(message)));
}

std::string SafeURL(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS())
    return {};
  GURL::Replacements strip;
  strip.ClearUsername();
  strip.ClearPassword();
  strip.ClearQuery();
  strip.ClearRef();
  return url.ReplaceComponents(strip).spec();
}

}  // namespace

DevToolsConsoleReader::DevToolsConsoleReader(content::WebContents* page,
                                             int limit)
    : document_(page->GetPrimaryMainFrame()->GetWeakDocumentPtr()),
      host_(content::DevToolsAgentHost::GetOrCreateFor(page)),
      limit_(limit) {
  CHECK_GT(limit_, 0);
}

DevToolsConsoleReader::~DevToolsConsoleReader() {
  if (attached_ && host_)
    host_->DetachClient(this);
}

bool DevToolsConsoleReader::Start() {
  auto* frame = document_.AsRenderFrameHostIfValid();
  if (!frame || !frame->IsActive() || attached_)
    return false;
  attached_ = host_ && host_->AttachClient(this);
  if (!attached_)
    return false;
  Send(R"({"id":1,"method":"Runtime.enable"})");
  Send(R"({"id":2,"method":"Log.enable"})");
  return true;
}

base::Value DevToolsConsoleReader::Result() {
  auto* frame = document_.AsRenderFrameHostIfValid();
  if (!frame || !frame->IsActive() || !host_) return Error("The approved document changed or closed");
  if (!ready_) return Error("The page console did not respond");
  base::ListValue entries;
  for (auto& entry : messages_) entries.Append(std::move(entry));
  return base::Value(base::DictValue().Set("entries", std::move(entries))
      .Set("url", SafeURL(frame->GetLastCommittedURL())));
}

void DevToolsConsoleReader::Send(std::string_view command) {
  if (host_) host_->DispatchProtocolMessage(this, base::as_byte_span(command));
}
bool DevToolsConsoleReader::MayAttachToURL(const GURL& url, bool is_webui) {
  return !is_webui && url.SchemeIsHTTPOrHTTPS();
}
bool DevToolsConsoleReader::MayReadLocalFiles() { return false; }
bool DevToolsConsoleReader::MayWriteLocalFiles() { return false; }
void DevToolsConsoleReader::AgentHostClosed(content::DevToolsAgentHost*) { host_.reset(); }
void DevToolsConsoleReader::DispatchProtocolMessage(
    content::DevToolsAgentHost*, base::span<const uint8_t> bytes) {
  if (bytes.size() > 128 * 1024) return;
  auto message = base::JSONReader::ReadDict(base::as_string_view(bytes), base::JSON_PARSE_RFC);
  if (!message) return;
  if (message->FindInt("id") == 1 && message->FindDict("result")) ready_ = true;
  const auto* method = message->FindString("method");
  const auto* params = message->FindDict("params");
  if (!method || !params) return;
  const auto field = [](const base::DictValue& dict, std::string_view name) {
    const auto* value = dict.FindString(name);
    return value ? *value : std::string();
  };
  std::string level, text, source;
  int line = 0;
  if (*method == "Log.entryAdded") {
    const auto* entry = params->FindDict("entry");
    if (!entry) return;
    level = field(*entry, "level");
    text = field(*entry, "text");
    source = field(*entry, "url");
    line = entry->FindInt("lineNumber").value_or(-1) + 1;
  } else if (*method == "Runtime.consoleAPICalled") {
    level = field(*params, "type");
    if (const auto* args = params->FindList("args")) {
      for (const auto& arg : *args) {
        const auto* object = arg.GetIfDict();
        if (!object) continue;
        if (!text.empty()) text += ' ';
        if (const auto* value = object->Find("value")) {
          text += value->is_string() ? value->GetString() : base::WriteJson(*value).value_or("");
        } else {
          text += field(*object, "description");
        }
        if (text.size() >= 800) break;
      }
    }
    if (const auto* stack = params->FindDict("stackTrace")) {
      if (const auto* frames = stack->FindList("callFrames"); frames && !frames->empty() && frames->front().is_dict()) {
        const auto& first = frames->front().GetDict();
        source = field(first, "url");
        line = first.FindInt("lineNumber").value_or(-1) + 1;
      }
    }
  } else if (*method == "Runtime.exceptionThrown") {
    const auto* exception = params->FindDict("exceptionDetails");
    if (!exception) return;
    level = "error";
    text = field(*exception, "text");
    if (const auto* error = exception->FindDict("exception")) text += " " + field(*error, "description");
    source = field(*exception, "url");
    line = exception->FindInt("lineNumber").value_or(-1) + 1;
  } else {
    return;
  }
  if (text.empty()) return;
  if (messages_.size() == static_cast<size_t>(limit_)) messages_.pop_front();
  std::string clipped;
  base::TruncateUTF8ToByteSize(text, 800, &clipped);
  messages_.push_back(base::DictValue().Set("level", level).Set("message", clipped)
      .Set("url", SafeURL(GURL(source))).Set("line", line));
}

bool DevToolsConsoleReader::IsTrusted() {
  return false;
}

}  // namespace zitter::prototype

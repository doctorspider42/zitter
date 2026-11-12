// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

#ifndef ZITTER_CHROMIUM_PROTOTYPE_DEVTOOLS_CONSOLE_READER_H_
#define ZITTER_CHROMIUM_PROTOTYPE_DEVTOOLS_CONSOLE_READER_H_

#include <deque>
#include <string_view>

#include "base/memory/scoped_refptr.h"
#include "base/values.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/weak_document_ptr.h"

namespace content { class WebContents; }

namespace zitter::prototype {

// Reads buffered console and diagnostic events from a single approved
// document. Only the two fixed Runtime.enable and Log.enable commands are sent.
class DevToolsConsoleReader final : private content::DevToolsAgentHostClient {
 public:
  DevToolsConsoleReader(content::WebContents* page, int limit);
  ~DevToolsConsoleReader() override;

  bool Start();
  base::Value Result();

 private:
  void Send(std::string_view command);
  bool MayAttachToURL(const GURL& url, bool is_webui) override;
  bool IsTrusted() override;
  bool MayReadLocalFiles() override;
  bool MayWriteLocalFiles() override;
  void AgentHostClosed(content::DevToolsAgentHost* host) override;
  void DispatchProtocolMessage(content::DevToolsAgentHost* host,
                               base::span<const uint8_t> bytes) override;

  content::WeakDocumentPtr document_;
  scoped_refptr<content::DevToolsAgentHost> host_;
  std::deque<base::DictValue> messages_;
  const int limit_;
  bool attached_ = false;
  bool ready_ = false;
};

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_DEVTOOLS_CONSOLE_READER_H_

// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_PANEL_H_
#define ZITTER_CHROMIUM_PROTOTYPE_ASSISTANT_PANEL_H_
#include <memory>
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "build/build_config.h"
#if BUILDFLAG(IS_WIN)
#include "base/win/scoped_handle.h"
#endif
#include "ui/views/view.h"
namespace network { class SimpleURLLoader; }
namespace views { class Checkbox; class Combobox; class Label; class MdTextButton; }
namespace zitter::prototype {
class WorkspaceWindow;
class EditorControl;
class PanelScene;
class PanelViewport;
struct AssistantPermissions {
  bool tabs = false, pages = false, actions = false, history = false, web = true;
  bool confirm_clicks = true;
  base::DictValue ToValue() const;
};
class AssistantPanel final : public views::View {
 public:
  explicit AssistantPanel(WorkspaceWindow* window);
  ~AssistantPanel() override;
  void Show();
  void Stop();
  bool pinned() const { return pinned_; }
  bool busy() const { return busy_; }
  void SetPinned(bool pinned);
  void SendForTesting(std::string text);
  void ConfigureForTesting(std::string provider, std::string model, AssistantPermissions permissions);
  bool ContainsForTesting(std::string_view text) const;
 private:
  struct Session { std::string id, provider, model, profile, transcript; AssistantPermissions permissions; };
  void NewSession();
  void SwitchSession();
  void Send();
  void PickReference();
  void StartRuntime();
  void Request(std::string path, base::DictValue body, base::OnceCallback<void(base::DictValue)> done);
  void PollRuntime(int attempts);
  void OnTurn(base::DictValue reply);
  void RunNext();
  void ToolFinished(base::Value value);
  void Append(std::string text);
  void UpdateControls();
  raw_ptr<WorkspaceWindow> window_;
  std::unique_ptr<PanelScene> scene_;
  raw_ptr<PanelViewport> viewport_ = nullptr;
  raw_ptr<views::View> output_ = nullptr;
  raw_ptr<EditorControl> prompt_ = nullptr, model_ = nullptr;
  raw_ptr<views::Combobox> provider_ = nullptr, sessions_control_ = nullptr;
  raw_ptr<views::Combobox> mentions_ = nullptr;
  raw_ptr<views::View> permissions_view_ = nullptr;
  std::vector<std::string> references_;
  raw_ptr<views::Checkbox> tabs_ = nullptr, pages_ = nullptr, actions_ = nullptr, history_ = nullptr, web_ = nullptr, research_ = nullptr;
  raw_ptr<views::Checkbox> confirm_clicks_ = nullptr;
  raw_ptr<views::View> send_ = nullptr, stop_ = nullptr, confirm_ = nullptr;
  raw_ptr<views::View> pin_action_ = nullptr;
  std::vector<Session> sessions_;
  size_t selected_ = 0;
  bool busy_ = false, pinned_ = false;
  int port_ = 0;
  std::string token_;
  base::Process process_;
#if BUILDFLAG(IS_WIN)
  base::win::ScopedHandle process_job_;
#endif
  std::unique_ptr<network::SimpleURLLoader> loader_;
  base::ListValue actions_queue_, results_;
  std::optional<base::DictValue> confirmation_;
  size_t action_index_ = 0;
  base::OneShotTimer startup_timer_;
  base::WeakPtrFactory<AssistantPanel> weak_factory_{this};
};
}
#endif

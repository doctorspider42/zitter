#include "chromium/app/ui_lab.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process_metrics.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/timer/timer.h"
#include "base/trace_event/trace_event.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/viz/common/frame_timing_details.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chromium/app/ui_lab_metal.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/compositor/compositor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/presentation_feedback.h"
#include "ui/views/background.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace zitter {
namespace {
using ui_lab::Rect;

// Optimized retained Views reference with the exact same primitive scene.
// No text, shadows, rounded corners or production workspace services are timed.
std::vector<Rect> ReferenceScene(float width, float height, uint32_t rows, uint32_t selected) {
  std::vector<Rect> result;
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) return result;
  result.reserve(68);
  result.push_back({0, 0, width, height, 0xff18202c});
  result.push_back({16, 20, std::max(0.f, width - 32), 28, 0xff30405a});
  for (uint32_t row = 0; row < std::min(rows, 200u); ++row) {
    const float y = 64 + row * 36;
    if (y + 32 > height) break;
    result.push_back({8, y, std::max(0.f, width - 16), 32, row == selected ? 0xff405e92u : 0xff202c40u});
    result.push_back({20, y + 10, 12, 12, 0xff8da7d0});
    result.push_back({44, y + 12, std::max(0.f, width - 76 - (row % 4) * 12), 8, 0xffb0bfd8});
  }
  return result;
}

bool EquivalentScenes() {
  for (float width : {200.f, 280.f, 360.f}) {
    for (float height : {136.f, 780.f, 1024.f}) {
      for (uint32_t selected : {0u, 1u, 19u}) {
        auto a = ReferenceScene(width, height, 25, selected);
        auto b = ui_lab::scene(width, height, 25, selected);
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
          if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].width != b[i].width ||
              a[i].height != b[i].height || a[i].color != b[i].color) return false;
        }
      }
    }
  }
  return true;
}

class Sidebar final : public views::View {
  METADATA_HEADER(Sidebar, views::View)
 public:
  Sidebar(bool retained, bool optimized, base::RepeatingCallback<void(float)> clicked)
      : retained_(retained), optimized_(optimized), clicked_(std::move(clicked)) {}
  void Update(base::span<const Rect> scene) {
    if (retained_) {
      while (children().size() > scene.size()) RemoveChildViewT(children().back());
      while (children().size() < scene.size()) AddChildView(std::make_unique<views::View>());
      for (size_t i = 0; i < scene.size(); ++i) {
        const auto& r = scene[i];
        children()[i]->SetBounds(r.x, r.y, r.width, r.height);
        if (i >= scene_.size() || r.color != scene_[i].color) {
          children()[i]->SetBackground(views::CreateSolidBackground(r.color));
        }
      }
    }
    if (!retained_) {
      if (!optimized_) {
        SchedulePaint();
      } else {
        // Invalidate both old and new bounds. Unchanged rows keep their raster
        // tiles; removed/moved primitives are cleared by the scene background.
        gfx::Rect header_damage, body_damage;
        const auto damage = [&](const gfx::Rect& bounds) {
          (bounds.y() < 64 ? header_damage : body_damage).Union(bounds);
        };
        for (size_t i = 0; i < std::max(scene.size(), scene_.size()); ++i) {
          const auto bounds = [](const Rect& r) { return gfx::Rect(r.x, r.y, r.width, r.height); };
          if (i < scene.size() && i < scene_.size() &&
              bounds(scene[i]) == bounds(scene_[i]) && scene[i].color == scene_[i].color) continue;
          if (i < scene_.size()) damage(bounds(scene_[i]));
          if (i < scene.size()) damage(bounds(scene[i]));
        }
        // Scheduling is not free: submit at most two disjoint dirty bands,
        // and only one when geometry changes invalidate the full background.
        if (!header_damage.IsEmpty()) SchedulePaintInRect(header_damage);
        if (!body_damage.IsEmpty() && !header_damage.Contains(body_damage)) SchedulePaintInRect(body_damage);
      }
    }
    scene_.assign(scene.begin(), scene.end());
  }
  void OnPaint(gfx::Canvas* canvas) override {
    TRACE_EVENT("benchmark", "Zitter.UiLab.SidebarPaint", "retained", retained_);
    views::View::OnPaint(canvas);
    if (!retained_) {
      gfx::Rect clip;
      const bool cull = optimized_ && canvas->GetClipBounds(&clip);
      for (const auto& r : scene_) {
        const gfx::Rect bounds(r.x, r.y, r.width, r.height);
        if (!cull || bounds.Intersects(clip)) canvas->FillRect(bounds, r.color);
      }
    }
  }
  bool OnMousePressed(const ui::MouseEvent& event) override {
    clicked_.Run(event.y()); return true;
  }
 private:
  bool retained_, optimized_;
  base::RepeatingCallback<void(float)> clicked_;
  std::vector<Rect> scene_;
};
BEGIN_METADATA(Sidebar)
END_METADATA

class Lab final : public UiLab, public content::WebContentsObserver {
 public:
  explicit Lab(base::OnceClosure done) : done_(std::move(done)) {}
  ~Lab() override {
    weak_.InvalidateWeakPtrs();
    Observe(nullptr);
    if (native_host_) native_host_->Detach();
    metal_.reset();
    widget_.reset();
  }
  void Start() {
    const auto* cmd = base::CommandLine::ForCurrentProcess();
    backend_ = cmd->GetSwitchValueASCII("zitter-ui-lab");
    scenario_ = cmd->GetSwitchValueASCII("zitter-ui-scenario");
    variant_ = cmd->GetSwitchValueASCII("zitter-ui-variant");
    if (variant_.empty()) variant_ = "optimized";
    optimized_ = variant_ == "optimized";
    if (scenario_.empty()) scenario_ = "selection";
    url_ = GURL(cmd->GetSwitchValueASCII("zitter-performance-url"));
    const auto directory = cmd->GetSwitchValuePath("user-data-dir");
    std::string marker;
    if ((backend_ != "views" && backend_ != "rust-skia" && backend_ != "rust-metal") ||
        (scenario_ != "selection" && scenario_ != "resize") ||
        (variant_ != "baseline" && variant_ != "optimized") ||
        directory.empty() || !directory.IsAbsolute() ||
        directory != g_browser_process->profile_manager()->user_data_dir() ||
        !base::ReadFileToString(directory.AppendASCII(".zitter-ui-lab"), &marker) ||
        marker != "isolated-ui-lab\n" || !url_.SchemeIsHTTPOrHTTPS() ||
        (cmd->HasSwitch("zitter-perf-iterations") &&
         !base::StringToInt(cmd->GetSwitchValueASCII("zitter-perf-iterations"), &iterations_)) ||
        iterations_ < 1 || iterations_ > 1000 || !EquivalentScenes()) {
      Finish(false); return;
    }
    interactive_ = cmd->HasSwitch("zitter-ui-interactive");
    capture_ = cmd->HasSwitch("zitter-ui-capture");
    deadline_.Start(FROM_HERE, base::Seconds(45),
                    base::BindOnce(&Lab::Finish, weak_.GetWeakPtr(), false));
    g_browser_process->profile_manager()->CreateProfileAsync(directory.AppendASCII("UiLab"),
        base::BindOnce(&Lab::Ready, weak_.GetWeakPtr()));
  }
  void Raise() override { if (widget_) widget_->Activate(); }
  void Navigate(const GURL& url) override { if (page_) page_->LoadInitialURL(url); }
 private:
  void Ready(Profile* profile) {
    if (!profile) { Finish(false); return; }
    profile_ = std::make_unique<ScopedProfileKeepAlive>(profile, ProfileKeepAliveOrigin::kBrowserWindow);
    auto root = std::make_unique<views::View>();
    auto* layout = root->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal));
    auto clicked = base::BindRepeating(&Lab::Click, weak_.GetWeakPtr());
    if (backend_ == "rust-metal") {
      metal_ = ui_lab::CreateMetalSurface(clicked, optimized_);
      if (!metal_) { Finish(false); return; }
      native_host_ = root->AddChildView(std::make_unique<views::NativeViewHost>());
      sidebar_view_ = native_host_;
    } else {
      sidebar_ = root->AddChildView(std::make_unique<Sidebar>(backend_ == "views", optimized_, clicked));
      sidebar_view_ = sidebar_;
    }
    sidebar_view_->SetPreferredSize(gfx::Size(280, 780));
    page_ = root->AddChildView(std::make_unique<views::WebView>(profile));
    layout->SetFlexForView(page_, 1);
    delegate_ = std::make_unique<views::WidgetDelegate>();
    delegate_->SetTitle(u"Zitter UI lab — isolated GPU experiment");
    delegate_->SetHasWindowSizeControls(true);
    delegate_->RegisterWindowClosingCallback(base::BindOnce(&Lab::WindowClosing, weak_.GetWeakPtr()));
    delegate_->SetContentsView(std::move(root));
    widget_ = std::make_unique<views::Widget>();
    views::Widget::InitParams params(views::Widget::InitParams::CLIENT_OWNS_WIDGET,
                                   views::Widget::InitParams::TYPE_WINDOW);
    params.delegate = delegate_.get();
    params.bounds = gfx::Rect(80, 80, 1280, 820);
    widget_->Init(std::move(params));
    if (native_host_) native_host_->Attach(metal_->native_view());
    widget_->Show();
    widget_->Activate();
    ui_lab::ShowLabWindow(widget_->GetNativeWindow());
    widget_->LayoutRootViewIfNecessary();
    Observe(page_->GetWebContents());
    page_->LoadInitialURL(url_);
  }
  void DidStopLoading() override {
    if (loaded_ || finished_) return;
    const auto* entry = web_contents()->GetController().GetLastCommittedEntry();
    if (web_contents()->GetLastCommittedURL() != url_ || !entry || entry->GetHttpStatusCode() != 200 ||
        web_contents()->GetPrimaryMainFrame()->IsErrorDocument()) { Finish(false); return; }
    loaded_ = true;
    web_contents()->GetPrimaryMainFrame()->InsertVisualStateCallback(
        base::BindOnce(&Lab::PageFrame, weak_.GetWeakPtr()));
  }
  void PageFrame(bool ok) {
    if (!ok) { Finish(false); return; }
    // Prime the native swapchain before the settling interval. Its first
    // drawable may be discarded by the initial CoreAnimation transaction.
    Update(false);
    LOG(INFO) << "ZITTER_UI_READY {\"backend\":\"" << backend_
              << "\",\"scene_parity\":true,\"page_surface\":\"native-webcontents\"}";
    if (interactive_) {
      deadline_.Stop();
      if (metal_) base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, base::BindOnce(&Lab::Update, weak_.GetWeakPtr(), false), base::Milliseconds(500));
      return;
    }
    PostStep(base::Seconds(3));
  }
  void Click(float y) {
    if (!interactive_ || !loaded_) return;
    const int row = ui_lab::hit_test(y, 25);
    if (row >= 0) { selected_ = row; Update(false); }
  }
  void PostStep(base::TimeDelta delay) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&Lab::Step, weak_.GetWeakPtr()), delay);
  }
  void Step() {
    if (finished_) return;
    TRACE_EVENT("benchmark", "Zitter.UiLab.Update", "trial", trial_,
                "backend", backend_, "scenario", scenario_);
    if (trial_ == 0) {
      measured_start_ = base::TimeTicks::Now();
      cpu_start_ = process_metrics_->GetCumulativeCPUUsage();
    }
    started_ = base::TimeTicks::Now();
    selected_ = (trial_ + kWarmups + 1) % 16;
    if (scenario_ == "resize") {
      sidebar_view_->SetPreferredSize(gfx::Size(240 + ((trial_ + kWarmups) % 16) * 8, 780));
      widget_->LayoutRootViewIfNecessary();
    }
    Update(true);
  }
  void Update(bool timed) {
    const auto before = base::TimeTicks::Now();
    const float width = sidebar_view_->width(), height = sidebar_view_->height();
    auto cpp_scene = backend_ == "views" ? ReferenceScene(width, height, 25, selected_) : std::vector<Rect>();
    auto rust_scene = backend_ == "views" ? rust::Vec<Rect>() : ui_lab::scene(width, height, 25, selected_);
    if (capture_) {
      // A frame identity rendered by the SAME surface as the scene, so a
      // capture cannot acknowledge a newer marker over older sidebar pixels.
      const uint32_t id = timed ? trial_ + kWarmups + 1 : 0;
      const uint32_t code = (0xa55au << 10) | id;
      for (int bit = 0; bit < 26; ++bit) {
        Rect r{float(4 + bit * 4), 4, 4, 4, (code & (1u << (25 - bit))) ? 0xffeeeeeeu : 0xff111111u};
        if (backend_ == "views") cpp_scene.push_back(r); else rust_scene.push_back(r);
      }
    }
    // SAFETY: CXX Vec owns this contiguous allocation and outlives the span.
    // Consumers synchronously copy it before returning; no borrowed data escapes.
    const auto scene = backend_ == "views" ? base::span<const Rect>(cpp_scene)
        : UNSAFE_BUFFERS(base::span<const Rect>(rust_scene.data(), rust_scene.size()));
    scene_ms_ = (base::TimeTicks::Now() - before).InMillisecondsF();
    if (metal_) {
      const double before_draw_ms = timed ? (base::TimeTicks::Now() - started_).InMillisecondsF() : 0;
      metal_->Draw(scene, base::BindOnce(&Lab::MetalPresented, weak_.GetWeakPtr(), timed, before_draw_ms));
    } else {
      if (timed) {
        if (base::CommandLine::ForCurrentProcess()->HasSwitch("zitter-ui-profile")) {
          widget_->GetCompositor()->RequestSuccessfulPresentationTimeForNextFrame(
              base::BindOnce(&Lab::ProfileFrame, weak_.GetWeakPtr(), trial_, started_));
        }
        widget_->GetCompositor()->RequestPresentationTimeForNextFrame(
            base::BindOnce(&Lab::ViewsPresented, weak_.GetWeakPtr()));
      }
      sidebar_->Update(scene);
    }
    if (timed) submit_ms_ = (base::TimeTicks::Now() - started_).InMillisecondsF();
  }
  void ViewsPresented(const gfx::PresentationFeedback& feedback) {
    TRACE_EVENT_INSTANT("benchmark", "Zitter.UiLab.Presented", "trial", trial_,
        "presentation_us", feedback.timestamp.since_origin().InMicroseconds(),
        "callback_delay_ms", (base::TimeTicks::Now() - feedback.timestamp).InMillisecondsF(),
        "vsync_interval_ms", feedback.interval.InMillisecondsF());
    if (feedback.failed() || feedback.timestamp < started_) { Finish(false); return; }
    reported_presentation_ = feedback.timestamp;
    callback_ms_ = (base::TimeTicks::Now() - started_).InMillisecondsF();
    Presented((feedback.timestamp - started_).InMillisecondsF(), -1, feedback.flags);
  }
  void ProfileFrame(int trial, base::TimeTicks started, const viz::FrameTimingDetails& frame) {
    const auto us = [](base::TimeTicks ticks) { return ticks.since_origin().InMicroseconds(); };
    LOG(INFO) << "ZITTER_UI_FRAME {\"trial\":" << trial << ",\"start_us\":" << us(started)
              << ",\"received_us\":" << us(frame.received_compositor_frame_timestamp)
              << ",\"draw_start_us\":" << us(frame.draw_start_timestamp)
              << ",\"swap_start_us\":" << us(frame.swap_timings.swap_start)
              << ",\"swap_end_us\":" << us(frame.swap_timings.swap_end)
              << ",\"presentation_us\":" << us(frame.presentation_feedback.timestamp)
              << ",\"callback_us\":" << us(base::TimeTicks::Now())
              << ",\"interval_us\":" << frame.presentation_feedback.interval.InMicroseconds()
              << ",\"flags\":" << frame.presentation_feedback.flags << "}";
  }
  void MetalPresented(bool timed, double before_draw_ms, bool ok, double latency, double gpu) {
    if (!timed) return;  // Initial drawable may precede the first window transaction.
    if (!ok && trial_ < 0 && warmup_skipped_ < 10) {
      LOG(INFO) << "ZITTER_UI_WARMUP_DROP " << ++warmup_skipped_;
      PostStep(base::Milliseconds(33));
      return;
    }
    if (!ok) { Finish(false); return; }
    reported_presentation_ = started_ + base::Milliseconds(before_draw_ms + latency);
    callback_ms_ = (base::TimeTicks::Now() - started_).InMillisecondsF();
    Presented(before_draw_ms + latency, gpu, 0);
  }
  void Presented(double latency, double gpu, uint32_t flags) {
    if (finished_) return;
    if (trial_ >= 0) {
      LOG(INFO) << "ZITTER_UI_SAMPLE {\"trial\":" << trial_
                << ",\"scene_ms\":" << scene_ms_ << ",\"submit_ms\":" << submit_ms_
                << ",\"update_to_present_ms\":" << latency << ",\"metal_gpu_ms\":" << gpu
                << ",\"feedback_flags\":" << flags
                << ",\"start_us\":" << started_.since_origin().InMicroseconds()
                << ",\"marker_id\":" << trial_ + kWarmups + 1
                << ",\"callback_ms\":" << callback_ms_
                << ",\"presentation_kind\":\"" << (metal_ ? "metal-drawable" : "chromium-displaylink-target") << "\"}";
    }
    const auto remaining = std::max(base::TimeDelta(), reported_presentation_ - base::TimeTicks::Now());
    if (++trial_ == iterations_) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, base::BindOnce(&Lab::ActiveFinished, weak_.GetWeakPtr()), remaining);
      return;
    }
    deadline_.Start(FROM_HERE, base::Seconds(10),
                    base::BindOnce(&Lab::Finish, weak_.GetWeakPtr(), false));
    // Identical update-start cadence for every backend/variant, independent
    // of callback latency. Keep one update in flight and vary the vsync phase.
    const auto next_start = started_ + base::Milliseconds(40 + 3 + ((trial_ + kWarmups) * 17) % 13);
    PostStep(std::max(remaining, next_start - base::TimeTicks::Now()));
  }
  void ActiveFinished() {
    const auto cpu = process_metrics_->GetCumulativeCPUUsage();
    const auto memory = process_metrics_->GetMemoryInfo();
    if (!cpu.has_value() || !cpu_start_.has_value() || !memory.has_value()) { Finish(false); return; }
    LOG(INFO) << "ZITTER_UI_RESOURCE {\"browser_cpu_ms\":" << (*cpu - *cpu_start_).InMillisecondsF()
              << ",\"wall_ms\":" << (base::TimeTicks::Now() - measured_start_).InMillisecondsF()
              << ",\"browser_footprint_bytes\":" << memory->physical_footprint_bytes << "}";
    cpu_start_ = cpu;
    measured_start_ = base::TimeTicks::Now();
    // No repaint timer, no animation loop. Measure browser activity at rest.
    deadline_.Start(FROM_HERE, base::Seconds(2), base::BindOnce(&Lab::IdleFinished, weak_.GetWeakPtr()));
  }
  void IdleFinished() {
    const auto cpu = process_metrics_->GetCumulativeCPUUsage();
    if (!cpu.has_value()) { Finish(false); return; }
    LOG(INFO) << "ZITTER_UI_IDLE {\"browser_cpu_ms\":" << (*cpu - *cpu_start_).InMillisecondsF()
              << ",\"wall_ms\":" << (base::TimeTicks::Now() - measured_start_).InMillisecondsF() << "}";
    Finish(true);
  }
  void Finish(bool ok) {
    if (finished_) return;
    finished_ = true;
    deadline_.Stop();
    if (!ok) LOG(ERROR) << "UI lab failed: backend=" << backend_ << " trial=" << trial_
                       << " loaded=" << loaded_ << " sidebar="
                       << (sidebar_view_ ? sidebar_view_->bounds().ToString() : "none");
    LOG(INFO) << "ZITTER_UI_STATUS {\"ok\":" << (ok ? "true" : "false") << "}";
    if (done_) base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE, std::move(done_));
  }
  void WindowClosing() { Finish(interactive_ && loaded_); }
  static constexpr int kWarmups = 10;
  std::string backend_, scenario_, variant_;
  GURL url_;
  base::OnceClosure done_;
  std::unique_ptr<ScopedProfileKeepAlive> profile_;
  std::unique_ptr<views::WidgetDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  std::unique_ptr<ui_lab::MetalSurface> metal_;
  raw_ptr<views::NativeViewHost> native_host_ = nullptr;
  raw_ptr<views::View> sidebar_view_ = nullptr;
  raw_ptr<Sidebar> sidebar_ = nullptr;
  raw_ptr<views::WebView> page_ = nullptr;
  int iterations_ = 120, trial_ = -kWarmups;
  int warmup_skipped_ = 0;
  uint32_t selected_ = 0;
  bool loaded_ = false, finished_ = false, interactive_ = false, optimized_ = true, capture_ = false;
  double scene_ms_ = 0, submit_ms_ = 0, callback_ms_ = 0;
  base::TimeTicks reported_presentation_;
  base::TimeTicks started_;
  base::TimeTicks measured_start_;
  std::unique_ptr<base::ProcessMetrics> process_metrics_ = base::ProcessMetrics::CreateCurrentProcessMetrics();
  base::expected<base::TimeDelta, base::ProcessCPUUsageError> cpu_start_ = base::unexpected(base::ProcessCPUUsageError::kSystemError);
  base::OneShotTimer deadline_;
  base::WeakPtrFactory<Lab> weak_{this};
};
}  // namespace
std::unique_ptr<UiLab> UiLab::Create(base::OnceClosure done) {
  auto result = std::make_unique<Lab>(std::move(done));
  result->Start();
  return result;
}
}  // namespace zitter

// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// zitter_setup.exe: Zitter's installer and uninstaller (design: Instalator).
// A small window drawn with Direct2D in the design's Noc theme and fonts, which
// it carries; no Chromium code runs here.
//
//   zitter_setup.exe                     install, or update or remove what it finds
//   zitter_setup.exe --uninstall [--system]          Apps & features' remove
//   zitter_setup.exe --install-now --dir D [--system] [--no-desktop]
//                                        the elevated half of an install for everyone
//   --quiet with either: no questions, no Zitter launched afterwards, and the
//   exit code says whether it worked (for scripts and package managers)
#include <windows.h>

#include <d2d1_1.h>
#include <dwmapi.h>
#include <dwrite_3.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "chromium/installer/setup_install.h"
#include "chromium/installer/setup_resource.h"
#include "chromium/installer/setup_text.h"

namespace zitter::setup {
namespace {
using Microsoft::WRL::ComPtr;

// Design tokens: Noc with the violet workspace accent (zitter-win.css).
constexpr D2D1_COLOR_F Rgb(uint32_t rgb, float alpha = 1) {
  return {((rgb >> 16) & 0xff) / 255.f, ((rgb >> 8) & 0xff) / 255.f, (rgb & 0xff) / 255.f, alpha};
}
constexpr D2D1_COLOR_F kVoid = Rgb(0x100b2c), kBase = Rgb(0x1b1440), kRaised = Rgb(0x1e1b28),
                       kHover = Rgb(0x2b2458), kLine = Rgb(0x322f58), kLineStrong = Rgb(0x736d89),
                       kInk = Rgb(0xf3f1f8), kMuted = Rgb(0xaaa5bb),
                       kAccent = Rgb(0x9a92ff), kOnAccent = Rgb(0x0c0b11), kZing = Rgb(0xd7ff3f),
                       kOk = Rgb(0x6fd6cb), kDanger = Rgb(0xff7a55), kCloseRed = Rgb(0xc42b1c),
                       kWhite = Rgb(0xffffff), kTile = Rgb(0x14121e);
constexpr float kWidth = 640, kHeight = 440, kHead = 38;
constexpr UINT kProgressMessage = WM_APP + 1, kDoneMessage = WM_APP + 2;
constexpr UINT_PTR kAnimationTimer = 1;

struct Style {
  const wchar_t* family;
  float size;
  DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
  bool italic = false;
  float line = 0;      // Line height; 0 keeps the font's.
  float spacing = 0;   // Letter spacing in DIPs.
};
constexpr wchar_t kDisplay[] = L"Instrument Serif", kSans[] = L"Schibsted Grotesk", kMono[] = L"JetBrains Mono";
const Style kHeadline{kDisplay, 56, DWRITE_FONT_WEIGHT_NORMAL, true, 53};
const Style kHeadlineLarge{kDisplay, 72, DWRITE_FONT_WEIGHT_NORMAL, true, 66};
const Style kBody{kSans, 13, DWRITE_FONT_WEIGHT_NORMAL, false, 18};
const Style kStrong{kSans, 13, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, 18};
const Style kLabel{kSans, 12, DWRITE_FONT_WEIGHT_MEDIUM, false, 16};
const Style kEyebrow{kSans, 10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, 14, 0.84f};
const Style kPath{kMono, 12, DWRITE_FONT_WEIGHT_NORMAL, false, 16};
const Style kPlatform{kMono, 11, DWRITE_FONT_WEIGHT_MEDIUM, false, 16};
const Style kPercent{kMono, 28, DWRITE_FONT_WEIGHT_MEDIUM, false, 28, -0.56f};
const Style kKet{kDisplay, 22, DWRITE_FONT_WEIGHT_NORMAL, true, 22};

enum class Page { kFresh, kExisting, kRemove, kWorking };
enum Control {
  kNone, kClose, kForMe, kForAll, kChange, kShortcut, kCancel, kInstall,
  kRemoveZitter, kElsewhere, kUpdate, kWipe, kBack, kConfirmRemove,
};
enum class Look { kPrimary, kQuiet, kOutline, kDanger, kDangerStrong };

struct Hit {
  D2D1_RECT_F rect;
  Control id;
};

std::wstring Substitute(const wchar_t* pattern, const std::wstring& value) {
  std::wstring out(pattern);
  if (const auto at = out.find(L"%s"); at != std::wstring::npos) out.replace(at, 2, value);
  return out;
}

class Window {
 public:
  explicit Window(HINSTANCE instance) : instance_(instance), text_(Strings()) {}
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  int Run(int show) {
    ParseCommandLine();
    if (!CreateResources()) return 1;
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &Window::Procedure;
    window_class.hInstance = instance_;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_SETUP));
    window_class.lpszClassName = L"ZitterSetup";
    RegisterClassExW(&window_class);
    // Sized at 96 DPI, then rescaled to the monitor it lands on.
    hwnd_ = CreateWindowExW(0, L"ZitterSetup", L"Zitter", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, static_cast<int>(kWidth), static_cast<int>(kHeight),
                            nullptr, nullptr, instance_, this);
    if (!hwnd_) return 1;
    FitToDpi();
    // A shadowed frame without the system's titlebar: the header draws it.
    const MARGINS margins{0, 0, 0, 1};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(hwnd_, show);
    if (install_now_) StartInstall();
    else if (uninstall_only_ && quiet_) StartUninstall();
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (worker_.joinable()) worker_.join();
    return exit_code_;
  }

 private:
  void ParseCommandLine() {
    int count = 0;
    LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> args;
    // CommandLineToArgvW returns a C array of |count| strings.
#pragma clang unsafe_buffer_usage begin
    for (int i = 1; raw && i < count; ++i) args.emplace_back(raw[i]);
#pragma clang unsafe_buffer_usage end
    LocalFree(raw);
    auto has = [&](const wchar_t* name) { return std::find(args.begin(), args.end(), name) != args.end(); };
    placement_.system = has(L"--system");
    if (auto dir = std::find(args.begin(), args.end(), L"--dir"); dir != args.end() && std::next(dir) != args.end())
      placement_.directory = *std::next(dir);
    desktop_shortcut_ = !has(L"--no-desktop");
    quiet_ = has(L"--quiet");
    existing_ = FindExisting();
    if (has(L"--uninstall")) {
      if (existing_) placement_ = existing_->placement;
      page_ = Page::kRemove;
      uninstall_only_ = true;
    } else if (has(L"--install-now")) {
      install_now_ = true;
    } else if (existing_) {
      page_ = Page::kExisting;
    }
    if (placement_.directory.empty()) placement_.directory = DefaultDirectory(placement_.system);
    // An installation for everyone changes Program Files and HKLM.
    if (placement_.system && (install_now_ || uninstall_only_) && !IsElevated()) {
      Relaunch(GetCommandLineArguments());
      ExitProcess(0);
    }
  }

  static std::wstring GetCommandLineArguments() {
    // Everything after the program name, as given.
    std::wstring line = GetCommandLineW();
    size_t at = 0;
    if (!line.empty() && line.front() == L'"') at = line.find(L'"', 1) + 1;
    else at = line.find(L' ');
    return at == std::wstring::npos || at >= line.size() ? std::wstring() : line.substr(at);
  }

  bool Relaunch(const std::wstring& arguments) {
    std::wstring self(MAX_PATH, L'\0');
    self.resize(GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size())));
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.lpVerb = L"runas";
    info.lpFile = self.c_str();
    info.lpParameters = arguments.c_str();
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&info);
  }

  bool CreateResources() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
      return false;
    // The design's fonts, from this executable's resources.
    ComPtr<IDWriteInMemoryFontFileLoader> loader;
    ComPtr<IDWriteFontSetBuilder1> builder;
    if (FAILED(dwrite_->CreateInMemoryFontFileLoader(&loader)) || FAILED(dwrite_->RegisterFontFileLoader(loader.Get())) ||
        FAILED(dwrite_->CreateFontSetBuilder(&builder)))
      return false;
    for (int id : {IDR_FONT_SERIF_ITALIC, IDR_FONT_SERIF, IDR_FONT_SANS, IDR_FONT_SANS_ITALIC, IDR_FONT_MONO}) {
      HRSRC resource = FindResourceW(instance_, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
      HGLOBAL data = resource ? LoadResource(instance_, resource) : nullptr;
      ComPtr<IDWriteFontFile> file;
      if (data && SUCCEEDED(loader->CreateInMemoryFontFileReference(dwrite_.Get(), LockResource(data),
                                                                    SizeofResource(instance_, resource), nullptr, &file)))
        builder->AddFontFile(file.Get());  // Every named instance of the variable fonts.
    }
    ComPtr<IDWriteFontSet> set;
    return SUCCEEDED(builder->CreateFontSet(&set)) && SUCCEEDED(dwrite_->CreateFontCollectionFromFontSet(set.Get(), &fonts_));
  }

  void FitToDpi() {
    scale_ = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.f;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = static_cast<int>(std::lround(kWidth * scale_)), height = static_cast<int>(std::lround(kHeight * scale_));
    SetWindowPos(hwnd_, nullptr, (work.left + work.right - width) / 2, (work.top + work.bottom - height) / 2, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    target_.Reset();
  }

  static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCCREATE) {
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
    }
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->Handle(hwnd, message, w, l) : DefWindowProcW(hwnd, message, w, l);
  }

  LRESULT Handle(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
      case WM_NCCALCSIZE:
        if (w) return 0;  // The whole window is client area.
        break;
      case WM_NCHITTEST: {
        POINT point{static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l))};
        ScreenToClient(hwnd, &point);
        const float x = point.x / scale_, y = point.y / scale_;
        return y < kHead && Find(x, y) == kNone ? HTCAPTION : HTCLIENT;
      }
      case WM_DPICHANGED: {
        scale_ = HIWORD(w) / 96.f;
        const auto* rect = reinterpret_cast<const RECT*>(l);
        SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        target_.Reset();
        return 0;
      }
      case WM_PAINT: {
        PAINTSTRUCT paint;
        BeginPaint(hwnd, &paint);
        Paint();
        EndPaint(hwnd, &paint);
        return 0;
      }
      case WM_MOUSEMOVE: {
        if (!tracking_) {
          TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
          tracking_ = TrackMouseEvent(&track);
        }
        SetHover(Find(static_cast<short>(LOWORD(l)) / scale_, static_cast<short>(HIWORD(l)) / scale_));
        return 0;
      }
      case WM_MOUSELEAVE:
        tracking_ = false;
        SetHover(kNone);
        return 0;
      case WM_LBUTTONDOWN:
        pressed_ = Find(static_cast<short>(LOWORD(l)) / scale_, static_cast<short>(HIWORD(l)) / scale_);
        if (pressed_ != kNone) SetCapture(hwnd);
        Invalidate();
        return 0;
      case WM_LBUTTONUP: {
        const Control released = Find(static_cast<short>(LOWORD(l)) / scale_, static_cast<short>(HIWORD(l)) / scale_);
        const Control pressed = std::exchange(pressed_, kNone);
        ReleaseCapture();
        if (released == pressed && released != kNone) Activate(released);
        Invalidate();
        return 0;
      }
      case WM_KEYDOWN:
        if (w == VK_TAB) MoveFocus(GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
        else if ((w == VK_RETURN || w == VK_SPACE) && focus_ != kNone) Activate(focus_);
        else if (w == VK_RETURN) Activate(DefaultControl());
        else if (w == VK_ESCAPE && page_ != Page::kWorking) Activate(kClose);
        return 0;
      case WM_TIMER:
        Invalidate();
        return 0;
      case kProgressMessage:
        progress_ = static_cast<double>(w) / 1000.0;
        Invalidate();
        return 0;
      case kDoneMessage:
        Finished();
        return 0;
      case WM_CLOSE:
        if (page_ == Page::kWorking) return 0;  // Never mid-copy.
        DestroyWindow(hwnd);
        return 0;
      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
  }

  // --- Drawing ---------------------------------------------------------------

  void Invalidate() { InvalidateRect(hwnd_, nullptr, FALSE); }

  ComPtr<IDWriteTextFormat> Format(const Style& style, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
    ComPtr<IDWriteTextFormat> format;
    dwrite_->CreateTextFormat(style.family, fonts_.Get(), style.weight,
                              style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, style.size, L"", &format);
    format->SetTextAlignment(align);
    if (style.line > 0) format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, style.line, style.line * 0.8f);
    return format;
  }

  ComPtr<IDWriteTextLayout> Layout(const std::wstring& text, const Style& style, float width,
                                   DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
    ComPtr<IDWriteTextLayout> layout;
    dwrite_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), Format(style, align).Get(), width, 10000, &layout);
    ComPtr<IDWriteTextLayout1> spaced;
    if (style.spacing != 0 && SUCCEEDED(layout.As(&spaced)))
      spaced->SetCharacterSpacing(0, style.spacing, 0, {0, static_cast<UINT32>(text.size())});
    return layout;
  }

  float TextWidth(const std::wstring& text, const Style& style) {
    DWRITE_TEXT_METRICS metrics{};
    Layout(text, style, 10000)->GetMetrics(&metrics);
    return metrics.widthIncludingTrailingWhitespace;
  }

  // Draws |text| at the top of |box| and returns its height.
  float Print(const std::wstring& text, const Style& style, D2D1_RECT_F box, D2D1_COLOR_F color,
             DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, bool ellipsis = false) {
    auto layout = Layout(text, style, box.right - box.left, align);
    if (ellipsis) {
      layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
      ComPtr<IDWriteInlineObject> dots;
      dwrite_->CreateEllipsisTrimmingSign(layout.Get(), &dots);
      DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
      layout->SetTrimming(&trimming, dots.Get());
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    target_->DrawTextLayout({box.left, box.top}, layout.Get(), Brush(color), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    return metrics.height;
  }

  ID2D1SolidColorBrush* Brush(D2D1_COLOR_F color) {
    brush_->SetColor(color);
    return brush_.Get();
  }

  void Fill(D2D1_RECT_F r, D2D1_COLOR_F color, float radius = 0) {
    if (radius > 0) target_->FillRoundedRectangle({r, radius, radius}, Brush(color));
    else target_->FillRectangle(r, Brush(color));
  }
  // An inset ring, as box-shadow: inset 0 0 0 <width>.
  void Ring(D2D1_RECT_F r, D2D1_COLOR_F color, float radius, float width = 1) {
    const float h = width / 2;
    target_->DrawRoundedRectangle({{r.left + h, r.top + h, r.right - h, r.bottom - h}, radius - h, radius - h}, Brush(color), width);
  }
  void FocusRing(D2D1_RECT_F r, float radius) {
    target_->DrawRoundedRectangle({{r.left - 3, r.top - 3, r.right + 3, r.bottom + 3}, radius + 3, radius + 3}, Brush(kZing), 2);
  }

  void Register(D2D1_RECT_F rect, Control id) { hits_.push_back({rect, id}); }

  Control Find(float x, float y) const {
    for (const auto& hit : hits_)
      if (x >= hit.rect.left && x < hit.rect.right && y >= hit.rect.top && y < hit.rect.bottom) return hit.id;
    return kNone;
  }

  void SetHover(Control id) {
    if (hover_ == id) return;
    hover_ = id;
    Invalidate();
  }

  void Button(D2D1_RECT_F r, const std::wstring& label, Look look, Control id) {
    Register(r, id);
    const bool hovered = hover_ == id, pill = look == Look::kPrimary || look == Look::kOutline || look == Look::kDangerStrong;
    const float radius = pill ? (r.bottom - r.top) / 2 : 8;
    const float shift = pressed_ == id && hovered ? 1.f : 0.f;
    const D2D1_RECT_F box{r.left, r.top + shift, r.right, r.bottom + shift};
    D2D1_COLOR_F ink = kInk;
    switch (look) {
      case Look::kPrimary: Fill(box, kAccent, radius); ink = kOnAccent; break;
      case Look::kOutline: if (hovered) Fill(box, kHover, radius); Ring(box, kLineStrong, radius); break;
      case Look::kQuiet: if (hovered) Fill(box, kHover, radius); break;
      case Look::kDanger: if (hovered) Fill(box, kHover, radius); ink = kDanger; break;
      case Look::kDangerStrong: if (hovered) Fill(box, kHover, radius); Ring(box, kDanger, radius, 1.5f); ink = kDanger; break;
    }
    const float text_top = box.top + (box.bottom - box.top - 18) / 2;
    Print(label, kStrong, {box.left, text_top, box.right, box.bottom}, ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (focus_ == id) FocusRing(box, radius);
  }
  float ButtonWidth(const std::wstring& label, Look look) {
    const bool pill = look == Look::kPrimary || look == Look::kOutline || look == Look::kDangerStrong;
    return std::ceil(TextWidth(label, kStrong)) + (pill ? 32 : 24);
  }
  // Lays out buttons from |right| leftwards; returns the left edge.
  float ButtonsRightToLeft(float right, float top, std::initializer_list<std::tuple<std::wstring, Look, Control>> buttons) {
    std::vector<std::tuple<std::wstring, Look, Control>> list(buttons);
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
      const float width = ButtonWidth(std::get<0>(*it), std::get<1>(*it));
      Button({right - width, top, right, top + 32}, std::get<0>(*it), std::get<1>(*it), std::get<2>(*it));
      right -= width + 8;
    }
    return right + 8;
  }

  // zt-toggle: a 34 × 20 pill, its knob 12 px.
  void Toggle(D2D1_RECT_F r, bool on, Control id) {
    Register(r, id);
    if (on) Fill(r, kAccent, 10);
    else Ring(r, kLineStrong, 10, 1.5f);
    const float x = r.left + 4 + (on ? 14.f : 0.f) + 6;
    target_->FillEllipse({{x, r.top + 10}, 6, 6}, Brush(on ? kOnAccent : kMuted));
    if (focus_ == id) FocusRing(r, 10);
  }

  // A radio card: raised and ringed in the accent when chosen.
  void Card(D2D1_RECT_F r, const std::wstring& title, const std::wstring& detail, bool on, Control id) {
    Register(r, id);
    if (on) {
      Fill(r, kRaised, 12);
      Ring({r.left - 2, r.top - 2, r.right + 2, r.bottom + 2}, kAccent, 14, 2);
    } else {
      if (hover_ == id) Fill(r, kHover, 12);
      Ring(r, kLineStrong, 12);
    }
    Print(title, kStrong, {r.left + 14, r.top + 12, r.right - 14, r.bottom}, kInk);
    Print(detail, kLabel, {r.left + 14, r.top + 34, r.right - 14, r.bottom}, kMuted);
    if (focus_ == id) FocusRing(r, 12);
  }

  std::wstring DisplayPath(const std::wstring& path) {
    std::wstring prefix(MAX_PATH, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", prefix.data(), MAX_PATH);
    prefix.resize(length < MAX_PATH ? length : 0);
    if (length && path.size() >= prefix.size() &&
        CompareStringOrdinal(path.c_str(), static_cast<int>(prefix.size()), prefix.c_str(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL)
      return L"%LOCALAPPDATA%" + path.substr(prefix.size());
    return path;
  }

  void Paint() {
    if (!target_) {
      RECT client;
      GetClientRect(hwnd_, &client);
      ComPtr<ID2D1HwndRenderTarget> target;
      const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
          D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE), 96.f * scale_, 96.f * scale_);
      if (FAILED(d2d_->CreateHwndRenderTarget(properties, D2D1::HwndRenderTargetProperties(hwnd_,
              D2D1::SizeU(client.right, client.bottom)), &target)))
        return;
      target_ = target;
      target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
      target_->CreateSolidColorBrush(kInk, &brush_);
    }
    hits_.clear();
    target_->BeginDraw();
    target_->Clear(kVoid);
    // Header: the eyebrow and the close button (design: zw-tool).
    const wchar_t* eyebrow = page_ == Page::kExisting ? text_.eyebrow_found
                             : page_ == Page::kRemove || (page_ == Page::kWorking && removing_) ? text_.eyebrow_remove
                                                                                                : text_.eyebrow_install;
    std::wstring upper(eyebrow);
    CharUpperBuffW(upper.data(), static_cast<DWORD>(upper.size()));
    Print(upper, kEyebrow, {16, 12, 400, kHead}, kMuted);
    const D2D1_RECT_F close{kWidth - 46, 0, kWidth, kHead};
    if (page_ != Page::kWorking) {
      Register(close, kClose);
      if (hover_ == kClose) Fill(close, kCloseRed);
      const D2D1_COLOR_F glyph = hover_ == kClose ? kWhite : kInk;
      const float cx = kWidth - 23, cy = kHead / 2;
      target_->DrawLine({cx - 5, cy - 5}, {cx + 5, cy + 5}, Brush(glyph), 1);
      target_->DrawLine({cx + 5, cy - 5}, {cx - 5, cy + 5}, Brush(glyph), 1);
    }
    // Body: bg-base, 20 px above and below, 32 px at the sides.
    Fill({0, kHead, kWidth, kHeight}, kBase);
    const D2D1_RECT_F body{32, kHead + 20, kWidth - 32, kHeight - 20};
    switch (page_) {
      case Page::kFresh: PaintFresh(body); break;
      case Page::kExisting: PaintExisting(body); break;
      case Page::kRemove: PaintRemove(body); break;
      case Page::kWorking: PaintWorking(body); break;
    }
    if (target_->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) target_.Reset();
  }

  void PaintFresh(D2D1_RECT_F b) {
    float y = b.top + Print(text_.where, kHeadline, {b.left, b.top, b.right, b.bottom}, kInk) + 16;
    const float half = (b.right - b.left - 10) / 2;
    Card({b.left, y, b.left + half, y + 62}, text_.for_me, text_.for_me_detail, !placement_.system, kForMe);
    Card({b.right - half, y, b.right, y + 62}, text_.for_all, text_.for_all_detail, placement_.system, kForAll);
    y += 62 + 16;
    Print(text_.folder, kLabel, {b.left, y, b.right, y + 16}, kMuted);
    y += 22;
    const float change = ButtonWidth(text_.change, Look::kOutline);
    const D2D1_RECT_F field{b.left, y, b.right - change - 8, y + 32};
    Fill(field, kRaised, 8);
    Ring(field, kLineStrong, 8);
    Print(DisplayPath(placement_.directory), kPath, {field.left + 12, y + 8, field.right - 12, y + 24}, kInk,
         DWRITE_TEXT_ALIGNMENT_LEADING, true);
    Button({b.right - change, y, b.right, y + 32}, text_.change, Look::kOutline, kChange);
    y += 32 + 16;
    Print(text_.desktop_shortcut, kBody, {b.left, y + 1, b.right - 40, y + 20}, kInk);
    Toggle({b.right - 34, y, b.right, y + 20}, desktop_shortcut_, kShortcut);
    const float footer = b.bottom - 32;
    Print(text_.platform, kPlatform, {b.left, footer + 8, b.left + 200, footer + 24}, kMuted);
    ButtonsRightToLeft(b.right, footer, {{text_.cancel, Look::kQuiet, kCancel}, {text_.install, Look::kPrimary, kInstall}});
  }

  void PaintExisting(D2D1_RECT_F b) {
    float y = b.top + Print(text_.already_here, kHeadline, b, kInk) + 16;
    const D2D1_RECT_F card{b.left, y, b.right, y + 68};
    Fill(card, kRaised, 12);
    Ring(card, kLine, 12);
    const D2D1_RECT_F tile{card.left + 16, y + 14, card.left + 56, y + 54};
    Fill(tile, kTile, 10);
    Print(L"|z⟩", kKet, {tile.left, tile.top + 9, tile.right, tile.bottom}, kAccent, DWRITE_TEXT_ALIGNMENT_CENTER);
    const float status = TextWidth(text_.installed, kLabel);
    Print(L"Zitter " + existing_->version, kStrong, {tile.right + 14, y + 15, card.right - status - 32, y + 33}, kInk);
    Print(DisplayPath(existing_->placement.directory), kPath, {tile.right + 14, y + 35, card.right - status - 32, y + 51},
         kMuted, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    Print(text_.installed, kLabel, {card.right - status - 16, y + 26, card.right - 16, y + 42}, kOk);
    y += 68 + 16;
    Print(Substitute(text_.update_keeps, PayloadVersion()), kBody, {b.left, y, b.right, b.bottom}, kMuted);
    const float footer = b.bottom - 32;
    Button({b.left - 12, footer, b.left - 12 + ButtonWidth(text_.remove_zitter, Look::kDanger), footer + 32},
           text_.remove_zitter, Look::kDanger, kRemoveZitter);
    ButtonsRightToLeft(b.right, footer, {{text_.install_elsewhere, Look::kOutline, kElsewhere}, {text_.update, Look::kPrimary, kUpdate}});
  }

  void PaintRemove(D2D1_RECT_F b) {
    float y = b.top + Print(text_.remove_question, kHeadline, b, kInk) + 16;
    // „Usunę aplikację z <folder> razem ze skrótami.”, the folder in mono ink.
    const std::wstring path = DisplayPath(placement_.directory);
    const std::wstring sentence = Substitute(text_.remove_from, path);
    auto layout = Layout(sentence, kBody, b.right - b.left);
    if (const auto at = sentence.find(path); at != std::wstring::npos) {
      const DWRITE_TEXT_RANGE range{static_cast<UINT32>(at), static_cast<UINT32>(path.size())};
      layout->SetFontFamilyName(kMono, range);
      layout->SetFontSize(12, range);
      ComPtr<ID2D1SolidColorBrush> ink;
      target_->CreateSolidColorBrush(kInk, &ink);
      layout->SetDrawingEffect(ink.Get(), range);
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    target_->DrawTextLayout({b.left, y}, layout.Get(), Brush(kMuted));
    y += metrics.height + 16;
    const D2D1_RECT_F card{b.left, y, b.right, y + 64};
    Fill(card, kRaised, 12);
    Ring(card, kLine, 12);
    Print(text_.remove_data, kStrong, {card.left + 16, y + 14, card.right - 70, y + 32}, kInk);
    Print(text_.remove_data_detail, kLabel, {card.left + 16, y + 34, card.right - 70, y + 50}, kMuted);
    Toggle({card.right - 50, y + 22, card.right - 16, y + 42}, remove_data_, kWipe);
    const float footer = b.bottom - 32;
    if (uninstall_only_) ButtonsRightToLeft(b.right, footer, {{text_.cancel, Look::kQuiet, kCancel}, {text_.remove_zitter, Look::kDangerStrong, kConfirmRemove}});
    else ButtonsRightToLeft(b.right, footer, {{text_.back, Look::kQuiet, kBack}, {text_.remove_zitter, Look::kDangerStrong, kConfirmRemove}});
  }

  void PaintWorking(D2D1_RECT_F b) {
    const float title = Print(text_.about_to_tremble, kHeadlineLarge, {b.left, b.top, b.left + 400, b.bottom}, kInk);
    const wchar_t* note = removing_ ? text_.removing : placement_.system ? text_.copying_for_all : text_.copying_for_me;
    Print(note, kBody, {b.left, b.top + title + 12, b.left + 360, b.bottom}, kMuted);
    // Design: three dots trembling like a particle (zing, accent, zing), in
    // five 52 ms steps; still under reduced motion.
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    static constexpr std::array<D2D1_POINT_2F, 5> kSteps = {{{0, 0}, {3, -3}, {-3, 3}, {3, 3}, {-3, -3}}};
    const ULONGLONG now = GetTickCount64();
    for (int i = 0; i < 3; ++i) {
      const int delay = i == 0 ? 0 : i == 1 ? 90 : 170;
      const auto step = animations ? kSteps.at(((now + delay) % 260) / 52) : kSteps.at(0);
      const float cx = b.right - 13 - (2 - i) * 40.f + step.x, cy = b.top + 28 + 13 + step.y;
      target_->FillEllipse({{cx, cy}, 13, 13}, Brush(i == 1 ? kAccent : kZing));
    }
    const float bar = b.bottom - 38;
    const int percent = static_cast<int>(std::lround(progress_ * 100));
    Print(removing_ ? text_.removing_files : text_.copying_files, kLabel, {b.left, bar - 26, b.right - 120, bar - 10}, kMuted);
    Print(std::to_wstring(percent) + L"%", kPercent, {b.right - 160, bar - 38, b.right, bar - 10}, kInk, DWRITE_TEXT_ALIGNMENT_TRAILING);
    const D2D1_RECT_F track{b.left, bar, b.right, bar + 6};
    Fill(track, kHover, 3);
    Ring(track, kLine, 3);
    if (progress_ > 0) Fill({b.left, bar, b.left + (b.right - b.left) * static_cast<float>(progress_), bar + 6}, kAccent, 3);
    Print(text_.platform, kPlatform, {b.left, bar + 16, b.right, bar + 32}, kMuted);
  }

  // --- Behavior --------------------------------------------------------------

  Control DefaultControl() const {
    switch (page_) {
      case Page::kFresh: return kInstall;
      case Page::kExisting: return kUpdate;
      case Page::kRemove: return kNone;  // Removing is never the Enter default.
      case Page::kWorking: return kNone;
    }
    return kNone;
  }

  void MoveFocus(int direction) {
    if (hits_.empty()) return;
    std::vector<Control> order;
    for (const auto& hit : hits_) order.push_back(hit.id);
    auto at = std::find(order.begin(), order.end(), focus_);
    int index = at == order.end() ? (direction > 0 ? -1 : 0) : static_cast<int>(at - order.begin());
    index = (index + direction + static_cast<int>(order.size())) % static_cast<int>(order.size());
    focus_ = order.at(static_cast<size_t>(index));
    Invalidate();
  }

  void Activate(Control id) {
    switch (id) {
      case kClose: case kCancel: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
      case kForMe: case kForAll:
        placement_.system = id == kForAll;
        placement_.directory = DefaultDirectory(placement_.system);
        break;
      case kChange: PickFolder(); break;
      case kShortcut: desktop_shortcut_ = !desktop_shortcut_; break;
      case kWipe: remove_data_ = !remove_data_; break;
      case kElsewhere: page_ = Page::kFresh; placement_ = {false, DefaultDirectory(false)}; break;
      case kBack: page_ = Page::kExisting; break;
      case kRemoveZitter: page_ = Page::kRemove; placement_ = existing_->placement; break;
      case kUpdate: placement_ = existing_->placement; StartInstall(); break;
      case kInstall: StartInstall(); break;
      case kConfirmRemove: StartUninstall(); break;
      case kNone: break;
    }
    focus_ = id == kForMe || id == kForAll || id == kShortcut || id == kWipe ? id : kNone;
    Invalidate();
  }

  void PickFolder() {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(text_.pick_folder);
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (SUCCEEDED(dialog->Show(hwnd_)) && SUCCEEDED(dialog->GetResult(&item)) &&
        SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
      std::wstring chosen(path);
      // A parent folder gets its own Zitter folder.
      const auto name = chosen.substr(chosen.find_last_of(L"\\/") + 1);
      if (CompareStringOrdinal(name.c_str(), -1, L"Zitter", -1, TRUE) != CSTR_EQUAL) {
        if (chosen.back() != L'\\') chosen += L'\\';
        chosen += L"Zitter";
      }
      placement_.directory = chosen;
    }
    CoTaskMemFree(path);
  }

  // A quiet run ends instead of asking.
  void Refuse(const std::wstring& message, UINT icon) {
    if (!quiet_) {
      MessageBoxW(hwnd_, message.c_str(), L"Zitter", MB_OK | icon);
      return;
    }
    exit_code_ = 1;
    page_ = Page::kFresh;  // Lets WM_CLOSE through.
    DestroyWindow(hwnd_);
  }

  void StartInstall() {
    if (IsRunning(placement_.directory)) {
      Refuse(text_.close_zitter_first, MB_ICONINFORMATION);
      return;
    }
    // Everyone's installation runs elevated, in a second copy of this window.
    if (placement_.system && !IsElevated()) {
      std::wstring arguments = L"--install-now --system --dir \"" + placement_.directory + L"\"";
      if (!desktop_shortcut_) arguments += L" --no-desktop";
      if (Relaunch(arguments)) DestroyWindow(hwnd_);
      return;
    }
    Start(false);
  }

  void StartUninstall() {
    if (IsRunning(placement_.directory)) {
      Refuse(text_.close_zitter_first, MB_ICONINFORMATION);
      return;
    }
    if (placement_.system && !IsElevated()) {
      if (Relaunch(quiet_ ? L"--uninstall --system --quiet" : L"--uninstall --system")) DestroyWindow(hwnd_);
      return;
    }
    Start(true);
  }

  void Start(bool remove) {
    removing_ = remove;
    page_ = Page::kWorking;
    progress_ = 0;
    SetTimer(hwnd_, kAnimationTimer, 52, nullptr);
    const HWND hwnd = hwnd_;
    const Placement placement = placement_;
    const bool desktop = desktop_shortcut_, wipe = remove_data_;
    worker_ = std::thread([this, hwnd, placement, desktop, wipe, remove] {
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
      auto report = [hwnd](double done) {
        PostMessageW(hwnd, kProgressMessage, static_cast<WPARAM>(std::lround(std::clamp(done, 0.0, 1.0) * 1000)), 0);
      };
      std::wstring error = remove ? Uninstall(placement, wipe, report) : Install(placement, desktop, report);
      {
        std::lock_guard lock(result_lock_);
        result_ = std::move(error);
      }
      CoUninitialize();
      PostMessageW(hwnd, kDoneMessage, 0, 0);
    });
  }

  void Finished() {
    KillTimer(hwnd_, kAnimationTimer);
    worker_.join();
    std::wstring error;
    {
      std::lock_guard lock(result_lock_);
      error = result_;
    }
    if (!error.empty()) {
      page_ = removing_ ? Page::kRemove : Page::kFresh;
      Invalidate();
      Refuse(error == L"running" ? std::wstring(text_.close_zitter_first) : Substitute(text_.failed, error),
             MB_ICONWARNING);
      return;
    }
    if (removing_) {
      DeleteSelfLater(placement_.directory);
    } else if (!IsElevated() && !quiet_) {
      // Zitter opens once installed; an elevated copy would run it as administrator.
      const std::wstring exe = placement_.directory + L"\\zitter.exe";
      ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, placement_.directory.c_str(), SW_SHOWNORMAL);
    }
    page_ = Page::kFresh;  // Lets WM_CLOSE through.
    DestroyWindow(hwnd_);
  }

  HINSTANCE instance_;
  const setup::Text text_;
  HWND hwnd_ = nullptr;
  float scale_ = 1;
  ComPtr<ID2D1Factory> d2d_;
  ComPtr<IDWriteFactory5> dwrite_;
  ComPtr<IDWriteFontCollection1> fonts_;
  ComPtr<ID2D1HwndRenderTarget> target_;
  ComPtr<ID2D1SolidColorBrush> brush_;
  std::vector<Hit> hits_;
  Control hover_ = kNone, pressed_ = kNone, focus_ = kNone;
  bool tracking_ = false;
  Page page_ = Page::kFresh;
  Placement placement_;
  std::optional<Existing> existing_;
  bool desktop_shortcut_ = true, remove_data_ = false, removing_ = false;
  bool install_now_ = false, uninstall_only_ = false, quiet_ = false;
  int exit_code_ = 0;
  double progress_ = 0;
  std::thread worker_;
  std::mutex result_lock_;
  std::wstring result_;
};
}  // namespace
}  // namespace zitter::setup

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int show) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  zitter::setup::Window window(instance);
  const int result = window.Run(show);
  CoUninitialize();
  return result;
}

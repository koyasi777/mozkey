// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "renderer/renderer_server.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "base/random.h"
#include "base/system_util.h"
#include "base/vlog.h"
#include "client/client_interface.h"
#include "config/config_handler.h"
#include "ipc/ipc.h"
#include "ipc/named_event.h"
#include "ipc/process_watch_dog.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "protocol/renderer_command.pb.h"
#include "renderer/renderer_interface.h"
#include "renderer/renderer_style_handler.h"

#ifdef _WIN32
#include <windows.h>

#include "base/const.h"
#include "base/win32/win_util.h"
#endif  // _WIN32

namespace mozc {
namespace renderer {

namespace {
#ifdef _WIN32
constexpr int kNumConnections = 1;
#else   // _WIN32
constexpr int kNumConnections = 10;
#endif  // _WIN32
constexpr absl::Duration kIPCServerTimeOut = absl::Milliseconds(1000);
constexpr char kServiceName[] = "renderer";

std::string ConstructServiceName(bool for_testing) {
  std::string name = kServiceName;
  if (for_testing) {
    absl::StrAppend(&name, ".test.", Random().Utf8String(16, 'a', 'z'));
  }
  const std::string desktop_name = SystemUtil::GetDesktopNameAsString();
  if (!desktop_name.empty()) {
    absl::StrAppend(&name, ".", desktop_name);
  }
  return name;
}

using ColorTheme = config::Config::RendererWindowColorTheme;
using CandidatePalette = config::Config::CandidateWindowColorPalette;
using RubyPalette = config::Config::RubyWindowColorPalette;

constexpr uint32_t kMaxCornerRadius = 24;
constexpr uint32_t kMinWindowSizePercent = 80;
constexpr uint32_t kMaxWindowSizePercent = 200;
constexpr uint32_t kMinWindowOpacityPercent = 20;
constexpr uint32_t kMaxWindowOpacityPercent = 100;
constexpr uint32_t kMaxShadowSize = 96;
constexpr uint32_t kMaxShadowDistance = 96;
constexpr uint32_t kMaxShadowOpacityPercent = 100;
constexpr uint32_t kMaxRubyHorizontalPadding = 40;
constexpr uint32_t kMaxRubyVerticalPadding = 24;
constexpr uint32_t kMaxRubyCompositionGap = 32;
constexpr uint32_t kMinFontWeight = 100;
constexpr uint32_t kMaxFontWeight = 900;
constexpr uint32_t kFontWeightStep = 100;

ColorTheme GetCandidateWindowColorTheme(const config::Config& config) {
  if (config.has_candidate_window_color_theme()) {
    return config.candidate_window_color_theme();
  }
  return config.use_dark_mode_candidate_window()
             ? config::Config::RENDERER_WINDOW_COLOR_DARK
             : config::Config::RENDERER_WINDOW_COLOR_LIGHT;
}

ColorTheme NormalizeDependentColorTheme(ColorTheme theme,
                                        ColorTheme candidate_theme) {
  if (theme == config::Config::RENDERER_WINDOW_COLOR_FOLLOW_CANDIDATE) {
    return candidate_theme;
  }
  return theme;
}

uint32_t ClampCornerRadius(uint32_t radius) {
  return std::clamp(radius, 0u, kMaxCornerRadius);
}

uint32_t ClampWindowSizePercent(uint32_t percent) {
  return std::clamp(percent, kMinWindowSizePercent, kMaxWindowSizePercent);
}

uint32_t ClampWindowOpacityPercent(uint32_t percent) {
  return std::clamp(percent, kMinWindowOpacityPercent,
                    kMaxWindowOpacityPercent);
}

uint32_t ClampRubyHorizontalPadding(uint32_t padding) {
  return std::clamp(padding, 0u, kMaxRubyHorizontalPadding);
}

uint32_t ClampRubyVerticalPadding(uint32_t padding) {
  return std::clamp(padding, 0u, kMaxRubyVerticalPadding);
}

uint32_t ClampRubyCompositionGap(uint32_t gap) {
  return std::clamp(gap, 0u, kMaxRubyCompositionGap);
}

uint32_t NormalizeFontWeight(uint32_t weight) {
  const uint32_t clamped =
      std::clamp(weight, kMinFontWeight, kMaxFontWeight);
  return ((clamped + kFontWeightStep / 2) / kFontWeightStep) *
         kFontWeightStep;
}

RendererStyleHandler::WindowShadowStyle BuildShadowStyle(
    uint32_t size, uint32_t opacity_percent, uint32_t angle_degrees,
    uint32_t distance) {
  RendererStyleHandler::WindowShadowStyle style;
  style.size = std::clamp(size, 0u, kMaxShadowSize);
  style.opacity_percent =
      std::clamp(opacity_percent, 0u, kMaxShadowOpacityPercent);
  style.angle_degrees = angle_degrees % 360u;
  style.distance = std::clamp(distance, 0u, kMaxShadowDistance);
  return style;
}

RendererStyleHandler::CandidateWindowEffectStyle BuildCandidateEffectStyle(
    uint32_t opacity_percent, uint32_t shadow_size,
    uint32_t shadow_opacity_percent, uint32_t shadow_angle_degrees,
    uint32_t shadow_distance) {
  RendererStyleHandler::CandidateWindowEffectStyle style;
  style.opacity_percent = ClampWindowOpacityPercent(opacity_percent);
  style.shadow = BuildShadowStyle(shadow_size, shadow_opacity_percent,
                                  shadow_angle_degrees, shadow_distance);
  return style;
}

void ApplyPaletteToCandidateStyle(const CandidatePalette& palette,
                                  RendererStyle* style) {
  RendererStyleHandler::ApplyCandidateWindowCustomColors(
      palette.background_color(), palette.text_color(),
      palette.selected_background_color(), palette.selected_border_color(),
      palette.border_color(), palette.shortcut_text_color(),
      palette.shortcut_background_color(), palette.description_text_color(),
      palette.footer_text_color(), palette.footer_background_color(),
      palette.footer_border_color(), palette.scrollbar_background_color(),
      palette.scrollbar_indicator_color(), style);
}

void BuildCandidateLikeStyle(ColorTheme theme, const CandidatePalette& palette,
                             const std::string& font_name,
                             uint32_t font_weight, uint32_t size_percent,
                             RendererStyle* style) {
  RendererStyleHandler::GetDefaultRendererStyle(style);
  RendererStyleHandler::ApplyCandidateWindowTheme(
      theme == config::Config::RENDERER_WINDOW_COLOR_DARK, style);
  if (theme == config::Config::RENDERER_WINDOW_COLOR_CUSTOM) {
    ApplyPaletteToCandidateStyle(palette, style);
  }
  RendererStyleHandler::ApplyCandidateRubyFont(font_name, style);
  style->mutable_candidate_style()->set_font_weight(
      static_cast<int32_t>(NormalizeFontWeight(font_weight)));
  RendererStyleHandler::ApplyCandidateWindowSize(
      ClampWindowSizePercent(size_percent), style);
}

uint32_t RgbFromColor(const RendererStyle::RGBAColor& color,
                      uint32_t fallback) {
  if (!color.IsInitialized()) {
    return fallback;
  }
  return ((static_cast<uint32_t>(color.r()) & 0xff) << 16) |
         ((static_cast<uint32_t>(color.g()) & 0xff) << 8) |
         (static_cast<uint32_t>(color.b()) & 0xff);
}

RendererStyleHandler::RubyWindowStyle RubyStyleFromCandidateStyle(
    const RendererStyle& style, uint32_t corner_radius, uint32_t size_percent) {
  RendererStyleHandler::RubyWindowStyle ruby_style;
  ruby_style.corner_radius = corner_radius;
  ruby_style.size_percent = ClampWindowSizePercent(size_percent);
  if (style.has_border_color()) {
    ruby_style.border_color = RgbFromColor(style.border_color(),
                                           ruby_style.border_color);
  }
  if (style.candidate_style().has_background_color()) {
    ruby_style.background_color = RgbFromColor(
        style.candidate_style().background_color(), ruby_style.background_color);
  }
  if (style.candidate_style().has_foreground_color()) {
    ruby_style.text_color = RgbFromColor(style.candidate_style().foreground_color(),
                                         ruby_style.text_color);
  }
  return ruby_style;
}

RendererStyleHandler::RubyWindowStyle BuildRubyStyle(
    ColorTheme theme, const RubyPalette& palette, const RendererStyle& candidate_style,
    uint32_t corner_radius, uint32_t size_percent) {
  const uint32_t clamped_size_percent = ClampWindowSizePercent(size_percent);
  if (theme == config::Config::RENDERER_WINDOW_COLOR_FOLLOW_CANDIDATE) {
    return RubyStyleFromCandidateStyle(candidate_style, corner_radius,
                                      clamped_size_percent);
  }

  if (theme == config::Config::RENDERER_WINDOW_COLOR_CUSTOM) {
    RendererStyleHandler::RubyWindowStyle ruby_style;
    ruby_style.background_color = palette.background_color();
    ruby_style.text_color = palette.text_color();
    ruby_style.border_color = palette.border_color();
    ruby_style.corner_radius = corner_radius;
    ruby_style.size_percent = clamped_size_percent;
    return ruby_style;
  }

  RendererStyle preset_style;
  RendererStyleHandler::GetDefaultRendererStyle(&preset_style);
  RendererStyleHandler::ApplyCandidateWindowTheme(
      theme == config::Config::RENDERER_WINDOW_COLOR_DARK, &preset_style);
  return RubyStyleFromCandidateStyle(preset_style, corner_radius,
                                    clamped_size_percent);
}

void UpdateRendererStyleFromConfig() {
  config::ConfigHandler::Reload();

  const auto shared_config = config::ConfigHandler::GetSharedConfig();

  const ColorTheme candidate_color_theme =
      GetCandidateWindowColorTheme(*shared_config);
  const ColorTheme suggest_color_theme = NormalizeDependentColorTheme(
      shared_config->suggest_window_color_theme(), candidate_color_theme);
  const ColorTheme ruby_color_theme =
      shared_config->ruby_window_color_theme();

  RendererStyle candidate_style;
  BuildCandidateLikeStyle(candidate_color_theme,
                          shared_config->candidate_window_custom_color_palette(),
                          shared_config->candidate_ruby_font_name(),
                          shared_config->candidate_window_font_weight(),
                          shared_config->candidate_window_size_percent(),
                          &candidate_style);

  const bool suggest_follows_candidate =
      shared_config->suggest_window_color_theme() ==
      config::Config::RENDERER_WINDOW_COLOR_FOLLOW_CANDIDATE;
  RendererStyle suggestion_style;
  BuildCandidateLikeStyle(
      suggest_color_theme,
      suggest_follows_candidate
          ? shared_config->candidate_window_custom_color_palette()
          : shared_config->suggest_window_custom_color_palette(),
      shared_config->candidate_ruby_font_name(),
      shared_config->suggest_window_font_weight(),
      shared_config->suggest_window_size_percent(), &suggestion_style);

  const uint32_t candidate_corner_radius =
      ClampCornerRadius(shared_config->candidate_window_custom_corner_radius());
  const uint32_t suggestion_corner_radius =
      ClampCornerRadius(shared_config->suggest_window_custom_corner_radius());
  const uint32_t ruby_corner_radius =
      ClampCornerRadius(shared_config->ruby_window_custom_corner_radius());

  RendererStyleHandler::RubyWindowStyle ruby_style = BuildRubyStyle(
      ruby_color_theme, shared_config->ruby_window_custom_color_palette(),
      candidate_style, ruby_corner_radius,
      shared_config->ruby_window_size_percent());
  ruby_style.corner_radius = ruby_corner_radius;
  ruby_style.size_percent =
      ClampWindowSizePercent(shared_config->ruby_window_size_percent());
  ruby_style.font_weight =
      NormalizeFontWeight(shared_config->ruby_window_font_weight());
  ruby_style.opacity_percent =
      ClampWindowOpacityPercent(shared_config->ruby_window_opacity_percent());
  ruby_style.horizontal_padding = ClampRubyHorizontalPadding(
      shared_config->ruby_window_horizontal_padding());
  ruby_style.vertical_padding = ClampRubyVerticalPadding(
      shared_config->ruby_window_vertical_padding());
  ruby_style.composition_gap = ClampRubyCompositionGap(
      shared_config->ruby_window_composition_gap());
  ruby_style.shadow = BuildShadowStyle(
      shared_config->ruby_window_shadow_size(),
      shared_config->ruby_window_shadow_opacity_percent(),
      shared_config->ruby_window_shadow_angle_degrees(),
      shared_config->ruby_window_shadow_distance());

  const RendererStyleHandler::CandidateWindowEffectStyle
      candidate_effect_style = BuildCandidateEffectStyle(
          shared_config->candidate_window_opacity_percent(),
          shared_config->candidate_window_shadow_size(),
          shared_config->candidate_window_shadow_opacity_percent(),
          shared_config->candidate_window_shadow_angle_degrees(),
          shared_config->candidate_window_shadow_distance());
  const RendererStyleHandler::CandidateWindowEffectStyle
      suggestion_effect_style = BuildCandidateEffectStyle(
          shared_config->suggest_window_opacity_percent(),
          shared_config->suggest_window_shadow_size(),
          shared_config->suggest_window_shadow_opacity_percent(),
          shared_config->suggest_window_shadow_angle_degrees(),
          shared_config->suggest_window_shadow_distance());

  RendererStyleHandler::SetRendererWindowStyles(
      candidate_style, suggestion_style, ruby_style, candidate_corner_radius,
      suggestion_corner_radius, candidate_effect_style, suggestion_effect_style);
}

}  // namespace

class RendererServerSendCommand : public client::SendCommandInterface {
 public:
  RendererServerSendCommand() : receiver_handle_(0) {}
  RendererServerSendCommand(const RendererServerSendCommand&) = delete;
  RendererServerSendCommand& operator=(const RendererServerSendCommand&) =
      delete;
  ~RendererServerSendCommand() override = default;

  bool SendCommand(const mozc::commands::SessionCommand& command,
                   mozc::commands::Output* output) override {
#ifdef _WIN32
    if ((command.type() != commands::SessionCommand::SELECT_CANDIDATE) &&
        (command.type() != commands::SessionCommand::HIGHLIGHT_CANDIDATE)) {
      // Unsupported command.
      return false;
    }

    HWND target = WinUtil::DecodeWindowHandle(receiver_handle_);
    if (target == nullptr) {
      LOG(ERROR) << "target window is nullptr";
      return false;
    }
    UINT mozc_msg = ::RegisterWindowMessageW(kMessageReceiverMessageName);
    if (mozc_msg == 0) {
      LOG(ERROR) << "RegisterWindowMessage failed: " << ::GetLastError();
      return false;
    }
    WPARAM type = static_cast<WPARAM>(command.type());
    LPARAM id = static_cast<LPARAM>(command.id());
    ::PostMessage(target, mozc_msg, type, id);
#endif  // _WIN32

    // TODO(all): implementation for Mac/Linux
    return true;
  }

  void set_receiver_handle(uint32_t receiver_handle) {
    receiver_handle_ = receiver_handle;
  }

 private:
  uint32_t receiver_handle_;
};

RendererServer::RendererServer() : RendererServer(false) {}

RendererServer::RendererServer(bool for_testing)
    : IPCServer(ConstructServiceName(for_testing), kNumConnections,
                kIPCServerTimeOut),
      renderer_interface_(nullptr),
      send_command_(std::make_unique<RendererServerSendCommand>()) {
  watch_dog_ = std::make_unique<ProcessWatchDog>(
      [this](ProcessWatchDog::SignalType type) {
        if (type == ProcessWatchDog::SignalType::PROCESS_SIGNALED ||
            type == ProcessWatchDog::SignalType::THREAD_SIGNALED) {
          MOZC_VLOG(1) << "Parent process is terminated: call Hide event";
          mozc::commands::RendererCommand command;
          command.set_type(mozc::commands::RendererCommand::UPDATE);
          command.set_visible(false);
          if (std::string proto_message = command.SerializeAsString();
              !proto_message.empty()) {
            AsyncExecCommand(proto_message);
          } else {
            LOG(ERROR) << "SerializeToString failed";
          }
        }
      });

#ifndef NDEBUG
  mozc::internal::SetConfigVLogLevel(
      config::ConfigHandler::GetSharedConfig()->verbose_level());
#endif  // NDEBUG

  UpdateRendererStyleFromConfig();
}

RendererServer::~RendererServer() = default;

void RendererServer::SetRendererInterface(
    RendererInterface* renderer_interface) {
  renderer_interface_ = renderer_interface;
  if (renderer_interface_ != nullptr) {
    renderer_interface_->SetSendCommandInterface(send_command_.get());
  }
}

int RendererServer::StartServer() {
  if (!Connected()) {
    LOG(ERROR) << "cannot start server";
    return -1;
  }

  LoopAndReturn();

  // send "ready" event to the client
  absl::string_view name = GetServiceName();
  NamedEventNotifier notifier(name);
  notifier.Notify();

  // start main event loop
  return StartMessageLoop();
}

bool RendererServer::Process(absl::string_view request, std::string* response) {
  // No need to set the result code.
  response->clear();

  // Cannot call the method directly like renderer_interface_->ExecCommand()
  // as it's not thread-safe.
  return AsyncExecCommand(request);
}

bool RendererServer::ExecCommandInternal(
    const commands::RendererCommand& command) {
  if (renderer_interface_ == nullptr) {
    LOG(ERROR) << "renderer_interface is nullptr";
    return false;
  }

  MOZC_VLOG(2) << command;

  // Check process info if update mode
  if (command.type() == commands::RendererCommand::UPDATE) {
    UpdateRendererStyleFromConfig();

    // set HWND of message-only window
    if (command.has_application_info() &&
        command.application_info().has_receiver_handle()) {
      send_command_->set_receiver_handle(
          command.application_info().receiver_handle());
    } else {
      LOG(WARNING) << "receiver_handle is not set";
    }

    // watch the parent application.
    if (command.has_application_info() &&
        command.application_info().has_process_id() &&
        command.application_info().has_thread_id()) {
      if (!watch_dog_->SetId(static_cast<ProcessWatchDog::ProcessId>(
                                 command.application_info().process_id()),
                             static_cast<ProcessWatchDog::ThreadId>(
                                 command.application_info().thread_id()))) {
        LOG(ERROR) << "Cannot set new ids for watch dog";
      }
    } else {
      LOG(WARNING) << "process id and thread id are not set";
    }
  }

  if (renderer_interface_->ExecCommand(command)) {
    return true;
  }

  return false;
}

}  // namespace renderer
}  // namespace mozc

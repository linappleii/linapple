// SPDX-License-Identifier: GPL-2.0-only
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

#include "core/LinAppleCore.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/SaveStateManager.h"
#include "frontends/common/imgui/ImGuiApp.h"
#include "frontends/common/imgui/ImGuiBackend.h"
#include "frontends/common/sdl/JoystickFrontend.h"
#include "frontends/common/sdl/MouseInput.h"
#include "frontends/sdl3-imgui/Frame.h"
#include "frontends/sdl3-imgui/Sdl3Gui.h"

namespace {

struct PendingDialogResult {
  ImGuiDialogAction action;
  std::string path;
  std::string extra;
};

struct DialogSharedState {
  std::mutex mutex;
  bool alive{true};
  uint64_t generation{1};
  std::queue<PendingDialogResult> queue;
};

struct DialogCallbackContext {
  std::shared_ptr<DialogSharedState> shared;
  uint64_t generation;
  ImGuiDialogAction action;
  std::string extra;
};

struct Sdl3GuiState {
  bool initialized{false};
  SDL_Window* window{nullptr};
  std::shared_ptr<DialogSharedState> dialog_state;
};

Sdl3GuiState sdl3_gui{};

constexpr const char* disk_filter_patterns =
    "dsk;do;po;nib;nb2;woz;2mg;iie;apl;prg;zip;gz";

constexpr std::array<SDL_DialogFileFilter, 2> disk_file_filters = {
    {
        {"Apple II Disk Images", disk_filter_patterns},
        {"All Files", "*"},
    },
};

constexpr std::array<SDL_DialogFileFilter, 2> hdd_file_filters = {
    {
        {"Hard Disk Images", "hdv;po;2mg"},
        {"All Files", "*"},
    },
};

constexpr std::array<SDL_DialogFileFilter, 2> savestate_file_filters = {
    {
        {"LinApple Save States", "aws"},
        {"All Files", "*"},
    },
};

// NOLINTNEXTLINE(misc-const-correctness) -- Required by SDL_DialogFileCallback.
auto SDLCALL sdl3_dialog_callback(void* userdata, const char* const* filelist,
                                  int filter) -> void {
  (void)filter;
  const std::unique_ptr<DialogCallbackContext> ctx(
      static_cast<DialogCallbackContext*>(userdata));
  if (ctx == nullptr || ctx->shared == nullptr) {
    return;
  }

  if (filelist == nullptr || filelist[0] == nullptr || filelist[0][0] == '\0') {
    return;
  }

  const std::lock_guard<std::mutex> lock(ctx->shared->mutex);
  if (!ctx->shared->alive || ctx->generation != ctx->shared->generation) {
    return;
  }
  ctx->shared->queue.push(
      PendingDialogResult{ctx->action, filelist[0], ctx->extra});
}

auto host_open_file_dialog(ImGuiDialogAction action, const char* default_dir,
                           const char* extra_param) -> void {
  if (!sdl3_gui.initialized || sdl3_gui.dialog_state == nullptr ||
      action == imgui_dialog_none) {
    return;
  }

  uint64_t gen = 0;
  {
    const std::lock_guard<std::mutex> lock(sdl3_gui.dialog_state->mutex);
    gen = sdl3_gui.dialog_state->generation;
  }

  std::unique_ptr<DialogCallbackContext> ctx(new DialogCallbackContext{
      sdl3_gui.dialog_state,
      gen,
      action,
      extra_param != nullptr ? extra_param : "",
  });

  const char* start_dir = (default_dir != nullptr) ? default_dir : "";
  switch (action) {
    case imgui_dialog_open_disk1:
    case imgui_dialog_open_disk1_and_boot:
    case imgui_dialog_open_disk2:
      SDL_ShowOpenFileDialog(sdl3_dialog_callback, ctx.release(),
                             sdl3_gui.window, disk_file_filters.data(),
                             static_cast<int>(disk_file_filters.size()),
                             start_dir, false);
      break;
    case imgui_dialog_choose_disk_dir:
      SDL_ShowOpenFolderDialog(sdl3_dialog_callback, ctx.release(),
                               sdl3_gui.window, start_dir, false);
      break;
    case imgui_dialog_open_hdd1:
    case imgui_dialog_open_hdd2:
      SDL_ShowOpenFileDialog(sdl3_dialog_callback, ctx.release(),
                             sdl3_gui.window, hdd_file_filters.data(),
                             static_cast<int>(hdd_file_filters.size()),
                             start_dir, false);
      break;
    case imgui_dialog_load_state:
      SDL_ShowOpenFileDialog(sdl3_dialog_callback, ctx.release(),
                             sdl3_gui.window, savestate_file_filters.data(),
                             static_cast<int>(savestate_file_filters.size()),
                             start_dir, false);
      break;
    case imgui_dialog_save_state:
      SDL_ShowSaveFileDialog(sdl3_dialog_callback, ctx.release(),
                             sdl3_gui.window, savestate_file_filters.data(),
                             static_cast<int>(savestate_file_filters.size()),
                             start_dir);
      break;
    case imgui_dialog_create_disk:
      SDL_ShowSaveFileDialog(sdl3_dialog_callback, ctx.release(),
                             sdl3_gui.window, disk_file_filters.data(),
                             static_cast<int>(disk_file_filters.size()),
                             start_dir);
      break;
    case imgui_dialog_none:
    default:
      break;
  }
}

auto host_save_screenshot() -> bool { return frame_save_bmp(); }

auto host_toggle_fullscreen() -> void { process_button_click(btn_fullscr, 0); }

auto host_is_fullscreen() -> bool { return system_state.fullscreen; }

auto host_toggle_debugger() -> void { process_button_click(btn_debug, 0); }

auto host_is_debugger_active() -> bool {
  return system_state.mode == app_mode_debug ||
         system_state.mode == app_mode_stepping;
}

auto host_boot_or_reset() -> void {
  process_button_click(btn_run, SDL_KMOD_LCTRL);
}

auto host_request_restart() -> void {
  system_state.restart = true;
  SDL_Event qe{};
  qe.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&qe);
}

auto format_quick_slot_path(int slot, char* out, size_t out_size) -> void {
  std::snprintf(
      out, out_size, "%.*s/SaveState%d.aws",
      static_cast<int>(std::strlen(system_state.save_state_dir.data())),
      system_state.save_state_dir.data(), slot);
}

auto host_quick_save_slot(int slot) -> bool {
  std::array<char, path_max_len> fpath{};
  format_quick_slot_path(slot, fpath.data(), fpath.size());
  save_state_set_filename(fpath.data());
  save_state_save();
  struct stat st{};
  return stat(fpath.data(), &st) == 0;
}

auto host_quick_load_slot(int slot) -> bool {
  std::array<char, path_max_len> fpath{};
  format_quick_slot_path(slot, fpath.data(), fpath.size());
  save_state_set_filename(fpath.data());
  if (!save_state_load()) {
    return false;
  }
  if (!mouse_input_consumer_present()) {
    mouse_input_release();
  }
  return true;
}

auto host_refresh_status_leds() -> void { frame_refresh_status(draw_leds); }

auto host_release_mouse_capture() -> void { mouse_input_release(); }

auto host_reinit_joystick() -> void { joy_frontend_initialize(); }

auto host_get_ticks_ms() -> uint64_t { return SDL_GetTicks(); }

auto drain_pending_dialogs() -> void {
  if (sdl3_gui.dialog_state == nullptr) {
    return;
  }
  std::queue<PendingDialogResult> ready;
  {
    const std::lock_guard<std::mutex> lock(sdl3_gui.dialog_state->mutex);
    ready.swap(sdl3_gui.dialog_state->queue);
  }
  while (!ready.empty()) {
    const PendingDialogResult& item = ready.front();
    imgui_app_handle_dialog_result(item.action, item.path.c_str(),
                                   item.extra.c_str());
    ready.pop();
  }
}

}  // namespace

auto sdl3_gui_is_active() noexcept -> bool { return sdl3_gui.initialized; }

auto sdl3_gui_init(SDL_Window* win, SDL_Renderer* ren) -> bool {
  if (win == nullptr || ren == nullptr) {
    return false;
  }
  if (sdl3_gui.initialized) {
    sdl3_gui_shutdown();
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;

  if (!ImGui_ImplSDL3_InitForSDLRenderer(win, ren)) {
    ImGui::DestroyContext();
    return false;
  }
  if (!ImGui_ImplSDLRenderer3_Init(ren)) {
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    return false;
  }

  sdl3_gui.window = win;
  sdl3_gui.dialog_state = std::make_shared<DialogSharedState>();
  sdl3_gui.initialized = true;

  const ImGuiHostCallbacks callbacks{
      host_open_file_dialog,    host_save_screenshot,
      host_toggle_fullscreen,   host_is_fullscreen,
      host_toggle_debugger,     host_is_debugger_active,
      host_boot_or_reset,       host_request_restart,
      host_quick_save_slot,     host_quick_load_slot,
      host_refresh_status_leds, host_release_mouse_capture,
      host_reinit_joystick,     host_get_ticks_ms,
  };
  const float dpi_scale = std::max(1.0F, SDL_GetWindowDisplayScale(win));
  imgui_app_init(callbacks, dpi_scale);
  return true;
}

auto sdl3_gui_shutdown() -> void {
  if (sdl3_gui.dialog_state != nullptr) {
    const std::lock_guard<std::mutex> lock(sdl3_gui.dialog_state->mutex);
    sdl3_gui.dialog_state->alive = false;
    ++sdl3_gui.dialog_state->generation;
    std::queue<PendingDialogResult> empty;
    sdl3_gui.dialog_state->queue.swap(empty);
  }
  sdl3_gui.dialog_state.reset();

  if (!sdl3_gui.initialized) {
    return;
  }

  imgui_app_shutdown();
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  sdl3_gui.initialized = false;
  sdl3_gui.window = nullptr;
}

auto sdl3_gui_process_event(const SDL_Event* event) -> bool {
  if (!sdl3_gui.initialized || event == nullptr) {
    return false;
  }

  ImGui_ImplSDL3_ProcessEvent(event);

  if (event->type == SDL_EVENT_DROP_FILE) {
    imgui_app_handle_file_drop(event->drop.data);
    return true;
  }

  // Super+M toggles GUI chrome without colliding with Apple //e Open/Solid
  // Apple (Left/Right Alt) shortcuts, and respects hotkey enable state.
  if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
      keyboard_get_hotkeys_enabled() && event->key.key == SDLK_M &&
      (event->key.mod & SDL_KMOD_GUI) != 0) {
    imgui_app_toggle_visible();
    return true;
  }

  const ImGuiIO& io = ImGui::GetIO();

  // Only consume keyboard input when an ImGui text widget actively wants text
  // input; never block global F1-F12 hotkeys or swallow KEY_UP for a held
  // F-key so buttondown cannot get stuck.
  if (io.WantTextInput) {
    if (event->type == SDL_EVENT_TEXT_INPUT) {
      return true;
    }
    if (event->type == SDL_EVENT_KEY_DOWN) {
      const bool is_fkey =
          (event->key.key >= SDLK_F1 && event->key.key <= SDLK_F12);
      if (!is_fkey) {
        linapple_set_key_release_all();
        return true;
      }
    }
    if (event->type == SDL_EVENT_KEY_UP && buttondown == -1) {
      return true;
    }
  }

  if (io.WantCaptureMouse && (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                              event->type == SDL_EVENT_MOUSE_BUTTON_UP ||
                              event->type == SDL_EVENT_MOUSE_MOTION ||
                              event->type == SDL_EVENT_MOUSE_WHEEL)) {
    return true;
  }

  return false;
}

auto sdl3_gui_get_viewport_rect(int win_w, int win_h) noexcept -> SDL_FRect {
  const ImGuiViewportRect vp = imgui_app_get_content_rect(win_w, win_h);
  return SDL_FRect{vp.x, vp.y, vp.w, vp.h};
}

auto sdl3_gui_scale_viewport_rect(const SDL_FRect& logical_rect, int win_w,
                                  int win_h, int out_w, int out_h) noexcept
    -> SDL_FRect {
  if (win_w <= 0 || win_h <= 0 || out_w <= 0 || out_h <= 0) {
    return logical_rect;
  }
  const float scale_x = static_cast<float>(out_w) / static_cast<float>(win_w);
  const float scale_y = static_cast<float>(out_h) / static_cast<float>(win_h);
  return SDL_FRect{
      logical_rect.x * scale_x,
      logical_rect.y * scale_y,
      logical_rect.w * scale_x,
      logical_rect.h * scale_y,
  };
}

auto sdl3_gui_sync_cursor(bool mouse_captured, bool is_fullscreen) -> void {
  if (!sdl3_gui.initialized) {
    return;
  }
  ImGuiIO& io = ImGui::GetIO();
  const bool gui_wants_cursor =
      !mouse_captured && imgui_app_wants_host_cursor(is_fullscreen);
  if (gui_wants_cursor) {
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    SDL_ShowCursor();
  } else {
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    if (mouse_captured ||
        (is_fullscreen && system_state.mode != app_mode_debug)) {
      SDL_HideCursor();
    }
  }
}

auto sdl3_gui_render(SDL_Renderer* ren) -> void {
  if (!sdl3_gui.initialized || ren == nullptr) {
    return;
  }

  drain_pending_dialogs();

  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  imgui_app_draw_frame();

  ImGui::Render();
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
}
